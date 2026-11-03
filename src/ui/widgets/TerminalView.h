#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "term/TermKeys.h"
#include "ui/features/TerminalSession.h"
#include "ui/widgets/Widget.h"

namespace tf::ui {

// The font a terminal view draws with when `terminal.font` / `terminal.fontSize` differ from the editor's: one
// renderer shared by every terminal view (one glyph atlas / texture), owned by the TerminalPanel. A replaced renderer
// is parked in `retired` until a view paints with a live Renderer and can release its texture.
struct TerminalFont {
  std::unique_ptr<gfx::TextRenderer> tr;  // null: use the root's (editor) text renderer
  std::vector<std::unique_ptr<gfx::TextRenderer>> retired;
};

// A mouse report for a program that asked for mouse tracking (DECSET 1000/1002/1003; 1006 = SGR encoding).
struct TermMouse {
  enum Kind { Press, Release, Motion, WheelUp, WheelDown } kind = Press;
  int button = 0;        // 0 left, 1 middle, 2 right (Motion without a button: 3)
  int col = 0, row = 0;  // 0-based cell
  bool shift = false, alt = false, ctrl = false;
};
std::string encodeTermMouse(const TermMouse& m, bool sgr);

// The terminal view (W8f): draws a TerminalSession's VtScreen as a cell grid in the Visual Studio terminal style and
// turns keys, text, paste, mouse and focus into bytes for the program. One view lives in one tab of an editor panel;
// destroying the view destroys the session (kills the process tree).
//
//  * Rendering: backgrounds as merged rects, text as runs of equal colour at exact cell positions (a non-ASCII cell is
//    its own run, so a fallback font's advance cannot shift the grid), 16 ANSI colours from the Theme (terminalAnsiN),
//    the 256 colour cube and grays computed, truecolour as is. Bold = bright for ANSI 0-7 plus a one pixel double
//    strike (there is no bold face), dim = 60% towards the background, italic is drawn upright (no italic face),
//    underline / strike are rects, inverse swaps. The cursor follows DECSCUSR: block (inverse cell), underline, bar,
//    hollow when unfocused, blinking for the blinking shapes.
//  * Scrollback: wheel (`wheelLines` per notch), Shift+PageUp / PageDown, a scrollbar on the right; any key or text
//  typed
//    snaps to the bottom; while scrolled back the view keeps its place when new lines arrive.
//  * Selection: mouse drag, double click = word, triple click = line, Shift+click extends. Ctrl+C copies when there is
//    a selection (and clears it), otherwise ^C goes to the program; Ctrl+Shift+C / Ctrl+Insert only copy. Ctrl+V,
//    Ctrl+Shift+V and Shift+Insert paste (bracketed when the program asked), as does a right click without
//    selection (a right click with a selection copies it, like Windows Terminal).
//  * A program that enabled mouse tracking gets the mouse (Shift bypasses it for selecting); in the alternate screen
//    without tracking the wheel sends Up / Down keys (xterm "alternate scroll").
class TerminalView : public Widget {
 public:
  TerminalView();
  ~TerminalView() override;

  void setSession(std::unique_ptr<TerminalSession> s);
  TerminalSession* session() const { return session_.get(); }
  void setFont(std::shared_ptr<TerminalFont> f);
  int wheelLines = 3;

  // Rows x columns a rectangle of this size holds with the current font (at least 1 x 2). Needs a root.
  std::pair<int, int> gridFor(const RectF& area) const;

  // ---- scrollback ----
  int scrollOffset() const { return offset_; }  // lines above the live screen (0 = at the bottom)
  void scrollBy(int lines);                     // + scrolls up into the history
  void scrollToBottom();

  // ---- selection / clipboard ----
  bool hasSelection() const { return selActive_; }
  std::string selectedText() const;
  void clearSelection();
  void selectAll();                // the visible screen and the scrollback
  bool copySelection(bool clear);  // false when nothing is selected
  void pasteText(const std::string& text);
  void paste();  // from the clipboard

  // The window gained / lost the focus (the widget focus is reported through onFocusChanged).
  void setWindowFocused(bool on);
  void syncFocusReport();  // tells the program (DECSET 1004) when the focus changed since it was last told

  // Text of screen row `r` (visible, honouring the scroll offset): tests.
  std::string visibleRowText(int r) const;
  // Window rectangle of a visible cell (hit testing in tests, positioning popups).
  RectF cellRect(int row, int col) const;

  bool acceptsFocus() const override { return true; }
  bool onEvent(const InputEvent& e) override;
  void onFocusChanged(bool gained) override;

 protected:
  void paint(PaintContext& ctx) override;
  void paintOverlay(PaintContext& ctx) override;
  void onLayout() override;
  int onTick(int64_t nowMs) override;

 private:
  struct SelPos {
    long long row = 0;  // absolute: 0 = oldest scrollback line, scrollbackSize() + r = screen row r
    int col = 0;
    bool operator<(const SelPos& o) const { return row != o.row ? row < o.row : col < o.col; }
    bool operator==(const SelPos& o) const { return row == o.row && col == o.col; }
  };
  struct Metrics {
    float cw = 8, ch = 16, ox = 0, oy = 0;
    float sbW = 0;  // scrollbar column
  };
  const gfx::TextRenderer* renderer() const;
  Metrics metrics() const;
  const VtRow& absRow(long long abs) const;
  SelPos cellAt(float x, float y, bool clampToView) const;
  bool onKey(const InputEvent& e);
  void sendKey(const TermKeyEvent& k);
  void sendBytes(std::string s);
  void sendMouse(const InputEvent& e, TermMouse::Kind kind);
  bool mouseDown(const InputEvent& e);
  bool mouseMove(const InputEvent& e);
  bool mouseUp(const InputEvent& e);
  std::pair<SelPos, SelPos> wordRange(const SelPos& p) const;
  std::pair<SelPos, SelPos> lineRange(long long row) const;
  RectF scrollbarThumb() const;
  void noteOutput();
  void resetBlink();

  std::unique_ptr<TerminalSession> session_;
  std::shared_ptr<TerminalFont> font_;
  int offset_ = 0;
  float wheelFrac_ = 0;
  bool selActive_ = false, selecting_ = false, reporting_ = false, draggingBar_ = false;
  SelPos selAnchor_, selHead_, selOrigA_, selOrigB_;
  int selClicks_ = 0;
  int reportButton_ = 0;
  float barGrab_ = 0;
  bool windowFocused_ = true, reportedFocus_ = false, blinkOn_ = true;
  int64_t blinkAtMs_ = 0;
  size_t lastScrollback_ = 0;
  std::string runBuf_;
};

}  // namespace tf::ui
