#include "core/Log.h"

#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <system_error>
#include <thread>

#include "core/CrashHandler.h"
#include "core/Platform.h"

#ifndef TENFOLD_GIT_COMMIT
#define TENFOLD_GIT_COMMIT "unknown"
#endif

namespace fs = std::filesystem;

namespace tf {

namespace detail {
std::atomic<int> g_logLevel{-1};
}

namespace {

constexpr size_t kMaxRecordMessage = 16 * 1024;   // longer messages are cut ("...[truncated]")
constexpr size_t kUrgentBytes = 64 * 1024;        // buffered bytes that wake the writer early
constexpr size_t kMaxBuffered = 8 * 1024 * 1024;  // beyond this (stalled disk) records are dropped

enum State { kStopped = 0, kRunning = 1 };

struct Impl {
  // --- buffer (short critical sections only) ---
  std::mutex mu;
  std::condition_variable cv;
  std::string buf;
  bool urgent = false;
  bool stopThread = false;
  std::atomic<int> state{kStopped};
  std::atomic<uint64_t> dropped{0};
  uint64_t droppedReported = 0;  // io thread / ioMu
  int flushIntervalMs = 500;
  std::thread thread;
  std::mutex lifecycleMu;  // serialises init() / shutdown()

  // --- file (ioMu) ---
  std::mutex ioMu;
  fs::path fallback;
  fs::path wsRoot;        // empty: use the fallback
  fs::path explicitFile;  // setFile() override
  fs::path path;          // current target, mirrors the above
  std::ofstream out;
  bool open = false;
  uint64_t size = 0;
  uint64_t maxBytes = 5u << 20;
  int keep = 3;
  bool header = true;
  bool envFixed = false;
  std::string startTs;  // timestamp of init(), used by the session header
};

Impl& impl() {
  static Impl* p = new Impl;  // leaked on purpose: usable during static destruction
  return *p;
}

int threadNumber() {
  static std::atomic<int> next{1};
  thread_local int id = next.fetch_add(1);  // trivially destructible: safe at thread / process exit
  return id;
}

char levelChar(LogLevel l) {
  switch (l) {
    case LogLevel::Error: return 'E';
    case LogLevel::Warn: return 'W';
    case LogLevel::Info: return 'I';
    default: return 'D';
  }
}

// "2026-10-25T12:34:56.789Z" (24 chars) without gmtime (not thread-safe on every libc).
void appendTimestamp(std::string& s) {
  using namespace std::chrono;
  const auto now = system_clock::now();
  const int64_t ms = duration_cast<milliseconds>(now.time_since_epoch()).count();
  int64_t days = ms / 86400000;
  int64_t rem = ms % 86400000;
  if (rem < 0) {
    rem += 86400000;
    --days;
  }
  // civil_from_days (H. Hinnant)
  const int64_t z = days + 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const int64_t doe = z - era * 146097;
  const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const int64_t mp = (5 * doy + 2) / 153;
  const int d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
  const int m = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
  const int64_t y = yoe + era * 400 + (m <= 2);
  char b[32];
  std::snprintf(b, sizeof b, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", static_cast<int>(y), m, d,
                static_cast<int>(rem / 3600000), static_cast<int>(rem / 60000 % 60), static_cast<int>(rem / 1000 % 60),
                static_cast<int>(rem % 1000));
  s.append(b);
}

// One record: "<ts> <L> t<N> <category>: <message>\n" with continuation lines indented.
void formatRecord(std::string& s, LogLevel l, std::string_view cat, std::string_view msg) {
  appendTimestamp(s);
  s += ' ';
  s += levelChar(l);
  s += " t";
  s += std::to_string(threadNumber());
  s += ' ';
  for (char c : cat) s += static_cast<unsigned char>(c) <= ' ' ? '_' : c;
  if (cat.empty()) s += "log";
  s += ": ";
  bool truncated = false;
  if (msg.size() > kMaxRecordMessage) {
    msg = msg.substr(0, kMaxRecordMessage);
    truncated = true;
  }
  while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r' || msg.back() == ' ')) msg.remove_suffix(1);
  for (char c : msg) {
    if (c == '\n') s += "\n    ";
    else if (c == '\r') continue;
    else if (c == '\t' || static_cast<unsigned char>(c) >= ' ') s += c;
    else s += '?';
  }
  if (truncated) s += "...[truncated]";
  s += '\n';
}

fs::path sibling(const fs::path& base, int index) {
  if (index == 0) return base;
  fs::path stem = base.stem();
  return base.parent_path() / (stem.string() + "." + std::to_string(index) + base.extension().string());
}

std::string platformString() {
#if defined(_WIN32)
  const char* os = "windows";
#elif defined(__APPLE__)
  const char* os = "macos";
#else
  const char* os = "linux";
#endif
#if defined(__x86_64__) || defined(_M_X64)
  const char* arch = "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
  const char* arch = "arm64";
#else
  const char* arch = "unknown-arch";
#endif
  return std::string(os) + "/" + arch;
}

// ---- file operations, all under ioMu ----

void closeLocked(Impl& s) {
  if (s.open) {
    s.out.flush();
    s.out.close();
  }
  s.out.clear();
  s.open = false;
}

void rotateLocked(Impl& s) {
  closeLocked(s);
  std::error_code ec;
  if (s.keep <= 1) {
    fs::remove(s.path, ec);
  } else {
    fs::remove(sibling(s.path, s.keep - 1), ec);
    for (int i = s.keep - 2; i >= 1; --i) {
      fs::path from = sibling(s.path, i);
      if (!fs::exists(from, ec)) continue;
      fs::path to = sibling(s.path, i + 1);
      fs::remove(to, ec);
      fs::rename(from, to, ec);
    }
    fs::path first = sibling(s.path, 1);
    fs::remove(first, ec);
    fs::rename(s.path, first, ec);
    if (ec) fs::remove(s.path, ec);
  }
  s.size = 0;
}

bool openLocked(Impl& s) {
  if (s.open) return true;
  if (s.path.empty()) return false;
  std::error_code ec;
  if (s.path.has_parent_path()) fs::create_directories(s.path.parent_path(), ec);
  s.out.clear();
  s.out.open(s.path, std::ios::binary | std::ios::app);
  if (!s.out.is_open()) return false;
  s.open = true;
  const auto sz = fs::file_size(s.path, ec);
  s.size = ec ? 0 : static_cast<uint64_t>(sz);
  if (s.header) {
    std::string h;
    h = s.startTs;
    h += " I t0 log: ---- session start: commit " TENFOLD_GIT_COMMIT ", ";
    h += platformString();
    h += ", pid " + std::to_string(currentProcessId());
    h += s.wsRoot.empty() ? "" : ", workspace " + s.wsRoot.generic_string();
    h += " ----\n";
    s.out.write(h.data(), static_cast<std::streamsize>(h.size()));
    s.size += h.size();
  }
  return true;
}

// Writes whole lines, rotating at line boundaries so no file exceeds maxBytes (except for one oversized line).
void writeLocked(Impl& s, const std::string& chunk) {
  size_t pos = 0;
  const size_t n = chunk.size();
  while (pos < n) {
    if (!openLocked(s)) return;  // no usable file: records are lost, never fatal
    const uint64_t room = s.maxBytes > s.size ? s.maxBytes - s.size : 0;
    size_t end = pos;
    while (end < n) {
      size_t nl = chunk.find('\n', end);
      size_t le = nl == std::string::npos ? n : nl + 1;
      if (le - pos > room) {
        if (end == pos && s.size == 0) end = le;  // a single line larger than the limit: write it whole
        break;
      }
      end = le;
    }
    if (end == pos) {
      rotateLocked(s);
      continue;
    }
    s.out.write(chunk.data() + pos, static_cast<std::streamsize>(end - pos));
    s.size += end - pos;
    pos = end;
  }
  if (s.open) s.out.flush();
}

// Takes the buffered records and writes them. Holds ioMu for the whole call (flush() relies on that).
void drainLocked(Impl& s) {
  std::string chunk;
  {
    std::lock_guard<std::mutex> lk(s.mu);
    chunk.swap(s.buf);
    s.urgent = false;
  }
  const uint64_t d = s.dropped.load();
  if (d > s.droppedReported) {
    std::string r;
    formatRecord(r, LogLevel::Warn, "log",
                 "dropped " + std::to_string(d - s.droppedReported) + " record(s): disk too slow");
    chunk += r;
    s.droppedReported = d;
  }
  if (!chunk.empty()) writeLocked(s, chunk);
}

void retarget(Impl& s) {
  fs::path want = !s.explicitFile.empty() ? s.explicitFile
                  : !s.wsRoot.empty()     ? Log::workspaceLogPath(s.wsRoot)
                                          : s.fallback;
  if (want == s.path) return;
  closeLocked(s);
  s.path = want;
  setCrashLogPath(s.path);
}

void writerThread(Impl* s) {
  std::unique_lock<std::mutex> lk(s->mu);
  while (!s->stopThread) {
    s->cv.wait_for(lk, std::chrono::milliseconds(s->flushIntervalMs),
                   [&] { return s->stopThread || s->urgent || s->buf.size() >= kUrgentBytes; });
    if (s->stopThread) break;
    if (s->buf.empty() && s->dropped.load() == s->droppedReported) continue;
    lk.unlock();
    {
      std::lock_guard<std::mutex> io(s->ioMu);
      drainLocked(*s);
    }
    lk.lock();
  }
}

void atExitShutdown() { Log::shutdown(); }

bool wordEq(std::string_view a, const char* b) {
  size_t n = std::strlen(b);
  if (a.size() != n) return false;
  for (size_t i = 0; i < n; ++i)
    if (std::tolower(static_cast<unsigned char>(a[i])) != b[i]) return false;
  return true;
}

}  // namespace

// ---------------------------------------------------------------------------

LogLevel Log::parseLevel(std::string_view s, LogLevel fallback) {
  while (!s.empty() && s.front() == ' ') s.remove_prefix(1);
  while (!s.empty() && s.back() == ' ') s.remove_suffix(1);
  if (wordEq(s, "error")) return LogLevel::Error;
  if (wordEq(s, "warn") || wordEq(s, "warning")) return LogLevel::Warn;
  if (wordEq(s, "info")) return LogLevel::Info;
  if (wordEq(s, "debug") || wordEq(s, "trace")) return LogLevel::Debug;
  if (wordEq(s, "off") || wordEq(s, "none") || wordEq(s, "0") || wordEq(s, "false")) return LogLevel::Off;
  return fallback;
}

const char* Log::levelName(LogLevel l) {
  switch (l) {
    case LogLevel::Error: return "error";
    case LogLevel::Warn: return "warn";
    case LogLevel::Info: return "info";
    case LogLevel::Debug: return "debug";
    default: return "off";
  }
}

fs::path Log::workspaceLogPath(const fs::path& root) { return root / ".tenfold" / "log.txt"; }

fs::path Log::userLogPath() {
  fs::path crashes = defaultCrashDir();
  if (crashes.empty()) return {};
  return crashes.parent_path() / "log.txt";
}

void Log::init(const LogConfig& cfg) {
  Impl& s = impl();
  std::lock_guard<std::mutex> life(s.lifecycleMu);
  if (s.state.load() == kRunning) return;
  LogLevel level = cfg.level;
  bool fixed = false;
  if (cfg.useEnv) {
    if (const char* e = std::getenv("TENFOLD_LOG")) {
      const LogLevel v = parseLevel(e, LogLevel::Off);
      if (v != LogLevel::Off || parseLevel(e, LogLevel::Debug) == LogLevel::Off) {  // not "unparsable"
        level = v;
        fixed = true;
      }
    }
  }
  {
    std::lock_guard<std::mutex> io(s.ioMu);
    s.maxBytes = cfg.maxBytes ? cfg.maxBytes : (5u << 20);
    s.keep = cfg.keep < 1 ? 1 : cfg.keep;
    s.header = cfg.sessionHeader;
    s.envFixed = fixed;
    s.startTs.clear();
    appendTimestamp(s.startTs);
    s.fallback = cfg.fallbackFile.empty() ? Log::userLogPath() : cfg.fallbackFile;
    retarget(s);
    s.droppedReported = s.dropped.load();
  }
  {
    std::lock_guard<std::mutex> lk(s.mu);
    s.buf.clear();
    s.stopThread = false;
    s.urgent = false;
    s.flushIntervalMs = cfg.flushIntervalMs < 10 ? 10 : cfg.flushIntervalMs;
    s.state.store(kRunning);
  }
  detail::g_logLevel.store(level == LogLevel::Off ? -1 : static_cast<int>(level));
  s.thread = std::thread(writerThread, &s);  // also at level Off: setLevel() may enable logging later
  static std::once_flag once;
  std::call_once(once, [] { std::atexit(atExitShutdown); });
}

bool Log::running() { return impl().state.load() == kRunning; }

void Log::shutdown() {
  Impl& s = impl();
  std::lock_guard<std::mutex> life(s.lifecycleMu);
  if (s.state.load() != kRunning) return;
  detail::g_logLevel.store(-1);
  {
    std::lock_guard<std::mutex> lk(s.mu);
    s.stopThread = true;
    s.state.store(kStopped);
  }
  s.cv.notify_all();
  if (s.thread.joinable()) s.thread.join();
  {
    std::lock_guard<std::mutex> io(s.ioMu);
    drainLocked(s);
    closeLocked(s);
    s.wsRoot.clear();
    s.explicitFile.clear();
    s.path.clear();
    s.dropped.store(0);
    s.droppedReported = 0;
  }
  setCrashLogPath({});
}

void Log::setLevel(LogLevel l) {
  Impl& s = impl();
  if (s.state.load() != kRunning || s.envFixed) return;
  detail::g_logLevel.store(l == LogLevel::Off ? -1 : static_cast<int>(l));
}

void Log::write(LogLevel l, std::string_view category, std::string_view msg) {
  if (!enabled(l)) return;
  Impl& s = impl();
  if (s.state.load(std::memory_order_acquire) != kRunning) return;
  std::string rec;
  rec.reserve(64 + category.size() + msg.size());
  formatRecord(rec, l, category, msg);
  bool wake = false;
  {
    std::lock_guard<std::mutex> lk(s.mu);
    if (s.state.load(std::memory_order_relaxed) != kRunning) return;
    if (s.buf.size() > kMaxBuffered) {
      s.dropped.fetch_add(1);
      return;
    }
    s.buf += rec;
    if (l == LogLevel::Error) s.urgent = true;
    wake = s.urgent || s.buf.size() >= kUrgentBytes;
  }
  if (wake) s.cv.notify_one();
}

void Log::flush() {
  Impl& s = impl();
  if (s.state.load() != kRunning) return;
  std::lock_guard<std::mutex> io(s.ioMu);
  drainLocked(s);
}

void Log::setWorkspace(const fs::path& root) {
  Impl& s = impl();
  std::lock_guard<std::mutex> io(s.ioMu);
  if (s.state.load() == kRunning) drainLocked(s);  // pending records belong to the old file
  s.wsRoot = root.empty() ? fs::path() : fs::absolute(root);
  retarget(s);
}

void Log::setFile(const fs::path& file) {
  Impl& s = impl();
  std::lock_guard<std::mutex> io(s.ioMu);
  if (s.state.load() == kRunning) drainLocked(s);
  s.explicitFile = file;
  retarget(s);
}

fs::path Log::filePath() {
  Impl& s = impl();
  if (s.state.load() != kRunning) return {};
  std::lock_guard<std::mutex> io(s.ioMu);
  return s.path;
}

uint64_t Log::droppedCount() { return impl().dropped.load(); }

// ---------------------------------------------------------------------------

std::string_view Log::route(std::string_view def, std::string_view msg, std::string_view* rest) {
  struct Rule {
    const char* prefix;
    const char* category;
    bool strip;
  };
  static const Rule rules[] = {
      {"vcs: ", "vcs", true},        {"fast index", "index", false},       {"file watcher: ", "watch", true},
      {"build: ", "build", true},    {"compile database", "build", false}, {"cmake ", "build", false},
      {"vcxproj: ", "build", false}, {"vs configuration", "build", false}, {"clangd", "clangd", false},
      {"debug: ", "debug", true},
  };
  for (const Rule& r : rules) {
    size_t n = std::strlen(r.prefix);
    if (msg.size() >= n && msg.compare(0, n, r.prefix) == 0) {
      if (rest) *rest = r.strip ? msg.substr(n) : msg;
      return r.category;
    }
  }
  if (rest) *rest = msg;
  return def;
}

std::function<void(const std::string&)> Log::tee(std::string category, std::function<void(const std::string&)> next) {
  return [category = std::move(category), next = std::move(next)](const std::string& m) {
    if (enabled(LogLevel::Error)) {
      std::string_view rest;
      std::string_view cat = route(category, m, &rest);
      LogLevel l = LogLevel::Info;
      if (m.find("giving up") != std::string::npos) l = LogLevel::Error;
      else if (m.find("failed") != std::string::npos || m.find("could not") != std::string::npos ||
               m.find("crashed") != std::string::npos || m.find("unavailable") != std::string::npos ||
               m.find("not found") != std::string::npos || m.find("limit reached") != std::string::npos)
        l = LogLevel::Warn;
      write(l, cat, rest);
    }
    if (next) next(m);
  };
}

std::vector<std::string> Log::tailLines(const fs::path& file, size_t n) {
  std::vector<std::string> lines;
  std::ifstream in(file, std::ios::binary);
  if (!in) return lines;
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    lines.push_back(std::move(line));
    line.clear();
    if (n && lines.size() > n * 2 + 1024) lines.erase(lines.begin(), lines.end() - static_cast<long>(n));
  }
  if (n && lines.size() > n) lines.erase(lines.begin(), lines.end() - static_cast<long>(n));
  return lines;
}

}  // namespace tf
