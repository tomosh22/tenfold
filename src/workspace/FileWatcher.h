#pragma once
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace tf {

// Recursive file-system watcher with its own background thread.
//
//  Linux   : inotify (one watch per non-ignored directory; new directories are
//            picked up as they appear). If inotify is unavailable or the
//            per-user watch limit is hit, it degrades to a polling scan and
//            emits a Rescan event so the consumer can resynchronise.
//  Windows : ReadDirectoryChangesW on the root (recursive).
//  Other   : polling stub (FSEvents later).
//
// Events are coalesced per path and delivered only after the path has been
// quiet for `debounceMs` (200 ms by default). The callback runs on the
// watcher thread, never concurrently with itself; route results to the
// Editor with Editor::post(). The callback must not call stop().
class FileWatcher {
 public:
  struct Event {
    enum Kind {
      Created,
      Modified,
      Deleted,
      Renamed,  // path = new name, oldPath = previous name
      Rescan,   // events were lost (queue overflow / limit reached): resynchronise from disk
    } kind = Modified;
    std::filesystem::path path, oldPath;
    bool isDirectory = false;  // Deleted/Renamed/Created of a directory (children are not enumerated)
  };
  using Callback = std::function<void(Event)>;
  using IgnoreFn = std::function<bool(const std::filesystem::path&)>;  // true -> do not watch this directory

  struct Options {
    int debounceMs = 200;
    int pollMs = 2000;          // polling mode scan interval
    bool forcePolling = false;  // use the portable polling scan on every platform (tests, odd file systems)
    size_t maxWatches = 0;      // inotify: act as if the watch limit were this (0 = system limit); for tests
    std::function<void(const std::string&)> log;
  };

  FileWatcher();
  explicit FileWatcher(Options opt);
  ~FileWatcher();
  FileWatcher(const FileWatcher&) = delete;
  FileWatcher& operator=(const FileWatcher&) = delete;

  // Starts watching `root` recursively. `ignoreFn(dir)` is asked about every
  // directory below the root (not the root itself). Returns false if nothing
  // could be started. Calling start() twice without stop() is an error.
  bool start(const std::filesystem::path& root, IgnoreFn ignoreFn, Callback cb);
  // Wakes the thread, joins it, and drops events that were still being debounced.
  void stop();
  bool running() const;

  // Also watch one directory that ignoreFn would exclude (non-recursive), e.g.
  // the folder holding compile_commands.json. Linux only (no-op elsewhere); thread-safe.
  void addDirectory(const std::filesystem::path& dir);

  // Backend capabilities (valid while running). The polling backend is not
  // event driven (latency is pollMs + debounce) and cannot pair renames: a
  // rename is seen as Deleted(old) + Created(new), and directory events are
  // not reported (their files are).
  bool eventDriven() const;
  bool detectsRenames() const;

  // Diagnostics: "inotify", "readdirchanges", "poll" or "" when stopped.
  std::string mode() const;
  size_t watchCount() const;  // inotify watches (== directories) currently held

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace tf
