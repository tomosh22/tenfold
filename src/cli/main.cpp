// tenfold-cli: a scriptable, headless front end for the editor model.
// It exists to exercise and demonstrate the engine (navigation history,
// clangd-backed semantics, fast index) without a GUI, and to serve as the
// reference for how a graphical front end drives tf::Editor.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/resource.h>
#endif

#include "app/Editor.h"
#include "semantic/CompileCommands.h"

using namespace tf;
namespace fs = std::filesystem;

namespace {

void usage() {
  std::cerr <<
      "usage: tenfold-cli <workspace-root> [options]\n"
      "       tenfold-cli compdb <file.sln|file.vcxproj> [--config Debug] [--platform x64] [-o out.json]\n"
      "       tenfold-cli bench <workspace-root> [--file <rel path>:<line>:<col>] [--sym <query>]... [--runs N]\n"
      "                         [--no-clangd] [--clangd <path>] [--wait-index <ms>] [--json]\n"
      "options:\n"
      "  --clangd <path>     clangd executable (default: from PATH)\n"
      "  --no-clangd         fast index only (approximate results)\n"
      "  --budget <ms>       how long to wait for clangd before showing approximate results (250)\n"
      "  --trace             log every LSP message to stderr\n"
      "  --quiet             suppress engine log\n"
      "commands (stdin, one per line):\n"
      "  open <file>[:line[:col]]   open a file and place the caret (1-based)\n"
      "  goto <line> [col]          move caret (ordinary caret motion)\n"
      "  def | decl | refs          go to definition / declaration / list references\n"
      "  sym <query>                workspace symbol search (fuzzy)\n"
      "  hover | complete           hover text / completion list at caret\n"
      "  switch                     switch between source and header\n"
      "  back | fwd                 navigate backward / forward\n"
      "  history                    print the navigation stack\n"
      "  insert <text>              insert text at caret (\\n for newline)\n"
      "  wait-index [ms]            wait for clangd background index\n"
      "  wait-file [ms]             wait for clangd to finish parsing the current file\n"
      "  wait-fast [ms]             wait for the fast index\n"
      "  status | where | diags | quit\n";
}

std::string unescape(const std::string& s) {
  std::string out;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\\' && i + 1 < s.size()) {
      char c = s[++i];
      out += c == 'n' ? '\n' : c == 't' ? '\t' : c;
    } else out += s[i];
  }
  return out;
}

void printLocs(const std::vector<SymbolLocation>& locs, Confidence c) {
  std::cout << "  [" << toString(c) << "] " << locs.size() << " result(s)\n";
  for (auto& l : locs) {
    std::cout << "    " << toString(l.loc);
    if (!l.kind.empty()) std::cout << "  " << l.kind;
    if (!l.name.empty()) std::cout << " " << (l.container.empty() ? l.name : l.container + "::" + l.name);
    std::cout << "\n";
  }
}

int runCompdb(int argc, char** argv) {
  if (argc < 3) { usage(); return 2; }
  fs::path in = argv[2];
  compdb::VcxprojOptions vo;
  fs::path out;
  for (int i = 3; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--config" && i + 1 < argc) vo.configuration = argv[++i];
    else if (a == "--platform" && i + 1 < argc) vo.platform = argv[++i];
    else if (a == "-o" && i + 1 < argc) out = argv[++i];
  }
  std::string err;
  std::vector<CompileCommand> cmds;
  if (in.extension() == ".sln") cmds = compdb::fromSolution(in, vo, &err);
  else cmds = compdb::fromVcxproj(in, vo, &err);
  if (!err.empty()) std::cerr << err;
  if (out.empty()) out = in.parent_path() / "compile_commands.json";
  if (!CompileDatabase::save(cmds, out, &err)) { std::cerr << err << "\n"; return 1; }
  std::cout << "wrote " << cmds.size() << " commands to " << out << "\n";
  return 0;
}

// ---------------------------------------------------------------------------
// bench: reproducible timings for the plan's performance budgets (docs/BENCHMARKS.md)
// ---------------------------------------------------------------------------

using BenchClock = std::chrono::steady_clock;

double msSince(BenchClock::time_point t) {
  return std::chrono::duration<double, std::milli>(BenchClock::now() - t).count();
}

double median(std::vector<double> v) {
  if (v.empty()) return 0;
  std::sort(v.begin(), v.end());
  size_t n = v.size();
  return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2;
}

// Peak resident set size of this process in MiB, or -1 when unavailable.
double peakRssMiB() {
#if defined(__linux__)
  std::ifstream f("/proc/self/status");
  std::string line;
  while (std::getline(f, line)) {
    if (line.rfind("VmHWM:", 0) == 0) return std::stod(line.substr(6)) / 1024.0;  // kB
  }
  return -1;
#elif defined(__unix__) || defined(__APPLE__)
  struct rusage ru;
  if (getrusage(RUSAGE_SELF, &ru) != 0) return -1;
#if defined(__APPLE__)
  return static_cast<double>(ru.ru_maxrss) / (1024.0 * 1024.0);  // bytes
#else
  return static_cast<double>(ru.ru_maxrss) / 1024.0;  // kB
#endif
#else
  return -1;
#endif
}

std::string jsonEscape(const std::string& s) {
  std::string o;
  for (unsigned char c : s) {
    if (c == '"' || c == '\\') { o += '\\'; o += static_cast<char>(c); }
    else if (c == '\n') o += "\\n";
    else if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; }
    else o += static_cast<char>(c);
  }
  return o;
}

// Ordered key/value store. The JSON writer and the Python aggregator treat every numeric key under
// "phases" generically, so new phases (fastIndexWarm, findInFiles, ...) are added with phase() only.
struct BenchReport {
  std::vector<std::pair<std::string, double>> phases;  // milliseconds unless the key says otherwise
  std::vector<std::pair<std::string, double>> counts;
  std::vector<std::pair<std::string, std::string>> meta;
  void phase(const std::string& k, double v) { phases.emplace_back(k, v); }
  void count(const std::string& k, double v) { counts.emplace_back(k, v); }
  void note(const std::string& k, const std::string& v) { meta.emplace_back(k, v); }
};

std::string num(double v) {
  char b[48];
  std::snprintf(b, sizeof b, "%.3f", v);
  return b;
}

void writeJson(std::ostream& os, const BenchReport& r) {
  auto obj = [&](const char* name, const auto& items, auto fmt) {
    os << "\"" << name << "\":{";
    bool first = true;
    for (auto& [k, v] : items) {
      os << (first ? "" : ",") << "\"" << jsonEscape(k) << "\":" << fmt(v);
      first = false;
    }
    os << "}";
  };
  os << "{\"schema\":1,";
  obj("meta", r.meta, [](const std::string& v) { return "\"" + jsonEscape(v) + "\""; });
  os << ",";
  obj("counts", r.counts, [](double v) { return num(v); });
  os << ",";
  obj("phases", r.phases, [](double v) { return num(v); });
  os << "}\n";
}

void writeTable(std::ostream& os, const BenchReport& r) {
  for (auto& [k, v] : r.meta) os << std::left << std::setw(34) << k << v << "\n";
  os << "\n";
  for (auto& [k, v] : r.counts) os << std::left << std::setw(34) << k << std::fixed << std::setprecision(0) << v << "\n";
  os << "\n";
  for (auto& [k, v] : r.phases) {
    os << std::left << std::setw(34) << k << std::right << std::setw(12) << std::fixed << std::setprecision(2) << v
       << (k == "peakRssMiB" ? " MiB" : " ms") << "\n";
  }
}

// "rel/path:line:col" -> parts. Splits from the right so Windows drive letters survive.
bool parseFileSpec(const std::string& spec, std::string& file, int& line, int& col) {
  size_t p1 = spec.rfind(':');
  if (p1 == std::string::npos || p1 == 0) return false;
  size_t p0 = spec.rfind(':', p1 - 1);
  if (p0 == std::string::npos || p0 == 0) return false;
  try {
    line = std::stoi(spec.substr(p0 + 1, p1 - p0 - 1));
    col = std::stoi(spec.substr(p1 + 1));
  } catch (...) { return false; }
  file = spec.substr(0, p0);
  return line >= 1 && col >= 1;
}

int runBench(int argc, char** argv) {
  if (argc < 3) { usage(); return 2; }
  fs::path root = argv[2];
  std::string fileSpec;
  std::vector<std::string> syms;
  int runs = 5;
  int waitIndexMs = 0;
  bool json = false;
  Editor::Options opt;
  for (int i = 3; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--file" && i + 1 < argc) fileSpec = argv[++i];
    else if (a == "--sym" && i + 1 < argc) syms.push_back(argv[++i]);
    else if (a == "--runs" && i + 1 < argc) runs = std::max(1, std::stoi(argv[++i]));
    else if (a == "--clangd" && i + 1 < argc) opt.semantic.clangdPath = argv[++i];
    else if (a == "--wait-index" && i + 1 < argc) waitIndexMs = std::max(0, std::stoi(argv[++i]));
    else if (a == "--no-clangd") opt.semantic.useClangd = false;
    else if (a == "--json") json = true;
    else { usage(); return 2; }
  }
  std::error_code ec;
  if (!fs::is_directory(root, ec)) { std::cerr << "not a directory: " << root << "\n"; return 2; }
  // Default: the README's s->area() case in examples/sample_project.
  if (fileSpec.empty() && fs::is_regular_file(root / "src" / "main.cpp", ec)) fileSpec = "src/main.cpp:12:17";
  if (syms.empty()) syms = {"Shape", "area"};
  std::string relFile;
  int line = 0, col = 0;
  if (!fileSpec.empty() && !parseFileSpec(fileSpec, relFile, line, col)) {
    std::cerr << "bad --file (expected <rel path>:<line>:<col>): " << fileSpec << "\n";
    return 2;
  }

  // Phase boundaries come from the engine's own log lines (no engine changes needed):
  // "workspace scan: N files in X ms" -> scan; "compile database: ..." -> compdb ready;
  // "clangd started: ..." -> clangd initialized.
  BenchClock::time_point t0 = BenchClock::now();
  double scanMs = -1, tCompdb = -1, tClangd = -1;
  std::mutex logMu;
  opt.log = [&](const std::string& m) {
    std::lock_guard<std::mutex> lk(logMu);
    if (m.rfind("workspace scan:", 0) == 0) {
      size_t p = m.rfind(" in ");
      if (p != std::string::npos) scanMs = std::stod(m.substr(p + 4));
    } else if (m.rfind("compile database:", 0) == 0) {
      tCompdb = msSince(t0);
    } else if (m.rfind("clangd started:", 0) == 0) {
      tClangd = msSince(t0);
    }
  };

  BenchReport rep;
  Editor ed(root, opt);
  t0 = BenchClock::now();
  std::string err;
  bool started = ed.start(&err);
  double startMs = msSince(t0);
  bool haveClangd = ed.semantic().clangd() != nullptr;
  auto st = ed.semantic().status();

  double compdb = -1, clangdStart = -1;
  {
    std::lock_guard<std::mutex> lk(logMu);
    // The compdb figure includes the fast-index wait when flags are heuristic (include inference needs it).
    if (scanMs >= 0 && tCompdb >= 0) compdb = std::max(0.0, tCompdb - scanMs);
    if (tCompdb >= 0 && tClangd >= 0) clangdStart = std::max(0.0, tClangd - tCompdb);
  }

  rep.note("workspace", ed.workspace().root().generic_string());
  rep.note("clangd", haveClangd ? st.clangdVersion : (started ? "disabled" : "unavailable: " + err));
  rep.note("compdbSource", st.compdbSource);
  rep.note("file", fileSpec.empty() ? "(none)" : fileSpec);
  rep.note("runs", std::to_string(runs));

  if (scanMs >= 0) rep.phase("scanMs", scanMs);
  if (compdb >= 0) rep.phase("compdbMs", compdb);
  if (clangdStart >= 0) rep.phase("clangdStartMs", clangdStart);
  rep.phase("startReturnMs", startMs);  // Editor::start(): scan + compdb + clangd launch

  bool fastDone = ed.semantic().waitForFastIndex(600000);
  rep.phase("fastIndexDoneMs", msSince(t0));  // launch -> fast index complete
  st = ed.semantic().status();
  if (fastDone) rep.phase("fastIndexMs", st.fastSeconds * 1000.0);  // indexing wall time itself
  rep.count("sourceFiles", static_cast<double>(ed.workspace().sourceFiles().size()));
  rep.count("headerFiles", static_cast<double>(ed.workspace().headerFiles().size()));
  rep.count("fastFiles", static_cast<double>(st.fastFiles));
  rep.count("fastSymbols", static_cast<double>(st.fastSymbols));
  rep.count("compdbEntries", static_cast<double>(st.compdbEntries));

  // --- open the file, wait for its TU, first and warm go-to-definition ---
  if (!relFile.empty()) {
    Location where{root / relFile, {line - 1, col - 1}};
    std::string e;
    auto tOpen = BenchClock::now();
    if (!ed.gotoLocation(where, &e)) {
      rep.note("fileError", e);
    } else {
      if (haveClangd) {
        if (ed.semantic().waitForFile(ed.currentDocument()->path(), 300000)) {
          rep.phase("tuParsedMs", msSince(t0));  // launch -> clangd fileStatus idle for the chosen file
          rep.phase("tuParsedAfterOpenMs", msSince(tOpen));
        } else {
          rep.note("tuParsed", "timeout");
        }
      }
      // One def request: time to the first callback and to the final (authoritative if clangd) one.
      bool firstWasApprox = false;
      auto timedDef = [&](double& firstMs, double& finalMs, size_t& n) {
        ed.gotoLocation(where);  // reset caret (not timed)
        bool fin = false;
        firstMs = finalMs = -1;
        n = 0;
        auto t = BenchClock::now();
        ed.gotoDefinition([&](Confidence c, const std::vector<SymbolLocation>& locs, const std::string&) {
          if (firstMs < 0) {
            firstMs = msSince(t);
            firstWasApprox = c == Confidence::Approximate;
          }
          if (c == Confidence::Authoritative || !haveClangd) {
            finalMs = msSince(t);
            n = locs.size();
            fin = true;
          }
        });
        ed.waitFor([&] { return fin; }, 30000);
      };
      double first, fin;
      size_t n;
      timedDef(first, fin, n);
      if (fin >= 0) {
        rep.phase("defFirstMs", fin);
        if (haveClangd && firstWasApprox) rep.phase("defFirstApproxMs", first);
        rep.count("defResults", static_cast<double>(n));
      } else {
        rep.note("defFirst", "timeout");
      }
      std::vector<double> warm;
      for (int i = 0; i < runs; ++i) {
        timedDef(first, fin, n);
        if (fin >= 0) warm.push_back(fin);
      }
      if (!warm.empty()) rep.phase("defWarmMs", median(warm));
    }
  }

  // --- workspace symbol search (clangd's background index may still be running; the percentage is recorded) ---
  if (haveClangd && waitIndexMs > 0) {
    auto tIdx = BenchClock::now();
    ed.semantic().waitForClangdIndex(waitIndexMs);
    rep.phase("clangdIndexWaitMs", msSince(tIdx));
  }
  rep.count("clangdIndexPercent", haveClangd ? ed.semantic().status().clangdIndex.percentage : -1);
  for (auto& q : syms) {
    std::vector<double> fastT;
    size_t fastN = 0;
    for (int i = 0; i < runs; ++i) {
      auto t = BenchClock::now();
      auto r = ed.semantic().approximateSymbols(q);
      fastT.push_back(msSince(t));
      fastN = r.size();
    }
    rep.phase("symFast[" + q + "]Ms", median(fastT));
    rep.count("symFast[" + q + "]Results", static_cast<double>(fastN));
    if (haveClangd) {
      std::vector<double> authT;
      size_t authN = 0;
      for (int i = 0; i < runs; ++i) {
        bool fin = false;
        double ms = -1;
        auto t = BenchClock::now();
        ed.findSymbols(q, [&](Confidence c, const std::vector<SymbolLocation>& locs, const std::string&) {
          if (c == Confidence::Authoritative) { ms = msSince(t); authN = locs.size(); fin = true; }
        });
        ed.waitFor([&] { return fin; }, 30000);
        if (ms >= 0) authT.push_back(ms);
      }
      if (!authT.empty()) {
        rep.phase("symAuth[" + q + "]Ms", median(authT));
        rep.count("symAuth[" + q + "]Results", static_cast<double>(authN));
      }
    }
  }

  rep.phase("peakRssMiB", peakRssMiB());  // editor process only (clangd is a child process)
  ed.shutdown();
  if (json) writeJson(std::cout, rep);
  else writeTable(std::cout, rep);
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) { usage(); return 2; }
  if (std::strcmp(argv[1], "compdb") == 0) return runCompdb(argc, argv);
  if (std::strcmp(argv[1], "bench") == 0) return runBench(argc, argv);

  Editor::Options opt;
  bool quiet = false;
  for (int i = 2; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--clangd" && i + 1 < argc) opt.semantic.clangdPath = argv[++i];
    else if (a == "--no-clangd") opt.semantic.useClangd = false;
    else if (a == "--budget" && i + 1 < argc) opt.semantic.clangdBudgetMs = std::stoi(argv[++i]);
    else if (a == "--trace") opt.semantic.traceLsp = true;
    else if (a == "--quiet") quiet = true;
    else { usage(); return 2; }
  }
  if (!quiet) opt.log = [](const std::string& m) { std::cerr << "[tenfold] " << m << "\n"; };

  Editor ed(argv[1], opt);
  auto t0 = std::chrono::steady_clock::now();
  std::string err;
  bool started = ed.start(&err);
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
  std::cout << "workspace " << ed.workspace().root() << ": " << ed.workspace().sourceFiles().size() << " sources, "
            << ed.workspace().headerFiles().size() << " headers (" << ms << " ms)\n";
  if (!started) std::cout << "warning: " << err << " (continuing with approximate results)\n";

  auto printCursor = [&] {
    if (ed.currentDocument()) std::cout << "  at " << toString(ed.cursor()) << "\n";
    else std::cout << "  (no document)\n";
  };

  // Each semantic command may produce two callbacks (approximate, then
  // authoritative). We wait for the authoritative one unless clangd is off.
  auto runNav = [&](auto issue) {
    bool gotFinal = false, gotAny = false;
    issue([&](Confidence c, const std::vector<SymbolLocation>& locs, const std::string& e) {
      gotAny = true;
      if (!e.empty()) std::cout << "  error: " << e << "\n";
      printLocs(locs, c);
      if (c == Confidence::Authoritative || !ed.semantic().clangd()) gotFinal = true;
    });
    ed.waitFor([&] { return gotFinal; }, 30000);
    if (!gotAny) std::cout << "  (timed out)\n";
    printCursor();
  };

  std::string line;
  while (std::getline(std::cin, line)) {
    std::istringstream ss(line);
    std::string cmd;
    ss >> cmd;
    if (cmd.empty() || cmd[0] == '#') continue;
    std::cout << "> " << line << "\n";
    try {
      if (cmd == "quit" || cmd == "exit") break;
      if (cmd == "open") {
        std::string spec;
        ss >> spec;
        int l = 1, c = 1;
        // file[:line[:col]]
        size_t p1 = spec.rfind(':');
        if (p1 != std::string::npos && p1 > 0 && std::isdigit(static_cast<unsigned char>(spec[p1 + 1 < spec.size() ? p1 + 1 : p1]))) {
          size_t p0 = spec.rfind(':', p1 - 1);
          if (p0 != std::string::npos && p0 > 0 && std::isdigit(static_cast<unsigned char>(spec[p0 + 1]))) {
            l = std::stoi(spec.substr(p0 + 1, p1 - p0 - 1));
            c = std::stoi(spec.substr(p1 + 1));
            spec = spec.substr(0, p0);
          } else {
            l = std::stoi(spec.substr(p1 + 1));
            spec = spec.substr(0, p1);
          }
        }
        std::string e;
        if (!ed.gotoLocation({ed.workspace().root() / spec, {l - 1, c - 1}}, &e)) std::cout << "  error: " << e << "\n";
        printCursor();
      } else if (cmd == "goto") {
        int l = 1, c = 1;
        ss >> l >> c;
        if (c < 1) c = 1;
        ed.moveCursor({l - 1, c - 1});
        printCursor();
      } else if (cmd == "def") {
        runNav([&](auto cb) { ed.gotoDefinition(cb); });
      } else if (cmd == "decl") {
        runNav([&](auto cb) { ed.gotoDeclaration(cb); });
      } else if (cmd == "refs") {
        runNav([&](auto cb) { ed.findReferences(cb); });
      } else if (cmd == "sym") {
        std::string q;
        std::getline(ss, q);
        q.erase(0, q.find_first_not_of(' '));
        runNav([&](auto cb) { ed.findSymbols(q, cb); });
      } else if (cmd == "hover") {
        bool done = false;
        ed.hover([&](const std::string& t, const std::string& e) {
          if (!e.empty()) std::cout << "  error: " << e << "\n";
          std::istringstream ts(t);
          std::string tl;
          while (std::getline(ts, tl)) std::cout << "  | " << tl << "\n";
          done = true;
        });
        ed.waitFor([&] { return done; }, 30000);
      } else if (cmd == "complete") {
        bool done = false;
        ed.complete([&](const std::vector<LspCompletionItem>& items, const std::string& e) {
          if (!e.empty()) std::cout << "  error: " << e << "\n";
          std::cout << "  " << items.size() << " completion(s)\n";
          for (size_t i = 0; i < items.size() && i < 15; ++i) std::cout << "    " << items[i].label << "  " << items[i].detail << "\n";
          done = true;
        });
        ed.waitFor([&] { return done; }, 30000);
      } else if (cmd == "switch") {
        bool done = false;
        ed.switchSourceHeader([&](bool ok, const std::string& e) {
          if (!ok) std::cout << "  error: " << e << "\n";
          done = true;
        });
        ed.waitFor([&] { return done; }, 30000);
        printCursor();
      } else if (cmd == "back") {
        std::cout << (ed.back() ? "  ok" : "  (nothing to go back to)") << "\n";
        printCursor();
      } else if (cmd == "fwd") {
        std::cout << (ed.forward() ? "  ok" : "  (nothing to go forward to)") << "\n";
        printCursor();
      } else if (cmd == "history") {
        auto& nav = ed.navigation();
        size_t i = 0;
        for (auto& e : nav.entries()) {
          std::cout << (i == nav.cursor() ? "  * " : "    ") << i << ": " << toString(e.loc)
                    << (e.kind == NavigationHistory::Kind::Jump ? " (jump" : " (caret") << (e.label.empty() ? "" : " " + e.label) << ")\n";
          ++i;
        }
        if (nav.cursor() == nav.entries().size()) std::cout << "  * (live) " << toString(ed.cursor()) << "\n";
      } else if (cmd == "insert") {
        std::string t;
        std::getline(ss, t);
        t.erase(0, t.find_first_not_of(' '));
        ed.insertText(unescape(t));
        printCursor();
      } else if (cmd == "wait-index") {
        int ms2 = 60000; ss >> ms2;
        std::cout << (ed.semantic().waitForClangdIndex(ms2) ? "  index ready" : "  timeout") << "\n";
      } else if (cmd == "wait-file") {
        int ms2 = 60000; ss >> ms2;
        if (ed.currentDocument()) std::cout << (ed.semantic().waitForFile(ed.currentDocument()->path(), ms2) ? "  file ready" : "  timeout") << "\n";
      } else if (cmd == "wait-fast") {
        int ms2 = 60000; ss >> ms2;
        std::cout << (ed.semantic().waitForFastIndex(ms2) ? "  fast index ready" : "  timeout") << "\n";
      } else if (cmd == "status") {
        auto s = ed.semantic().status();
        std::cout << "  clangd: " << (s.clangdRunning ? "running" : "not running") << " " << s.clangdVersion
                  << " encoding=" << s.offsetEncoding << " index=" << (s.clangdIndex.active ? "indexing " : "")
                  << s.clangdIndex.percentage << "%\n"
                  << "  fast index: " << (s.fastIndexDone ? "done" : "running") << " " << s.fastFiles << " files, "
                  << s.fastSymbols << " symbols" << (s.fastIndexDone ? " in " + std::to_string(s.fastSeconds) + "s" : "") << "\n"
                  << "  compile db: " << s.compdbSource << " " << s.compdbEntries << " entries (" << s.compdbPath << ")\n";
      } else if (cmd == "where") {
        printCursor();
      } else if (cmd == "diags") {
        ed.pump();
        if (ed.currentDocument()) {
          for (auto& d : ed.diagnostics(ed.currentDocument()->path()))
            std::cout << "  " << toString(d.range.start) << " [" << d.severity << "] " << d.message << "\n";
        }
      } else {
        std::cout << "  unknown command\n";
      }
    } catch (const std::exception& e) {
      std::cout << "  error: " << e.what() << "\n";
    }
  }
  ed.shutdown();
  return 0;
}
