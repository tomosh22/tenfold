#pragma once
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "app/Bookmarks.h"
#include "build/AnsiStrip.h"
#include "build/BuildRunner.h"
#include "build/ErrorParser.h"
#include "core/Document.h"
#include "core/Selection.h"
#include "nav/NavigationHistory.h"
#include "search/TextSearch.h"
#include "semantic/SemanticEngine.h"
#include "workspace/FileWatcher.h"
#include "workspace/Workspace.h"

namespace tf {

// The UI-independent editor model: workspace + open documents + a set of
// carets/selections + navigation history + semantic engine, with a main-thread
// work queue so background results are handed to the UI on its own thread.
//
// A front end (GPU/native window, TUI, test harness) drives this class and
// renders its state. The caret set (primary first) lives here; rendering state
// (scroll, layout) belongs to the front end's view layer.
class Editor {
 public:
  struct Options {
    SemanticEngine::Options semantic;
    NavigationHistory::Options navigation;
    std::function<void(const std::string&)> log;
    int watchPollMs = 2000;          // scan interval when the file watcher has to poll
    bool watchForcePolling = false;  // use the polling backend even where native events exist
    // Monotonic milliseconds used for the typing-pause undo boundary; null means std::chrono::steady_clock.
    std::function<int64_t()> clock;
    // Fired on the pump thread whenever the clangd status text changes ("clangd restarting (2)...", "clangd stopped:
    // crashed 5 times"; empty when clangd runs normally or is disabled).
    std::function<void(const std::string&)> onClangdStatus;
  };

  enum class Direction { Left, Right, Up, Down, LineStart, LineEnd, DocStart, DocEnd, PageUp, PageDown };
  // Left/Right: Char or Word. Up/Down: Line (one line) or Page. The other directions ignore the unit.
  enum class Unit { Char, Word, Line, Page };

  explicit Editor(std::filesystem::path root);
  Editor(std::filesystem::path root, Options opt);
  ~Editor();

  bool start(std::string* error = nullptr);  // scan workspace, start semantic engine
  void shutdown();

  Workspace& workspace() { return ws_; }
  SemanticEngine& semantic() { return *semantic_; }
  bool started() const { return semantic_ != nullptr; }  // semantic() is only valid after start()
  NavigationHistory& navigation() { return nav_; }
  // --- clangd lifecycle (crash recovery is automatic, see SemanticEngine) ---
  // Status-bar text as of the last pumped change (pump thread).
  const std::string& clangdStatusText() const { return clangdStatus_; }
  // The "Restart clangd" command: blocks for the handshake, re-opens the open documents, resets the crash count.
  bool restartClangd(std::string* error = nullptr);

  // --- documents ---
  Document* open(const std::filesystem::path& path, std::string* error = nullptr);
  Document* document(const std::filesystem::path& path);
  std::vector<Document*> documents();
  bool close(const std::filesystem::path& path);
  bool save(Document& doc, std::string* error = nullptr);
  // Replace the buffer with the file's current content (undo history is cleared,
  // the document is clean afterwards). Only the changed line range is replaced,
  // so navigation history entries and the language server stay in sync.
  bool reloadFromDisk(Document& doc, std::string* error = nullptr);

  // --- file watching ---
  // Fired on the pump thread when a file that is open in a Document changed on
  // disk and its content differs from what we last loaded / saved / reloaded.
  // The UI decides: reloadFromDisk(doc) or keep the buffer (the notification
  // repeats only on the next change on disk).
  using ExternalChangeHandler = std::function<void(Document&)>;
  void onExternalChange(ExternalChangeHandler h) { externalChange_ = std::move(h); }
  FileWatcher* fileWatcher() { return watcher_.get(); }  // null when watching is disabled

  // --- caret & navigation ---
  // The primary caret (carets_[0].head) with the current file.
  Location cursor() const { return cursor_; }
  Document* currentDocument() { return current_; }
  // Ordinary caret motion within the current document (arrow keys, mouse).
  // Collapses to a single caret at `p`.
  void moveCursor(Position p);
  // Jump somewhere (opens the file if needed) and record it in history.
  bool gotoLocation(const Location& loc, std::string* error = nullptr, const std::string& label = {});
  // Make `d` the current document with a single caret at `p` WITHOUT touching navigation history (a tab switch is
  // not a jump). Use gotoLocation for real navigation.
  void activateDocument(Document& d, Position p = {}) { setCurrent(&d, p); }
  bool back();
  bool forward();
  // Jump to entry `index` of navigation().entries() by walking the history cursor with back()/forward(), so the jump
  // itself adds no entries (leaving the live position records it once, exactly like back()). The current document
  // and caret change like after back()/forward(). False for an index out of range or when a step fails.
  bool goToHistoryEntry(size_t index);
  // Where the last edit ended (the primary caret after typing, deleting, undo/redo, replaceRange, applyEdits to the
  // current document). It follows line insertions / deletions in its file like history entries do, also for changes
  // that are not user edits (reloadFromDisk). Nullopt before the first edit.
  std::optional<Location> lastEditLocation() const {
    return lastEdit_.file.empty() ? std::nullopt : std::optional<Location>(lastEdit_);
  }

  // --- semantic commands (results arrive via post(), i.e. on the pump thread) ---
  // If the result is a single unambiguous location the editor jumps there and
  // then calls `done`. With several candidates it does not jump; the UI picks.
  using NavResult = std::function<void(Confidence, const std::vector<SymbolLocation>&, const std::string& error)>;
  void gotoDefinition(NavResult done);
  void gotoDeclaration(NavResult done);
  void findReferences(NavResult done);
  void findSymbols(const std::string& query, NavResult done);
  void hover(std::function<void(const std::string&, const std::string& error)> done);
  // Hover for any position of `doc` (mouse dwell), not only the caret. Without clangd the text is approximate.
  void hoverAt(Document& doc, Position pos, std::function<void(const std::string&, const std::string& error)> done);
  // Signature help at the primary caret (clangd only: `error` is set without it).
  void signatureHelp(std::function<void(const LspSignatureHelp&, const std::string& error)> done);
  void complete(std::function<void(const std::vector<LspCompletionItem>&, const std::string& error)> done);
  void switchSourceHeader(std::function<void(bool jumped, const std::string& error)> done);

  // --- clangd features (no approximate tier: results come from clangd or `error` is set; always via post()) ---
  template <class T>
  using Done = std::function<void(const T&, const std::string& error)>;
  // Semantic tokens of every open document arrive here after didOpen / didChange (debounced); stale versions
  // (the document moved on) are dropped. Registering the handler switches the automatic requests on.
  using TokenHandler = std::function<void(Document&, const std::vector<TokenSpan>&)>;
  void onSemanticTokens(TokenHandler h);
  void semanticTokens(Done<std::vector<TokenSpan>> done);  // current document, on demand
  void inlayHints(Range range, Done<std::vector<LspInlayHint>> done);
  void foldingRanges(Done<std::vector<LspFoldingRange>> done);
  void documentHighlights(Done<std::vector<LspHighlight>> done);  // at the primary caret
  // Hierarchies start at the primary caret; the items they return can be expanded further and jumped to with
  // gotoHierarchyItem (goes through gotoLocation, so it lands in navigation history).
  void callHierarchy(Done<std::vector<HierarchyItem>> done);
  void incomingCalls(const HierarchyItem& item, Done<std::vector<CallEdge>> done);
  void outgoingCalls(const HierarchyItem& item, Done<std::vector<CallEdge>> done);
  void typeHierarchy(Done<std::vector<HierarchyItem>> done);
  void supertypes(const HierarchyItem& item, Done<std::vector<HierarchyItem>> done);
  void subtypes(const HierarchyItem& item, Done<std::vector<HierarchyItem>> done);
  bool gotoHierarchyItem(const HierarchyItem& item, std::string* error = nullptr);
  // Code actions for `range` of the current document (fix-its use the diagnostics clangd published).
  void codeActions(Range range, Done<std::vector<CodeAction>> done);
  // Applies the action's edit, then runs its command (whose edits arrive through clangd's workspace/applyEdit and
  // are applied here too). `done(true, "")` once everything ran; edits form ONE undo step per document.
  void applyCodeAction(const CodeAction& action, std::function<void(bool ok, const std::string& error)> done);
  // Applies edits (ranges in byte columns) to documents, opening the ones that are not open yet (nothing is
  // written to disk). One undo group per document; carets of the current document follow the edits. All files are
  // opened before anything is changed, so failure to open one leaves every document untouched.
  bool applyEdits(const FileEdits& edits, std::string* error = nullptr);
  // clangd formatting of the whole current document / of a range; applied as one undo group.
  // `done` receives the number of edits applied.
  void formatDocument(Done<size_t> done);
  void formatRange(Range range, Done<size_t> done);

  // --- find in workspace (results arrive via post(), i.e. on the pump thread) ---
  // Searches Workspace::codeFiles() (+ other files if opts.includeOtherFiles) in the background. Open documents
  // are searched from their in-memory text (snapshotted now). `onDone` always fires, after the last batch;
  // after cancel() no further batches are delivered. The handle stays valid until shutdown.
  using SearchBatch = std::function<void(const std::vector<SearchMatch>&)>;
  using SearchDone = std::function<void(const SearchSummary&)>;
  SearchHandle findInWorkspace(const std::string& query, SearchOptions opts, SearchBatch onBatch, SearchDone onDone);
  // Jump to a result (goes through gotoLocation, so it lands in navigation history).
  bool gotoSearchMatch(const SearchMatch& m, std::string* error = nullptr);

  // --- selections & multiple carets ---
  // All carets / selections, primary first, the rest in document order. Never empty while a document is current.
  // Carets never overlap or touch: any command that makes two meet merges them.
  const std::vector<Selection>& selections() const { return carets_; }
  // Replace the caret set (mouse / Ctrl+click). Positions are clamped; carets[0] becomes the primary.
  void setSelections(std::vector<Selection> carets);
  // Selected text of every non-empty selection in document order, joined with '\n'.
  std::string selectedText() const;
  // Move every caret. `extend` keeps each anchor (grows the selection); otherwise selections collapse (Left/Right
  // with a selection and no extend jump to its edge). Up/Down/PageUp/PageDown keep a per-caret preferred column
  // (in code points). Word = run of identifier characters (CppLexer rules), punctuation run, or line break.
  void moveCarets(Direction dir, Unit unit = Unit::Char, bool extend = false);
  void addCaretAbove();  // adds a caret above the top-most caret (same preferred column), no-op on line 0
  void addCaretBelow();  // adds a caret below the bottom-most caret
  // If a caret is empty select the word under the primary caret; otherwise add the next occurrence (case-sensitive
  // text of the primary selection, after it, wrapping) that does not overlap a selection. The new one is primary.
  void addCaretAtNextOccurrence();
  void collapseToPrimary();
  // Rectangular selection: one selection per line between a.line and b.line, from a's visual column to b's visual
  // column (code points), clamped to each line's length. The primary is on b's line.
  void columnSelect(Position a, Position b);
  void setPageLines(int n) { pageLines_ = n > 0 ? n : 1; }

  // --- editing at the caret(s) ---
  // Every command edits at every caret and forms ONE undo step; undo()/redo() restore the caret set.
  // insertText replaces selections. A single typed character (one code point) is coalesced with the preceding
  // typing into one step according to Workspace::Settings::undoGrouping / undoGroupPauseMs.
  void insertText(const std::string& text);
  // One text per caret, in document order (texts.size() must equal selections().size(), else nothing happens): paste
  // of N lines into N carets. One undo step.
  void insertTextPerCaret(const std::vector<std::string>& texts);
  void backspace();
  void del();
  void deleteRange(Range r);
  void replaceRange(Range r, const std::string& text);
  bool undo();
  bool redo();
  // --- editing conveniences (B8) ---
  // Every command below edits at every caret and forms ONE undo step. The behaviours that could differ from 10x
  // (OPEN_QUESTIONS #8) are switched by EditingOptions; the UI fills them from the editor.* settings.
  struct EditingOptions {
    bool autoIndent = true;               // Enter copies / extends the indentation; '}' dedents
    bool autoClosePairs = true;           // ( [ { " ' close themselves, skip-over, wrap selections, pair backspace
    bool detectIndent = true;             // take the indent unit from the document's first lines
    std::string indentUnit = "    ";      // "\t" or N spaces; the fallback when detection finds nothing
    bool trimTrailingWhitespace = false;  // on save()
  };
  EditingOptions& editing() { return editing_; }
  const EditingOptions& editing() const { return editing_; }
  // The indent unit used for `doc`: detected from its first lines (editing().detectIndent) or the setting.
  std::string indentUnitFor(const Document& doc) const;
  // Typed text (the UI's text input): auto-close / skip-over / wrap-selection for ( [ { " ', dedent for '}' as the
  // first non-blank character; everything else is insertText. Pasted text must use insertText.
  void typeText(const std::string& text);
  // Enter: new line with the previous line's indentation (+1 level after '{', '(', '[' or an operator; `{|}` opens
  // a block with the closer on its own line). Plain "\n" when autoIndent is off.
  void insertNewline();
  void insertTab();           // Tab: a selection over several lines -> indentLines(), else the indent unit
  void indentLines();         // one indent unit on every line touched by a caret
  void outdentLines();        // removes one unit (or less) from the same lines
  void toggleLineComment();   // "//" at the lines' minimum indentation; uncomments when every non-blank line has it
  void toggleBlockComment();  // /* */ around each selection (a caret: around its line's text); unwraps when wrapped
  void duplicate();           // selection -> copy right after it (selected); caret -> the line below
  void moveLines(bool down);  // lines touched by carets swap with the neighbour line
  void joinLines();           // line + next line (selection: all its lines), leading blanks of the next -> one space
  void deleteLines();         // lines touched by carets, line break included
  bool gotoLine(int line, int column = 1, std::string* error = nullptr);  // 1-based, through gotoLocation
  // Line ending of `doc` (LF / CRLF): marks the document dirty; not an undo step.
  void setLineEnding(Document& doc, LineEnding e);
  // Find / replace in the current document. Matches come from TextSearch's matcher (regex needs PCRE2; matches are
  // clipped to their first line). `error` carries "invalid regex: ..." and the like.
  std::vector<Range> findInDocument(const SearchOptions& opts, std::string* error = nullptr);
  // Replaces every match (one undo step, carets follow); returns the count. Regex group references are not expanded.
  size_t replaceAll(const SearchOptions& opts, const std::string& replacement, std::string* error = nullptr);
  // Bookmarks (line based, follow edits, persisted in .tenfold/bookmarks.json when they change and on save()).
  Bookmarks& bookmarks() { return bookmarks_; }
  bool toggleBookmark();                            // at the primary caret's line; true when now set
  bool gotoBookmark(bool forward);                  // next / previous across files (wraps), via gotoLocation
  bool gotoBookmarkItem(const Bookmarks::Item& b);  // via gotoLocation, column 0
  void saveBookmarks();                             // no-op unless something changed

  // Injectable time source (milliseconds, monotonic) for the typing-pause boundary.
  void setClock(std::function<int64_t()> clock) { opt_.clock = std::move(clock); }

  // --- main-thread queue ---
  void post(std::function<void()> fn);
  size_t pump();                                            // run queued callbacks; returns count
  bool waitFor(std::function<bool()> pred, int timeoutMs);  // pump until pred() or timeout

  const std::vector<Diagnostic>& diagnostics(const std::filesystem::path& file) const;
  // Replaces the diagnostics of `file` (what clangd's publishDiagnostics does, on the calling thread): lets front ends
  // and tests show diagnostics without a language server.
  void setDiagnostics(const std::filesystem::path& file, std::vector<Diagnostic> ds);

  // --- build (C1) ---
  // Runs the workspace build (see detectBuildCommand: settings / overrides, else msbuild / cmake --build / make) in
  // the background; a running build is cancelled and replaced. Output lines and the final result arrive via post()
  // (the pump thread). Messages found in the output are available through buildMessages() as they stream in; they
  // and buildDiagnostics() are cleared when a new build starts. False (with `error`) if no command applies or the
  // process cannot start; nothing is cleared then.
  using BuildLines = std::function<void(const std::vector<std::string>&)>;
  using BuildDone = std::function<void(const BuildResult&)>;
  bool build(const BuildOverrides& overrides = {}, BuildLines onLines = {}, BuildDone onDone = {},
             std::string* error = nullptr);
  void cancelBuild();  // non-blocking; the done callback fires with cancelled = true
  bool buildRunning() const { return buildRunning_; }
  const BuildResult& lastBuildResult() const { return lastBuild_; }
  const std::vector<BuildMessage>& buildMessages() const { return buildMessages_; }
  // Jump (through gotoLocation, so Back returns) to the next / previous error or warning that has a source
  // location, wrapping around; notes and location-less messages (LINK, make) are skipped. The first call after a
  // build starts at the first (next) / last (prev) message. Nullopt when there is nothing to jump to.
  std::optional<BuildMessage> nextBuildMessage(std::string* error = nullptr) { return stepBuildMessage(1, error); }
  std::optional<BuildMessage> prevBuildMessage(std::string* error = nullptr) { return stepBuildMessage(-1, error); }
  // Moves the F4 / Shift+F4 cursor to `messageIndex` (an index into buildMessages(), e.g. the build-panel line that
  // was clicked) so the next nextBuildMessage() continues after it. Out-of-range indexes are ignored.
  void setBuildCursor(size_t messageIndex);
  // Build messages of one file as diagnostics (severity 1 error, 2 warning, 3 note), kept apart from the clangd
  // ones so a UI can colour them differently. A message without a column covers its whole line
  // (end = start of the next line).
  const std::vector<Diagnostic>& buildDiagnostics(const std::filesystem::path& file) const;

 private:
  struct CaretEdit {
    Range range;
    std::string text;
    // Where the caret ends up, as byte offsets into `text` (npos = its end); anchorOffset npos = same as the head.
    size_t headOffset = std::string::npos, anchorOffset = std::string::npos;
  };
  void setCurrent(Document* d, Position p);
  void setPrimaryOnly(Position p);
  void normalizeCarets(size_t primaryIndex = 0);  // sort, merge overlapping / touching, primary first, sync cursor_
  void clampCarets();
  void restoreCaretsAfterHistory();
  void commitMove(const Location& from);
  void noteEdit() {
    if (current_) lastEdit_ = cursor_;
  }
  void applyCaretEdits(std::vector<CaretEdit> edits, bool joinPrevious);
  void typeEdits(std::vector<CaretEdit> edits, const std::string& typed);  // insertText's undo coalescing
  // Batch edits (sorted, disjoint) as one undo step. `carets` maps the batch result to the new caret set and sets
  // the index of the primary in it; without it every caret is carried through the edits.
  using CaretFn = std::function<std::vector<Selection>(const std::vector<Range>& after, size_t& primary)>;
  void runLineEdits(std::vector<Document::BatchEdit> edits, const CaretFn& carets = {});
  std::vector<std::pair<int, int>> selectedLineBlocks() const;  // merged inclusive line ranges touched by carets
  int64_t nowMs() const;
  void breakTyping() { typingOpen_ = false; }
  void startWatching();
  void handleFileEvent(const FileWatcher::Event& ev);  // pump thread
  void checkExternalChange(Document& doc);
  void rescanWorkspace();
  void refreshBuildSystem();
  std::optional<BuildMessage> stepBuildMessage(int dir, std::string* error);
  void addBuildMessage(BuildMessage m);
  void autoJump(Confidence c, std::vector<SymbolLocation> locs, const std::string& err, NavResult done);
  // Wraps `done` so it runs on the pump thread.
  template <class T>
  SemanticEngine::Callback<T> posted(Done<T> done) {
    return [this, done = std::move(done)](T v, const std::string& err) {
      post([done, v = std::move(v), err] {
        if (done) done(v, err);
      });
    };
  }
  void applyFormatting(Done<size_t> done, const TextEditList& edits, const std::string& err,
                       const std::filesystem::path& file, int version);
  void hookTokens();
  TokenHandler tokenHandler_;

  Workspace ws_;
  Options opt_;
  std::unique_ptr<SemanticEngine> semantic_;
  std::string clangdStatus_;
  NavigationHistory nav_;
  std::map<std::string, std::unique_ptr<Document>> docs_;  // key: generic path
  Document* current_ = nullptr;
  Location lastEdit_;              // see lastEditLocation()
  Location cursor_;                // file + carets_[0].head
  std::vector<Selection> carets_;  // primary first, then document order; empty without a current document
  std::vector<int> wants_;         // per caret preferred column (code points), -1 = unset
  int pageLines_ = 30;
  bool inEdit_ = false;  // a caret command is editing: the document listener must not touch carets
  // Typing coalescer: the open typing step can be extended while nothing else happened.
  bool typingOpen_ = false;
  Document* typingDoc_ = nullptr;
  int typingVersion_ = 0;
  int64_t typingTime_ = 0;
  std::vector<Selection> typingCarets_;
  EditingOptions editing_;
  Bookmarks bookmarks_;
  bool bookmarksLoaded_ = false;
  std::map<std::string, std::vector<Diagnostic>> diagnostics_;
  std::vector<SearchHandle> searches_;

  struct BuildSession;
  std::unique_ptr<BuildRunner> buildRunner_;
  std::shared_ptr<BuildSession> buildSession_;  // the build whose output we accept
  bool buildRunning_ = false;
  BuildResult lastBuild_;
  std::vector<BuildMessage> buildMessages_;
  std::map<std::string, std::vector<Diagnostic>> buildDiags_;  // key: docKey
  int buildCursor_ = -1;                                       // index into buildMessages_ of the last jump

  std::unique_ptr<FileWatcher> watcher_;
  bool buildRefreshRunning_ = false, buildRefreshAgain_ = false;
  ExternalChangeHandler externalChange_;
  std::map<std::string, size_t> diskHash_;  // key: generic path; hash of the bytes last loaded / saved
  std::mutex qmu_;
  std::deque<std::function<void()>> queue_;
  std::condition_variable qcv_;
};

}  // namespace tf
