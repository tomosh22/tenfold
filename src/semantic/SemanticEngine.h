#pragma once
#include <atomic>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/Document.h"
#include "lsp/LspClient.h"
#include "semantic/CompileCommands.h"
#include "semantic/FastIndex.h"
#include "workspace/Workspace.h"

namespace tf {

// How much to trust a result.
//   Approximate   : from the lexer-based FastIndex. Instant, never needs build
//                   flags, but heuristic (may include false positives, cannot
//                   resolve templates / auto / typedefs / overloads).
//   Authoritative : from clangd, i.e. the real Clang frontend with the
//                   project's compile flags. This is "IntelliSense-grade".
enum class Confidence { Approximate, Authoritative };
inline const char* toString(Confidence c) { return c == Confidence::Authoritative ? "authoritative" : "approximate"; }

struct SymbolLocation {
  Location loc;            // byte-column position, ready for TextBuffer
  std::string name;
  std::string container;   // scope / qualifier
  std::string kind;        // human-readable kind
  bool definition = true;
  Confidence confidence = Confidence::Authoritative;
};

struct Diagnostic {
  std::filesystem::path file;
  Range range;
  int severity = 1;
  std::string message;
  std::string code;
};

// Two-tier semantic engine.
//
//  tier 1  FastIndex   : whole workspace lexed in parallel at startup (and
//                        re-lexed per file on edit). Powers instant symbol
//                        search and acts as the fallback for navigation
//                        while clangd is still parsing.
//  tier 2  clangd      : authoritative definitions, references, hover,
//                        completion, diagnostics, rename. Fed by a
//                        compile_commands.json we discover or generate
//                        (CMake, .sln/.vcxproj, or heuristic include paths).
//
// Query policy ("fast AND can't miss"): every navigation query goes to
// clangd. If clangd answers within `clangdBudgetMs` the caller sees exactly
// one Authoritative callback. Otherwise the caller gets an Approximate
// callback right away (so the UI never blocks) followed by the Authoritative
// one when clangd finishes. clangd's answer always wins.
class SemanticEngine {
 public:
  struct Options {
    bool useClangd = true;
    std::string clangdPath;                 // empty -> resolve from settings / PATH
    std::vector<std::string> clangdArgs;    // extra args
    int clangdBudgetMs = 250;               // wait this long before showing approximate results
    int indexThreads = 0;                   // 0 -> hardware concurrency
    std::filesystem::path logDir;           // empty -> <root>/.tenfold
    std::function<void(const std::string&)> log;
    bool traceLsp = false;
  };

  struct Status {
    bool clangdRunning = false;
    bool clangdInitialized = false;
    std::string clangdVersion;
    std::string offsetEncoding;
    IndexProgress clangdIndex;
    bool fastIndexDone = false;
    size_t fastFiles = 0;
    size_t fastSymbols = 0;
    double fastSeconds = 0;
    std::string compdbSource;  // existing | cmake | vcxproj | heuristic | none
    std::filesystem::path compdbPath;
    size_t compdbEntries = 0;
  };

  using LocationsCallback = std::function<void(Confidence, std::vector<SymbolLocation>, const std::string& error)>;
  template <class T>
  using Callback = std::function<void(T, const std::string& error)>;

  SemanticEngine(Workspace& ws, Options opt);
  ~SemanticEngine();

  // Prepares compile commands (may run cmake), starts clangd, starts fast indexing.
  // Blocking only for the clangd handshake; indexing continues in the background.
  bool start(std::string* error = nullptr);
  void stop();
  Status status() const;
  bool waitForFastIndex(int timeoutMs) const;
  bool waitForClangdIndex(int timeoutMs) const;
  bool waitForFile(const std::filesystem::path& file, int timeoutMs) const;  // clangd AST ready
  bool fileReady(const std::filesystem::path& file) const;

  // --- file-system changes (see FileWatcher; wired up by Editor) ---
  // fileChanged / fileRemoved are thread-safe and cheap (one file). Files that
  // are open in a Document are skipped unless `includeOpen`, because the
  // in-memory text, not the disk, is what the index should reflect for those.
  void fileChanged(const std::filesystem::path& file, bool includeOpen = false);
  void fileRemoved(const std::filesystem::path& file, bool includeOpen = false);
  // Re-lex many files on the engine's worker thread (renamed / created directories, rescans).
  void indexFilesAsync(std::vector<std::filesystem::path> files);
  // Re-run compile-command discovery on the worker thread (call after
  // compile_commands.json / CMakeLists.txt changed). `done(locationChanged)`
  // runs on the worker thread; clangd re-reads a database in place, so only
  // call restartClangd() when the location changed. `sources`/`headers` are a
  // snapshot of the workspace lists (taken on the caller's thread).
  void refreshCompileCommands(std::vector<std::filesystem::path> sources, std::vector<std::filesystem::path> headers,
                              std::function<void(bool locationChanged)> done = {});
  // Restart clangd against the current compile database and re-open the open
  // documents. Blocks for the clangd handshake; call from the editor thread.
  bool restartClangd(std::string* error = nullptr);

  // Document lifecycle (forwarded to clangd and the fast index).
  void didOpen(Document& doc);
  void didChange(Document& doc, const Document::Change& change);
  void didSave(Document& doc);
  void didClose(Document& doc);

  // Navigation. `doc` must be open (didOpen) for clangd to see unsaved edits.
  void gotoDefinition(const Document& doc, Position pos, LocationsCallback cb);
  void gotoDeclaration(const Document& doc, Position pos, LocationsCallback cb);
  void gotoTypeDefinition(const Document& doc, Position pos, LocationsCallback cb);
  void findReferences(const Document& doc, Position pos, LocationsCallback cb);
  void findImplementations(const Document& doc, Position pos, LocationsCallback cb);
  void workspaceSymbols(const std::string& query, LocationsCallback cb);
  void documentSymbols(const Document& doc, LocationsCallback cb);
  void switchSourceHeader(const Document& doc, Callback<std::filesystem::path> cb);

  // Information / editing.
  void hover(const Document& doc, Position pos, Callback<std::string> cb);
  void completion(const Document& doc, Position pos, Callback<std::vector<LspCompletionItem>> cb);
  void signatureHelp(const Document& doc, Position pos, Callback<LspSignatureHelp> cb);
  void rename(const Document& doc, Position pos, const std::string& newName,
              Callback<std::map<std::filesystem::path, std::vector<std::pair<Range, std::string>>>> cb);

  void onDiagnostics(std::function<void(const std::filesystem::path&, std::vector<Diagnostic>)> h);

  FastIndex& fastIndex() { return fast_; }
  const std::vector<CompileCommand>& compileCommands() const { return compileCommands_; }
  LspClient* clangd() { return clangd_ && clangd_->initialized() ? clangd_.get() : nullptr; }

  // Approximate lookups (also used by the UI for instant "peek" popups).
  std::vector<SymbolLocation> approximateDefinitions(const Document& doc, Position pos) const;
  std::vector<SymbolLocation> approximateSymbols(const std::string& query, size_t limit = 200) const;
  // While clangd's background index is incomplete it can only see the current
  // TU, so "go to definition" of a function defined in another .cpp stops at
  // the declaration. This appends fast-index definitions of `name` (flagged
  // Approximate) when the authoritative results contain no definition.
  void augmentWithFastDefinitions(const std::string& name, std::vector<SymbolLocation>& locs) const;

 private:
  struct Pending;
  // Returns true if the directory holding the compile database changed.
  bool prepareCompileCommands(const std::vector<std::filesystem::path>& sources,
                              const std::vector<std::filesystem::path>& headers, bool reconfigure);
  void runAsync(std::function<void()> task);
  void workerLoop();
  bool startClangd(std::string* error);
  void startFastIndex();
  void timerLoop();
  void log(const std::string& msg) const;

  // Encoding conversion helpers.
  Position toServer(const Document& doc, Position p) const;
  Location fromServer(const LspLocation& l) const;
  std::vector<SymbolLocation> fromServer(const std::vector<LspLocation>& ls, Confidence c) const;
  void locationQuery(const char* what, const Document& doc, Position pos, LocationsCallback cb,
                     std::function<int(LspClient&, const std::string&, Position, LspClient::Callback<std::vector<LspLocation>>)> issue);
  void deliverWithBudget(std::vector<SymbolLocation> approx, LocationsCallback cb,
                         std::function<void(std::function<void(std::vector<SymbolLocation>, std::string)>)> issue);

  Workspace& ws_;
  Options opt_;
  FastIndex fast_;
  std::unique_ptr<LspClient> clangd_;
  std::vector<CompileCommand> compileCommands_;
  std::string compdbSource_ = "none";
  std::filesystem::path compdbPath_;
  std::vector<std::string> sharedFlags_;  // flags applied to every file via clangd config

  // Worker for file-system driven maintenance (reconfigure, bulk re-index).
  std::thread worker_;
  std::mutex workMu_;
  std::condition_variable workCv_;
  std::deque<std::function<void()>> work_;
  bool workStop_ = false;

  std::thread indexThread_;
  std::atomic<bool> fastDone_{false};
  FastIndex::Stats fastStats_;

  // Deadline timer for the "approximate first" policy.
  std::thread timer_;
  std::atomic<bool> timerRun_{false};
  mutable std::mutex mu_;
  mutable std::condition_variable cv_;
  struct Pending {
    std::chrono::steady_clock::time_point deadline;
    std::shared_ptr<struct PendingState> state;
  };
  std::deque<Pending> pending_;

  std::map<std::string, std::string> fileStatus_;  // uri -> clangd state
  std::map<std::string, const Document*> openDocs_;  // uri -> doc (for encoding conversion)
  std::function<void(const std::filesystem::path&, std::vector<Diagnostic>)> diagHandler_;
};

}  // namespace tf
