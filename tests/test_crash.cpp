// D2: clangd crash recovery (against a fake clangd, tests/test_helper_main.cpp) and the process crash handler.
#include <atomic>
#include <chrono>
#include <csignal>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>

#include "app/Editor.h"
#include "core/CrashHandler.h"
#include "core/Platform.h"
#include "lsp/Process.h"
#include "test_main.h"

#ifndef _WIN32
#include <signal.h>
#include <sys/types.h>
#endif

using namespace tf;
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

namespace {

struct TempDir {
  fs::path path;
  TempDir() {
    static std::atomic<int> n{0};
    path =
        fs::temp_directory_path() / ("tenfold_crash_" + std::to_string(currentProcessId()) + "_" + std::to_string(n++));
    fs::remove_all(path);
    fs::create_directories(path);
    std::error_code ec;
    path = fs::weakly_canonical(path, ec);
  }
  ~TempDir() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
};

std::vector<std::string> readLines(const fs::path& p) {
  std::vector<std::string> out;
  std::ifstream in(p);
  for (std::string l; std::getline(in, l);) out.push_back(l);
  return out;
}

int countPrefix(const std::vector<std::string>& lines, const std::string& prefix) {
  int n = 0;
  for (auto& l : lines)
    if (l.rfind(prefix, 0) == 0) ++n;
  return n;
}

// An Editor on a temp workspace whose clangd is the fake server.
struct Fx {
  TempDir dir;
  fs::path file, log, launches, pidFile;
  std::unique_ptr<Editor> ed;
  bool ok = false;
  std::mutex mu;
  std::vector<std::string> statuses;  // Editor::Options::onClangdStatus, in order

  struct Config {
    std::vector<std::string> fakeArgs;
    int initialMs = 60, maxMs = 400, maxCrashes = 5, healthyMs = 600000;
  };

  explicit Fx(const Config& c) {
    file = dir.path / "a.cpp";
    log = dir.path / "fake.log";
    launches = dir.path / "fake.launches";
    pidFile = dir.path / "fake.pid";
    std::ofstream(file, std::ios::binary) << "int a;\nint b;\nint c;\n";
    Editor::Options o;
    o.semantic.clangdPath = TENFOLD_TEST_HELPER;
    o.semantic.clangdArgs = {"--fake-clangd", "--log=" + log.string(), "--launch-file=" + launches.string(),
                             "--pid-file=" + pidFile.string()};
    for (auto& a : c.fakeArgs) o.semantic.clangdArgs.push_back(a);
    o.semantic.semanticTokensDebounceMs = -1;
    o.semantic.restartBackoffInitialMs = c.initialMs;
    o.semantic.restartBackoffMaxMs = c.maxMs;
    o.semantic.restartMaxFastCrashes = c.maxCrashes;
    o.semantic.restartHealthyMs = c.healthyMs;
    o.log = [](const std::string& m) {
      if (std::getenv("TENFOLD_TEST_VERBOSE")) std::cerr << "    [engine] " << m << "\n";
    };
    o.onClangdStatus = [this](const std::string& s) {
      std::lock_guard<std::mutex> lk(mu);
      statuses.push_back(s);
    };
    ed = std::make_unique<Editor>(dir.path, o);
    std::string err;
    ok = ed->start(&err);
    if (!ok) std::cerr << "    engine start: " << err << "\n";
  }

  bool sawStatus(const std::string& s) {
    std::lock_guard<std::mutex> lk(mu);
    for (auto& x : statuses)
      if (x == s) return true;
    return false;
  }

  bool waitHealth(SemanticEngine::ClangdHealth::Phase phase, int ms = 20000) {
    return ed->waitFor([&] { return ed->semantic().clangdHealth().phase == phase; }, ms);
  }
  // Running again after `restarts` completed restarts.
  bool waitRestarted(int restarts, int ms = 20000) {
    return ed->waitFor(
        [&] {
          auto h = ed->semantic().clangdHealth();
          return h.phase == SemanticEngine::ClangdHealth::Phase::Running && h.totalRestarts >= restarts &&
                 ed->semantic().clangd() != nullptr;
        },
        ms);
  }

  struct Reply {
    bool done = false;
    std::string text, error;
    double ms = 0;
  };
  Reply hover(int line) {
    auto st = std::make_shared<std::pair<std::atomic<bool>, std::pair<std::string, std::string>>>();
    st->first = false;
    auto mu2 = std::make_shared<std::mutex>();
    Document* d = ed->document(file);
    Reply r;
    if (!d) return r;
    auto t0 = Clock::now();
    ed->semantic().hover(*d, {line, 0}, [st, mu2](std::string text, const std::string& err) {
      {
        std::lock_guard<std::mutex> lk(*mu2);
        st->second = {text, err};
      }
      st->first = true;
    });
    r.done = ed->waitFor([&] { return st->first.load(); }, 20000);
    r.ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    std::lock_guard<std::mutex> lk(*mu2);
    r.text = st->second.first;
    r.error = st->second.second;
    return r;
  }

  bool openAndEdit() {
    std::string e;
    if (!ed->gotoLocation({file, {1, 0}}, &e)) return false;
    ed->insertText("UNSAVED ");  // never written to disk: only a re-sent didOpen carries it
    return true;
  }
};

bool contains(const std::string& s, const std::string& w) { return s.find(w) != std::string::npos; }

}  // namespace

TEST(crash_backoff_delays) {
  SemanticEngine::Options o;  // defaults: 0.5 s doubling, capped at 30 s
  CHECK_EQ(SemanticEngine::backoffDelayMs(o, 1), 500);
  CHECK_EQ(SemanticEngine::backoffDelayMs(o, 2), 1000);
  CHECK_EQ(SemanticEngine::backoffDelayMs(o, 3), 2000);
  CHECK_EQ(SemanticEngine::backoffDelayMs(o, 4), 4000);
  CHECK_EQ(SemanticEngine::backoffDelayMs(o, 7), 30000);
  CHECK_EQ(SemanticEngine::backoffDelayMs(o, 100), 30000);
  CHECK_EQ(o.restartHealthyMs, 60000);
  SemanticEngine::ClangdHealth h;
  h.phase = SemanticEngine::ClangdHealth::Phase::Restarting;
  h.crashes = 2;
  CHECK_EQ(h.text(), std::string("clangd restarting (2)..."));
  h.phase = SemanticEngine::ClangdHealth::Phase::Stopped;
  h.crashes = 5;
  CHECK_EQ(h.text(), std::string("clangd stopped: crashed 5 times"));
}

TEST(crash_midrequest_errors_then_restarts_and_reopens_documents) {
  Fx::Config c;
  c.fakeArgs = {"--die-after-requests=2", "--die-launches=1"};
  Fx f(c);
  REQUIRE(f.ok);
  REQUIRE(f.openAndEdit());
  auto r1 = f.hover(1);
  REQUIRE(r1.done);
  CHECK(r1.error.empty());
  CHECK(contains(r1.text, "UNSAVED int b;"));
  // The second request kills the server: it must fail promptly, not hang.
  auto r2 = f.hover(1);
  REQUIRE(r2.done);
  CHECK(!r2.error.empty());
  CHECK(r2.ms < 5000);
  // The fast tier keeps answering while clangd is down or restarting (an approximate answer or an error, never a hang).
  auto r3 = f.hover(1);
  CHECK(r3.done);
  REQUIRE(f.waitRestarted(1));
  CHECK(f.sawStatus("clangd restarting (1)..."));
  // The new server holds the unsaved text.
  auto r4 = f.hover(1);
  REQUIRE(r4.done);
  CHECK(r4.error.empty());
  CHECK(contains(r4.text, "UNSAVED int b;"));
  auto lines = readLines(f.log);
  CHECK_EQ(countPrefix(lines, "launch "), 2);
  CHECK_EQ(countPrefix(lines, "open "), 2);
  // Edits after the restart still reach the new server.
  f.ed->insertText("AGAIN ");
  auto r5 = f.hover(1);
  CHECK(contains(r5.text, "UNSAVED AGAIN int b;"));
  f.ed->pump();
  CHECK(f.ed->clangdStatusText().empty());
}

#ifndef _WIN32
TEST(crash_segfault_restarts) {
  Fx::Config c;
  c.fakeArgs = {"--die-after-requests=1", "--die-launches=1", "--die-mode=segv"};
  Fx f(c);
  REQUIRE(f.ok);
  REQUIRE(f.openAndEdit());
  auto r = f.hover(1);
  REQUIRE(r.done);
  CHECK(!r.error.empty());
  REQUIRE(f.waitRestarted(1));
  auto r2 = f.hover(1);
  CHECK(contains(r2.text, "UNSAVED int b;"));
}

TEST(crash_hang_then_killed_fails_the_request) {
  Fx::Config c;
  c.fakeArgs = {"--hang-method=textDocument/hover"};
  Fx f(c);
  REQUIRE(f.ok);
  REQUIRE(f.openAndEdit());
  auto st = std::make_shared<std::atomic<int>>(0);
  Document* d = f.ed->document(f.file);
  REQUIRE(d != nullptr);
  f.ed->semantic().hover(*d, {1, 0}, [st](std::string, const std::string& err) { *st = err.empty() ? 1 : 2; });
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  CHECK_EQ(st->load(), 0);  // genuinely hung
  int pid = 0;
  std::ifstream(f.pidFile) >> pid;
  REQUIRE(pid > 0);
  auto t0 = Clock::now();
  ::kill(pid, SIGKILL);
  CHECK(f.ed->waitFor([&] { return st->load() != 0; }, 10000));
  CHECK_EQ(st->load(), 2);
  CHECK(Clock::now() - t0 < std::chrono::seconds(5));
  REQUIRE(f.waitRestarted(1));
}
#endif

TEST(crash_backoff_grows_between_restarts) {
  Fx::Config c;
  c.fakeArgs = {"--die-on-init-launches=3"};  // launches 1-3 die right after the handshake
  c.initialMs = 100;
  c.maxMs = 100000;
  Fx f(c);
  REQUIRE(f.ok);
  REQUIRE(f.waitRestarted(3));
  auto lines = readLines(f.log);
  std::vector<long long> t;
  for (auto& l : lines)
    if (l.rfind("launch ", 0) == 0) t.push_back(std::stoll(l.substr(l.find("t=") + 2)));
  REQUIRE(t.size() == 4);
  CHECK(t[1] - t[0] >= 100);  // 1st restart after 100 ms, 2nd after 200 ms, 3rd after 400 ms
  CHECK(t[2] - t[1] >= 200);
  CHECK(t[3] - t[2] >= 400);
  CHECK(f.sawStatus("clangd restarting (1)..."));
  CHECK(f.sawStatus("clangd restarting (2)..."));
  CHECK(f.sawStatus("clangd restarting (3)..."));
}

TEST(crash_gives_up_after_repeated_fast_crashes_and_manual_restart_recovers) {
  Fx::Config c;
  c.fakeArgs = {"--die-on-init-launches=3"};
  c.initialMs = 20;
  c.maxCrashes = 3;
  Fx f(c);
  REQUIRE(f.ok);
  REQUIRE(f.waitHealth(SemanticEngine::ClangdHealth::Phase::Stopped));
  auto h = f.ed->semantic().clangdHealth();
  CHECK_EQ(h.crashes, 3);
  CHECK_EQ(h.text(), std::string("clangd stopped: crashed 3 times"));
  f.ed->pump();
  CHECK_EQ(f.ed->clangdStatusText(), std::string("clangd stopped: crashed 3 times"));
  CHECK(f.ed->semantic().clangd() == nullptr);
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  CHECK_EQ(countPrefix(readLines(f.log), "launch "), 3);  // no further attempts
  // Manual restart: launch 4 is healthy.
  std::string err;
  REQUIRE(f.ed->restartClangd(&err));
  CHECK(f.ed->semantic().clangd() != nullptr);
  CHECK(f.ed->semantic().clangdHealth().phase == SemanticEngine::ClangdHealth::Phase::Running);
  CHECK_EQ(f.ed->semantic().clangdHealth().crashes, 0);
  f.ed->pump();
  CHECK(f.ed->clangdStatusText().empty());
  REQUIRE(f.openAndEdit());
  auto r = f.hover(1);
  CHECK(contains(r.text, "UNSAVED int b;"));
}

TEST(crash_manual_restart_reopens_documents) {
  Fx::Config c;
  Fx f(c);
  REQUIRE(f.ok);
  REQUIRE(f.openAndEdit());
  std::string err;
  REQUIRE(f.ed->restartClangd(&err));
  auto r = f.hover(1);
  REQUIRE(r.done);
  CHECK(contains(r.text, "UNSAVED int b;"));
  auto lines = readLines(f.log);
  CHECK_EQ(countPrefix(lines, "launch "), 2);
  CHECK_EQ(countPrefix(lines, "open "), 2);
  CHECK_EQ(f.ed->semantic().clangdHealth().totalRestarts, 1);
}

TEST(crash_shutdown_during_backoff_does_not_hang) {
  Fx::Config c;
  c.fakeArgs = {"--die-after-requests=1", "--die-launches=1"};
  c.initialMs = 600000;  // ten minutes: only an interrupted wait lets this test finish
  c.maxMs = 600000;
  Fx f(c);
  REQUIRE(f.ok);
  REQUIRE(f.openAndEdit());
  auto r = f.hover(1);
  CHECK(!r.error.empty());
  REQUIRE(f.waitHealth(SemanticEngine::ClangdHealth::Phase::Restarting));
  CHECK_EQ(f.ed->semantic().clangdHealth().text(), std::string("clangd restarting (1)..."));
  auto t0 = Clock::now();
  f.ed->shutdown();
  CHECK(Clock::now() - t0 < std::chrono::seconds(10));
}

TEST(crash_manual_restart_during_backoff_takes_over) {
  Fx::Config c;
  c.fakeArgs = {"--die-after-requests=1", "--die-launches=1"};
  c.initialMs = 600000;
  c.maxMs = 600000;
  Fx f(c);
  REQUIRE(f.ok);
  REQUIRE(f.openAndEdit());
  (void)f.hover(1);
  REQUIRE(f.waitHealth(SemanticEngine::ClangdHealth::Phase::Restarting));
  std::string err;
  REQUIRE(f.ed->restartClangd(&err));
  auto r = f.hover(1);
  CHECK(contains(r.text, "UNSAVED int b;"));
  CHECK_EQ(countPrefix(readLines(f.log), "launch "), 2);
}

// ---------------------------------------------------------------------------
// Process crash handler
// ---------------------------------------------------------------------------

namespace {
std::string crashFileOf(const fs::path& dir, const char* ext) {
  std::error_code ec;
  for (auto& e : fs::directory_iterator(dir, ec))
    if (e.path().extension() == ext && e.path().filename().string().rfind("crash-", 0) == 0) return e.path().string();
  return {};
}
}  // namespace

TEST(crash_handler_writes_report) {
  for (const char* how : {"segv", "abort", "fpe"}) {
#ifdef _WIN32
    if (std::string(how) == "fpe") continue;  // raise(SIGFPE) is not an exception on Windows
#endif
    TempDir dir;
    fs::path crashDir = dir.path / "crashes";
    Process p;
    std::string err;
    REQUIRE(p.start(TENFOLD_TEST_HELPER, {"crash", crashDir.string(), how}, {}, {}, &err));
    p.closeStdin();
    char buf[64];
    while (p.read(buf, sizeof buf) > 0) {}
    CHECK(p.wait(20000) != 0);
#ifdef _WIN32
    std::string report = crashFileOf(crashDir, ".dmp");
    CHECK(!report.empty());
    if (!report.empty()) CHECK(fs::file_size(report) > 1000);
#else
    std::string report = crashFileOf(crashDir, ".txt");
    CHECK(!report.empty());
    if (report.empty()) continue;
    std::ifstream in(report);
    std::stringstream ss;
    ss << in.rdbuf();
    std::string text = ss.str();
    const char* name = std::string(how) == "segv" ? "SIGSEGV" : std::string(how) == "abort" ? "SIGABRT" : "SIGFPE";
    CHECK(contains(text, name));
    CHECK(contains(text, "pid: "));
    CHECK(contains(text, "fault address: 0x"));
    size_t bt = text.find("backtrace:");
    CHECK(bt != std::string::npos);
    // At least one frame address after the marker: "[0x..." from backtrace_symbols_fd (glibc), "#0 0x..." from the
    // manual frame walk (macOS). Not a byte count: the formats differ per platform.
    CHECK(bt != std::string::npos && text.find("0x", bt) != std::string::npos);
#endif
  }
}

TEST(crash_default_dir_is_under_user_data) {
  fs::path d = defaultCrashDir();
  if (d.empty()) return;  // no home directory in this environment
  CHECK_EQ(d.filename().string(), std::string("crashes"));
  CHECK_EQ(d.parent_path().filename().string(), std::string("tenfold"));
}
