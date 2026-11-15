// W9i: editor-level stress. Scripted sessions through the real AppHost / Workbench / EditorApp (mock font, null
// renderer: the CPU cost of a frame, not of the GPU) against a generated project of ~20k files, measured against the
// frame budget (8 ms keystroke-to-paint, docs/BENCHMARKS.md) and memory (this process and clangd, from /proc).
//
// Two kinds of tests:
//   * `stress_smoke_*` run always, on a tiny synthetic tree and without clangd: they keep every scenario and its
//     bookkeeping working (and are the regression tests of the W9i fixes).
//   * `stress_ui_*` run only with TENFOLD_STRESS_DIR=<generated project> (tools/stress/gen_codebase.py ... --variant
//     cmake) or TENFOLD_STRESS_VS_DIR=<generated project, --variant vs>; without it they print SKIP. They work on a
//     scratch copy (generated data is never modified or committed; the copy is deleted at exit unless
//     TENFOLD_STRESS_KEEP=1) with the real clangd. TENFOLD_STRESS_TABS (default 300) sets the tab count and
//     TENFOLD_STRESS_JSON=<file> appends one JSON line per metric (what tools/stress/run_editor_stress.py reads).
//
// Budgets are PERF_CHECKs (enforced only with TENFOLD_ENFORCE_PERF=1, scaled by TENFOLD_PERF_SCALE, optimised
// unsanitised builds). Hangs, crashes, leaks of processes / threads and wrong results are plain CHECKs.
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>

#ifdef __linux__
#include <unistd.h>
#endif
#ifdef __GLIBC__
#include <malloc.h>
#endif

#include "../text/text_test_util.h"
#include "core/PathUtil.h"
#include "ui/gfx/Renderer.h"
#include "ui/gfx/TextRenderer.h"
#include "ui/shell/AppHost.h"
#include "ui/widgets/TerminalView.h"
#include "ui/widgets/TextView.h"
#include "ui_test.h"

using namespace tf;
using namespace tf::ui;
namespace fs = std::filesystem;

namespace {

using clk = std::chrono::steady_clock;
double msSince(clk::time_point t) { return std::chrono::duration<double, std::milli>(clk::now() - t).count(); }

#if defined(__SANITIZE_ADDRESS__)
constexpr bool kSanitized = true;
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
constexpr bool kSanitized = true;
#else
constexpr bool kSanitized = false;
#endif
#else
constexpr bool kSanitized = false;
#endif

const char* envOr(const char* name, const char* dflt = "") {
  const char* e = std::getenv(name);
  return e && *e ? e : dflt;
}

size_t envSize(const char* name, size_t dflt) {
  const char* e = std::getenv(name);
  return e && *e ? static_cast<size_t>(std::atoll(e)) : dflt;
}

// ---- process memory (Linux /proc; elsewhere zeros) ----
struct Mem {
  double rssMiB = 0, hwmMiB = 0;
  int threads = 0;
};

Mem selfMem() {
  Mem m;
#ifdef __linux__
  std::ifstream f("/proc/self/status");
  std::string line;
  while (std::getline(f, line)) {
    if (line.rfind("VmRSS:", 0) == 0) m.rssMiB = std::atof(line.c_str() + 6) / 1024.0;
    else if (line.rfind("VmHWM:", 0) == 0) m.hwmMiB = std::atof(line.c_str() + 6) / 1024.0;
    else if (line.rfind("Threads:", 0) == 0) m.threads = std::atoi(line.c_str() + 8);
  }
#endif
  return m;
}

void resetHwm() {
#ifdef __linux__
  std::ofstream("/proc/self/clear_refs") << "5";  // peak RSS (VmHWM) restarts at the current RSS
#endif
}

// The real clangd binaries (not the pip launcher script) that descend from this process: {count, total RSS MiB}.
struct ClangdMem {
  int count = 0;
  double rssMiB = 0;
};
ClangdMem clangdMem() {
  ClangdMem r;
#ifdef __linux__
  std::error_code ec;
  std::map<long, long> parent;
  std::map<long, double> rss;
  std::map<long, std::string> exe;
  for (const auto& e : fs::directory_iterator("/proc", ec)) {
    const std::string n = e.path().filename().string();
    if (n.empty() || !std::isdigit(static_cast<unsigned char>(n[0]))) continue;
    std::ifstream st(e.path() / "stat");
    std::string line;
    if (!std::getline(st, line)) continue;
    const size_t rp = line.rfind(')');
    if (rp == std::string::npos) continue;
    long ppid = 0, pgrp, sess, tty, tpgid;
    char state;
    unsigned flags;
    if (std::sscanf(line.c_str() + rp + 2, "%c %ld %ld %ld %ld %ld %u", &state, &ppid, &pgrp, &sess, &tty, &tpgid,
                    &flags) < 2)
      continue;
    const long pid = std::atol(n.c_str());
    parent[pid] = ppid;
    if (state == 'Z') continue;
    const fs::path x = fs::read_symlink(e.path() / "exe", ec);
    if (ec) continue;
    exe[pid] = x.filename().string();
    std::ifstream sm(e.path() / "status");
    while (std::getline(sm, line))
      if (line.rfind("VmRSS:", 0) == 0) rss[pid] = std::atof(line.c_str() + 6) / 1024.0;
  }
  const long me = static_cast<long>(::getpid());
  for (const auto& [pid, name] : exe) {
    if (name != "clangd") continue;
    long p = pid;
    for (int depth = 0; depth < 16 && p > 1; ++depth) {
      auto it = parent.find(p);
      if (it == parent.end()) break;
      p = it->second;
      if (p == me) {
        ++r.count;
        r.rssMiB += rss[pid];
        break;
      }
    }
  }
#endif
  return r;
}

double loadAvg() {
#ifdef __linux__
  std::ifstream f("/proc/loadavg");
  double l = 0;
  f >> l;
  return l;
#else
  return 0;
#endif
}

// Samples the peak RSS of clangd (and this process's thread count) in the background while a scenario runs.
struct Sampler {
  std::atomic<bool> stop{false};
  std::atomic<double> clangdPeak{0};
  std::atomic<int> clangdCountMax{0};
  std::thread th;
  Sampler() {
    th = std::thread([this] {
      while (!stop) {
        const ClangdMem c = clangdMem();
        double p = clangdPeak;
        while (c.rssMiB > p && !clangdPeak.compare_exchange_weak(p, c.rssMiB)) {}
        if (c.count > clangdCountMax) clangdCountMax = c.count;
        for (int i = 0; i < 10 && !stop; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(20));
      }
    });
  }
  ~Sampler() {
    stop = true;
    if (th.joinable()) th.join();
  }
};

// ---- results ----
struct Pct {
  size_t n = 0;
  double p50 = 0, p99 = 0, max = 0, avg = 0;
};
Pct pct(std::vector<double> v) {
  Pct r;
  r.n = v.size();
  if (v.empty()) return r;
  std::sort(v.begin(), v.end());
  r.p50 = v[v.size() / 2];
  r.p99 = v[std::min(v.size() - 1, static_cast<size_t>(static_cast<double>(v.size()) * 0.99))];
  r.max = v.back();
  double s = 0;
  for (double x : v) s += x;
  r.avg = s / static_cast<double>(v.size());
  return r;
}

std::string jsonEscape(const std::string& s) {
  std::string o;
  for (char c : s) {
    if (c == '"' || c == '\\') o += '\\';
    if (static_cast<unsigned char>(c) >= 0x20) o += c;
  }
  return o;
}

void emit(const std::string& line) {
  std::printf("STRESS: %s\n", line.c_str());
  std::fflush(stdout);
  if (const char* j = std::getenv("TENFOLD_STRESS_JSON"); j && *j) {
    std::ofstream out(j, std::ios::app);
    out << line << "\n";
  }
}

// One scenario's measurements. `metric` = a series of ms values (one per action / frame).
struct Scenario {
  std::string name;
  clk::time_point t0 = clk::now();
  double load0 = loadAvg();
  Sampler sampler;
  Scenario(std::string n) : name(std::move(n)) {
    resetHwm();
    std::printf("STRESS: --- %s (load average %.2f)\n", name.c_str(), load0);
    std::fflush(stdout);
  }
  Pct metric(const std::string& what, const std::vector<double>& ms, double budgetMs = 0) {
    const Pct p = pct(ms);
    std::ostringstream o;
    o << "{\"scenario\":\"" << name << "\",\"metric\":\"" << jsonEscape(what) << "\",\"n\":" << p.n
      << ",\"p50\":" << p.p50 << ",\"p99\":" << p.p99 << ",\"max\":" << p.max << ",\"avg\":" << p.avg
      << ",\"budget\":" << budgetMs << ",\"load\":" << loadAvg() << "}";
    emit(o.str());
    if (budgetMs > 0 && p.n > 0) {
#if defined(NDEBUG) && !defined(__SANITIZE_ADDRESS__)
      PERF_CHECK(p.p99 < tftest::perfBudget(budgetMs),
                 name + " " + what + " p99 (" + std::to_string(budgetMs) + " ms)");
#endif
    }
    return p;
  }
  void value(const std::string& what, double v, const char* unit = "") {
    std::ostringstream o;
    o << "{\"scenario\":\"" << name << "\",\"value\":\"" << jsonEscape(what) << "\",\"v\":" << v << ",\"unit\":\""
      << unit << "\"}";
    emit(o.str());
  }
  void finish() {
    const Mem m = selfMem();
    std::ostringstream o;
    o << "{\"scenario\":\"" << name << "\",\"summary\":1,\"seconds\":" << msSince(t0) / 1000.0
      << ",\"peakRssMiB\":" << m.hwmMiB << ",\"endRssMiB\":" << m.rssMiB << ",\"threads\":" << m.threads
      << ",\"clangdPeakRssMiB\":" << sampler.clangdPeak.load() << ",\"clangdProcessesMax\":" << sampler.clangdCountMax
      << ",\"load0\":" << load0 << ",\"load1\":" << loadAvg() << "}";
    emit(o.str());
  }
};

// ---- the rig: a real AppHost on a workspace ----
struct NullRenderer : gfx::Renderer {
  void beginFrame(int, int, gfx::Color) override {}
  void drawRect(const gfx::RectF&, gfx::Color) override {}
  void drawLine(float, float, float, float, float, gfx::Color) override {}
  void drawGlyphs(gfx::TextureId, const gfx::GlyphQuad*, int, gfx::Color) override {}
  void pushScissor(const gfx::RectF&) override {}
  void popScissor() override {}
  gfx::TextureId createTexture(int, int, gfx::TextureFormat) override { return 1; }
  void updateTexture(gfx::TextureId, int, int, int, int, const void*, int) override {}
  void destroyTexture(gfx::TextureId) override {}
  void endFrame() override {}
  bool readPixels(int, int, uint8_t*) override { return false; }
};

InputEvent keyEv(Key k, uint8_t mods = 0, InputType t = InputType::KeyDown) {
  InputEvent e;
  e.type = t;
  e.key = k;
  e.mods = mods;
  return e;
}

void writeFile(const fs::path& p, const std::string& text) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary | std::ios::trunc) << text;
}
std::string readFile(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

std::string replaceAll(std::string s, const std::string& from, const std::string& to) {
  for (size_t p = 0; (p = s.find(from, p)) != std::string::npos; p += to.size()) s.replace(p, from.size(), to);
  return s;
}

// The scratch directory of the process; removed when the process exits (unless TENFOLD_STRESS_KEEP=1).
struct Scratch {
  fs::path base = uitest::tempRoot() / ("tf_stress_" + std::to_string(clk::now().time_since_epoch().count()));
  ~Scratch() {
    if (std::string(envOr("TENFOLD_STRESS_KEEP")) == "1") return;
    std::error_code ec;
    fs::remove_all(base, ec);
  }
};
const fs::path& scratchBase() {
  static Scratch s;
  return s.base;
}

// A scratch copy of a generated project (made once per source directory); `@CORPUS_ROOT@` in compile_commands.json
// names the copy, as the corpus runner does. The index caches clangd and tenfold wrote are removed first.
fs::path scratchCopy(const fs::path& src) {
  static std::map<std::string, fs::path> copies;
  if (std::string(envOr("TENFOLD_STRESS_NOCOPY")) ==
      "1") {  // development: work in the directory itself (it is modified)
    std::error_code ec;
    fs::remove_all(src / ".cache", ec);
    fs::remove_all(src / ".tenfold", ec);
    const fs::path cc = src / "compile_commands.json";
    if (fs::is_regular_file(cc, ec)) writeFile(cc, replaceAll(readFile(cc), "@CORPUS_ROOT@", src.generic_string()));
    return src;
  }
  auto it = copies.find(src.string());
  if (it != copies.end()) {
    std::error_code ec;
    fs::remove_all(it->second / ".cache", ec);
    fs::remove_all(it->second / ".tenfold", ec);
    return it->second;
  }
  const fs::path dst = scratchBase() / ("p" + std::to_string(copies.size()));
  fs::create_directories(dst.parent_path());
  const auto t = clk::now();
  fs::copy(src, dst, fs::copy_options::recursive);
  const fs::path cc = dst / "compile_commands.json";
  std::error_code ec;
  if (fs::is_regular_file(cc, ec)) writeFile(cc, replaceAll(readFile(cc), "@CORPUS_ROOT@", dst.generic_string()));
  std::printf("STRESS: copied %s to %s in %.1f s\n", src.string().c_str(), dst.string().c_str(), msSince(t) / 1000);
  copies[src.string()] = dst;
  return dst;
}

struct Rig {
  fs::path root, user;
  NullRenderer rec;
  std::unique_ptr<AppHost> host;
  int64_t now = 1000;
  clk::time_point epoch = clk::now();
  std::vector<std::string> reports;
  std::vector<double> frameMs;  // every frame()

  Rig(const fs::path& r, bool clangd, const std::string& userSettings = {}, bool startEngine = true) : root(r) {
    static int n = 0;
    user = scratchBase() / ("user" + std::to_string(n++));
    fs::create_directories(user);
    if (!userSettings.empty()) writeFile(user / "settings.txt", userSettings);
    AppHost::Options o;
    o.userDir = user;
    o.renderer = &rec;
    o.startEngine = startEngine;
    o.editor.semantic.useClangd = clangd;
    o.editor.semantic.persistFastIndex = false;
    o.editor.vcs.enabled = false;
    o.watchSettings = false;
    o.defaultFile = "";
    o.report = [this](const std::string& s) { reports.push_back(s); };
    o.makeText = [](const Settings&, float, std::string*) -> std::unique_ptr<gfx::TextRenderer> {
      gfx::FontStack stack;
      stack.add(std::make_shared<tftext::MockFont>(
          U"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ._-:,;/()[]{}<>+*&#?=!\"'\\|", 8));
      return gfx::TextRenderer::fromFonts(std::move(stack));
    };
    host = std::make_unique<AppHost>(std::move(o));
  }
  ~Rig() {
    host.reset();
    std::error_code ec;
    fs::remove_all(user, ec);
  }

  bool open(const fs::path& target) {
    std::string err;
    const bool ok = host->open(target, {}, false, &err);
    if (!ok) reports.push_back(err);
    frame();
    return ok;
  }
  Editor& ed() { return *host->editor(); }
  EditorApp& app() { return *host->app(); }
  Workbench& bench() { return *host->bench(); }

  int64_t clock() const { return 1000 + static_cast<int64_t>(msSince(epoch)); }
  // One frame as the program runs it: pump engine results, tick the app, render. Returns its ms.
  double frame() {
    const auto t = clk::now();
    if (host->hasWorkspace()) {
      now = clock();
      host->editor()->pump();
      host->app()->tick(now);
      host->bench()->render(rec, 1280, 720);
    }
    const double ms = msSince(t);
    frameMs.push_back(ms);
    return ms;
  }
  // An action followed by the frame that shows it: the keystroke-to-paint time in ms.
  double step(const std::function<void()>& fn, const std::string& what = {}) {
    const auto t = clk::now();
    fn();
    frame();
    const double ms = msSince(t);
    if (ms > 1000) std::printf("STRESS: slow step %.0f ms%s%s\n", ms, what.empty() ? "" : ": ", what.c_str());
    return ms;
  }
  double key(Key k, uint8_t mods = 0) {
    return step([&] { app().handleEvent(keyEv(k, mods), clock()); });
  }
  double type(const std::string& s) {
    return step([&] {
      InputEvent e;
      e.type = InputType::Text;
      e.text = s;
      app().handleEvent(e, clock());
    });
  }
  double run(const char* cmd) {
    return step([&] { app().runCommand(cmd); });
  }
  bool until(const std::function<bool()>& pred, int timeoutMs, int pollMs = 5) {
    const auto t = clk::now();
    while (msSince(t) < timeoutMs) {
      frame();
      if (pred()) return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(pollMs));
    }
    return false;
  }
  // Frames for `ms` of wall time (the background work proceeds; every frame is recorded).
  void idle(int ms, int pollMs = 8) {
    until([] { return false; }, ms, pollMs);
  }
  Document* openDoc(const fs::path& p) {
    if (bench().openFile(p) < 0) return nullptr;
    bench().sync();
    frame();
    return ed().currentDocument();
  }
  TextView* view() { return dynamic_cast<TextView*>(bench().focused()); }
  size_t tabCount() {
    size_t n = 0;
    for (Panel* p : bench().panels()) n += p->tabs().tabs().size();
    return n;
  }
};

// ---- test data ----

// A synthetic project: `files` .cpp / .h pairs under modules, each with a few functions; no compile database
// (heuristic flags). Returns the root.
fs::path makeSmallProject(const char* tag, int files) {
  static int n = 0;
  const fs::path root = scratchBase() / (std::string(tag) + std::to_string(n++));
  for (int i = 0; i < files; ++i) {
    const std::string m = "mod" + std::to_string(i % 7), s = std::to_string(i);
    writeFile(root / m / ("u" + s + ".h"),
              "#pragma once\nint update_" + s + "(int a);\nstruct Obj" + s + " { int value; int get() const; };\n");
    std::string cpp = "#include \"u" + s + ".h\"\nint update_" + s + "(int a) {\n  int needle = a + " + s +
                      ";\n  return needle;\n}\nint Obj" + s + "::get() const { return value; }\n";
    writeFile(root / m / ("u" + s + ".cpp"), cpp);
  }
  return root;
}

// A big C++-looking file with `lines` lines and many `int` tokens and braces (folding, carets).
std::string bigSource(int lines) {
  std::string t;
  t.reserve(static_cast<size_t>(lines) * 28);
  for (int i = 0; i < lines; ++i) {
    switch (i % 8) {
      case 0: t += "int big_fn_" + std::to_string(i) + "(int a, int b) {\n"; break;
      case 1: t += "  int local_" + std::to_string(i) + " = a * " + std::to_string(i % 1000) + " + b;\n"; break;
      case 2: t += "  if (local_" + std::to_string(i - 1) + " > 5) return 1;\n"; break;
      case 3: t += "  const char* s = \"string // not a comment\";\n"; break;
      case 4: t += "  // trailing comment line\n"; break;
      case 5: t += "  return a;\n"; break;
      case 6: t += "}\n"; break;
      default: t += "\n"; break;
    }
  }
  return t;
}

// ---- scenarios ----

// Hundreds of tabs: open each file, cycle through them with the MRU switcher and the tab keys, type in one of them,
// close them all. Documents must be released again.
void scenarioTabs(Rig& r, Scenario& sc, size_t want) {
  std::vector<fs::path> files;
  const auto& src = r.ed().workspace().sourceFiles();
  const auto& hdr = r.ed().workspace().headerFiles();
  const size_t stride = std::max<size_t>(1, src.size() / std::max<size_t>(1, want * 2 / 3));
  for (size_t i = 0; i < src.size() && files.size() < want * 2 / 3; i += stride) files.push_back(src[i]);
  const size_t hstride = std::max<size_t>(1, hdr.size() / std::max<size_t>(1, want / 3));
  for (size_t i = 0; i < hdr.size() && files.size() < want; i += hstride) files.push_back(hdr[i]);
  REQUIRE(files.size() >= 10);
  const size_t docsBefore = r.ed().documents().size();

  std::vector<double> openMs, cycleMs, typeMs, closeMs;
  for (const fs::path& f : files)
    openMs.push_back(r.step(
        [&] {
          r.bench().openFile(f);
          r.bench().sync();
        },
        "open " + f.string()));
  CHECK_EQ(r.tabCount(), files.size());
  sc.metric("open a file into a new tab (+ frame)", openMs);

  Panel* p = r.bench().activePanel();
  for (size_t i = 0; i < files.size(); ++i)
    cycleMs.push_back(r.step([&] {
      r.bench().cycleMru(p, true);
      r.bench().sync();
    }));
  for (size_t i = 0; i < files.size(); ++i)
    cycleMs.push_back(r.step([&] {
      r.bench().stepTab(p, 1);
      r.bench().sync();
    }));
  sc.metric("switch to the next tab (MRU and sequential, + frame)", cycleMs);

  r.bench().openFile(files[files.size() / 2]);
  r.bench().sync();
  r.frame();
  r.key(Key::End, ModCtrl);
  for (int i = 0; i < 100; ++i) typeMs.push_back(r.type("x"));
  sc.metric("type a character with all tabs open", typeMs, 8.0);
  for (int i = 0; i < 100; ++i) r.key(Key::Backspace);

  // The edited document is unsaved: close it without the prompt (discard) like the others.
  std::vector<int> ids;
  for (Panel* q : r.bench().panels())
    for (const auto& t : q->tabs().tabs()) ids.push_back(t.id);
  for (int id : ids)
    closeMs.push_back(r.step([&] { r.bench().closeTab(id, true); }, "close " + r.bench().tabPath(id).string()));
  r.bench().sync();
  r.frame();
  sc.metric("close a tab (+ frame)", closeMs);
  CHECK_EQ(r.tabCount(), size_t(0));
  CHECK(r.ed().documents().size() <= docsBefore + 2);  // every document of the tabs was released
}

// Find in files across the whole project, then replace in files (open documents edited in place, closed ones on
// disk), then undo it: the open ones with Ctrl+Z, the closed ones by replacing back.
void scenarioFindReplace(Rig& r, Scenario& sc, const std::string& needle, size_t openFirst) {
  FindInFiles& f = r.app().findInFiles();
  std::vector<double> frames;
  const auto t0 = clk::now();
  f.open(needle);
  const bool done = r.until(
      [&] {
        frames.push_back(r.frameMs.back());
        return !f.running() && !f.matches().empty();
      },
      kSanitized ? 300000 : 120000, 2);
  const double findMs = msSince(t0);
  CHECK(done);
  sc.value("find in files: time to all results (" + needle + ")", findMs, "ms");
  sc.value("find in files: matches", static_cast<double>(f.matches().size()));
  sc.metric("frame while the search streams in", frames);
  const size_t found = f.matches().size();
  std::vector<double> stepMs;
  for (int i = 0; i < 40; ++i) stepMs.push_back(r.step([&] { f.step(true); }));
  sc.metric("step to the next result (+ frame)", stepMs);
  f.close();
  r.frame();
  REQUIRE(found > 0);

  // Open documents first so some of the replacements are edits in place.
  std::set<fs::path> openFiles;
  for (const SearchMatch& m : f.matches()) {
    if (openFiles.size() >= openFirst) break;
    if (openFiles.insert(m.file).second) r.bench().openFile(m.file);
  }
  r.bench().sync();
  r.frame();

  ReplaceInFiles& rep = r.app().replaceInFiles();
  const std::string repl = needle + "_W9I";
  rep.open(needle);
  REQUIRE(r.until([&] { return !rep.running() && !rep.entries().empty(); }, kSanitized ? 300000 : 120000, 2));
  const size_t entries = rep.entries().size();
  sc.value("replace in files: matches listed", static_cast<double>(entries));
  rep.replaceInput()->setText(repl, true);
  r.frame();
  const auto t1 = clk::now();
  rep.apply();  // the first call asks for confirmation (closed files are written)
  r.frame();
  CHECK(rep.apply());
  r.frame();
  sc.value("replace in files: apply", msSince(t1), "ms");
  const Editor::ReplaceInFilesResult res = rep.lastResult();
  sc.value("replace in files: occurrences replaced", static_cast<double>(res.replaced));
  sc.value("replace in files: open documents edited", static_cast<double>(res.openDocuments));
  sc.value("replace in files: files written", static_cast<double>(res.filesWritten));
  CHECK(res.errors.empty());
  CHECK_EQ(res.replaced + res.stale, entries);

  // Undo: Ctrl+Z in each open document (one undo step per document), then replace back on disk.
  std::vector<double> undoMs;
  for (const fs::path& p : openFiles) {
    r.openDoc(p);
    undoMs.push_back(r.key(Key('Z'), ModCtrl));
  }
  sc.metric("undo the replacement in an open document (+ frame)", undoMs);
  for (const fs::path& p : openFiles)
    if (Document* d = r.ed().document(p)) CHECK(d->buffer().text().find(repl) == std::string::npos);

  rep.open(repl);
  REQUIRE(r.until([&] { return !rep.running() && !rep.entries().empty(); }, kSanitized ? 300000 : 120000, 2));
  rep.replaceInput()->setText(needle, true);
  r.frame();
  rep.apply();
  r.frame();
  CHECK(rep.apply());
  r.frame();
  // Everything is back: nothing contains the replacement any more (closed files on disk, open ones in the buffers).
  FindInFiles& f2 = r.app().findInFiles();
  f2.open(repl);
  r.idle(600);  // past the debounce: the search has started
  REQUIRE(r.until([&] { return !f2.running(); }, kSanitized ? 300000 : 120000, 2));
  r.frame();
  size_t left = 0;
  for (const SearchMatch& m : f2.matches()) {
    (void)m;
    ++left;
  }
  sc.value("replacement text left after undoing", static_cast<double>(left));
  f2.close();
  r.frame();
}

// 10k carets (one per `int` of a 100k-line file), typing, deleting, undoing and collapsing them.
void scenarioCarets(Rig& r, Scenario& sc, int lines, size_t wantCarets) {
  const fs::path file = r.root / "w9i_big_carets.cpp";
  writeFile(file, bigSource(lines));
  REQUIRE(r.openDoc(file) != nullptr);
  r.frame();
  SearchOptions o;
  o.query = "int";
  o.caseSensitive = true;
  const auto t0 = clk::now();
  const size_t n = r.ed().selectAllOccurrences(&o);
  r.frame();
  sc.value("select all occurrences: carets", static_cast<double>(n));
  sc.value("select all occurrences: time", msSince(t0), "ms");
  CHECK(n >= wantCarets);
  CHECK_EQ(r.ed().selections().size(), n);
  std::vector<double> typeMs, delMs;
  const int keys = static_cast<int>(envSize("TENFOLD_STRESS_CARET_KEYS", 40));
  for (int i = 0; i < keys; ++i) typeMs.push_back(r.type("z"));
  sc.metric("type a character with " + std::to_string(n) + " carets", typeMs, 8.0);
  for (int i = 0; i < keys; ++i) delMs.push_back(r.key(Key::Backspace));
  sc.metric("Backspace with " + std::to_string(n) + " carets", delMs, 8.0);
  std::vector<double> moveMs;
  for (int i = 0; i < 20; ++i) moveMs.push_back(r.key(i % 2 ? Key::Left : Key::Right));
  sc.metric("move " + std::to_string(n) + " carets", moveMs, 8.0);
  const auto t1 = clk::now();
  r.key(Key('Z'), ModCtrl);
  sc.value("undo after typing with all carets: time", msSince(t1), "ms");
  const double c = r.key(Key::Escape);
  sc.value("Escape (collapse to one caret): time", c, "ms");
  CHECK_EQ(r.ed().selections().size(), size_t(1));
  std::vector<double> scroll;
  for (int i = 0; i < 20; ++i) scroll.push_back(r.key(Key::PageDown));
  sc.metric("PageDown after collapsing", scroll, 8.0);
  r.bench().closeTab(r.bench().activePanel()->activeId(), true);
  r.bench().sync();
  r.frame();
}

// Folding a 100k+ line file: collapse all / expand all / toggle, scrolling and typing while folded.
void scenarioFold(Rig& r, Scenario& sc, int lines) {
  const fs::path file = r.root / "w9i_big_fold.cpp";
  writeFile(file, bigSource(lines));
  const auto t0 = clk::now();
  REQUIRE(r.openDoc(file) != nullptr);
  sc.value("open the file (+ first frame)", msSince(t0), "ms");
  TextView* v = r.view();
  REQUIRE(v != nullptr);
  std::vector<double> foldOps;
  for (int round = 0; round < 3; ++round) {
    foldOps.push_back(r.run("Editor.CollapseAllRegions"));
    foldOps.push_back(r.run("Editor.ExpandAllRegions"));
  }
  sc.value("CollapseAllRegions (first)", foldOps[0], "ms");
  sc.value("ExpandAllRegions (first)", foldOps[1], "ms");
  sc.metric("collapse all / expand all (+ frame)", foldOps);
  r.run("Editor.CollapseAllRegions");
  std::vector<double> scroll, typeMs, toggle;
  for (int i = 0; i < 60; ++i) scroll.push_back(r.key(Key::PageDown));
  for (int i = 0; i < 20; ++i) scroll.push_back(r.key(Key::PageUp));
  sc.metric("PageDown / PageUp with everything collapsed", scroll, 8.0);
  for (int i = 0; i < 30; ++i) toggle.push_back(r.run("Editor.ToggleCollapseExpandRegion"));
  sc.metric("toggle one region (+ frame)", toggle, 8.0);
  r.key(Key::Home, ModCtrl);
  for (int i = 0; i < 100; ++i) typeMs.push_back(r.type("y"));
  sc.metric("type a character with everything collapsed", typeMs, 8.0);
  r.run("Editor.ExpandAllRegions");
  typeMs.clear();
  for (int i = 0; i < 100; ++i) typeMs.push_back(r.type("y"));
  sc.metric("type a character after expanding all", typeMs, 8.0);
  r.key(Key('Z'), ModCtrl);
  r.bench().closeTab(r.bench().activePanel()->activeId(), true);
  r.bench().sync();
  r.frame();
}

// A flood of terminal output (the shell script of the user setting) next to an editor that is typed into, while the
// fast index and clangd work on the project (when the rig has them).
void scenarioTerminalFlood(Rig& r, Scenario& sc, const fs::path& typeInto, int seconds) {
  REQUIRE(r.openDoc(typeInto) != nullptr);
  r.app().terminalPanel()->wake = [] {};
  r.bench().splitActive(Splitter::Orientation::Horizontal);
  r.frame();
  std::string err;
  const int id = r.app().terminalPanel()->show(&err);
  REQUIRE(id >= 0);
  r.frame();
  TerminalView* tv = r.app().terminalPanel()->view(id);
  REQUIRE(tv != nullptr);
  // Back to the editor: typing goes to the document while the terminal tab keeps painting in the other panel.
  Panel* edPanel = nullptr;
  for (Panel* p : r.bench().panels())
    if (!dynamic_cast<TerminalView*>(p->activeContent())) edPanel = p;
  REQUIRE(edPanel != nullptr);
  r.bench().setActivePanel(edPanel);
  r.bench().sync();
  r.frame();
  std::vector<double> typeMs;
  const auto t0 = clk::now();
  int sent = 0;
  while (msSince(t0) < seconds * 1000) {
    typeMs.push_back(r.type(sent % 40 == 39 ? "\n" : "k"));
    ++sent;
    std::this_thread::sleep_for(std::chrono::milliseconds(12));  // a fast typist
  }
  sc.metric("type a character while the terminal floods", typeMs, 8.0);
  size_t lines = 0;
  if (tv && tv->session()) lines = static_cast<size_t>(tv->session()->screen().rows());
  sc.value("terminal rows", static_cast<double>(lines));
  sc.metric("every frame during the flood", r.frameMs, 0);
  r.app().terminalPanel()->closeAll();
  r.frame();
}

fs::path firstSource(Rig& r) {
  const auto& s = r.ed().workspace().sourceFiles();
  return s.empty() ? fs::path() : s[s.size() / 3];
}

// Types continuously (paced like a person) in a source file while the fast index and clangd's background index run.
void scenarioTypeWhileIndexing(Rig& r, Scenario& sc, int keys, bool waitForIndex) {
  const fs::path f = firstSource(r);
  REQUIRE(!f.empty());
  REQUIRE(r.openDoc(f) != nullptr);
  r.key(Key::End, ModCtrl);
  std::vector<double> typeMs, phase1, phase2;
  const auto t0 = clk::now();
  bool fastDone = false;
  double fastAt = -1;
  for (int i = 0; i < keys; ++i) {
    const double ms = r.type(i % 30 == 29 ? "\n" : "q");
    typeMs.push_back(ms);
    if (!fastDone && r.ed().semantic().waitForFastIndex(0)) {
      fastDone = true;
      fastAt = msSince(t0);
    }
    (fastDone ? phase2 : phase1).push_back(ms);
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
  }
  sc.value("typed keys", keys);
  sc.value("fast index finished after (ms into typing, -1 = before)", fastAt);
  sc.metric("type a character, all", typeMs, 8.0);
  sc.metric("type a character while the fast index runs", phase1, 8.0);
  sc.metric("type a character after the fast index", phase2, 8.0);
  if (waitForIndex && r.ed().semantic().clangd()) {
    // keep typing until clangd's background index reports done (or a cap), recording the same series
    std::vector<double> more;
    const auto t1 = clk::now();
    bool done = false;
    while (msSince(t1) < 240000 && !done) {
      more.push_back(r.type("w"));
      std::this_thread::sleep_for(std::chrono::milliseconds(15));
      done = r.ed().semantic().status().clangdIndex.percentage == 100 && !r.ed().semantic().status().clangdIndex.active;
    }
    sc.metric("type a character while clangd's background index runs", more, 8.0);
    sc.value("clangd index finished", done ? 1 : 0);
    sc.value("clangd index wait", msSince(t1), "ms");
  }
  r.key(Key('Z'), ModCtrl);
}

// Switch the Visual Studio configuration while the index is being built; keep typing meanwhile.
void scenarioConfigSwitch(Rig& r, Scenario& sc, int switches) {
  const auto cfgs = r.ed().vsConfigurations();
  REQUIRE(cfgs.size() >= 2);
  const fs::path f = firstSource(r);
  REQUIRE(r.openDoc(f) != nullptr);
  r.key(Key::End, ModCtrl);
  std::vector<double> typeMs, switchMs, reloadMs;
  for (int s = 0; s < switches; ++s) {
    const std::string target = cfgs[static_cast<size_t>(s + 1) % cfgs.size()].name();
    r.run("Build.SelectConfiguration");
    auto& pk = r.app().configurationPicker();
    REQUIRE(pk.isOpen());
    const int idx = pk.popup()->indexOfKey(target);
    REQUIRE(idx >= 0);
    for (int guard = 0; guard < 64 && pk.popup()->selected() != idx; ++guard)
      r.key(pk.popup()->selected() < idx ? Key::Down : Key::Up);
    switchMs.push_back(r.key(Key::Enter));
    const auto t = clk::now();
    // Keep typing while the new configuration is prepared and clangd restarts (indexing again).
    bool done = false;
    while (msSince(t) < 180000 && !done) {
      typeMs.push_back(r.type("c"));
      std::this_thread::sleep_for(std::chrono::milliseconds(15));
      done = !pk.reloading();
    }
    reloadMs.push_back(msSince(t));
    CHECK(done);
    r.idle(300);
  }
  sc.metric("press Enter in the configuration picker (+ frame)", switchMs);
  sc.metric("type a character while the configuration reloads", typeMs, 8.0);
  sc.metric("time until the configuration finished reloading", reloadMs);
  CHECK_EQ(r.ed().workspace().activeVsConfiguration().name(), cfgs[static_cast<size_t>(switches) % cfgs.size()].name());
  r.key(Key('Z'), ModCtrl);
}

// Open the workspace, let it index for `indexMs`, close it (the empty window) and open it again, `cycles` times.
// No clangd process may outlive its workspace; threads and memory must not grow from cycle to cycle.
void scenarioOpenClose(Rig& r, Scenario& sc, const fs::path& target, int cycles, int indexMs, bool clangd) {
  std::vector<double> openMs, closeMs, rss, threads, trimmed;
  for (int c = 0; c < cycles; ++c) {
    openMs.push_back(r.step([&] {
      std::string e;
      r.host->open(target, {}, false, &e);
    }));
    CHECK(r.host->hasWorkspace());
    if (r.host->hasWorkspace()) {
      const fs::path f = firstSource(r);
      if (!f.empty()) r.openDoc(f);
      for (int i = 0; i < 20; ++i) r.type("e");
    }
    r.idle(indexMs);
    const auto t = clk::now();
    std::string e;
    CHECK(r.host->openEmpty(&e));
    closeMs.push_back(msSince(t));
    r.frame();
    if (clangd) {
      // clangd must be gone shortly after the workspace closed
      bool gone = false;
      for (int i = 0; i < 100 && !gone; ++i) {
        gone = clangdMem().count == 0;
        if (!gone) std::this_thread::sleep_for(std::chrono::milliseconds(50));
      }
      CHECK(gone);
    }
    const Mem m = selfMem();
    rss.push_back(m.rssMiB);
    threads.push_back(m.threads);
#ifdef __GLIBC__
    ::malloc_trim(0);  // what is left after returning the free heap: separates a leak from allocator retention
    trimmed.push_back(selfMem().rssMiB);
#endif
  }
  sc.metric("File > Open (build the workspace + first frame)", openMs);
  sc.metric("Close Solution (tear down the workspace)", closeMs);
  std::ostringstream o;
  for (size_t i = 0; i < rss.size(); ++i) o << (i ? "," : "") << static_cast<int>(rss[i]);
  sc.value("RSS MiB after each close: " + o.str(), rss.empty() ? 0 : rss.back(), "MiB");
  if (!trimmed.empty()) {
    o.str("");
    for (size_t i = 0; i < trimmed.size(); ++i) o << (i ? "," : "") << static_cast<int>(trimmed[i]);
    sc.value("RSS MiB after each close and malloc_trim: " + o.str(), trimmed.back(), "MiB");
  }
  o.str("");
  for (size_t i = 0; i < threads.size(); ++i) o << (i ? "," : "") << static_cast<int>(threads[i]);
  sc.value("threads after each close: " + o.str(), threads.empty() ? 0 : threads.back());
#ifdef __linux__
  if (threads.size() >= 3) {
    CHECK(threads.back() <= threads[1] + 4);                 // no thread leak from cycle to cycle
    if (!kSanitized) CHECK(rss.back() < rss[1] * 1.5 + 64);  // no unbounded growth
  }
#endif
}

// A terminal flood setting: the shell script prints a lot of coloured text for as long as it is read.
std::string floodSettings() {
  return "terminal.command = /bin/sh -c \"yes 'flood flood flood 0123456789 abcdefghijklmnopqrstuvwxyz "
         "ABCDEFGHIJKLMNOPQRSTUVWXYZ'\"\n";
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// Smoke tests: always run, tiny tree, no clangd.
// ---------------------------------------------------------------------------------------------------------------

TEST(stress_smoke_tabs_cycle_and_close) {
  Rig r(makeSmallProject("smoke_tabs", 60), false);
  REQUIRE(r.open(r.root));
  Scenario sc("smoke tabs");
  scenarioTabs(r, sc, 50);
  sc.finish();
}

TEST(stress_smoke_find_and_replace_in_files_and_back) {
  const fs::path root = makeSmallProject("smoke_find", 40);
  Rig r(root, false);
  REQUIRE(r.open(root));
  std::map<fs::path, std::string> before;
  for (const auto& f : r.ed().workspace().sourceFiles()) before[f] = readFile(f);
  Scenario sc("smoke find replace");
  scenarioFindReplace(r, sc, "needle", 5);
  sc.finish();
  for (const auto& [f, text] : before) CHECK(readFile(f) == text);  // the files are exactly as they were
}

TEST(stress_smoke_many_carets_typing) {
  Rig r(makeSmallProject("smoke_carets", 4), false);
  REQUIRE(r.open(r.root));
  Scenario sc("smoke carets");
  scenarioCarets(r, sc, 6000, 700);
  sc.finish();
}

TEST(stress_smoke_fold_big_file) {
  Rig r(makeSmallProject("smoke_fold", 4), false);
  REQUIRE(r.open(r.root));
  Scenario sc("smoke fold");
  scenarioFold(r, sc, kSanitized ? 4000 : 20000);
  sc.finish();
}

TEST(stress_smoke_open_close_workspace_cycles) {
  const fs::path root = makeSmallProject("smoke_oc", 30);
  Rig r(root, false);
  Scenario sc("smoke open close");
  scenarioOpenClose(r, sc, root, 4, 100, false);
  sc.finish();
}

TEST(stress_smoke_type_while_the_fast_index_builds) {
  const fs::path root = makeSmallProject("smoke_type", 80);
  Rig r(root, false);
  REQUIRE(r.open(root));
  Scenario sc("smoke type indexing");
  scenarioTypeWhileIndexing(r, sc, 60, false);
  sc.finish();
}

#ifndef _WIN32
TEST(stress_smoke_terminal_flood_beside_an_editor) {
  const fs::path root = makeSmallProject("smoke_term", 10);
  Rig r(root, false, floodSettings());
  REQUIRE(r.open(root));
  Scenario sc("smoke terminal flood");
  scenarioTerminalFlood(r, sc, r.ed().workspace().sourceFiles()[0], 2);
  sc.finish();
}
#endif

// ---------------------------------------------------------------------------------------------------------------
// The real thing: TENFOLD_STRESS_DIR (cmake variant) / TENFOLD_STRESS_VS_DIR (vs variant), with clangd.
// ---------------------------------------------------------------------------------------------------------------

#define STRESS_PROJECT(var)                                                                            \
  const char* srcEnv = std::getenv(var);                                                               \
  if (!srcEnv || !*srcEnv) {                                                                           \
    std::printf("    SKIP: set " var " to a generated project (tools/stress/run_editor_stress.sh)\n"); \
    return;                                                                                            \
  }                                                                                                    \
  const fs::path project = scratchCopy(srcEnv)

TEST(stress_ui_tabs_300_open_cycle_close) {
  STRESS_PROJECT("TENFOLD_STRESS_DIR");
  Rig r(project, true);
  REQUIRE(r.open(project));
  Scenario sc("tabs");
  scenarioTabs(r, sc, envSize("TENFOLD_STRESS_TABS", 300));
  sc.finish();
}

TEST(stress_ui_find_and_replace_in_files_whole_project) {
  STRESS_PROJECT("TENFOLD_STRESS_DIR");
  Rig r(project, false);  // the search and the replacement do not need clangd; the fast index is running
  REQUIRE(r.open(project));
  r.ed().semantic().waitForFastIndex(300000);
  Scenario sc("find replace");
  scenarioFindReplace(r, sc, envOr("TENFOLD_STRESS_NEEDLE", "Update"), 60);
  sc.finish();
}

TEST(stress_ui_10k_carets_typing) {
  STRESS_PROJECT("TENFOLD_STRESS_DIR");
  Rig r(project, false);
  REQUIRE(r.open(project));
  Scenario sc("10k carets");
  scenarioCarets(r, sc, 120000, 10000);
  sc.finish();
}

TEST(stress_ui_fold_unfold_120k_line_file) {
  STRESS_PROJECT("TENFOLD_STRESS_DIR");
  Rig r(project, false);
  REQUIRE(r.open(project));
  Scenario sc("fold 120k lines");
  scenarioFold(r, sc, 120000);
  sc.finish();
}

TEST(stress_ui_terminal_flood_while_indexing) {
  STRESS_PROJECT("TENFOLD_STRESS_DIR");
  Rig r(project, true, floodSettings());
  REQUIRE(r.open(project));
  Scenario sc("terminal flood");
  scenarioTerminalFlood(r, sc, firstSource(r), static_cast<int>(envSize("TENFOLD_STRESS_FLOOD_SECONDS", 30)));
  sc.finish();
}

TEST(stress_ui_type_continuously_while_the_index_builds) {
  STRESS_PROJECT("TENFOLD_STRESS_DIR");
  Rig r(project, true);
  REQUIRE(r.open(project));
  Scenario sc("type while indexing");
  scenarioTypeWhileIndexing(r, sc, static_cast<int>(envSize("TENFOLD_STRESS_KEYS", 600)), true);
  sc.finish();
}

TEST(stress_ui_open_close_workspace_while_indexing) {
  STRESS_PROJECT("TENFOLD_STRESS_DIR");
  Rig r(project, true);
  Scenario sc("open close");
  scenarioOpenClose(r, sc, project, static_cast<int>(envSize("TENFOLD_STRESS_CYCLES", 6)), 4000, true);
  sc.finish();
}

TEST(stress_ui_switch_vs_configuration_mid_index) {
  STRESS_PROJECT("TENFOLD_STRESS_VS_DIR");
  Rig r(project, true);
  REQUIRE(r.open(project));
  Scenario sc("config switch");
  scenarioConfigSwitch(r, sc, static_cast<int>(envSize("TENFOLD_STRESS_SWITCHES", 4)));
  sc.finish();
}

TEST(stress_ui_open_close_solution_while_indexing_vs) {
  STRESS_PROJECT("TENFOLD_STRESS_VS_DIR");
  Rig r(project, true);
  Scenario sc("open close vs");
  scenarioOpenClose(r, sc, project, static_cast<int>(envSize("TENFOLD_STRESS_CYCLES", 6)), 4000, true);
  sc.finish();
}
