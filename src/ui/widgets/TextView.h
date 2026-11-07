#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "app/Editor.h"
#include "ui/settings/Settings.h"
#include "ui/view/FoldModel.h"
#include "ui/view/Highlighter.h"
#include "ui/view/InlayMap.h"
#include "ui/view/Layout.h"
#include "ui/view/RowBlocks.h"
#include "ui/view/Scrollbar.h"
#include "ui/view/Viewport.h"
#include "ui/view/WrapLayout.h"
#include "ui/widgets/Widget.h"
#include "vcs/LineDiff.h"

namespace tf::ui {

// The editor view (B2a): shows one Document of an Editor with a line-number gutter, current-line highlight, every
// selection (behind the text) and every caret (overlay layer, on top of the text), scrolls vertically and horizontally
// (smoothly when asked to) and turns mouse / keyboard input into Editor commands. The view never changes carets or text
// itself: clicks go through Editor::setSelections / columnSelect, text through typeText (auto-close / auto-indent live
// in the Editor), so undo groups, navigation history and LSP sync stay the Editor's business. Scroll state is the
// view's own.
//
// Binding: TextView(editor) follows Editor::currentDocument(); TextView(editor, doc) is pinned to `doc` (tabs). Only
// the Editor's current document has carets; a pinned view of another document shows none until activate() (called by
// a click or by the owner when its tab is selected) makes it current via Editor::activateDocument, which records no
// navigation history. Keys are not handled here: EditorApp maps them to commands (KeyMap -> CommandRegistry); the view
// handles text input (InputType::Text -> Editor::typeText), the mouse and the wheel.
//
// Colouring (B2b): C/C++ files are coloured lexically at once (Highlighter, cached per line, visible lines only) and
// upgraded to clangd semantic tokens when setSemanticTokens delivers them (they override identifier colours only);
// other files are plain. Decorations: diagnostics squiggles (overlay layer, from Editor::diagnostics), a vertical
// scrollbar child with marker lanes (search matches via setSearchMarkers, carets, diagnostics), a column guide,
// visible whitespace and the matching bracket of the caret ((){}[]; angle brackets are skipped: the lexer cannot
// tell templates from comparisons).
//
// Soft word wrap (B2c, on by default): long lines wrap at the view width (WrapLayout owns the line <-> visual row
// map). Everything that was per line is per visual row: scrolling and the scrollbar, hit testing, selections,
// current-line highlight, squiggles, find marks and the caret. The gutter numbers a line on its first row; horizontal
// scrolling is off. Caret Up / Down / PageUp / PageDown and Home / End move by visual row (moveCaretsVisual, which
// EditorApp offers to the caret commands); everything else stays with the Editor.
//
// Glyph margin (C2c): when the Editor has a debugger backend or any breakpoint, the gutter grows a lane (one line
// height wide) at its left edge, left of the line numbers (Layout::setGlyphLane). It shows, on the first visual row of
// a line only, the breakpoint of that line (red disc; hollow ring = disabled; a white plus on the disc = conditional)
// and the yellow current-statement arrow (Break mode, in every view of that file; the line also gets a background
// band over all its rows). The arrow is drawn over a breakpoint disc. A bookmark's flag keeps its small square just
// right of the lane (at the gutter's left edge when there is no lane). A left click in the lane toggles a breakpoint
// on the clicked line through Breakpoints (the DebugSession pushes it to the backend); it moves neither caret nor
// focus. The view repaints when Breakpoints::generation() or DebugSession::generation() moves.
// Version control (C3c): when Editor::vcs() has a base for the document, a narrow change strip between the line
// numbers and the text shows the hunks (added green, modified blue, deleted a red wedge on the boundary between two
// rows; a wrapped line's mark spans all its rows) and the scrollbar gets change marks. Clicking the strip, or
// toggleInlineDiff(), opens an inline diff block under the hunk: header row with "Revert hunk" and a close button,
// then the base (old) lines on a red tint. The block is a view decoration: it reserves display rows (RowBlocks maps
// WrapLayout rows to display rows; WrapLayout and the Document never know about it), follows edits above it and
// goes away when its hunk changes.
//
// Code folding (W8f, 10x CollapseRegions): C/C++ files have regions (FoldModel: brace blocks, multi-line comments,
// #pragma region) and the view can collapse them. A collapsed region shows its head line followed by a {...} marker
// and hides the rest; hidden lines have no rows at all (WrapLayout::setHidden, so the row model that scrolling, the
// scrollbar, hit testing, selections and caret movement already go through simply skips them, with or without
// wrap). Regions are computed on a copy of the lines off the main thread once typing pauses (a result for an older
// version is dropped), the commands compute them on the spot when the set is stale. A caret that lands in a hidden
// line (a jump, a search hit, Left from below) expands the region; so does any edit touching its lines. A click on
// the marker expands; editor.showFoldGlyphs (default on, like Visual Studio's outlining margin) adds a gutter lane with
// a boxed +/- per region start and a guide line through the open region.
//
// Dirtiness: input handling and onTick() compare the Editor state (document, version, selections) with what was last
// drawn, so changes made elsewhere (LSP edits, Back/Forward, another view) repaint too. Drive onTick through
// RootWidget::tick(nowMs) after each batch of events and wait for at most the returned ms (blink / scroll animation).
class TextView : public Widget {
 public:
  struct Settings {
    bool smoothScroll = false;  // OPEN_QUESTIONS #4 (answered): off by default
    bool wordWrap = true;       // OPEN_QUESTIONS #4 (answered): soft wrap on by default
    bool caretBlink = true;
    int caretBlinkMs = 530;
    bool highlightCurrentLine = true;
    int wheelLines = 3;           // lines per wheel notch
    int tabWidth = 4;             // editor.tabWidth (applied to the shared TextLayout)
    int columnGuide = 0;          // editor.columnGuide: column of the vertical guide, 0 = off
    bool showWhitespace = false;  // editor.showWhitespace: dots for spaces, arrows for tabs
    bool showFoldGlyphs = true;   // editor.showFoldGlyphs: the outlining margin (boxed +/- and a guide per region)
    bool outlineEnabled = true;   // outline.enabled / outline.pragmaRegions / outline.statementBlocks (VS C++ options)
    bool outlinePragmaRegions = true;
    bool outlineStatementBlocks = true;
    std::function<int64_t()> clock;  // monotonic ms for blink; null = steady_clock
  };

  explicit TextView(Editor& editor, Document* pinned = nullptr);
  ~TextView() override;

  Settings& settings() { return settings_; }
  Editor& editor() { return editor_; }
  Document* document() const { return pinned_ ? pinned_ : editor_.currentDocument(); }
  bool isCurrent() const { return document() && document() == editor_.currentDocument(); }
  Viewport& viewport() { return vp_; }
  Scrollbar& scrollbar() { return *sb_; }
  Highlighter& highlighter() { return hl_; }
  // editor.* settings (EditorApp calls this on construction of a view and on every settings change).
  void applySettings(const EditorSettings& s);
  // Clangd semantic tokens for this view's document (A9: Editor::onSemanticTokens), with the server legend.
  void setSemanticTokens(std::vector<TokenSpan> spans, const LspSemanticTokensLegend& legend);
  // Search matches for the scrollbar's marker lane (B5b).
  void setSearchMarkers(const std::vector<Range>& matches);
  // Find / replace in file (B8): every match is highlighted in the visible lines (`current` is drawn stronger, -1 =
  // none) and fills the scrollbar's search lane instead of the find-in-files markers until cleared (empty list).
  void setFindMatches(std::vector<Range> matches, int current);
  const std::vector<Range>& findMatches() const { return findMatches_; }
  // Soft wrap on / off at run time (View.ToggleWordWrap); keeps the first visible line in place.
  void setWordWrap(bool on);
  bool wordWrapActive() const;  // wrap is on and the view has a document and a size
  // Visible whitespace on / off at run time (ToggleWhitespace); editor.showWhitespace sets the start-up state.
  void setShowWhitespace(bool on);
  // Scrolls by `lines` display rows (negative: up) without touching the carets, except those that would leave the
  // view: they move to the nearest fully visible row (same x). ScrollOneLineUp / ScrollOneLineDown.
  void scrollLines(int lines);
  // The line <-> visual row map (bound to the current width), for tests and geometry users.
  WrapLayout& wrapLayout() const;
  // Moves every caret by visual row when wrap is on (Up / Down / PageUp / PageDown keep the x of the first move;
  // LineStart / LineEnd go to the row's edge first, then the line's). False when wrap is off or the direction is
  // not a row-wise one: the caller then uses Editor::moveCarets.
  bool moveCaretsVisual(Editor::Direction dir, Editor::Unit unit, bool extend);
  bool colouring() const;  // true for C/C++ files
  // The bracket at / before the primary caret and its partner (positions of the two bracket characters).
  std::optional<std::pair<Position, Position>> bracketMatch();
  // ---- version control (C3c) ----
  // Where something of an open inline block sits (window pixels), for tests and hit testing.
  struct InlineBlockInfo {
    DiffHunk hunk;
    int anchorLine = 0;  // the block sits under this line (-1: above the first line)
    int rows = 0;        // display rows: header + base lines
    RectF rect{0, 0, 0, 0}, header{0, 0, 0, 0}, revertButton{0, 0, 0, 0}, closeButton{0, 0, 0, 0};
    std::vector<std::string> lines;  // the base lines shown
  };
  // Opens (or closes, when open) the inline block of the hunk at `line` (-1 = the primary caret's line). False when
  // there is no change there. The Document is untouched.
  bool toggleInlineDiff(int line = -1);
  bool closeInlineDiffs();  // true if any was open
  std::vector<InlineBlockInfo> inlineBlocks() const;
  // "Revert hunk" of block `i` (one undo step through VcsManager::revertHunk); the block goes with its hunk.
  bool revertInlineBlock(size_t i, std::string* error = nullptr);
  // Hunks of the document in the view's current text: the engine's when they belong to this version, else recomputed
  // (an edit is younger than the debounced diff). Empty without a base.
  std::vector<DiffHunk> currentHunks() const;
  // Index of the hunk a caret on `line` belongs to: the hunk containing it, else a deletion wedge at the line's top
  // edge, else one at its bottom edge; -1 for none.
  static int hunkIndexAt(const std::vector<DiffHunk>& hunks, int line);
  bool diffStripActive() const;  // version control has a base for this document
  RectF diffStripRect() const;   // the strip column (window pixels), full view height

  // ---- code folding (W8f) ----
  enum class FoldOp {
    Collapse,
    Expand,
    Toggle,
    CollapseAll,
    ExpandAll,
    CollapseAllRecursive,
    ExpandAllRecursive,
    ToggleAll,
    CollapseToDefinitions
  };
  // The 10x CollapseRegions commands on the primary caret's line. False when the file has no regions (not C/C++),
  // or nothing changed.
  bool foldCommand(FoldOp op);
  bool foldingActive() const { return colouring() && document() && settings_.outlineEnabled; }
  const FoldModel& folds() const { return fold_; }
  // Computes the regions on the calling thread now (waits for a running job); for the commands and for tests.
  void foldRefreshNow();
  // Collapses / expands the region starting on `line` (the gutter glyph and the marker click use these).
  bool toggleFoldAt(int line);
  void setShowFoldGlyphs(bool on);
  float foldLaneWidth() const;  // 0 unless editor.showFoldGlyphs and the file has folding
  RectF foldLaneRect() const;
  // Where the {...} marker of the collapsed region starting on `line` is drawn (window pixels); zero size when the
  // line is not a collapsed head or not on screen.
  RectF foldMarkerRect(int line) const;

  // Makes this view's document the Editor's current one (no history entry; the caret this view last had) and takes
  // focus.
  void activate();
  // AddCursorAtMousePos: adds a caret at the text position under a window point (makes this view's document current
  // first); the position is clamped like a click's.
  void addCaretAtPoint(float x, float y);

  // Geometry (absolute window pixels). positionAt clamps to the document; caretRect is the bar of a caret even when
  // it is scrolled out of view; both use scroll offsets of the last paint-independent state (the viewport now).
  Position positionAt(float x, float y) const;
  RectF caretRect(Position p) const;
  // The start of the identifier drawn under the point (window pixels), for commands that act on the symbol under the
  // mouse rather than at the caret. Nullopt over the gutter, the scrollbar, blank space, line ends,
  // numbers and punctuation. Pure geometry: it does not move the caret.
  std::optional<Position> identifierAt(float x, float y) const;
  RectF textArea() const;  // the clip rect of text (right of the gutter)
  int visibleLineCount() const;
  // Debugger glyph lane (C2c): present when the Editor has a debugger backend or any breakpoint; width 0 otherwise.
  bool glyphMarginActive() const;
  float glyphLaneWidth() const;
  RectF glyphLaneRect() const;  // window pixels; zero width when there is no lane
  void ensureCaretVisible(bool animate = true);
  std::string positionText() const;  // "Ln 3, Col 5" of the primary caret (Col is the visual column, tabs expanded)

  // ---- Quick Actions light bulb and inlay hints (W11-GUI-LSP) ----
  // The light bulb sits in the gutter, left of the line number of `line` (-1: none); a click on it calls onLightBulb.
  void setLightBulb(int line, bool error = false);  // `error`: the red variant (a fix for an error)
  int lightBulbLine() const { return bulbLine_; }
  bool lightBulbIsError() const { return bulbError_; }
  RectF lightBulbRect() const;  // window pixels; empty when there is no bulb or its line is off screen
  std::function<void()> onLightBulb;
  // Inlay hints (InlayMap): virtual text between characters. They are drawn only while `shown`; they move the text to
  // their right but never change a column: caretRect / positionAt / selections work in document columns.
  void setInlayHints(const std::vector<LspInlayHint>& hints);
  void setInlayHintsShown(bool on);
  bool inlayHintsShown() const { return hintsShown_; }
  const InlayMap& inlayMap() const { return hints_; }
  // First and last document line with a visible row (all lines before the first paint).
  std::pair<int, int> visibleLines() const;

  int settingsGen = 0;                   // EditorApp: the settings generation last applied to this view
  std::function<void()> onStateChanged;  // document, version or carets changed (status bar)

  bool acceptsFocus() const override { return true; }
  bool onEvent(const InputEvent& e) override;
  void onFocusChanged(bool gained) override;

 protected:
  void paint(PaintContext& ctx) override;
  void paintOverlay(PaintContext& ctx) override;
  int onTick(int64_t nowMs) override;
  void onLayout() override;

 private:
  // One visual row on screen: bytes [start, end) of `line`, drawn indent cells to the right of the text origin.
  struct RowRef {
    int line, row, vrow, start, end, indent;
  };
  Layout geometry() const;
  // ---- folding internals ----
  struct FoldJob;
  void foldOnChange(const Document::Change& c);
  void applyHidden(bool keepTop);
  void foldPoll(int64_t nowMs);
  void foldCancel();
  void foldMoveCarets();  // carets in hidden lines go to the end of their head line
  std::string foldMarkerText(const FoldModel::Region& r, int line, float* gap) const;
  void paintFoldGlyph(PaintContext& ctx, const Layout& L, const RowRef& r, float y);
  FoldModel::Options foldOptions() const;
  std::vector<FoldModel::Region> glyphRegions_;  // open regions touching the visible lines (set per paint)
  std::optional<int> statementLine() const;  // the line of the debugger's current statement when it is in this document
  void paintGlyphMargin(PaintContext& ctx, const Layout& L, const RowRef& r, float y, bool arrow);
  void toggleBreakpointAt(int line);
  int wrapCols() const;
  void syncWrap();
  // Pixels the shown inlay hints on row `row` of `line` push the caret position `col` to the right (0 without
  // hints). `end`: the position closes a range, so a hint that precedes the character at `col` is not counted.
  float hintShift(int line, int row, int col, bool end = false) const;
  bool rowHasHints(int line, int row) const;
  void paintLightBulb(PaintContext& ctx, const Layout& L, float y);
  float scrollX() const;
  float totalHeight() const;
  void collectVisibleRows(std::vector<RowRef>& out) const;
  std::pair<size_t, size_t> visibleRange(int line) const;  // indices into vis_ of the visible rows of `line`
  // Calls fn(const RowRef&, x0, x1) for each visible row of `line` that bytes [b0, b1] touch; x are pixels from the
  // text origin; withBreak adds the line break's cell after the last row.
  template <class Fn>
  void forSegments(int line, int b0, int b1, bool withBreak, Fn fn) const;
  void setMarkLines(Scrollbar::Lane lane, std::vector<int> lines);
  void pushMarkers();
  void drawRowText(PaintContext& ctx, const RowRef& r, float textX, float y, const RectF& clip,
                   const std::vector<gfx::ColorSpan>* spans);
  float scrollbarWidth() const;
  void bindListener(Document* d);
  // ---- version control internals ----
  struct Block {
    DiffHunk hunk;
    int anchor = 0;  // line the block sits under
    std::vector<std::string> lines;
    int rows() const { return 1 + static_cast<int>(lines.size()); }
  };
  const RowBlocks& rowMap() const;
  // Runs `f` (which edits blocks_) and keeps the first visible line where it was: blocks above it change its row.
  void mutateBlocks(const std::function<void()>& f);
  void refreshBlocks();
  void blocksOnChange(const Document::Change& c);
  int blockIndex(const DiffHunk& h) const;
  static int anchorOf(const DiffHunk& h) { return h.newCount > 0 ? h.newStart + h.newCount - 1 : h.newStart - 1; }
  InlineBlockInfo blockInfo(size_t i) const;
  bool vcsClick(const InputEvent& e);
  void paintDiffStrip(PaintContext& ctx, const Layout& L, float sy);
  void paintBlocks(PaintContext& ctx, const Layout& L, float sx);
  void updateDiffMarkers();
  void bindDocument();
  uint64_t diagnosticSignature() const;
  void updateDiagMarkers();
  void updateSearchLane();
  void paintWhitespace(PaintContext& ctx, const Layout& L, std::string_view line, float textX, float y,
                       const RectF& clip) const;
  void paintSquiggles(PaintContext& ctx, const Layout& L, float sx, float sy);
  int64_t now() const;
  bool syncState();
  float maxScrollY() const;
  void scrollBy(float dx, float dy);
  void mouseDown(const InputEvent& e);
  void mouseDrag(const InputEvent& e);

  Editor& editor_;
  Document* pinned_;
  Settings settings_;
  Viewport vp_;
  Highlighter hl_;
  mutable WrapLayout wrap_;
  FoldModel fold_;
  std::shared_ptr<FoldJob> foldJob_;
  std::thread foldThread_;
  int64_t foldEditMs_ = -1000000;
  mutable std::vector<RowRef> vis_;                // the visible rows of the last collectVisibleRows
  std::vector<int> markLines_[Scrollbar::kLanes];  // scrollbar markers as document lines (the bar gets display rows)
  uint32_t markGen_ = 0;
  // Inline diff blocks (C3c) and the display-row map derived from them.
  std::vector<Block> blocks_;
  uint32_t blocksGen_ = 1;
  mutable RowBlocks rowMap_;
  mutable uint64_t rowKey_[4] = {~0ull, 0, 0, 0};
  mutable uint32_t rowSerial_ = 0;
  uint32_t markSerial_ = ~0u;
  uint64_t vcsGen_ = ~0ull;
  const Document* vcsDoc_ = nullptr;
  int syncedCols_ = -1, anchorLine_ = -1;
  std::vector<Selection> prefSel_;  // carets as left by the last vertical move, with their x in cells
  std::vector<int> prefCol_;
  std::vector<gfx::ColorSpan> rowSpans_;
  Scrollbar* sb_ = nullptr;
  Document* listenDoc_ = nullptr;
  std::filesystem::path listenPath_;
  int listenId_ = 0;
  uint32_t themeGen_ = 0xFFFFFFFFu;
  uint64_t diagSig_ = 0;
  std::vector<Range> findMatches_;  // sorted
  int findCurrent_ = -1;
  std::vector<int> searchLines_;  // find-in-files markers, restored when the in-file matches go away
  uint64_t bookmarkGen_ = ~0ull, breakpointGen_ = ~0ull, debugGen_ = ~0ull;
  std::vector<int> bookmarkLines_;  // of the bound document, refreshed on paint
  InlayMap hints_;
  bool hintsShown_ = false;
  int bulbLine_ = -1;
  bool bulbError_ = false;

  // What was last seen, to detect changes.
  const Document* seenDoc_ = nullptr;
  int seenVersion_ = -1;
  std::vector<Selection> seenSel_;
  Position lastCaret_{0, 0};

  // Blink / animation.
  int64_t activityMs_ = 0, lastTick_ = -1;
  bool caretOn_ = true, pendingEnsure_ = false, skipEnsure_ = false;

  // Mouse gesture.
  enum class Drag { None, Char, AddCaret, Column } drag_ = Drag::None;
  Position anchor_{0, 0}, anchorEnd_{0, 0}, lastDragPos_{-1, -1};
  std::vector<Selection> base_;
  std::string number_;
};

}  // namespace tf::ui
