#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "app/Editor.h"
#include "ui/settings/Settings.h"
#include "ui/view/Highlighter.h"
#include "ui/view/Layout.h"
#include "ui/view/Scrollbar.h"
#include "ui/view/Viewport.h"
#include "ui/view/WrapLayout.h"
#include "ui/widgets/Widget.h"

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
    int wheelLines = 3;              // lines per wheel notch
    int tabWidth = 4;                // editor.tabWidth (applied to the shared TextLayout)
    int columnGuide = 0;             // editor.columnGuide: column of the vertical guide, 0 = off
    bool showWhitespace = false;     // editor.showWhitespace: dots for spaces, arrows for tabs
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
  // The line <-> visual row map (bound to the current width), for tests and geometry users.
  WrapLayout& wrapLayout() const;
  // Moves every caret by visual row when wrap is on (Up / Down / PageUp / PageDown keep the x of the first move;
  // LineStart / LineEnd go to the row's edge first, then the line's). False when wrap is off or the direction is
  // not a row-wise one: the caller then uses Editor::moveCarets.
  bool moveCaretsVisual(Editor::Direction dir, Editor::Unit unit, bool extend);
  bool colouring() const;  // true for C/C++ files
  // The bracket at / before the primary caret and its partner (positions of the two bracket characters).
  std::optional<std::pair<Position, Position>> bracketMatch();
  // Makes this view's document the Editor's current one (no history entry; the caret this view last had) and takes
  // focus.
  void activate();

  // Geometry (absolute window pixels). positionAt clamps to the document; caretRect is the bar of a caret even when
  // it is scrolled out of view; both use scroll offsets of the last paint-independent state (the viewport now).
  Position positionAt(float x, float y) const;
  RectF caretRect(Position p) const;
  RectF textArea() const;  // the clip rect of text (right of the gutter)
  int visibleLineCount() const;
  void ensureCaretVisible(bool animate = true);
  std::string positionText() const;  // "Ln 3, Col 5" of the primary caret (Col is the visual column, tabs expanded)

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
  int wrapCols() const;
  void syncWrap();
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
  mutable std::vector<RowRef> vis_;  // the visible rows of the last collectVisibleRows
  std::vector<int> markLines_[7];    // scrollbar markers as document lines (the bar gets visual rows)
  uint32_t markGen_ = 0;
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
  uint64_t bookmarkGen_ = ~0ull;
  std::vector<int> bookmarkLines_;  // of the bound document, refreshed on paint

  // What was last seen, to detect changes.
  const Document* seenDoc_ = nullptr;
  int seenVersion_ = -1;
  std::vector<Selection> seenSel_;
  Position lastCaret_{0, 0};

  // Blink / animation.
  int64_t activityMs_ = 0, lastTick_ = -1;
  bool caretOn_ = true, pendingEnsure_ = false;

  // Mouse gesture.
  enum class Drag { None, Char, AddCaret, Column } drag_ = Drag::None;
  Position anchor_{0, 0}, anchorEnd_{0, 0}, lastDragPos_{-1, -1};
  std::vector<Selection> base_;
  std::string number_;
};

}  // namespace tf::ui
