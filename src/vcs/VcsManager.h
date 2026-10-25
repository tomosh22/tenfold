#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "vcs/LineDiff.h"
#include "vcs/Vcs.h"

namespace tf {

class Document;
class Editor;

// Version control for one workspace, owned by the Editor (Editor::vcs()).
//
// Threading (docs/ARCHITECTURE.md "Version control"): every public method is for the main thread. One worker
// thread, started lazily by the first document, owns the VcsProvider and runs ALL blocking work (child processes,
// the line diff). It receives plain copies (paths, a snapshot of the lines) and answers only through
// Editor::post(), so no Document is ever touched off the main thread. Edits are debounced on the worker; results
// carry the edit sequence they were computed for and are dropped when the document moved on.
class VcsManager {
 public:
  using ProviderFactory = std::function<std::unique_ptr<VcsProvider>()>;

  struct Options {
    bool enabled = true;
    int debounceMs = 150;         // quiet time after an edit before the diff is recomputed
    int statusDebounceMs = 1000;  // quiet time after workspace file events before the status is re-read
    // Tried in order, the first whose detect() accepts the workspace root wins. Empty = defaultProviders().
    std::vector<ProviderFactory> providers;
    // Which providers are used when `providers` is empty (workspace.json "vcs.provider"): "auto" (Perforce, then
    // Git; the first whose detect() accepts the workspace wins), "perforce", "git" or "none" (version control off).
    std::string provider = "auto";
    std::string p4Path;  // workspace.json "vcs.p4Path": the p4 executable; empty = `p4` from PATH
    DiffOptions diff;
    std::function<void(const std::string&)> log;
    // Test hook: runs on the worker thread right before every diff is computed.
    std::function<void()> diffHook;
  };

  // Counters for tests and benchmarks.
  struct Stats {
    int diffsComputed = 0;  // diffs finished on the worker
    int staleDropped = 0;   // results discarded because the document or the base moved on
    int baseLoads = 0;      // base text requests answered
    int beforeFirstEditCalls = 0;
  };

  // Perforce first, then Git (a p4 client mapping the workspace is the more specific claim).
  static std::vector<ProviderFactory> defaultProviders(const std::string& p4Path = {});
  // The providers for a `vcs.provider` setting (see Options::provider); empty for "none". Unknown values mean "auto".
  static std::vector<ProviderFactory> providersFor(const std::string& setting, const std::string& p4Path = {});

  VcsManager(Editor& editor, Options opt);
  ~VcsManager();
  VcsManager(const VcsManager&) = delete;
  VcsManager& operator=(const VcsManager&) = delete;
  void shutdown();  // joins the worker; later notifications are ignored

  // ---- queries (main thread) ----
  bool active() const { return active_; }  // a provider accepted the workspace
  std::string providerName() const { return providerName_; }
  std::filesystem::path root() const { return root_; }
  // Bumped whenever anything a view shows changed (detection, status, base, hunks); poll it once per frame.
  uint64_t generation() const { return generation_; }
  // Hunks of an open document, valid for hunksVersion(path) of the document; empty without a base.
  const std::vector<DiffHunk>& hunks(const std::filesystem::path& file) const;
  int hunksVersion(const std::filesystem::path& file) const;  // Document::version() the hunks belong to; 0 = none yet
  bool hasBase(const std::filesystem::path& file) const;
  // The committed lines (split like the buffer; empty for a file that is new in the index).
  std::shared_ptr<const std::vector<std::string>> baseLines(const std::filesystem::path& file) const;
  std::vector<std::string> hunkBaseLines(const std::filesystem::path& file, size_t hunkIndex) const;
  // Provider status; an open document with unsaved changes that the provider calls Unmodified reads Modified.
  VcsStatus fileStatus(const std::filesystem::path& file) const;
  VcsFileInfo fileInfo(const std::filesystem::path& file) const;
  std::string lastError() const { return lastError_; }
  Stats stats() const;

  // Fired on the pump thread when generation() changed; `file` is empty for workspace-wide changes.
  using ChangedHandler = std::function<void(const std::filesystem::path& file)>;
  void onChanged(ChangedHandler h) { changed_ = std::move(h); }
  // Fired on the pump thread when a provider call failed (beforeFirstEdit, revert, ...).
  using ErrorHandler = std::function<void(const std::filesystem::path& file, const std::string& message)>;
  void onError(ErrorHandler h) { errorHandler_ = std::move(h); }

  // ---- commands (main thread) ----
  // Re-reads the status of the whole workspace and the base of every open document (call after commits made
  // outside the editor, e.g. when the window regains focus). Results arrive via post().
  void refreshStatus();
  // Provider revert on the worker, then (via post) an open document is reloaded from disk exactly like an external
  // change (Editor::reloadFromDisk: only the changed lines are replaced, navigation history follows) and the status
  // is re-read. `done(ok, error)` runs on the pump thread.
  using Done = std::function<void(bool ok, const std::string& error)>;
  void revertFile(const std::filesystem::path& file, Done done = {});
  // Explicit "check out" (C3c, Vcs.CheckOut): runs the provider's beforeFirstEdit for `file` right now, off the UI
  // thread, exactly what the first edit would have run (nothing for Git; `p4 edit` for Perforce), then re-reads the
  // file's status. An open document counts as checked out afterwards, so its first edit does not repeat it. A failure
  // is reported through onError like an auto-checkout failure; `done(ok, error)` runs on the pump thread.
  void checkOut(const std::filesystem::path& file, Done done = {});
  // Replaces the lines of hunk `index` of the open document by the base lines through the Document edit path (ONE
  // undo step, carets of the current document follow). Synchronous and exact: stale hunks are recomputed first.
  bool revertHunk(const std::filesystem::path& file, size_t index, std::string* error = nullptr);

  // ---- notifications from the Editor (main thread) ----
  void documentOpened(Document& doc);
  void documentChanged(Document& doc);  // every edit; the first one that makes the document dirty runs beforeFirstEdit
  void documentSaved(Document& doc);
  void documentReloaded(Document& doc);  // Editor::reloadFromDisk finished (clean state again)
  void documentClosed(Document& doc);
  void fileEvent(const std::filesystem::path& file);  // a workspace file changed on disk (file watcher)

 private:
  struct DocState {
    Document* doc = nullptr;
    std::filesystem::path path;
    uint64_t seq = 0;     // bumped by every edit / reload
    uint64_t baseId = 0;  // bumped by every base request
    std::shared_ptr<const std::vector<std::string>> base;
    std::vector<DiffHunk> hunks;
    int hunksVersion = 0;
    bool firstEditDone = false;
  };

  // worker side
  void ensureStarted();
  void push(std::function<void()> job);
  void schedule(const std::string& key, int delayMs, std::function<void()> fn);  // replaces a pending timer
  void workerLoop();
  void detectJob(std::filesystem::path root);

  // main side
  std::string keyOf(const std::filesystem::path& p) const;
  DocState* find(const std::filesystem::path& p) const;
  void requestBase(DocState& st);
  void requestFileStatus(const std::filesystem::path& file);
  void startDiff(const std::string& key);
  void changed(const std::filesystem::path& file);
  void reportError(const std::filesystem::path& file, const std::string& msg);
  void applyStatuses(std::vector<VcsFileInfo> infos, bool replaceAll);
  void post(std::function<void()> fn);

  Editor& editor_;
  Options opt_;

  // main thread state
  bool active_ = false;
  std::string providerName_;
  std::filesystem::path root_;
  std::string rootKey_;
  uint64_t generation_ = 0;
  std::string lastError_;
  std::map<std::string, std::unique_ptr<DocState>> docs_;  // key: keyOf(path)
  std::map<std::string, VcsFileInfo> statuses_;            // key: keyOf(path); Unmodified files are absent
  mutable std::unordered_map<std::string, std::string> keyCache_;
  ChangedHandler changed_;
  ErrorHandler errorHandler_;
  Stats mainStats_;
  bool stopped_ = false;

  // worker state
  std::unique_ptr<VcsProvider> provider_;  // created and used by the worker only
  std::thread thread_;
  std::mutex mu_;
  std::condition_variable cv_;
  std::deque<std::function<void()>> jobs_;
  struct Timer {
    std::chrono::steady_clock::time_point due;
    std::function<void()> fn;
  };
  std::map<std::string, Timer> timers_;
  bool stopWorker_ = false;
  bool started_ = false;
  std::atomic<int> diffsComputed_{0};
  std::atomic<int> beforeCalls_{0};
};

}  // namespace tf
