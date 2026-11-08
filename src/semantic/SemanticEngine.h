#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/Document.h"
#include "core/LspLineMap.h"
#include "lsp/LspClient.h"
#include "semantic/CompileCommands.h"
#include "semantic/FastIndex.h"
#include "semantic/FastScope.h"
#include "semantic/IncludeNav.h"
#include "semantic/PpCond.h"
#include "semantic/TuContext.h"
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
  Location loc;  // byte-column position, ready for TextBuffer
  std::string name;
  std::string container;  // scope / qualifier
  std::string kind;       // human-readable kind
  bool definition = true;
  Confidence confidence = Confidence::Authoritative;
  // Whole extent of the symbol (byte columns) for document symbols answered by clangd: a function definition's
  // extent contains its body, a declaration's ends at the `;`. Empty (start == end) when unknown.
  Range extent;
  // Go to definition with several meaningful targets (W10-CHOOSE: through a using-declaration or an alias, on a
  // constructor call): what this entry is, as the chooser shows it ("class Widget", "constructor Widget(int)",
  // "using-declaration", "typedef Bar -> Foo"). Empty for ordinary answers. The list is ordered declaration first,
  // real entity last; `preferred` marks the entry the chooser selects.
  std::string label;
  bool preferred = false;
  // Approximate answers only: clangd may still add targets (this single entry is a class used like a constructor
  // call, or an alias), so the Editor does not jump on it until the authoritative answer is in.
  bool refinable = false;
  // Find All References (VS_BEHAVIOUR N5): what the place does with the name, "Definition" (a declaration or definition
  // the fast index knows), "Write" (assigned, incremented), "Read", or "Other" (a spelling, not a use of a value:
  // preprocessor lines, using / typedef / namespace). The list is ordered definitions first, then by file, line and
  // column. Empty for every other answer.
  std::string role;
};

struct Diagnostic {
  std::filesystem::path file;
  Range range;
  int severity = 1;
  std::string message;
  std::string code;
};

// One decoded semantic token, byte columns. `type` indexes SemanticEngine::tokenLegend().types, `modifiers` is a
// bit set over tokenLegend().modifiers.
struct TokenSpan {
  int line = 0, col = 0, len = 0;
  uint16_t type = 0;
  uint32_t modifiers = 0;
};

// Edits grouped per file, ranges in byte columns, ready for Document::replace. Edits of one file never overlap.
using TextEditList = std::vector<std::pair<Range, std::string>>;
using FileEdits = std::map<std::filesystem::path, TextEditList>;

// A call hierarchy or type hierarchy node. `loc` (start of the name) can be passed to Editor::gotoLocation.
struct HierarchyItem {
  std::string name, detail, kind;
  Location loc;
  Range range, selection;  // byte columns
  LspHierarchyItem server;
};
struct CallEdge {
  HierarchyItem item;  // the caller (incoming) or callee (outgoing)
  std::vector<Range>
      ranges;  // call sites, byte columns; in item's file for incoming, in the queried item's for outgoing
};

// A quick fix or refactoring. Apply `edit` (if hasEdit) first, then run `command` (if hasCommand), whose edits
// arrive through onApplyEdit.
struct CodeAction {
  std::string title, kind;
  bool preferred = false;
  bool hasEdit = false;
  FileEdits edit;
  bool hasCommand = false;
  std::string command;
  Json arguments;
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
    std::string clangdPath;               // empty -> resolve from settings / PATH
    std::vector<std::string> clangdArgs;  // extra args
    int clangdBudgetMs = 250;             // wait this long before showing approximate results
    // A user-facing request (go to definition / declaration / type / implementations, references, rename) that clangd
    // has not answered after this long is cancelled ($/cancelRequest) and its callback gets an error: tenfold never
    // waits forever for a clangd that dropped a request (W9g G-2). 0 disables.
    int requestTimeoutMs = 25000;
    int indexThreads = 0;           // 0 -> hardware concurrency
    bool persistFastIndex = true;   // load/save <root>/.tenfold/fastindex.bin (FastIndexStore)
    int fastIndexSaveSeconds = 60;  // periodic save interval while the index is dirty
    std::filesystem::path logDir;   // empty -> <root>/.tenfold
    std::function<void(const std::string&)> log;
    bool traceLsp = false;
    bool utf16Positions = false;  // offer clangd only UTF-16 columns (it prefers UTF-8): exercises that path in tests
    int semanticTokensDebounceMs = 150;  // after didOpen / didChange (only while onSemanticTokens has a handler)
    // Crash recovery (D2): after an unexpected clangd exit the engine restarts it after restartBackoffInitialMs,
    // doubling per consecutive crash up to restartBackoffMaxMs. A crash after restartHealthyMs of healthy running
    // starts the count again; restartMaxFastCrashes consecutive fast crashes (or failed restarts) make it give up.
    int restartBackoffInitialMs = 500;
    int restartBackoffMaxMs = 30000;
    int restartHealthyMs = 60000;
    int restartMaxFastCrashes = 5;
    // Test/measurement hook (corpus harness): navigation, hover, symbol and rename queries behave as if clangd were
    // not running (fast tier only: an Approximate answer with the error "clangd not running") while clangd itself keeps
    // running and parsing. Switchable at any time with setFastOnlyQueries(); off in the product.
    bool fastOnlyQueries = false;
    // Visual Studio solutions keep one compile database and one clangd background index per solution configuration
    // (<root>/.tenfold/vs/<configuration>/), so an index never mixes the declarations of two configurations. At most
    // this many are kept; the least recently used are deleted when a configuration is selected.
    int maxVsConfigIndexes = 4;
    // Test hook: pretend no MSVC toolchain was found even where one is (forces the host standard library fallback of
    // .sln mode); the product decides by itself (off on Windows).
    bool forceHostStdlibFallback = false;
  };

  // Lifecycle of the clangd child as the status bar shows it.
  struct ClangdHealth {
    enum class Phase { Disabled, Starting, Running, Restarting, Stopped };
    Phase phase = Phase::Disabled;
    int crashes = 0;        // consecutive fast crashes so far (the attempt number while Restarting)
    int totalRestarts = 0;  // restarts completed since start() (automatic and manual)
    std::string lastError;  // why the last start/restart failed, if it did
    // "" while Disabled/Running, else "clangd starting...", "clangd restarting (2)...", "clangd stopped: crashed 5
    // times" / "clangd stopped: <error>".
    std::string text() const;
  };

  struct TokenStats {
    int fullRequests = 0, deltaRequests = 0;
    double lastDecodeMs = 0;  // time spent decoding the last reply into TokenSpans
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
    size_t fastRelexed = 0;  // files lexed at startup (the rest came from the persisted store)
    size_t fastReused = 0;
    std::string compdbSource;  // existing | cmake | vcxproj | heuristic | none
    std::filesystem::path compdbPath;
    size_t compdbEntries = 0;
    std::string clangdExe;     // executable the engine launched ("" before the first launch)
    std::string clangdSource;  // setting | bundled | PATH
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
  // True once clangd's background index has finished at least one pass and then stayed idle for `quietMs`. The bare
  // "progress end" is not a barrier: clangd announces it before the merged index it queries is rebuilt and may start
  // another pass right after (more files enqueued, shards loaded lazily), so a query made at that instant sees a
  // partial index (W9c: sym "op_mul" answered nothing; W9d: the lookup-namespaces rename flake under load).
  bool waitForClangdIndex(int timeoutMs, int quietMs = 300) const;
  bool waitForFile(const std::filesystem::path& file, int timeoutMs) const;  // clangd AST ready (true: not sent at all)
  bool fileReady(const std::filesystem::path& file) const;
  // See Options::fastOnlyQueries. Thread-safe.
  void setFastOnlyQueries(bool on) { fastOnly_.store(on, std::memory_order_relaxed); }
  bool fastOnlyQueries() const { return fastOnly_.load(std::memory_order_relaxed); }

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
  // Also the manual "restart clangd" command: it cancels a pending automatic restart, resets the crash count and
  // works after the engine gave up.
  bool restartClangd(std::string* error = nullptr);

  // --- clangd crash recovery (D2) ---
  // Thread-safe snapshot of the clangd lifecycle.
  ClangdHealth clangdHealth() const;
  // Delay before restart number `crashes` (1-based): initial * 2^(crashes-1), capped at the maximum.
  static int backoffDelayMs(const Options& opt, int crashes);
  std::string clangdStatusText() const { return clangdHealth().text(); }
  // Called (outside any engine lock, on the supervisor / calling thread) after every health change.
  void onClangdHealth(std::function<void(const ClangdHealth&)> h);
  // After an automatic restart the open documents must be re-sent from the thread that owns them. With a poster
  // set, the engine hands that step to it (Editor: its post()); without one it runs on the supervisor thread,
  // which is only safe while no document is edited concurrently (CLI, tests).
  void setPoster(std::function<void(std::function<void()>)> poster);

  // Document lifecycle (forwarded to clangd and the fast index).
  void didOpen(Document& doc);
  // The user brought an already open document into use (Editor::open on it): a document parked at a Visual Studio
  // configuration switch is sent to clangd again.
  void activateDocument(Document& doc);
  void didChange(Document& doc, const Document::Change& change);
  void didSave(Document& doc);
  void didClose(Document& doc);

  // Navigation. `doc` must be open (didOpen) for clangd to see unsaved edits.
  void gotoDefinition(const Document& doc, Position pos, LocationsCallback cb);
  void gotoDeclaration(const Document& doc, Position pos, LocationsCallback cb);
  void gotoTypeDefinition(const Document& doc, Position pos, LocationsCallback cb);
  void findReferences(const Document& doc, Position pos, LocationsCallback cb);
  void findImplementations(const Document& doc, Position pos, LocationsCallback cb);
  // Asynchronous and thread-safe: returns immediately. The approximate tier is computed on a dedicated symbol
  // thread (parallel scan of the FastIndex), the callbacks run on that thread / the timer thread / the clangd
  // reader thread (Editor::findSymbols post()s them to the pump thread). A newer workspaceSymbols call SUPERSEDES
  // every older one: the older query stops scanning and its callback is never invoked (at most one query is
  // ever pending). After stop() the callback is invoked once, inline, with an error.
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
  // Miscellaneous files (VS_BEHAVIOUR I5): a file that is no translation unit of the compile database and that no
  // translation unit includes, directly or through other headers (also not as a fragment given a TU context). Visual
  // Studio shows no error squiggles in such a file; with `show` false (the default) the engine does not hand clangd's
  // diagnostics of those files to the onDiagnostics handler (it hands an empty list, so earlier ones disappear).
  // Without a compile database nothing is a miscellaneous file. Switching it re-delivers the held diagnostics.
  bool isMiscFile(const std::filesystem::path& file) const;
  void setMiscFileDiagnostics(bool show);
  bool miscFileDiagnostics() const { return showMisc_; }

  // --- clangd-only features (constraint 1.4: no approximate tier; without clangd the callback gets an error) ---
  // Semantic tokens: the first request for a document is /full, later ones /full/delta against the previous result
  // id (falling back to /full if the server rejects it). Everything is decoded to byte columns.
  void semanticTokens(const Document& doc, Callback<std::vector<TokenSpan>> cb);
  // Requested automatically (debounced) after didOpen / didChange; `version` is the document version the tokens
  // were computed for. Handler runs on the engine's timer / reader thread.
  void onSemanticTokens(std::function<void(const std::filesystem::path&, int version, std::vector<TokenSpan>)> h);
  LspSemanticTokensLegend tokenLegend() const;
  TokenStats tokenStats() const;
  void inlayHints(const Document& doc, Range range, Callback<std::vector<LspInlayHint>> cb);
  void foldingRanges(const Document& doc, Callback<std::vector<LspFoldingRange>> cb);
  void documentHighlights(const Document& doc, Position pos, Callback<std::vector<LspHighlight>> cb);
  void prepareCallHierarchy(const Document& doc, Position pos, Callback<std::vector<HierarchyItem>> cb);
  void incomingCalls(const HierarchyItem& item, Callback<std::vector<CallEdge>> cb);
  void outgoingCalls(const HierarchyItem& item, Callback<std::vector<CallEdge>> cb);
  void prepareTypeHierarchy(const Document& doc, Position pos, Callback<std::vector<HierarchyItem>> cb);
  void supertypes(const HierarchyItem& item, Callback<std::vector<HierarchyItem>> cb);
  void subtypes(const HierarchyItem& item, Callback<std::vector<HierarchyItem>> cb);
  void formatDocument(const Document& doc, Callback<TextEditList> cb);
  void formatRange(const Document& doc, Range range, Callback<TextEditList> cb);
  void codeActions(const Document& doc, Range range, Callback<std::vector<CodeAction>> cb);
  void executeCommand(const std::string& command, Json arguments, Callback<Json> cb);
  // Edits clangd asks us to apply (workspace/applyEdit, e.g. while running a tweak command). Reader thread; the
  // server is told "applied" immediately, the Editor applies the edit on its own thread.
  void onApplyEdit(std::function<void(const std::string& label, FileEdits)> h);
  // workspace/didChangeWatchedFiles; type: 1 created, 2 changed, 3 deleted. Files open in a Document are skipped
  // (the server already has their text). Thread-safe.
  void watchedFileChanged(const std::filesystem::path& file, int type);

  // --- translation-unit context for included files (TuContext.h) ---
  // A file that is only ever #included (.tpp .ipp .inl .def, unity-build parts) has no compile command and is not
  // self-contained. When such a document is opened the engine finds a translation unit that includes it and gives
  // clangd a compile command that replays what the compiler had seen at the #include (the includer chain's text up
  // to the directive is forced in as a copy under <cache>/tu/), so the file is parsed in context instead of alone.
  struct TuContextInfo {
    std::filesystem::path root;                // the compile database entry the command was derived from
    std::vector<std::filesystem::path> chain;  // includers from `root` down to the file's direct includer
    std::vector<std::string> arguments;        // the compile command given to clangd
  };
  // The context given to clangd for `file`, if it needed and got one.
  std::optional<TuContextInfo> tuContext(const std::filesystem::path& file) const;

  // --- rename safety ---
  // clangd renames what its index knows. When the index is missing a file (a non-self-contained file parsed alone,
  // a translation unit not indexed yet) the rename leaves spellings of the old name behind and the code stops
  // compiling. This looks for them: every identifier token `oldName` (outside comments and literals) in the files
  // that were edited, the files that #include them (up to three levels) and the files they #include (two levels,
  // which is where .tpp / .inl / .def members of an edited header live) that is not at the start of an edit.
  // Cheap (a lexical scan of at most `maxFiles` files / 8 MB) and approximate in the safe direction: a same-named
  // unrelated symbol is reported too. Must run on the thread that owns the documents.
  struct RenameCoverage {
    std::vector<Location> missed;  // at most maxReport places
    size_t total = 0;              // all places found
    size_t filesScanned = 0;
    bool limited = false;  // a scan limit was hit: there may be more
  };
  RenameCoverage renameCoverage(const std::string& oldName, const FileEdits& edits, const std::filesystem::path& origin,
                                size_t maxReport = 20, size_t maxFiles = 300) const;

  FastIndex& fastIndex() { return fast_; }
  // Thread-safe snapshots. The compile database is published as an immutable vector swapped under mu_, so a
  // reader keeps a consistent view while the worker re-prepares it. clangd() returns the running, initialised
  // client (or null); the shared_ptr keeps it alive for the duration of a request even if restartClangd()
  // replaces it concurrently (the old client is stopped, so its callbacks fail fast).
  std::shared_ptr<const std::vector<CompileCommand>> compileCommands() const;
  std::shared_ptr<LspClient> clangd() const;

  // Approximate lookups (also used by the UI for instant "peek" popups).
  std::vector<SymbolLocation> approximateDefinitions(const Document& doc, Position pos,
                                                     FastIndex::Want want = FastIndex::Want::Any) const;
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
  std::shared_ptr<LspClient> launchClangd(std::string* error);  // start a client without publishing it
  void supervisorLoop();
  void handleCrash(const std::shared_ptr<LspClient>& dead);
  void noteCrash(std::shared_ptr<LspClient> c);
  void adoptPending(uint64_t id);
  void reopenDocuments(LspClient& c);
  void resetClientState();
  // `onlyIfConsecutive` >= 0: apply only while the consecutive-crash count still has that value (a Running that was
  // decided before a later crash was counted must not overwrite the Stopped / Restarting of that crash).
  void setHealth(ClangdHealth::Phase phase, int crashes, const std::string& err = {}, int onlyIfConsecutive = -1);
  void startFastIndex();
  void saveFastIndex();
  void timerLoop();
  void symbolLoop();
  void workspaceSymbolsBudgeted(const std::string& query, std::vector<SymbolLocation> approx, LocationsCallback cb);
  void log(const std::string& msg) const;

  // Request watchdog (Options::requestTimeoutMs): `fail(message)` runs once if the request has not completed in time.
  struct ReqGuard;
  // Go to definition answer assembly (W10-CHOOSE, SemanticEngine.cpp).
  struct DefQuery;
  void refineDefinition(std::shared_ptr<DefQuery> q, std::vector<SymbolLocation> locs);
  void aliasChoices(std::shared_ptr<DefQuery> q, SymbolLocation alias);
  // `prefix`: targets listed before the class (the aliases that led to it, W10-CHOOSE).
  void constructorChoices(std::shared_ptr<DefQuery> q, SymbolLocation cls, std::vector<SymbolLocation> prefix = {});
  void inheritedConstructorChoices(std::shared_ptr<DefQuery> q, SymbolLocation cls, SymbolLocation usingDecl,
                                   std::vector<SymbolLocation> prefix);
  bool classLike(const SymbolLocation& l) const;
  // Go To Document: the caret is on an #include directive (VS_BEHAVIOUR N1). False when it is not (nothing is
  // delivered); otherwise `cb` gets the file: authoritative at once when the include search path of the includer's
  // compile command finds it, else the fast index's best guess and then clangd's answer (system headers).
  bool gotoInclude(const Document& doc, Position pos, LocationsCallback& cb);
  incnav::SearchPath searchPathFor(const std::filesystem::path& file) const;
  void locationQueryWith(
      std::vector<SymbolLocation> approx, const char* what, const Document& doc, Position pos, LocationsCallback cb,
      std::function<int(LspClient&, const std::string&, Position, LspClient::Callback<std::vector<LspLocation>>)>
          issue);
  // Find All References: roles and the group order (N5).
  void classifyReferences(std::vector<SymbolLocation>& locs) const;
  std::string lineText(const std::filesystem::path& file, int line) const;
  std::optional<Symbol> fastSymbolAt(const Location& loc) const;
  std::string describeTarget(const Location& loc) const;
  bool openInClangd(const std::filesystem::path& file) const;
  Position serverPosition(const std::filesystem::path& file, Position p) const;
  void preferConstructorDefinition(SymbolLocation& ctor) const;
  void armRequestTimeout(std::shared_ptr<ReqGuard> g, std::shared_ptr<LspClient> c, std::string what,
                         std::function<void(const std::string&)> fail);
  // Replaces a declaration in `locs` by the out-of-line definition of the same function the fast index knows (W9g G-1).
  void preferDefinitions(std::vector<SymbolLocation>& locs) const;

  // Translation-unit context (see tuContext()). Called before a document is sent to clangd.
  // Returns false when the document must NOT be sent to clangd (a fragment of a class body, see TuContext.h).
  bool provideTuContext(LspClient& c, const Document& doc);
  bool unsent(const std::string& uri) const;
  void closeWhenIdle(const std::string& uri, std::chrono::steady_clock::time_point deadline);
  // A location clangd reports inside one of our prefix copies belongs to the file it was copied from (same lines).
  std::string remapUri(const std::string& uri) const;
  std::filesystem::path pathFromUri(const std::string& uri) const { return uriToPath(remapUri(uri)); }

  // Encoding conversion helpers.
  Position toServer(const Document& doc, Position p) const;
  Range toServer(const Document& doc, Range r) const { return {toServer(doc, r.start), toServer(doc, r.end)}; }
  Position posFromServer(const std::string& uri, Position p) const;
  // NEL / LS / PS (docs/ARCHITECTURE.md "Text for clangd"): the line map of what clangd was sent for an open document
  // that has them (null for the rest, the common case), kept up to date by didOpen / didChange.
  std::shared_ptr<const LspLineMap> lspMapFor(const std::string& uri) const;
  void noteLspText(const Document& doc);
  // A file that is not open, decoded, when it has NEL / LS / PS (null: it has none, or is not UTF-8); cached by
  // mtime and size.
  struct DiskSeparators {
    TextBuffer text;
    std::shared_ptr<const LspLineMap> map;
  };
  std::shared_ptr<const DiskSeparators> diskSeparators(const std::filesystem::path& file) const;
  // Encoding of a file that is NOT open as a Document (clangd reads those from disk, so its columns count the file's
  // raw bytes); cached per path and invalidated by mtime/size. Utf8 for unreadable or huge files.
  Encoding diskEncoding(const std::filesystem::path& file) const;
  Range rangeFromServer(const std::string& uri, Range r) const {
    return {posFromServer(uri, r.start), posFromServer(uri, r.end)};
  }
  HierarchyItem hierarchyItemFromServer(const LspHierarchyItem& it) const;
  FileEdits editsFromServer(const Json& workspaceEdit) const;
  std::vector<TokenSpan> decodeTokens(const std::string& uri, const std::vector<uint32_t>& data) const;
  void scheduleTokens(const Document& doc);
  void requestTokens(const std::string& uri, Callback<std::vector<TokenSpan>> cb);
  void runLater(int delayMs, std::function<void()> fn);
  template <class T>
  std::shared_ptr<LspClient> requireClangd(const Callback<T>& cb);
  Location fromServer(const LspLocation& l) const;
  std::vector<SymbolLocation> fromServer(const std::vector<LspLocation>& ls, Confidence c) const;
  void locationQuery(
      const char* what, const Document& doc, Position pos, LocationsCallback cb,
      std::function<int(LspClient&, const std::string&, Position, LspClient::Callback<std::vector<LspLocation>>)>
          issue);
  void deliverWithBudget(std::vector<SymbolLocation> approx, LocationsCallback cb,
                         std::function<void(std::function<void(std::vector<SymbolLocation>, std::string)>)> issue);

  Workspace& ws_;
  Options opt_;
  FastIndex fast_;
  // The include search path of the compile command of a file, if the compile database has one (FastScope).
  void includeDirsOf(const std::filesystem::path& file, std::vector<std::filesystem::path>& quote,
                     std::vector<std::filesystem::path>& angle) const;
  mutable std::mutex cmdIndexMu_;
  mutable std::shared_ptr<const std::vector<CompileCommand>> cmdIndexSnap_;  // the snapshot cmdIndex_ was built from
  mutable std::unordered_map<std::string, size_t> cmdIndex_;                 // path key -> index into the snapshot
  mutable ScopeCache scopeCache_;  // lexical scope maps of files (FastScope), keyed by the index entry
  // The compile command of exactly this file in `snap` (which must stay alive while the pointer is used), or null.
  const CompileCommand* commandOf(const std::shared_ptr<const std::vector<CompileCommand>>& snap,
                                  const std::filesystem::path& file) const;
  // --- preprocessor conditionals in the fast tier (PpCond.h, W12-FAST-PP) ---
  // Drops the by-name candidates whose `#if` / `#ifdef` / `#elif` / `#else` arm is certainly inactive in the
  // translation unit of `caretFile`; never returns fewer than all candidates when every one would be dropped.
  std::vector<FastIndex::Match> filterByConditions(const std::filesystem::path& caretFile,
                                                   std::vector<FastIndex::Match> matches) const;
  // `#define` / `#undef` of the word earlier in the caret's own file decide which macro it is: the last active
  // definition hides the earlier ones and every non-macro of that name, an `#undef` drops the earlier ones. Only
  // when every directive involved is certainly active (or certainly not); never narrows to nothing.
  std::vector<FastIndex::Match> narrowByMacroState(const std::filesystem::path& caretFile, int caretLine,
                                                   const std::string& word, bool followedByParen,
                                                   std::vector<FastIndex::Match> matches) const;
  // The file whose compile command gives `file` its macros: itself, the root recorded for an include-only file, else
  // the nearest includer with a compile command (breadth first through the include graph), else `file`.
  std::filesystem::path ppRootOf(const std::filesystem::path& file) const;
  std::shared_ptr<const pp::TuEnv> ppEnvOf(const std::filesystem::path& root) const;
  struct PpEnvEntry {
    std::shared_ptr<const std::vector<CompileCommand>> snap;  // keeps the snapshot alive (its address is the key)
    uint64_t gen = 0;                                         // fast_.generation() when built
    std::shared_ptr<const pp::TuEnv> env;
    std::filesystem::path root;  // ppRootOf results use `root`; ppEnvOf entries ignore it
    uint64_t tick = 0;
  };
  mutable std::mutex ppMu_;
  mutable std::map<std::string, PpEnvEntry> ppEnvs_;   // root key -> environment
  mutable std::map<std::string, PpEnvEntry> ppRoots_;  // file key -> root
  mutable uint64_t ppTick_ = 0;
  mutable std::shared_ptr<const std::vector<CompileCommand>> flagVocabSnap_;
  mutable std::shared_ptr<const std::unordered_set<std::string>> flagVocab_;  // names any command -D / -U's (ppMu_)
  std::shared_ptr<const std::unordered_set<std::string>> flagVocabulary(
      const std::shared_ptr<const std::vector<CompileCommand>>& snap) const;
  // The scope map of `file` for the fast tier: from the open document or from disk, cached per index entry.
  std::shared_ptr<const ScopeMap> scopeMapOf(const std::filesystem::path& file, const Document* doc = nullptr) const;
  // clangd_ and compileCommands_ are replaced from other threads than the ones reading them; both are only
  // touched through the snapshot accessors. clientMu_ is a leaf lock (never held across a call out).
  mutable std::mutex clientMu_;
  std::shared_ptr<LspClient> clangd_;                                   // guarded by clientMu_
  std::shared_ptr<const std::vector<CompileCommand>> compileCommands_;  // guarded by mu_
  void pruneVsConfigDirs(const std::filesystem::path& vsRoot, const std::filesystem::path& keep);
  std::shared_ptr<LspClient> client() const;  // current client regardless of initialisation state
  std::shared_ptr<LspClient> takeClient();    // detach the current client (caller stops it)
  std::atomic<bool> fastOnly_{false};
  // The client a query is sent to: clangd(), except while fast-only queries are switched on (then none).
  std::shared_ptr<LspClient> queryClient() const { return fastOnlyQueries() ? nullptr : clangd(); }
  std::string compdbSource_ = "none";
  std::atomic<bool> hostFallbackLogged_{false};  // the host standard library fallback is logged once
  std::string clangdExe_, clangdSource_;         // guarded by mu_
  std::filesystem::path compdbPath_;
  std::vector<std::string> sharedFlags_;  // flags applied to every file via clangd config

  // Worker for file-system driven maintenance (reconfigure, bulk re-index).
  std::thread worker_;
  std::mutex workMu_;
  std::condition_variable workCv_;
  std::deque<std::function<void()>> work_;
  bool workStop_ = false;

  // Symbol-search thread: runs the (parallel) fast-tier scan of workspaceSymbols off the caller's thread.
  // symPending_ holds at most one job (the newest); symGen_ is bumped by every call and polled by the scan.
  struct SymbolJob {
    uint64_t gen = 0;
    std::string query;
    LocationsCallback cb;
  };
  std::thread symThread_;
  std::mutex symMu_;
  std::condition_variable symCv_;
  std::unique_ptr<SymbolJob> symPending_;
  bool symStop_ = false;
  std::atomic<uint64_t> symGen_{0};

  // Crash supervisor (D2). supMu_ guards everything below it; lifeMu_ serialises client launches (supervisor
  // and manual restart); the lock order is lifeMu_ -> supMu_ -> clientMu_.
  std::thread supThread_;
  std::mutex healthCbMu_;  // serialises setHealth, so handlers see the states in the order they were set
  mutable std::mutex supMu_;
  std::condition_variable supCv_;
  std::mutex lifeMu_;
  bool supStop_ = false;
  std::shared_ptr<LspClient> crashed_;      // reported dead, not yet handled
  std::shared_ptr<LspClient> adoptClient_;  // restarted, waiting for the document re-sync on the owning thread
  uint64_t adoptGen_ = 0;
  uint64_t manualGen_ = 0;  // bumped by restartClangd(): abandons an automatic restart in progress
  int consecutive_ = 0;
  std::chrono::steady_clock::time_point runStart_;
  ClangdHealth health_;
  std::function<void(const ClangdHealth&)> healthHandler_;
  std::function<void(std::function<void()>)> poster_;

  std::thread indexThread_;
  std::atomic<bool> fastDone_{false};
  FastIndex::Stats fastStats_;
  std::mutex saveMu_;  // serialises saves and guards saveRun_/savedGen_
  std::condition_variable saveCv_;
  bool saveRun_ = false;
  uint64_t savedGen_ = ~0ull;  // FastIndex::generation() last persisted (or loaded unchanged)

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
  std::multimap<std::chrono::steady_clock::time_point, std::function<void()>> delayed_;  // guarded by mu_

  // Large open documents re-index lazily (see didChange): the generation of the pending re-index per uri (mu_).
  std::map<std::string, uint64_t> reindexGen_;
  // Visual Studio configuration switch (guarded by mu_). An open header that only the previous configuration
  // includes is "parked": not sent to the new clangd, whose index would otherwise gain the declarations of a header
  // that is not part of the active configuration (clangd parses every open document on its own). Interacting with it
  // again (editing, Editor::open) sends it. `switchFrom_` is the previous configuration's compile commands, held from
  // the reconfigure until the restart consumes them.
  std::set<std::string> parkedDocs_;
  std::shared_ptr<const std::vector<CompileCommand>> switchFrom_;
  std::string activeVsName_;
  bool unparkIfParked(Document& doc);

  // Semantic token state per document uri (tokMu_): the previous result id + data for delta, debounce generation.
  struct TokenState {
    std::string resultId;
    std::vector<uint32_t> data;
    uint64_t generation = 0;
    int pendingVersion = 0;
  };
  struct DiskEncoding {
    int64_t mtime = 0;
    uint64_t size = 0;
    Encoding enc = Encoding::Utf8;
  };
  mutable std::mutex encMu_;
  mutable std::unordered_map<std::string, DiskEncoding> diskEnc_;
  std::unordered_map<std::string, std::shared_ptr<const LspLineMap>> lspMaps_;  // uri -> map (mu_)
  std::atomic<int> lspMapCount_{0};                                             // lspMaps_.size(), read without mu_
  struct DiskSepEntry {
    int64_t mtime = 0;
    uint64_t size = 0;
    std::shared_ptr<const DiskSeparators> sep;
  };
  mutable std::unordered_map<std::string, DiskSepEntry> diskSep_;  // encMu_
  mutable std::mutex tokMu_;
  std::map<std::string, TokenState> tokens_;
  TokenStats tokenStats_;
  std::function<void(const std::filesystem::path&, int, std::vector<TokenSpan>)> tokenHandler_;
  std::function<void(const std::string&, FileEdits)> applyEditHandler_;

  // Translation-unit context state (tuMu_ is a leaf lock).
  struct TuOverride {
    std::filesystem::path file, directory;
    TuContextInfo info;
  };
  mutable std::mutex tuMu_;
  std::map<std::string, TuOverride> tuOverrides_;               // pathKey(file) -> command sent to clangd
  std::map<std::string, std::filesystem::path> tuPrefixOwner_;  // prefix file name -> file it was copied from
  std::atomic<bool> tuMapped_{false};                           // tuPrefixOwner_ is not empty

  // Background-index progress as seen by this engine (mu_): completed passes ("end" reports) of the current clangd
  // and when it last went idle (default time_point while a pass runs).
  std::set<std::string> unsent_;  // uris of open documents deliberately not sent to clangd (mu_)
  int indexEnds_ = 0;
  std::chrono::steady_clock::time_point indexIdleSince_{};

  std::map<std::string, std::string> fileStatus_;    // uri -> clangd state
  std::map<std::string, const Document*> openDocs_;  // uri -> doc (for encoding conversion)
  std::function<void(const std::filesystem::path&, std::vector<Diagnostic>)> diagHandler_;
  // Miscellaneous-file rule (isMiscFile): answers cached per compile database snapshot and fast index generation, and
  // the last diagnostics clangd published for those files (re-delivered when the switch flips).
  std::atomic<bool> showMisc_{false};
  mutable std::mutex miscMu_;
  mutable const void* miscSnap_ = nullptr;
  mutable uint64_t miscGen_ = 0;
  mutable std::map<std::string, bool> miscCache_;
  std::map<std::string, std::pair<std::filesystem::path, std::vector<Diagnostic>>> miscHeld_;
};

}  // namespace tf
