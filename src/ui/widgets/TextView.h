#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "app/Editor.h"
#include "ui/view/Layout.h"
#include "ui/view/Viewport.h"
#include "ui/widgets/Widget.h"

namespace tf::ui {

// The editor view (B2a): shows one Document of an Editor with a line-number gutter, current-line highlight, every
// selection (behind the text) and every caret (overlay layer, on top of the text), scrolls vertically and horizontally
// (smoothly by default) and turns mouse / keyboard input into Editor commands. The view never changes carets or text
// itself: clicks go through Editor::setSelections / columnSelect, text through insertText, so
// undo groups, navigation history and LSP sync stay the Editor's business. Scroll state is the view's own.
//
// Binding: TextView(editor) follows Editor::currentDocument(); TextView(editor, doc) is pinned to `doc` (tabs). Only
// the Editor's current document has carets; a pinned view of another document shows none until activate() (called by
// a click or by the owner when its tab is selected) makes it current via Editor::activateDocument, which records no
// navigation history. Keys are not handled here: EditorApp maps them to commands (KeyMap -> CommandRegistry); the view
// handles text input (InputType::Text -> Editor::insertText), the mouse and the wheel.
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
    std::function<int64_t()> clock;  // monotonic ms for blink; null = steady_clock
  };

  explicit TextView(Editor& editor, Document* pinned = nullptr) : editor_(editor), pinned_(pinned) {}

  Settings& settings() { return settings_; }
  Editor& editor() { return editor_; }
  Document* document() const { return pinned_ ? pinned_ : editor_.currentDocument(); }
  bool isCurrent() const { return document() && document() == editor_.currentDocument(); }
  Viewport& viewport() { return vp_; }
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
