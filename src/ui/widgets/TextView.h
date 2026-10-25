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
#include "ui/widgets/Widget.h"

namespace tf::ui {

// The editor view (B2a): shows one Document of an Editor with a line-number gutter, current-line highlight, every
// selection (behind the text) and every caret (overlay layer, on top of the text), scrolls vertically and horizontally
// (smoothly by default) and turns mouse / keyboard input into Editor commands. The view never changes carets or text
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
// Dirtiness: input handling and onTick() compare the Editor state (document, version, selections) with what was last
// drawn, so changes made elsewhere (LSP edits, Back/Forward, another view) repaint too. Drive onTick through
// RootWidget::tick(nowMs) after each batch of events and wait for at most the returned ms (blink / scroll animation).
class TextView : public Widget {
 public:
  struct Settings {
    bool smoothScroll = true;  // OPEN_QUESTIONS #4 (verify with the user): default on
    bool wordWrap = false;     // OPEN_QUESTIONS #4: default off. Reserved: wrapping is not implemented yet
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
  Layout geometry() const;
  float scrollbarWidth() const;
  void bindListener(Document* d);
  void bindDocument();
  uint64_t diagnosticSignature() const;
  void updateDiagMarkers();
  void updateSearchLane();
  void paintWhitespace(PaintContext& ctx, const Layout& L, const std::string& line, float textX, float y,
                       const RectF& clip) const;
  void paintSquiggles(PaintContext& ctx, const Layout& L, float sx, float sy, int first, int last);
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
