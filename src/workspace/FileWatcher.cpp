#include "workspace/FileWatcher.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <unordered_map>
#include <vector>

#include "core/PathUtil.h"

#if defined(__linux__)
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <sys/stat.h>
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

  // Extra roots (FileWatcher::addRoot). `extraRoots` is the requested set (guarded by mu); `rootOps` the changes the
  // watcher thread has not applied yet (guarded by mu); `activeRoots` what the thread watches now (thread-owned once
  // it runs; start() fills it before the thread exists).
  struct RootSpec {
    fs::path path;
    bool recursive = true;
  };
  struct RootOp {
    bool add = true;
    RootSpec root;
  };
  std::vector<RootSpec> extraRoots;
  std::vector<RootOp> rootOps;
  std::vector<RootSpec> activeRoots;

  std::vector<RootOp> takeRootOps() {
    std::lock_guard<std::mutex> lk(mu);
    std::vector<RootOp> ops;
    ops.swap(rootOps);
    return ops;
  }
  static void applyToActive(std::vector<RootSpec>& active, const RootOp& op) {
    auto it = std::find_if(active.begin(), active.end(), [&](const RootSpec& r) { return r.path == op.root.path; });
    if (op.add) {
      if (it == active.end()) active.push_back(op.root);
      else *it = op.root;
    } else if (it != active.end()) {
      active.erase(it);
    }
  }

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

  // A directory was renamed `from` -> `to` while events for paths below it are still waiting out the debounce: they
  // must be delivered under the new names. Otherwise a late "created from/x" arrives after the rename was handled, the
  // consumer finds nothing at that path, and the file is never listed under `to` (stale entry + missing entry).
  void remapPendingUnder(const std::string& from, const std::string& to) {
    const std::string prefix = from + "/";
    std::vector<Pending> moved;
    for (auto it = pending.begin(); it != pending.end();) {
      const std::string& key = it->first;
      if (key.compare(0, prefix.size(), prefix) == 0) {
        moved.push_back(std::move(it->second));
        it = pending.erase(it);
      } else {
        ++it;
      }
    }
    auto remap = [&](const fs::path& p) {
      const std::string s = pathKeyUtf8(p);
      return s.compare(0, prefix.size(), prefix) == 0 ? pathFromUtf8(to + s.substr(from.size())) : p;
    };
    // These events must come after the rename itself (whose entry the caller has just made), both in sequence and in
    // due time (an entry with an old `last` would be flushed on its own, before the rename), or the consumer sees
    // "deleted to/x" first and the rename then moves a file that is already gone.
    std::sort(moved.begin(), moved.end(), [](const Pending& a, const Pending& b) { return a.seq < b.seq; });
    for (Pending& p : moved) {
      p.seq = seq++;
      p.last = std::max(p.last, Clock::now());  // not due before the rename's own entry (made just before)
      p.ev.path = remap(p.ev.path);
      p.ev.oldPath = remap(p.ev.oldPath);
      auto& slot = pending[pathKeyUtf8(p.ev.path)];
      if (slot.ev.path.empty() || slot.seq < p.seq) slot = std::move(p);
    }
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
      std::string oldKey = pathKeyUtf8(e.oldPath);
      Event::Kind kind = Event::Renamed;
      fs::path oldPath = e.oldPath;
      auto old = pending.find(oldKey);
      if (old != pending.end()) {
        const Event& oe = old->second.ev;
        // A pending "created" folds a file rename into a creation at the new name. A directory stays a rename: the
        // consumer may already list files under the old name (their own events were delivered), and they move.
        if (oe.kind == Event::Created && !e.isDirectory) kind = Event::Created;
        else if (oe.kind == Event::Renamed) oldPath = oe.oldPath;
        pending.erase(old);
      }
      if (kind == Event::Renamed && oldPath == e.path) kind = Event::Modified;
      e.kind = kind;
      e.oldPath = kind == Event::Renamed ? oldPath : fs::path();
      const std::string newKey = pathKeyUtf8(e.path);
      const bool dir = e.isDirectory;
      pending[newKey] = Pending{std::move(e), now, seq++};
      if (dir) remapPendingUnder(oldKey, newKey);
      return;
    }
    std::string key = pathKeyUtf8(e.path);
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
      default: break;
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
        out[pathUtf8(it->path())] = s;
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
    for (auto& r : activeRoots) scanInto(r.path, r.recursive, s);
    return s;
  }

  // Applies the pending root changes to the polled set; an added root is read into `snap` (its files are known to the
  // consumer already: no events), a removed one leaves it.
  void applyRootOpsPoll(Snapshot& snap) {
    for (const RootOp& op : takeRootOps()) {
      applyToActive(activeRoots, op);
      if (op.add) {
        scanInto(op.root.path, op.root.recursive, snap);
      } else {
        const std::string prefix = pathUtf8(op.root.path) + static_cast<char>(fs::path::preferred_separator);
        for (auto it = snap.begin(); it != snap.end();)
          it = it->first.compare(0, prefix.size(), prefix) == 0 ? snap.erase(it) : std::next(it);
      }
    }
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
      applyRootOpsPoll(snap);
      auto now = Clock::now();
      if (now - lastScan >= std::chrono::milliseconds(opt.pollMs)) {
        lastScan = now;
        Snapshot cur = takeSnapshot();
        for (auto& [path, stamp] : cur) {
          auto it = snap.find(path);
          Event e;
          e.path = pathFromUtf8(path);
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
          e.path = pathFromUtf8(path);
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
  // The directory (device, inode) each watch is on, so that a rename event can tell whether the watch registered under
  // the old name is on the directory that moved, or on a different one that was found under that name later.
  struct DirId {
    dev_t dev = 0;
    ino_t ino = 0;
    bool operator==(const DirId& o) const { return dev == o.dev && ino == o.ino; }
  };
  std::unordered_map<int, DirId> wdId;
  std::atomic<bool> limitHit{false};
  // Non-recursive extra roots (watcher thread only): a directory that appears in one is not followed.
  std::set<std::string> flatDirs;
  bool isFlat(const fs::path& dir) const { return !flatDirs.empty() && flatDirs.count(dir.string()) != 0; }

  void watchRoot(const RootSpec& r) {
    if (r.recursive) {
      walkWatch(r.path, false);
    } else {
      flatDirs.insert(r.path.string());
      addWatch(r.path);
    }
  }

  void applyRootOps() {
    for (const RootOp& op : takeRootOps()) {
      applyToActive(activeRoots, op);
      if (op.add) {
        watchRoot(op.root);
      } else {
        flatDirs.erase(op.root.path.string());
        forgetWatchesUnder(op.root.path, true);
      }
    }
  }

  static constexpr uint32_t kMask =
      IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVED_FROM | IN_MOVED_TO | IN_ONLYDIR | IN_DONT_FOLLOW;

  // Paths the walk of a new directory could not handle under the name the walk had for them: the directory (or an
  // ancestor) was renamed on disk while this thread was late, so inotify_add_watch(parent/new) fails, or listing a
  // directory that has moved on stats every entry under a name that no longer exists (a directory iterator keeps
  // reading the old directory but its entries' paths are stale), or the listing ends early. Nothing inside such a path
  // would ever be reported. The rename's own event is still in the queue (this thread has not got to it yet): when it
  // arrives, those paths are looked up again under their new names. Bounded; entries that never see a rename (the
  // path was just deleted) fall off, and a retry costs one stat per entry plus the listing of directories it finds.
  std::vector<fs::path> unwatched;
  static constexpr size_t kMaxUnwatched = 4096;

  void noteUnwatched(const fs::path& path) {
    if (limitHit) return;
    if (unwatched.size() >= kMaxUnwatched) unwatched.erase(unwatched.begin(), unwatched.begin() + kMaxUnwatched / 2);
    unwatched.push_back(path);
  }

  // `from` was renamed to `to`: look again at what the walks missed below it.
  void retryUnwatchedUnder(const fs::path& from, const fs::path& to) {
    const std::string f = from.string();
    const std::string prefix = f + "/";
    std::vector<fs::path> retry;
    for (auto it = unwatched.begin(); it != unwatched.end();) {
      const std::string s = it->string();
      if (s == f || s.compare(0, prefix.size(), prefix) == 0) {
        retry.push_back(fs::path(to.string() + s.substr(f.size())));
        it = unwatched.erase(it);
      } else {
        ++it;
      }
    }
    for (const fs::path& p : retry) {
      std::error_code ec;
      const auto st = fs::symlink_status(p, ec);
      if (ec || st.type() == fs::file_type::not_found) {  // renamed again already (that rename is next), or deleted
        noteUnwatched(p);
      } else if (st.type() == fs::file_type::directory) {
        if (!ignore(p)) walkWatch(p, true);  // a directory that is watched already is only listed again: duplicates
      } else if (st.type() == fs::file_type::regular) {
        Event e;
        e.kind = Event::Created;
        e.path = p;
        note(std::move(e));
      }
    }
  }

  bool addWatch(const fs::path& dir) {
    if (opt.maxWatches > 0 && watchesPlatform() >= opt.maxWatches) {
      limitHit = true;
      return false;
    }
    // Watch the directory this path names *now*, and know which one that is: the path is only a name, the watch is
    // on an inode. Opening the directory first and adding the watch through /proc/self/fd/N ties both to the same
    // inode (a path-based add could land on another directory that has taken the name in between).
    int wd = -1;
    DirId id;
    bool haveId = false;
    static const bool procFd = ::access("/proc/self/fd", R_OK) == 0;
    if (procFd) {
      const int fd = ::open(dir.c_str(), O_PATH | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
      if (fd < 0) return false;
      struct stat st;
      if (::fstat(fd, &st) == 0) {
        id.dev = st.st_dev;
        id.ino = st.st_ino;
        haveId = true;
        const std::string viaFd = "/proc/self/fd/" + std::to_string(fd);
        wd = inotify_add_watch(inFd, viaFd.c_str(), kMask & ~static_cast<uint32_t>(IN_DONT_FOLLOW));
      }
      const int err = errno;
      ::close(fd);
      errno = err;
    } else {
      wd = inotify_add_watch(inFd, dir.c_str(), kMask);
      struct stat st;
      if (wd >= 0 && ::lstat(dir.c_str(), &st) == 0) {
        id.dev = st.st_dev;
        id.ino = st.st_ino;
        haveId = true;
      }
    }
    if (wd < 0) {
      if (errno == ENOSPC) limitHit = true;
      return false;
    }
    std::lock_guard<std::mutex> lk(mu);
    auto old = wdPath.find(wd);
    if (old != wdPath.end() && old->second != dir) pathWd.erase(old->second.string());
    wdPath[wd] = dir;
    pathWd[dir.string()] = wd;
    if (haveId) wdId[wd] = id;
    else wdId.erase(wd);
    return true;
  }

  // Watches `dir` and everything below it (minus ignored directories). With
  // emitFiles, regular files found are reported as Created (used for
  // directories that appeared after start, where files may predate the watch).
  void walkWatch(const fs::path& dir, bool emitFiles) {
    bool noted = false;
    if (!addWatch(dir)) {
      if (limitHit) return;
      noteUnwatched(dir);
      noted = true;
    }
    std::error_code ec;
    fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
    if (opt.onListing) opt.onListing(dir);
    for (; !ec && it != end && !limitHit; it.increment(ec)) {
      std::error_code e2;
      auto st = it->symlink_status(e2);
      if (e2) {  // vanished, or the directory moved away under the iterator
        noteUnwatched(it->path());
        continue;
      }
      if (st.type() == fs::file_type::directory) {
        if (!ignore(it->path())) walkWatch(it->path(), emitFiles);
      } else if (emitFiles && st.type() == fs::file_type::regular) {
        Event e;
        e.kind = Event::Created;
        e.path = it->path();
        note(std::move(e));
      }
    }
    if (ec && !noted) noteUnwatched(dir);  // the listing broke off: the rest of it is unknown
  }

  void forgetWatchesUnder(const fs::path& dir, bool removeFromKernel) {
    std::lock_guard<std::mutex> lk(mu);
    const std::string prefix = dir.string() + "/";
    for (auto it = wdPath.begin(); it != wdPath.end();) {
      const std::string p = it->second.string();
      if (p == dir.string() || p.compare(0, prefix.size(), prefix) == 0) {
        if (removeFromKernel) inotify_rm_watch(inFd, it->first);
        pathWd.erase(p);
        wdId.erase(it->first);
        it = wdPath.erase(it);
      } else {
        ++it;
      }
    }
  }

  // A watched directory was renamed: re-point every watch below it. Returns false when it must not (and did not):
  // nothing is watched under the old name, or the watch there is on a different directory than the one that moved
  // (this thread was late and found another directory under that name, which has the watch now), or the new name
  // cannot be looked at any more. The caller then lists the directory under its new name.
  bool renameWatches(const fs::path& from, const fs::path& to) {
    struct stat st;
    const bool looked = ::lstat(to.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
    std::lock_guard<std::mutex> lk(mu);
    auto at = pathWd.find(from.string());
    if (at == pathWd.end() || !looked) return false;
    auto id = wdId.find(at->second);
    if (id != wdId.end() && !(id->second == DirId{st.st_dev, st.st_ino})) return false;
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
    return true;
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
          wdId.erase(ev->wd);
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
        if (isDir && !ignore(path) && !isFlat(dir)) walkWatch(path, true);
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
          const fs::path from = m->second.path;
          const bool followed = !isDir || renameWatches(from, path);
          moves.erase(m);
          note(std::move(e));
          // After the rename's entry: what they report sorts behind it.
          if (isDir) {
            retryUnwatchedUnder(from, path);
            if (!followed) walkWatch(path, true);
          }
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
          if (isDir && !isFlat(dir)) walkWatch(path, true);
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
    wdId.clear();
  }

  void inotifyLoop() {
    std::vector<char> buf(256 * 1024);
    while (!stopFlag) {
      applyRootOps();
      int timeout = pendingTimeoutMs();
      if (!moves.empty()) timeout = timeout < 0 ? kMovePairMs : std::min(timeout, kMovePairMs);
      pollfd fds[2] = {{inFd, POLLIN, 0}, {wakeFd, POLLIN, 0}};
      int r = ::poll(fds, 2, timeout);
      if (r < 0 && errno != EINTR) break;
      if (stopFlag) break;
      if (r > 0 && (fds[1].revents & POLLIN)) {  // a root was added or removed: clear the wake-up, apply at the top
        uint64_t drain = 0;
        ssize_t n = ::read(wakeFd, &drain, sizeof drain);
        (void)n;
        continue;
      }
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
      for (auto& r : activeRoots) watchRoot(r);
      if (limitHit) {
        log("file watcher: inotify watch limit (" + std::to_string(readWatchLimit()) + ") reached after " +
            std::to_string(wdPath.size()) +
            " directories; falling back to polling. Raise fs.inotify.max_user_watches.");
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
  // One ReadDirectoryChangesW per root: the main root first, then the extra roots (FileWatcher::addRoot). The thread
  // waits on every root's event plus `stopEvent` (stop) and `opsEvent` (a root was added or removed).
  struct WinRoot {
    fs::path path;
    bool recursive = true;
    HANDLE dirHandle = INVALID_HANDLE_VALUE;
    HANDLE ioEvent = nullptr;
    OVERLAPPED ov{};
    std::vector<DWORD> buf = std::vector<DWORD>(64 * 1024 / sizeof(DWORD));  // DWORD-aligned
    fs::path pendingOld;
  };
  static constexpr size_t kMaxWinRoots = 62;  // WaitForMultipleObjects takes 64 handles; two are ours
  std::vector<std::unique_ptr<WinRoot>> winRoots;
  HANDLE stopEvent = nullptr;
  HANDLE opsEvent = nullptr;

  static constexpr DWORD kFilter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                                   FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE |
                                   FILE_NOTIFY_CHANGE_CREATION;

  bool issueRead(WinRoot& r) {
    ResetEvent(r.ioEvent);
    ZeroMemory(&r.ov, sizeof r.ov);
    r.ov.hEvent = r.ioEvent;
    return ReadDirectoryChangesW(r.dirHandle, r.buf.data(), static_cast<DWORD>(r.buf.size() * sizeof(DWORD)),
                                 r.recursive ? TRUE : FALSE, kFilter, nullptr, &r.ov, nullptr) != 0;
  }

  void closeRoot(WinRoot& r) {
    if (r.dirHandle != INVALID_HANDLE_VALUE) {
      CancelIo(r.dirHandle);
      CloseHandle(r.dirHandle);
    }
    if (r.ioEvent) CloseHandle(r.ioEvent);
    r.dirHandle = INVALID_HANDLE_VALUE;
    r.ioEvent = nullptr;
  }

  // Opens `path` and arms the first read; false when that fails (the root is then not watched).
  bool openRoot(const fs::path& path, bool recursive) {
    if (winRoots.size() >= kMaxWinRoots) {
      log("file watcher: too many roots; not watching " + pathUtf8(path));
      return false;
    }
    auto r = std::make_unique<WinRoot>();
    r->path = path;
    r->recursive = recursive;
    r->dirHandle =
        CreateFileW(path.c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                    OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
    r->ioEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (r->dirHandle == INVALID_HANDLE_VALUE || !r->ioEvent || !issueRead(*r)) {
      closeRoot(*r);
      return false;
    }
    winRoots.push_back(std::move(r));
    return true;
  }

  // True when any directory between the root and `p` is ignored.
  bool underIgnored(const WinRoot& r, const fs::path& p) const {
    fs::path cur = p.parent_path();
    while (!cur.empty() && cur != r.path && cur.has_relative_path()) {
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

  void handleBuffer(WinRoot& r, const std::vector<char>& bytes) {
    const char* p = bytes.data();
    for (;;) {
      const auto* info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(p);
      try {
        fs::path rel(std::wstring(info->FileName, info->FileNameLength / sizeof(WCHAR)));
        fs::path path = r.path / rel;
        if (!underIgnored(r, path)) {
          Event e;
          e.path = path;
          std::error_code ec;
          switch (info->Action) {
            case FILE_ACTION_ADDED:
              e.kind = Event::Created;
              e.isDirectory = fs::is_directory(path, ec);
              if (e.isDirectory && r.recursive && !ignore(path)) walkFiles(path);
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
            case FILE_ACTION_RENAMED_OLD_NAME: r.pendingOld = path; break;
            case FILE_ACTION_RENAMED_NEW_NAME:
              e.kind = Event::Renamed;
              e.oldPath = r.pendingOld;
              e.isDirectory = fs::is_directory(path, ec);
              r.pendingOld.clear();
              note(std::move(e));
              break;
            default: break;
          }
        } else if (info->Action == FILE_ACTION_RENAMED_OLD_NAME) {
          r.pendingOld.clear();
        }
      } catch (const std::exception& ex) {
        // One path std::filesystem cannot handle costs that event, not the watcher.
        log(std::string("file watcher: event skipped: ") + ex.what());
      }
      if (info->NextEntryOffset == 0) break;
      p += info->NextEntryOffset;
    }
  }

  bool startPlatform(Snapshot& pollSnap, bool& usePoll) {
    usePoll = false;
    stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    opsEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    const bool mainOk = stopEvent && opsEvent && openRoot(root, true);
    if (!mainOk) {
      log("file watcher: ReadDirectoryChangesW unavailable; using polling");
      closeAll();
      usePoll = true;
      std::lock_guard<std::mutex> lk(mu);
      mode = "poll";
      pollSnap = takeSnapshot();
      return true;
    }
    for (auto& r : activeRoots)
      if (!openRoot(r.path, r.recursive)) log("file watcher: cannot watch " + pathUtf8(r.path));
    std::lock_guard<std::mutex> lk(mu);
    mode = "readdirchanges";
    return true;
  }

  void wake() {
    if (stopEvent) SetEvent(stopEvent);
    if (opsEvent) SetEvent(opsEvent);
  }

  void closeAll() {
    for (auto& r : winRoots) closeRoot(*r);
    winRoots.clear();
    if (opsEvent) CloseHandle(opsEvent);
    if (stopEvent) CloseHandle(stopEvent);
    opsEvent = stopEvent = nullptr;
  }

  void addDirPlatform(const fs::path&) {}
  size_t watchesPlatform() const { return 0; }

  void applyRootOps() {
    for (const RootOp& op : takeRootOps()) {
      applyToActive(activeRoots, op);
      // winRoots[0] is the main root; an extra root with the same path is replaced.
      for (size_t i = 1; i < winRoots.size(); ++i)
        if (winRoots[i]->path == op.root.path) {
          closeRoot(*winRoots[i]);
          winRoots.erase(winRoots.begin() + static_cast<std::ptrdiff_t>(i));
          break;
        }
      if (op.add && !openRoot(op.root.path, op.root.recursive))
        log("file watcher: cannot watch " + pathUtf8(op.root.path));
    }
  }

  void threadMain(bool usePoll, Snapshot snap) {
    if (usePoll) {
      pollLoop(std::move(snap));
      return;
    }
    while (!stopFlag) {
      applyRootOps();
      std::vector<HANDLE> handles;
      handles.push_back(stopEvent);
      handles.push_back(opsEvent);
      for (auto& r : winRoots) handles.push_back(r->ioEvent);
      int t = pendingTimeoutMs();
      DWORD w = WaitForMultipleObjects(static_cast<DWORD>(handles.size()), handles.data(), FALSE,
                                       t < 0 ? INFINITE : static_cast<DWORD>(t));
      if (w == WAIT_OBJECT_0 || stopFlag) break;
      if (w >= WAIT_OBJECT_0 + 2 && w < WAIT_OBJECT_0 + handles.size()) {
        WinRoot& r = *winRoots[w - WAIT_OBJECT_0 - 2];
        DWORD n = 0;
        BOOL ok = GetOverlappedResult(r.dirHandle, &r.ov, &n, FALSE);
        if (!ok || n == 0) {
          // Buffer overflow (n == 0) or error: events were lost.
          Event e;
          e.kind = Event::Rescan;
          note(std::move(e));
          if (!issueRead(r)) break;
        } else {
          std::vector<char> bytes(reinterpret_cast<char*>(r.buf.data()), reinterpret_cast<char*>(r.buf.data()) + n);
          if (!issueRead(r)) break;  // re-arm before parsing so nothing is missed meanwhile
          handleBuffer(r, bytes);
        }
      }
      flushDue();
    }
    for (auto& r : winRoots) CancelIo(r->dirHandle);
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
  {
    // The roots requested so far; changes made from now on arrive as ops (applied again, which is harmless).
    std::lock_guard<std::mutex> lk(d.mu);
    d.activeRoots = d.extraRoots;
  }
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
  d.thread = std::thread([&d, usePoll, snap = std::move(snap)]() mutable {
    // An exception escaping this thread would terminate the process (0xc0000409 on Windows); a path std::filesystem
    // cannot handle must not take the editor down, it only ends watching.
    try {
      d.threadMain(usePoll, std::move(snap));
    } catch (const std::exception& e) { d.log(std::string("file watcher stopped: ") + e.what()); }
  });
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
  d.rootOps.clear();
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

void FileWatcher::addRoot(const fs::path& dir, bool recursive) {
  Impl& d = *impl_;
  {
    std::lock_guard<std::mutex> lk(d.mu);
    auto it = std::find_if(d.extraRoots.begin(), d.extraRoots.end(), [&](const auto& r) { return r.path == dir; });
    if (it != d.extraRoots.end() && it->recursive == recursive) return;
    if (it != d.extraRoots.end()) it->recursive = recursive;
    else d.extraRoots.push_back({dir, recursive});
    d.rootOps.push_back({true, {dir, recursive}});
  }
  d.cv.notify_all();
  if (d.running) d.wake();
}

void FileWatcher::removeRoot(const fs::path& dir) {
  Impl& d = *impl_;
  {
    std::lock_guard<std::mutex> lk(d.mu);
    auto it = std::find_if(d.extraRoots.begin(), d.extraRoots.end(), [&](const auto& r) { return r.path == dir; });
    if (it == d.extraRoots.end()) return;
    d.extraRoots.erase(it);
    d.rootOps.push_back({false, {dir, true}});
  }
  d.cv.notify_all();
  if (d.running) d.wake();
}

size_t FileWatcher::rootCount() const {
  std::lock_guard<std::mutex> lk(impl_->mu);
  return impl_->extraRoots.size();
}

std::string FileWatcher::mode() const {
  std::lock_guard<std::mutex> lk(impl_->mu);
  return impl_->mode;
}

bool FileWatcher::eventDriven() const {
  std::string m = mode();
  return !m.empty() && m != "poll";
}
bool FileWatcher::detectsRenames() const { return eventDriven(); }

size_t FileWatcher::watchCount() const { return impl_->watchesPlatform(); }

}  // namespace tf
