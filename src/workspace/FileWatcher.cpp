#include "workspace/FileWatcher.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <unordered_map>
#include <vector>

#if defined(__linux__)
#include <errno.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <unistd.h>
#include <fstream>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace tf {

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

struct FileWatcher::Impl {
  Options opt;
  Callback cb;
  IgnoreFn ignore;
  fs::path root;

  std::thread thread;
  std::atomic<bool> running{false};
  std::atomic<bool> stopFlag{false};

  mutable std::mutex mu;  // guards mode, wd maps, extraDirs, stop wakeups
  std::condition_variable cv;
  std::string mode;
  std::vector<fs::path> extraDirs;

  // --- debouncer (watcher thread only) ---
  struct Pending {
    Event ev;
    Clock::time_point last;
    uint64_t seq = 0;
  };
  std::map<std::string, Pending> pending;
  uint64_t seq = 0;

  void log(const std::string& m) const {
    if (opt.log) opt.log(m);
  }

  // Folds `e` into the pending set. Kinds merge so that a burst on one path
  // becomes one event: Created+Modified = Created, Created+Deleted = nothing,
  // Deleted+Created = Modified, a rename of a pending path moves its entry.
  void note(Event e) {
    const auto now = Clock::now();
    if (e.kind == Event::Rescan) {
      Pending p{e, now, seq++};
      pending["\1rescan"] = std::move(p);
      return;
    }
    if (e.kind == Event::Renamed) {
      std::string oldKey = e.oldPath.string();
      Event::Kind kind = Event::Renamed;
      fs::path oldPath = e.oldPath;
      auto old = pending.find(oldKey);
      if (old != pending.end()) {
        const Event& oe = old->second.ev;
        if (oe.kind == Event::Created) kind = Event::Created;
        else if (oe.kind == Event::Renamed) oldPath = oe.oldPath;
        pending.erase(old);
      }
      if (kind == Event::Renamed && oldPath == e.path) kind = Event::Modified;
      e.kind = kind;
      e.oldPath = kind == Event::Renamed ? oldPath : fs::path();
      pending[e.path.string()] = Pending{e, now, seq++};
      return;
    }
    std::string key = e.path.string();
    auto it = pending.find(key);
    if (it == pending.end()) {
      pending[key] = Pending{std::move(e), now, seq++};
      return;
    }
    Pending& p = it->second;
    p.last = now;
    switch (e.kind) {
      case Event::Modified:
        if (p.ev.kind == Event::Deleted) p.ev.kind = Event::Modified;
        break;
      case Event::Created:
        if (p.ev.kind == Event::Deleted && !e.isDirectory) p.ev.kind = Event::Modified;
        else if (p.ev.kind != Event::Renamed) p.ev.kind = Event::Created;
        p.ev.isDirectory = e.isDirectory;
        break;
      case Event::Deleted:
        if (p.ev.kind == Event::Created) {
          pending.erase(it);
        } else if (p.ev.kind == Event::Renamed) {
          fs::path gone = p.ev.oldPath;
          p.ev.kind = Event::Deleted;
          p.ev.oldPath.clear();
          Event d;
          d.kind = Event::Deleted;
          d.path = gone;
          d.isDirectory = p.ev.isDirectory;
          note(std::move(d));
        } else {
          p.ev.kind = Event::Deleted;
        }
        break;
      default:
        break;
    }
  }

  // Delivers every event that has been quiet for the debounce interval, oldest first.
  void flushDue(bool all = false) {
    const auto now = Clock::now();
    const auto window = std::chrono::milliseconds(opt.debounceMs);
    std::vector<Pending> due;
    for (auto it = pending.begin(); it != pending.end();) {
      if (all || now - it->second.last >= window) {
        due.push_back(std::move(it->second));
        it = pending.erase(it);
      } else {
        ++it;
      }
    }
    std::sort(due.begin(), due.end(), [](const Pending& a, const Pending& b) { return a.seq < b.seq; });
    for (auto& p : due) {
      if (stopFlag) return;
      if (cb) cb(std::move(p.ev));
    }
  }

  // Milliseconds until the next pending event is due, or -1 when there is none.
  int pendingTimeoutMs() const {
    if (pending.empty()) return -1;
    const auto now = Clock::now();
    const auto window = std::chrono::milliseconds(opt.debounceMs);
    auto best = std::chrono::milliseconds::max();
    for (auto& [k, p] : pending) {
      auto left = std::chrono::duration_cast<std::chrono::milliseconds>(p.last + window - now);
      if (left < best) best = left;
    }
    return static_cast<int>(std::max<long long>(1, best.count() + 1));
  }

  // ---- polling (portable fallback) -------------------------------------
  struct FileStamp {
    long long mtime = 0;
    uintmax_t size = 0;
    bool operator==(const FileStamp& o) const { return mtime == o.mtime && size == o.size; }
  };
  using Snapshot = std::unordered_map<std::string, FileStamp>;

  void scanInto(const fs::path& dir, bool recurse, Snapshot& out) const {
    std::error_code ec;
    fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
      std::error_code e2;
      auto st = it->symlink_status(e2);
      if (e2) continue;
      if (st.type() == fs::file_type::directory) {
        if (recurse && !ignore(it->path())) scanInto(it->path(), true, out);
      } else if (st.type() == fs::file_type::regular) {
        FileStamp s;
        auto t = fs::last_write_time(it->path(), e2);
        if (!e2) s.mtime = static_cast<long long>(t.time_since_epoch().count());
        s.size = fs::file_size(it->path(), e2);
        out[it->path().string()] = s;
      }
    }
  }

  Snapshot takeSnapshot() const {
    Snapshot s;
    scanInto(root, true, s);
    std::vector<fs::path> extras;
    {
      std::lock_guard<std::mutex> lk(mu);
      extras = extraDirs;
    }
    for (auto& d : extras) scanInto(d, false, s);
    return s;
  }

  void pollLoop(Snapshot snap) {
    auto lastScan = Clock::now();
    while (!stopFlag) {
      {
        std::unique_lock<std::mutex> lk(mu);
        int wait = pending.empty() ? opt.pollMs : std::min(opt.pollMs, std::max(10, pendingTimeoutMs()));
        cv.wait_for(lk, std::chrono::milliseconds(wait), [&] { return stopFlag.load(); });
      }
      if (stopFlag) break;
      auto now = Clock::now();
      if (now - lastScan >= std::chrono::milliseconds(opt.pollMs)) {
        lastScan = now;
        Snapshot cur = takeSnapshot();
        for (auto& [path, stamp] : cur) {
          auto it = snap.find(path);
          Event e;
          e.path = path;
          if (it == snap.end()) {
            e.kind = Event::Created;
            note(std::move(e));
          } else if (!(it->second == stamp)) {
            e.kind = Event::Modified;
            note(std::move(e));
          }
        }
        for (auto& [path, stamp] : snap) {
          if (cur.count(path)) continue;
          Event e;
          e.kind = Event::Deleted;
          e.path = path;
          note(std::move(e));
        }
        snap = std::move(cur);
      }
      flushDue();
    }
  }

#if defined(__linux__)
  int inFd = -1;
  int wakeFd = -1;
  std::unordered_map<int, fs::path> wdPath;
  std::unordered_map<std::string, int> pathWd;
  std::atomic<bool> limitHit{false};

  static constexpr uint32_t kMask = IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVED_FROM | IN_MOVED_TO | IN_ONLYDIR |
                                    IN_DONT_FOLLOW;

  bool addWatch(const fs::path& dir) {
    if (opt.maxWatches > 0 && watchesPlatform() >= opt.maxWatches) {
      limitHit = true;
      return false;
    }
    int wd = inotify_add_watch(inFd, dir.c_str(), kMask);
    if (wd < 0) {
      if (errno == ENOSPC) limitHit = true;
      return false;
    }
    std::lock_guard<std::mutex> lk(mu);
    auto old = wdPath.find(wd);
    if (old != wdPath.end() && old->second != dir) pathWd.erase(old->second.string());
    wdPath[wd] = dir;
    pathWd[dir.string()] = wd;
    return true;
  }

  // Watches `dir` and everything below it (minus ignored directories). With
  // emitFiles, regular files found are reported as Created (used for
  // directories that appeared after start, where files may predate the watch).
  void walkWatch(const fs::path& dir, bool emitFiles) {
    if (!addWatch(dir) && limitHit) return;
    std::error_code ec;
    fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
    for (; !ec && it != end && !limitHit; it.increment(ec)) {
      std::error_code e2;
      auto st = it->symlink_status(e2);
      if (e2) continue;
      if (st.type() == fs::file_type::directory) {
        if (!ignore(it->path())) walkWatch(it->path(), emitFiles);
      } else if (emitFiles && st.type() == fs::file_type::regular) {
        Event e;
        e.kind = Event::Created;
        e.path = it->path();
        note(std::move(e));
      }
    }
  }

  void forgetWatchesUnder(const fs::path& dir, bool removeFromKernel) {
    std::lock_guard<std::mutex> lk(mu);
    const std::string prefix = dir.string() + "/";
    for (auto it = wdPath.begin(); it != wdPath.end();) {
      const std::string p = it->second.string();
      if (p == dir.string() || p.compare(0, prefix.size(), prefix) == 0) {
        if (removeFromKernel) inotify_rm_watch(inFd, it->first);
        pathWd.erase(p);
        it = wdPath.erase(it);
      } else {
        ++it;
      }
    }
  }

  // A watched directory was renamed: re-point every watch below it.
  void renameWatches(const fs::path& from, const fs::path& to) {
    std::lock_guard<std::mutex> lk(mu);
    const std::string f = from.string();
    const std::string prefix = f + "/";
    for (auto& [wd, p] : wdPath) {
      const std::string s = p.string();
      if (s == f || s.compare(0, prefix.size(), prefix) == 0) {
        pathWd.erase(s);
        p = fs::path(to.string() + s.substr(f.size()));
        pathWd[p.string()] = wd;
      }
    }
  }

  struct MovedFrom {
    fs::path path;
    bool isDir;
    Clock::time_point when;
  };
  std::map<uint32_t, MovedFrom> moves;
  static constexpr int kMovePairMs = 40;

  void expireMoves(bool all) {
    const auto now = Clock::now();
    for (auto it = moves.begin(); it != moves.end();) {
      if (all || now - it->second.when >= std::chrono::milliseconds(kMovePairMs)) {
        Event e;
        e.kind = Event::Deleted;  // moved out of the watched tree
        e.path = it->second.path;
        e.isDirectory = it->second.isDir;
        if (it->second.isDir) forgetWatchesUnder(it->second.path, true);
        note(std::move(e));
        it = moves.erase(it);
      } else {
        ++it;
      }
    }
  }

  void handleBuffer(const char* buf, ssize_t len) {
    for (const char* p = buf; p < buf + len;) {
      const auto* ev = reinterpret_cast<const inotify_event*>(p);
      p += sizeof(inotify_event) + ev->len;
      if (ev->mask & IN_Q_OVERFLOW) {
        Event e;
        e.kind = Event::Rescan;
        note(std::move(e));
        continue;
      }
      if (ev->mask & IN_IGNORED) {
        std::lock_guard<std::mutex> lk(mu);
        auto it = wdPath.find(ev->wd);
        if (it != wdPath.end()) {
          auto pw = pathWd.find(it->second.string());
          if (pw != pathWd.end() && pw->second == ev->wd) pathWd.erase(pw);
          wdPath.erase(it);
        }
        continue;
      }
      if (ev->len == 0) continue;
      fs::path dir;
      {
        std::lock_guard<std::mutex> lk(mu);
        auto it = wdPath.find(ev->wd);
        if (it == wdPath.end()) continue;
        dir = it->second;
      }
      fs::path path = dir / ev->name;
      const bool isDir = (ev->mask & IN_ISDIR) != 0;
      Event e;
      e.path = path;
      e.isDirectory = isDir;
      if (ev->mask & IN_CREATE) {
        e.kind = Event::Created;
        if (isDir && !ignore(path)) walkWatch(path, true);
        note(std::move(e));
      } else if (ev->mask & IN_MODIFY) {
        if (isDir) continue;
        e.kind = Event::Modified;
        note(std::move(e));
      } else if (ev->mask & IN_DELETE) {
        e.kind = Event::Deleted;
        if (isDir) forgetWatchesUnder(path, false);
        note(std::move(e));
      } else if (ev->mask & IN_MOVED_FROM) {
        moves[ev->cookie] = MovedFrom{path, isDir, Clock::now()};
      } else if (ev->mask & IN_MOVED_TO) {
        auto m = moves.find(ev->cookie);
        if (m != moves.end() && !(isDir && ignore(path))) {
          e.kind = Event::Renamed;
          e.oldPath = m->second.path;
          if (isDir) renameWatches(m->second.path, path);
          moves.erase(m);
          note(std::move(e));
        } else {
          if (m != moves.end()) {  // renamed into an ignored directory: it left the watched set
            Event d;
            d.kind = Event::Deleted;
            d.path = m->second.path;
            d.isDirectory = isDir;
            if (isDir) forgetWatchesUnder(m->second.path, true);
            moves.erase(m);
            note(std::move(d));
          }
          if (isDir && ignore(path)) continue;
          e.kind = Event::Created;  // moved in from outside
          if (isDir) walkWatch(path, true);
          note(std::move(e));
        }
      }
    }
  }

  void closeInotify() {
    if (inFd >= 0) ::close(inFd);
    inFd = -1;
    std::lock_guard<std::mutex> lk(mu);
    wdPath.clear();
    pathWd.clear();
  }

  void inotifyLoop() {
    std::vector<char> buf(256 * 1024);
    while (!stopFlag) {
      int timeout = pendingTimeoutMs();
      if (!moves.empty()) timeout = timeout < 0 ? kMovePairMs : std::min(timeout, kMovePairMs);
      pollfd fds[2] = {{inFd, POLLIN, 0}, {wakeFd, POLLIN, 0}};
      int r = ::poll(fds, 2, timeout);
      if (r < 0 && errno != EINTR) break;
      if (stopFlag) break;
      if (r > 0 && (fds[0].revents & POLLIN)) {
        for (;;) {
          ssize_t n = ::read(inFd, buf.data(), buf.size());
          if (n <= 0) break;
          handleBuffer(buf.data(), n);
          if (limitHit) break;
        }
      }
      if (limitHit) {
        // Out of inotify watches while following a new directory: degrade to polling.
        log("file watcher: inotify watch limit reached; falling back to polling");
        closeInotify();
        moves.clear();
        {
          std::lock_guard<std::mutex> lk(mu);
          mode = "poll";
        }
        Snapshot snap = takeSnapshot();
        Event e;
        e.kind = Event::Rescan;
        note(std::move(e));
        flushDue(true);
        pollLoop(std::move(snap));
        return;
      }
      expireMoves(false);
      flushDue();
    }
  }

  static long readWatchLimit() {
    std::ifstream in("/proc/sys/fs/inotify/max_user_watches");
    long v = 0;
    in >> v;
    return v;
  }

  bool startPlatform(Snapshot& pollSnap, bool& usePoll) {
    usePoll = false;
    inFd = ::inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    wakeFd = ::eventfd(0, EFD_CLOEXEC);
    if (inFd < 0 || wakeFd < 0) {
      log("file watcher: inotify unavailable; using polling");
      if (inFd >= 0) ::close(inFd);
      inFd = -1;
      usePoll = true;
    } else {
      walkWatch(root, false);
      std::vector<fs::path> extras;
      {
        std::lock_guard<std::mutex> lk(mu);
        extras = extraDirs;
      }
      for (auto& d : extras) addWatch(d);
      if (limitHit) {
        log("file watcher: inotify watch limit (" + std::to_string(readWatchLimit()) + ") reached after " +
            std::to_string(wdPath.size()) + " directories; falling back to polling. Raise fs.inotify.max_user_watches.");
        closeInotify();
        usePoll = true;
      }
    }
    if (usePoll) {
      std::lock_guard<std::mutex> lk(mu);
      mode = "poll";
    } else {
      std::lock_guard<std::mutex> lk(mu);
      mode = "inotify";
    }
    if (usePoll) pollSnap = takeSnapshot();
    return true;
  }

  void wake() {
    if (wakeFd >= 0) {
      uint64_t one = 1;
      ssize_t r = ::write(wakeFd, &one, sizeof one);
      (void)r;
    }
  }

  void closeAll() {
    closeInotify();
    if (wakeFd >= 0) ::close(wakeFd);
    wakeFd = -1;
  }

  void addDirPlatform(const fs::path& dir) {
    if (inFd >= 0) addWatch(dir);
  }

  void threadMain(bool usePoll, Snapshot snap) {
    if (usePoll) pollLoop(std::move(snap));
    else inotifyLoop();
  }

  size_t watchesPlatform() const {
    std::lock_guard<std::mutex> lk(mu);
    return wdPath.size();
  }

#elif defined(_WIN32)
  HANDLE dirHandle = INVALID_HANDLE_VALUE;
  HANDLE stopEvent = nullptr;
  HANDLE ioEvent = nullptr;
  OVERLAPPED ov{};
  std::vector<DWORD> buf = std::vector<DWORD>(64 * 1024 / sizeof(DWORD));  // DWORD-aligned

  static constexpr DWORD kFilter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                                   FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE |
                                   FILE_NOTIFY_CHANGE_CREATION;

  bool issueRead() {
    ResetEvent(ioEvent);
    ZeroMemory(&ov, sizeof ov);
    ov.hEvent = ioEvent;
    return ReadDirectoryChangesW(dirHandle, buf.data(), static_cast<DWORD>(buf.size() * sizeof(DWORD)), TRUE, kFilter,
                                 nullptr, &ov, nullptr) != 0;
  }

  // True when any directory between the root and `p` is ignored.
  bool underIgnored(const fs::path& p) const {
    fs::path cur = p.parent_path();
    while (!cur.empty() && cur != root && cur.has_relative_path()) {
      if (ignore(cur)) return true;
      cur = cur.parent_path();
    }
    return false;
  }

  void walkFiles(const fs::path& dir) {
    std::error_code ec;
    fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
      std::error_code e2;
      auto st = it->symlink_status(e2);
      if (e2) continue;
      if (st.type() == fs::file_type::directory) {
        if (!ignore(it->path())) walkFiles(it->path());
      } else if (st.type() == fs::file_type::regular) {
        Event e;
        e.kind = Event::Created;
        e.path = it->path();
        note(std::move(e));
      }
    }
  }

  fs::path pendingOld;

  void handleBuffer(const std::vector<char>& bytes) {
    const char* p = bytes.data();
    for (;;) {
      const auto* info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(p);
      fs::path rel(std::wstring(info->FileName, info->FileNameLength / sizeof(WCHAR)));
      fs::path path = root / rel;
      if (!underIgnored(path)) {
        Event e;
        e.path = path;
        std::error_code ec;
        switch (info->Action) {
          case FILE_ACTION_ADDED:
            e.kind = Event::Created;
            e.isDirectory = fs::is_directory(path, ec);
            if (e.isDirectory && !ignore(path)) walkFiles(path);
            if (!e.isDirectory || !ignore(path)) note(std::move(e));
            break;
          case FILE_ACTION_REMOVED:
            e.kind = Event::Deleted;  // directory-ness unknown once gone; consumers match by prefix
            note(std::move(e));
            break;
          case FILE_ACTION_MODIFIED:
            if (!fs::is_directory(path, ec)) {
              e.kind = Event::Modified;
              note(std::move(e));
            }
            break;
          case FILE_ACTION_RENAMED_OLD_NAME:
            pendingOld = path;
            break;
          case FILE_ACTION_RENAMED_NEW_NAME:
            e.kind = Event::Renamed;
            e.oldPath = pendingOld;
            e.isDirectory = fs::is_directory(path, ec);
            pendingOld.clear();
            note(std::move(e));
            break;
          default:
            break;
        }
      } else if (info->Action == FILE_ACTION_RENAMED_OLD_NAME) {
        pendingOld.clear();
      }
      if (info->NextEntryOffset == 0) break;
      p += info->NextEntryOffset;
    }
  }

  bool startPlatform(Snapshot& pollSnap, bool& usePoll) {
    usePoll = false;
    dirHandle = CreateFileW(root.c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
    ioEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (dirHandle == INVALID_HANDLE_VALUE || !ioEvent || !stopEvent || !issueRead()) {
      log("file watcher: ReadDirectoryChangesW unavailable; using polling");
      closeAll();
      usePoll = true;
      std::lock_guard<std::mutex> lk(mu);
      mode = "poll";
      pollSnap = takeSnapshot();
      return true;
    }
    std::lock_guard<std::mutex> lk(mu);
    mode = "readdirchanges";
    return true;
  }

  void wake() {
    if (stopEvent) SetEvent(stopEvent);
  }

  void closeAll() {
    if (dirHandle != INVALID_HANDLE_VALUE) CloseHandle(dirHandle);
    if (ioEvent) CloseHandle(ioEvent);
    if (stopEvent) CloseHandle(stopEvent);
    dirHandle = INVALID_HANDLE_VALUE;
    ioEvent = stopEvent = nullptr;
  }

  void addDirPlatform(const fs::path&) {}
  size_t watchesPlatform() const { return 0; }

  void threadMain(bool usePoll, Snapshot snap) {
    if (usePoll) {
      pollLoop(std::move(snap));
      return;
    }
    HANDLE handles[2] = {ioEvent, stopEvent};
    while (!stopFlag) {
      int t = pendingTimeoutMs();
      DWORD r = WaitForMultipleObjects(2, handles, FALSE, t < 0 ? INFINITE : static_cast<DWORD>(t));
      if (r == WAIT_OBJECT_0 + 1 || stopFlag) break;
      if (r == WAIT_OBJECT_0) {
        DWORD n = 0;
        BOOL ok = GetOverlappedResult(dirHandle, &ov, &n, FALSE);
        if (!ok || n == 0) {
          // Buffer overflow (n == 0) or error: events were lost.
          Event e;
          e.kind = Event::Rescan;
          note(std::move(e));
          if (!issueRead()) break;
        } else {
          std::vector<char> bytes(reinterpret_cast<char*>(buf.data()), reinterpret_cast<char*>(buf.data()) + n);
          if (!issueRead()) break;  // re-arm before parsing so nothing is missed meanwhile
          handleBuffer(bytes);
        }
      }
      flushDue();
    }
    CancelIo(dirHandle);
  }

#else  // other platforms: polling stub (FSEvents later)
  bool startPlatform(Snapshot& pollSnap, bool& usePoll) {
    usePoll = true;
    {
      std::lock_guard<std::mutex> lk(mu);
      mode = "poll";
    }
    pollSnap = takeSnapshot();
    return true;
  }
  void wake() {}
  void closeAll() {}
  void addDirPlatform(const fs::path&) {}
  size_t watchesPlatform() const { return 0; }
  void threadMain(bool usePoll, Snapshot snap) { pollLoop(std::move(snap)); }
#endif
};

FileWatcher::FileWatcher() : impl_(std::make_unique<Impl>()) {}
FileWatcher::FileWatcher(Options opt) : impl_(std::make_unique<Impl>()) { impl_->opt = std::move(opt); }
FileWatcher::~FileWatcher() { stop(); }

bool FileWatcher::start(const fs::path& root, IgnoreFn ignoreFn, Callback cb) {
  Impl& d = *impl_;
  if (d.running) return false;
  d.root = root;
  d.ignore = ignoreFn ? std::move(ignoreFn) : IgnoreFn([](const fs::path&) { return false; });
  d.cb = std::move(cb);
  d.stopFlag = false;
  d.pending.clear();
  std::error_code ec;
  if (!fs::is_directory(root, ec)) return false;
  Impl::Snapshot snap;
  bool usePoll = false;
  if (d.opt.forcePolling) {
    usePoll = true;
    {
      std::lock_guard<std::mutex> lk(d.mu);
      d.mode = "poll";
    }
    snap = d.takeSnapshot();
  } else if (!d.startPlatform(snap, usePoll)) {
    return false;
  }
  d.running = true;
  d.thread = std::thread([&d, usePoll, snap = std::move(snap)]() mutable { d.threadMain(usePoll, std::move(snap)); });
  return true;
}

void FileWatcher::stop() {
  Impl& d = *impl_;
  if (!d.running.exchange(false)) return;
  {
    std::lock_guard<std::mutex> lk(d.mu);
    d.stopFlag = true;
  }
  d.cv.notify_all();
  d.wake();
  if (d.thread.joinable()) d.thread.join();
  d.closeAll();
  d.pending.clear();
  std::lock_guard<std::mutex> lk(d.mu);
  d.mode.clear();
}

bool FileWatcher::running() const { return impl_->running; }

void FileWatcher::addDirectory(const fs::path& dir) {
  Impl& d = *impl_;
  {
    std::lock_guard<std::mutex> lk(d.mu);
    if (std::find(d.extraDirs.begin(), d.extraDirs.end(), dir) != d.extraDirs.end()) return;
    d.extraDirs.push_back(dir);
  }
  if (d.running) d.addDirPlatform(dir);
}

std::string FileWatcher::mode() const {
  std::lock_guard<std::mutex> lk(impl_->mu);
  return impl_->mode;
}

size_t FileWatcher::watchCount() const { return impl_->watchesPlatform(); }

}  // namespace tf
