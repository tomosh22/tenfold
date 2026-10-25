// D3: the local rotated log (core/Log.h).
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <thread>

#include "app/Editor.h"
#include "core/Log.h"
#include "core/Platform.h"
#include "test_main.h"

using namespace tf;
namespace fs = std::filesystem;

namespace {

struct TempDir {
  fs::path path;
  TempDir() {
    static std::atomic<int> n{0};
    path =
        fs::temp_directory_path() / ("tenfold_log_" + std::to_string(currentProcessId()) + "_" + std::to_string(n++));
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

void setEnv(const char* name, const char* value) {
#ifdef _WIN32
  _putenv_s(name, value ? value : "");
#else
  if (value) ::setenv(name, value, 1);
  else ::unsetenv(name);
#endif
}

// Leaves the global logger stopped and the environment clean whatever a test does.
struct LogGuard {
  LogGuard() {
    Log::shutdown();
    setEnv("TENFOLD_LOG", nullptr);
  }
  ~LogGuard() {
    Log::shutdown();
    setEnv("TENFOLD_LOG", nullptr);
  }
};

LogConfig testConfig() {
  LogConfig c;
  c.useEnv = false;
  c.level = LogLevel::Debug;
  c.flushIntervalMs = 20;
  c.sessionHeader = false;
  return c;
}

std::vector<std::string> readLines(const fs::path& p) {
  std::vector<std::string> v;
  std::ifstream in(p, std::ios::binary);
  std::string l;
  while (std::getline(in, l)) v.push_back(l);
  return v;
}

std::string readAll(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

// "2026-10-25T12:34:56.789Z X tN cat: msg"
bool isRecordHeader(const std::string& l) {
  if (l.size() < 30) return false;
  const char* t = "dddd-dd-ddTdd:dd:dd.dddZ";
  for (size_t i = 0; i < 24; ++i) {
    if (t[i] == 'd' ? !std::isdigit(static_cast<unsigned char>(l[i])) : l[i] != t[i]) return false;
  }
  return l[24] == ' ' && l[26] == ' ' && l[27] == 't';
}

}  // namespace

TEST(log_line_format_and_levels) {
  LogGuard g;
  TempDir d;
  const fs::path f = d.path / "log.txt";
  LogConfig c = testConfig();
  c.level = LogLevel::Warn;
  Log::init(c);
  Log::setFile(f);
  CHECK(Log::enabled(LogLevel::Error));
  CHECK(Log::enabled(LogLevel::Warn));
  CHECK(!Log::enabled(LogLevel::Info));
  CHECK(!Log::enabled(LogLevel::Debug));
  Log::error("clangd", "e-msg");
  Log::warn("vcs", "w-msg");
  Log::info("index", "i-msg");
  Log::debug("ui", "d-msg");
  Log::flush();
  auto lines = readLines(f);
  REQUIRE(lines.size() == 2);
  CHECK(isRecordHeader(lines[0]));
  CHECK(lines[0].find(" E t") == 24);
  CHECK(lines[0].find(" clangd: e-msg") != std::string::npos);
  CHECK(lines[1].find(" W t") == 24);
  CHECK(lines[1].find(" vcs: w-msg") != std::string::npos);

  Log::setLevel(LogLevel::Debug);
  CHECK(Log::enabled(LogLevel::Debug));
  Log::debug("ui", "now visible\nsecond line\r\n");
  Log::flush();
  lines = readLines(f);
  REQUIRE(lines.size() == 4);
  CHECK(lines[2].find(" D t") == 24);
  CHECK_EQ(lines[3], std::string("    second line"));  // continuation lines are indented, \r removed
  CHECK(Log::parseLevel("DEBUG", LogLevel::Off) == LogLevel::Debug);
  CHECK(Log::parseLevel("warning", LogLevel::Off) == LogLevel::Warn);
  CHECK(Log::parseLevel("nonsense", LogLevel::Info) == LogLevel::Info);
}

TEST(log_session_header_is_written_once_per_file) {
  LogGuard g;
  TempDir d;
  const fs::path f = d.path / "sub" / "log.txt";  // the directory is created by the first record
  LogConfig c = testConfig();
  c.sessionHeader = true;
  Log::init(c);
  Log::setFile(f);
  CHECK(!fs::exists(f.parent_path()));
  Log::info("ui", "hello");
  Log::flush();
  auto lines = readLines(f);
  REQUIRE(lines.size() == 2);
  CHECK(lines[0].find("session start: commit ") != std::string::npos);
  CHECK(lines[0].find(", pid " + std::to_string(currentProcessId())) != std::string::npos);
  Log::info("ui", "again");
  Log::flush();
  CHECK_EQ(readLines(f).size(), size_t(3));
}

TEST(log_env_var_overrides_configured_level) {
  LogGuard g;
  TempDir d;
  setEnv("TENFOLD_LOG", "debug");
  LogConfig c = testConfig();
  c.useEnv = true;
  c.level = LogLevel::Error;
  Log::init(c);
  CHECK(Log::level() == LogLevel::Debug);
  Log::setLevel(LogLevel::Error);  // the environment wins over later settings too
  CHECK(Log::level() == LogLevel::Debug);
  Log::shutdown();

  setEnv("TENFOLD_LOG", "error");
  c.level = LogLevel::Debug;
  Log::init(c);
  CHECK(Log::level() == LogLevel::Error);
  Log::shutdown();

  setEnv("TENFOLD_LOG", "bogus");  // unparsable: the configured level stays
  c.level = LogLevel::Warn;
  Log::init(c);
  CHECK(Log::level() == LogLevel::Warn);
}

TEST(log_disabled_creates_no_file) {
  LogGuard g;
  TempDir d;
  const fs::path ws = d.path / "ws";
  fs::create_directories(ws);
  LogConfig c = testConfig();
  c.level = LogLevel::Off;
  c.fallbackFile = d.path / "user" / "log.txt";
  Log::init(c);
  Log::setWorkspace(ws);
  Log::error("x", "never written");
  Log::flush();
  Log::shutdown();
  CHECK(!fs::exists(ws / ".tenfold"));
  CHECK(!fs::exists(d.path / "user"));

  setEnv("TENFOLD_LOG", "off");
  c.useEnv = true;
  c.level = LogLevel::Debug;
  Log::init(c);
  Log::setWorkspace(ws);
  Log::error("x", "never written");
  Log::shutdown();
  CHECK(!fs::exists(ws / ".tenfold"));

  // Enabled but nothing logged: still no file (it is created by the first record).
  setEnv("TENFOLD_LOG", nullptr);
  Log::init(testConfig());
  Log::setWorkspace(ws);
  Log::shutdown();
  CHECK(!fs::exists(ws / ".tenfold"));
}

TEST(log_concurrent_threads_never_tear_lines) {
  LogGuard g;
  TempDir d;
  const fs::path f = d.path / "log.txt";
  LogConfig c = testConfig();
  c.maxBytes = 1u << 30;
  Log::init(c);
  Log::setFile(f);
  constexpr int kThreads = 8, kPerThread = 2000;
  std::vector<std::thread> ts;
  for (int t = 0; t < kThreads; ++t)
    ts.emplace_back([t] {
      const std::string pad(40 + t * 7, static_cast<char>('a' + t));
      for (int i = 0; i < kPerThread; ++i) {
        Log::write(i % 97 == 0 ? LogLevel::Error : LogLevel::Info, "thr",
                   "w" + std::to_string(t) + " seq=" + std::to_string(i) + " " + pad + " END");
        if (i % 500 == 0) Log::flush();  // flushes racing with writers
      }
    });
  for (auto& t : ts) t.join();
  Log::flush();
  auto lines = readLines(f);
  CHECK_EQ(lines.size(), size_t(kThreads * kPerThread));
  std::map<int, int> next;
  for (const auto& l : lines) {
    if (!isRecordHeader(l)) {
      CHECK(isRecordHeader(l));
      break;
    }
    size_t p = l.find(" thr: w");
    REQUIRE(p != std::string::npos);
    int t = l[p + 7] - '0';
    size_t s = l.find("seq=", p);
    REQUIRE(s != std::string::npos);
    int seq = std::atoi(l.c_str() + s + 4);
    CHECK_EQ(seq, next[t]++);  // per-thread order is preserved
    const std::string pad(40 + t * 7, static_cast<char>('a' + t));
    CHECK_EQ(l.substr(l.find(' ', s) + 1), pad + " END");
  }
  for (int t = 0; t < kThreads; ++t) CHECK_EQ(next[t], kPerThread);
}

TEST(log_rotates_at_size_boundary_and_keeps_three_files) {
  LogGuard g;
  TempDir d;
  const fs::path f = d.path / "log.txt";
  LogConfig c = testConfig();
  c.maxBytes = 1000;
  c.keep = 3;
  Log::init(c);
  Log::setFile(f);
  constexpr int kN = 120;
  for (int i = 0; i < kN; ++i) {
    char b[32];
    std::snprintf(b, sizeof b, "line-%04d", i);
    Log::info("rot", b);
    if (i % 7 == 0) Log::flush();  // chunk sizes vary: rotation must still be at line boundaries
  }
  Log::flush();
  CHECK(fs::exists(d.path / "log.txt"));
  CHECK(fs::exists(d.path / "log.1.txt"));
  CHECK(fs::exists(d.path / "log.2.txt"));
  CHECK(!fs::exists(d.path / "log.3.txt"));
  int expectNext = -1, total = 0;
  // Oldest to newest: log.2, log.1, log.txt. Sequence numbers are consecutive across the files.
  for (const char* name : {"log.2.txt", "log.1.txt", "log.txt"}) {
    CHECK(fs::file_size(d.path / name) <= 1000u);
    for (const auto& l : readLines(d.path / name)) {
      CHECK(isRecordHeader(l));
      int n = std::atoi(l.c_str() + l.find("line-") + 5);
      if (expectNext >= 0) CHECK_EQ(n, expectNext);
      expectNext = n + 1;
      ++total;
    }
  }
  CHECK_EQ(expectNext, kN);  // the newest record is in log.txt
  CHECK(total < kN);         // the oldest records were dropped
  CHECK(total > 20);

  // A single line larger than the limit is still written whole (to an otherwise empty file).
  Log::info("rot", std::string(3000, 'x'));
  Log::flush();
  auto big = readLines(d.path / "log.txt");
  REQUIRE(!big.empty());
  CHECK(big.back().size() > 3000);
}

TEST(log_rotation_resumes_existing_file_size) {
  LogGuard g;
  TempDir d;
  const fs::path f = d.path / "log.txt";
  {
    std::ofstream o(f, std::ios::binary);
    o << std::string(900, 'p') << "\n";  // 901 bytes already there
  }
  LogConfig c = testConfig();
  c.maxBytes = 1000;
  Log::init(c);
  Log::setFile(f);
  Log::info("a", "this record does not fit next to the old content of the file, it is too long for that");
  Log::flush();
  CHECK(fs::exists(d.path / "log.1.txt"));
  CHECK_EQ(readLines(d.path / "log.1.txt").size(), size_t(1));
  CHECK_EQ(readLines(f).size(), size_t(1));
}

TEST(log_calls_after_shutdown_are_noops) {
  LogGuard g;
  TempDir d;
  const fs::path f = d.path / "log.txt";
  Log::init(testConfig());
  Log::setFile(f);
  Log::info("a", "before");
  Log::shutdown();  // flushes
  CHECK_EQ(readLines(f).size(), size_t(1));
  CHECK(!Log::running());
  CHECK(!Log::enabled(LogLevel::Error));
  Log::error("a", "after");
  Log::flush();
  Log::setWorkspace(d.path);
  Log::setFile(d.path / "other.txt");
  Log::shutdown();  // twice
  CHECK_EQ(readLines(f).size(), size_t(1));
  CHECK(!fs::exists(d.path / ".tenfold"));
  CHECK(!fs::exists(d.path / "other.txt"));
  CHECK(Log::filePath().empty());

  // Logging threads racing with shutdown must neither crash nor hang.
  Log::init(testConfig());
  Log::setFile(f);
  std::atomic<bool> go{true};
  std::vector<std::thread> ts;
  for (int t = 0; t < 4; ++t)
    ts.emplace_back([&] {
      while (go.load()) Log::info("race", "x");
    });
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  Log::shutdown();
  go = false;
  for (auto& t : ts) t.join();
  // It can be started again.
  Log::init(testConfig());
  Log::setFile(f);
  Log::info("a", "again");
  Log::flush();
  CHECK(readAll(f).find("again") != std::string::npos);
}

TEST(log_workspace_switch_moves_the_file) {
  LogGuard g;
  TempDir d;
  const fs::path w1 = d.path / "w1", w2 = d.path / "w2";
  fs::create_directories(w1);
  fs::create_directories(w2);
  LogConfig c = testConfig();
  c.fallbackFile = d.path / "user" / "log.txt";
  Log::init(c);
  Log::info("a", "before workspace");  // goes to the user-level fallback
  Log::setWorkspace(w1);
  CHECK(Log::filePath() == Log::workspaceLogPath(w1));
  Log::info("a", "in w1");
  Log::setWorkspace(w2);  // pending records still land in w1's file
  Log::info("a", "in w2");
  Log::flush();
  CHECK(readAll(d.path / "user" / "log.txt").find("before workspace") != std::string::npos);
  const std::string a = readAll(w1 / ".tenfold" / "log.txt"), b = readAll(w2 / ".tenfold" / "log.txt");
  CHECK(a.find("in w1") != std::string::npos);
  CHECK(a.find("in w2") == std::string::npos);
  CHECK(b.find("in w2") != std::string::npos);
  Log::setWorkspace({});
  CHECK(Log::filePath() == c.fallbackFile);
}

TEST(log_tee_routes_categories_and_keeps_callbacks) {
  LogGuard g;
  TempDir d;
  const fs::path f = d.path / "log.txt";
  Log::init(testConfig());
  Log::setFile(f);
  std::vector<std::string> seen;
  auto cb = Log::tee("clangd", [&](const std::string& m) { seen.push_back(m); });
  cb("vcs: git at /x");
  cb("fast index: 3 files");
  cb("clangd crashed (2 in a row)");
  cb("something else");
  auto plain = Log::tee("editor");
  plain("no next callback");
  Log::flush();
  CHECK_EQ(seen.size(), size_t(4));
  CHECK_EQ(seen[0], std::string("vcs: git at /x"));  // the callback gets the original text
  const auto lines = readLines(f);
  REQUIRE(lines.size() == 5);
  CHECK(lines[0].find(" I t") == 24);
  CHECK(lines[0].find(" vcs: git at /x") != std::string::npos);
  CHECK(lines[1].find(" index: fast index: 3 files") != std::string::npos);
  CHECK(lines[2].find(" W t") == 24);
  CHECK(lines[2].find(" clangd: clangd crashed") != std::string::npos);
  CHECK(lines[3].find(" clangd: something else") != std::string::npos);
  CHECK(lines[4].find(" editor: no next callback") != std::string::npos);
}

TEST(log_editor_wiring_writes_workspace_log) {
  LogGuard g;
  TempDir d;
  Log::init(testConfig());
  Log::setFile({});
  Editor::Options opt;
  opt.semantic.useClangd = false;
  std::vector<std::string> seen;
  std::mutex mu;
  opt.log = [&](const std::string& m) {
    std::lock_guard<std::mutex> lk(mu);
    seen.push_back(m);
  };
  {
    Editor ed(d.path, opt);
    CHECK(ed.logFilePath() == d.path / ".tenfold" / "log.txt");
    std::string err;
    ed.start(&err);
    ed.shutdown();
  }
  Log::flush();
  const std::string text = readAll(d.path / ".tenfold" / "log.txt");
  CHECK(text.find("editor: workspace scan:") != std::string::npos);
  std::lock_guard<std::mutex> lk(mu);
  bool scan = false;
  for (auto& m : seen) scan |= m.rfind("workspace scan:", 0) == 0;
  CHECK(scan);  // the injected callback still receives the message
}

TEST(log_cost_filtered_and_logged_calls) {
  LogGuard g;
  TempDir d;
  using Clock = std::chrono::steady_clock;
  LogConfig c = testConfig();
  c.level = LogLevel::Info;
  c.maxBytes = 1u << 30;
  Log::init(c);
  Log::setFile(d.path / "log.txt");
  const int kFiltered = 5'000'000, kLogged = 200'000;
  auto t0 = Clock::now();
  for (int i = 0; i < kFiltered; ++i) TF_LOG_DEBUG("bench", "debug message that is filtered out");
  auto t1 = Clock::now();
  for (int i = 0; i < kFiltered / 10; ++i) Log::debug("bench", "debug message that is filtered out");
  auto t2 = Clock::now();
  for (int i = 0; i < kLogged; ++i)
    Log::info("bench", "info message with some typical length, number " + std::to_string(i));
  auto t3 = Clock::now();
  Log::flush();
  auto t4 = Clock::now();
  auto ns = [](auto a, auto b) { return std::chrono::duration<double, std::nano>(b - a).count(); };
  const double filteredMacro = ns(t0, t1) / kFiltered, filteredCall = ns(t1, t2) / (kFiltered / 10.0),
               info = ns(t2, t3) / kLogged;
  std::cerr << "    log cost: filtered-out TF_LOG_DEBUG " << filteredMacro << " ns, filtered-out Log::debug "
            << filteredCall << " ns, Log::info (buffered) " << info << " ns, final flush of " << kLogged << " records "
            << ns(t3, t4) / 1e6 << " ms\n";
  CHECK(filteredCall < 1000);  // generous: only catches formatting or locking on the filtered path
  CHECK(info < 20000);
}
