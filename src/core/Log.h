#pragma once
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace tf {

// Local, rotated diagnostic log (D3). There is no telemetry: nothing leaves the machine.
//
// One line per record, UTC timestamps with milliseconds, a small per-thread number and a category:
//   2026-10-25T12:34:56.789Z I t3 clangd: clangd started: clangd version 18, offsetEncoding=utf-16
// Multi-line messages are written as one record whose continuation lines are indented by four spaces, so every
// line of the file is either a record header or a continuation.
//
// Threading: every function is safe from any thread. write() formats outside any lock, appends to a small buffer
// under a short mutex and returns; a background thread does the disk I/O (flush every flushIntervalMs, at once for
// errors, and when the buffer is large). A caller therefore never waits for the disk, except in flush(),
// setWorkspace()/setFile() and shutdown(), which are explicit. If the disk stalls, the buffer is capped and further
// records are counted as dropped (one "dropped N" record is written afterwards).
//
// Lifetime: nothing is logged (and no file is created) until init() has been called, and never once logging is
// off (level Off or TENFOLD_LOG=off). The file is created by the first record, not by init(). After shutdown()
// every call is a no-op, so logging from destructors of statics is safe. The state is intentionally leaked so it
// outlives static destruction.
//
// Not async-signal-safe: do not call from a signal handler (CrashHandler never does; it only records the path).
enum class LogLevel : int { Error = 0, Warn = 1, Info = 2, Debug = 3, Off = 4 };

struct LogConfig {
  LogLevel level = LogLevel::Info;  // TENFOLD_LOG in the environment (error|warn|info|debug|off) overrides this
  bool useEnv = true;               // false: ignore TENFOLD_LOG (tests)
  uint64_t maxBytes = 5u << 20;     // rotate log.txt when it would grow beyond this
  int keep = 3;                     // log.txt, log.1.txt, log.2.txt (files in total)
  int flushIntervalMs = 500;
  // Where records go before a workspace is set: <user data dir>/tenfold/log.txt. Empty = that default.
  std::filesystem::path fallbackFile;
  bool sessionHeader = true;  // write version / platform / pid when the first file is opened
};

namespace detail {
extern std::atomic<int> g_logLevel;  // highest enabled LogLevel as int; -1 (everything off) until init()
}

class Log {
 public:
  // Starts the logger (idempotent while running; after shutdown() it can be started again). Registers an atexit
  // hook that flushes and joins the writer thread.
  static void init(const LogConfig& cfg = {});
  // Flushes, joins the writer thread and closes the file. Later calls are no-ops.
  static void shutdown();
  static bool running();

  // Cheap (one relaxed atomic load); use it to skip building expensive messages.
  static bool enabled(LogLevel l) { return static_cast<int>(l) <= detail::g_logLevel.load(std::memory_order_relaxed); }
  static LogLevel level() {
    const int v = detail::g_logLevel.load(std::memory_order_relaxed);
    return v < 0 ? LogLevel::Off : static_cast<LogLevel>(v);
  }
  // Changes the level of a running logger; ignored when TENFOLD_LOG fixed it at init().
  static void setLevel(LogLevel l);

  static void write(LogLevel l, std::string_view category, std::string_view msg);
  static void error(std::string_view category, std::string_view msg) { write(LogLevel::Error, category, msg); }
  static void warn(std::string_view category, std::string_view msg) { write(LogLevel::Warn, category, msg); }
  static void info(std::string_view category, std::string_view msg) { write(LogLevel::Info, category, msg); }
  static void debug(std::string_view category, std::string_view msg) { write(LogLevel::Debug, category, msg); }

  // Blocks until everything written so far is on disk.
  static void flush();

  // Redirects to <root>/.tenfold/log.txt (buffered records for the old file are written there first). An empty
  // root goes back to the fallback (user data dir) file. Remembered before init() too.
  static void setWorkspace(const std::filesystem::path& root);
  // Redirects to an explicit file (tests). Rotation siblings live next to it.
  static void setFile(const std::filesystem::path& file);
  // The file records currently go to (it may not exist yet); empty when logging is not running.
  static std::filesystem::path filePath();
  static std::filesystem::path workspaceLogPath(const std::filesystem::path& root);
  static std::filesystem::path userLogPath();  // <user data dir>/tenfold/log.txt; empty without a home directory

  // Number of records dropped because the disk stalled (statistics, tests).
  static uint64_t droppedCount();

  // Adapter for the Options::log callbacks (std::function<void(const std::string&)>): logs `msg` under a category
  // (see route()) and then calls `next` if it is set. Severity is Warn for messages that say they failed, could
  // not do something or crashed, Error for "giving up", else Info.
  static std::function<void(const std::string&)> tee(std::string category,
                                                     std::function<void(const std::string&)> next = {});
  // The routing used by tee(): "vcs: ..." -> vcs, "fast index..." -> index, "file watcher: ..." -> watch,
  // "build: ..." / "compile database" / "cmake" / "vcxproj" / "vs configuration" -> build, "clangd..." -> clangd,
  // anything else -> defaultCategory. `rest` receives the message without a redundant "<category>: " prefix.
  static std::string_view route(std::string_view defaultCategory, std::string_view msg, std::string_view* rest);

  // The last `n` lines of a log file (all when n == 0). Empty when the file cannot be read.
  static std::vector<std::string> tailLines(const std::filesystem::path& file, size_t n);

  static LogLevel parseLevel(std::string_view s, LogLevel fallback);
  static const char* levelName(LogLevel l);
};

#define TF_LOG(level, category, msg)                                       \
  do {                                                                     \
    if (::tf::Log::enabled(level)) ::tf::Log::write(level, category, msg); \
  } while (0)
#define TF_LOG_DEBUG(category, msg) TF_LOG(::tf::LogLevel::Debug, category, msg)

}  // namespace tf
