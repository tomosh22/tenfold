#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>

namespace tf {

// Terminal screen model (UI-agnostic): fed with the bytes a program writes to its terminal, it keeps the grid of
// cells, the cursor, the scrollback and the modes a renderer and a key encoder need. See docs/ARCHITECTURE.md
// ("Terminal engine"). Not thread-safe: the owner calls everything from one thread (the one the Pty callback posts to).

// A colour is one 32-bit code: 0 = the default foreground/background, 0x01000000 | n = palette entry n (0-255; 0-7
// normal, 8-15 bright), 0x02000000 | 0xRRGGBB = truecolour.
using VtColor = uint32_t;
constexpr VtColor kVtDefaultColor = 0;
constexpr VtColor vtIndexed(unsigned n) { return 0x01000000u | (n & 0xFFu); }
constexpr VtColor vtRgb(unsigned r, unsigned g, unsigned b) {
  return 0x02000000u | ((r & 0xFFu) << 16) | ((g & 0xFFu) << 8) | (b & 0xFFu);
}
constexpr bool vtIsDefault(VtColor c) { return c == 0; }
constexpr bool vtIsIndexed(VtColor c) { return (c >> 24) == 1; }
constexpr bool vtIsRgb(VtColor c) { return (c >> 24) == 2; }
constexpr unsigned vtIndex(VtColor c) { return c & 0xFFu; }
constexpr unsigned vtRed(VtColor c) { return (c >> 16) & 0xFFu; }
constexpr unsigned vtGreen(VtColor c) { return (c >> 8) & 0xFFu; }
constexpr unsigned vtBlue(VtColor c) { return c & 0xFFu; }

enum VtAttr : uint8_t {
  kVtBold = 1,
  kVtItalic = 2,
  kVtUnderline = 4,
  kVtInverse = 8,
  kVtDim = 16,
  kVtStrike = 32,
  kVtBlink = 64,
  kVtInvisible = 128,
};

struct VtCell {
  char32_t cp = U' ';  // U' ' for an empty cell; 0 for the right half of a wide character
  VtColor fg = kVtDefaultColor;
  VtColor bg = kVtDefaultColor;
  uint8_t attrs = 0;  // VtAttr bits
  uint8_t width = 1;  // 1, 2 (left half of a wide character) or 0 (its right half: draw nothing)
};

struct VtRow {
  std::vector<VtCell> cells;
  bool wrapped = false;  // the line continues on the next row (soft wrap): lets a copy join the pieces
};

// Appends cp as UTF-8 (invalid code points become U+FFFD).
void vtAppendUtf8(std::string& out, char32_t cp);

enum class VtCursorShape : uint8_t { Default, BlinkBlock, Block, BlinkUnderline, Underline, BlinkBar, Bar };

class VtScreen {
 public:
  VtScreen(int rows, int cols, size_t scrollbackLines = 10000);

  // Feeds output bytes (any chunking: sequences and UTF-8 characters may be split across calls).
  void feed(const char* data, size_t n);
  void feed(const std::string& s) { feed(s.data(), s.size()); }

  // Replies the terminal sends back to the program (DSR cursor report, device attributes). Called synchronously from
  // feed(); the owner writes the bytes to the Pty.
  void setReplyCallback(std::function<void(const std::string&)> cb) { reply_ = std::move(cb); }

  // Resize without reflow: rows/columns are truncated or padded with blanks (the cursor is clamped, and on the main
  // screen shrinking the height first scrolls the top lines into the scrollback so the cursor stays visible). The
  // scroll region is reset. Scrollback lines keep the width they had.
  void resize(int rows, int cols);

  int rows() const { return rows_; }
  int cols() const { return cols_; }
  // Visible cell (0-based); row and col must be in range.
  const VtCell& cell(int row, int col) const { return grid_[static_cast<size_t>(row)].cells[static_cast<size_t>(col)]; }
  const VtRow& row(int r) const { return grid_[static_cast<size_t>(r)]; }
  // Text of a visible row as UTF-8, trailing blanks trimmed.
  std::string rowText(int r) const;

  int cursorRow() const { return curRow_; }
  int cursorCol() const { return curCol_; }
  bool cursorVisible() const { return cursorVisible_; }
  VtCursorShape cursorShape() const { return cursorShape_; }

  // Scrollback (main screen only): 0 = oldest line. Lines may be shorter or longer than cols() after a resize.
  size_t scrollbackSize() const { return scrollback_.size(); }
  const VtRow& scrollbackRow(size_t i) const { return scrollback_[i]; }
  size_t scrollbackLimit() const { return scrollbackLimit_; }
  void setScrollbackLimit(size_t lines);
  void clearScrollback();

  bool altScreen() const { return alt_; }
  bool bracketedPaste() const { return bracketedPaste_; }    // DECSET 2004
  bool applicationCursorKeys() const { return appCursor_; }  // DECSET 1 (arrows send ESC O x)
  bool applicationKeypad() const { return appKeypad_; }      // ESC = / ESC >
  bool wrapMode() const { return autoWrap_; }                // DECSET 7
  bool focusReporting() const { return focusReporting_; }    // DECSET 1004
  int mouseTracking() const { return mouseTracking_; }       // 0 off, else 1000 / 1002 / 1003
  bool mouseSgr() const { return mouseSgr_; }                // DECSET 1006
  const std::string& title() const { return title_; }        // OSC 0 / 2 (UTF-8)
  int scrollTop() const { return top_; }                     // scroll region, 0-based inclusive
  int scrollBottom() const { return bottom_; }

  // Rendering support. generation() increases whenever anything visible changed (cells, cursor, title, mode, size);
  // rowDirty(r) is set for each visible row touched since the last clearDirty(). A renderer repaints the dirty rows
  // (or everything when allDirty()) and calls clearDirty(). Scrolling marks every row of the region dirty.
  uint64_t generation() const { return generation_; }
  bool rowDirty(int r) const { return allDirty_ || dirty_[static_cast<size_t>(r)] != 0; }
  bool allDirty() const { return allDirty_; }
  bool anyDirty() const;
  void clearDirty();

 private:
  enum class St : uint8_t { Ground, Escape, EscInter, CsiParam, CsiInter, CsiIgnore, Osc, OscEsc, StrIgnore, StrEsc };

  struct Saved {
    int row = 0, col = 0;
    VtColor fg = 0, bg = 0;
    uint8_t attrs = 0;
    bool wrapPending = false, originMode = false;
    uint8_t g0 = 0, g1 = 0, gl = 0;
  };

  // parser
  void handleByte(unsigned char b);
  void groundByte(unsigned char b);
  void c0(unsigned char b);
  void escDispatch(unsigned char f);
  void csiDispatch(unsigned char f);
  void oscDispatch();
  void sgr();
  void setMode(bool priv, int mode, bool on);
  void startSequence(St s);
  int param(size_t i, int def) const;
  bool newParam(bool colon);

  // grid operations
  void put(char32_t cp);
  void putCell(char32_t cp, int w);
  void lineFeed();
  void reverseIndex();
  void carriageReturn();
  void scrollUp(int n);
  void scrollDown(int n);
  void moveTo(int row, int col);
  void eraseCells(int row, int c0, int c1);  // [c0, c1)
  void eraseRows(int r0, int r1);            // [r0, r1)
  void insertChars(int n);
  void deleteChars(int n);
  void insertLines(int n);
  void deleteLines(int n);
  void tab(int n);
  void backTab(int n);
  VtCell blank() const;
  VtRow blankRow() const;
  void fixWide(VtRow& r, int col);
  void touch(int r) {
    dirty_[static_cast<size_t>(r)] = 1;
    changed_ = true;
  }
  void touchRegion(int r0, int r1);
  void setAlt(bool on, bool clearOnEnter, bool saveCursor);
  void fullReset();
  void softReset();
  void saveCursor(Saved& s) const;
  void restoreCursor(const Saved& s);
  void reply(const std::string& s) {
    if (reply_) reply_(s);
  }
  void resizeGrid(std::vector<VtRow>& g, int rows, int cols);
  void defaultTabs();

  int rows_, cols_;
  std::vector<VtRow> grid_, altGrid_;
  std::deque<VtRow> scrollback_;
  size_t scrollbackLimit_;
  bool alt_ = false;

  int curRow_ = 0, curCol_ = 0;
  bool wrapPending_ = false;
  bool cursorVisible_ = true;
  VtCursorShape cursorShape_ = VtCursorShape::Default;
  VtColor fg_ = 0, bg_ = 0;
  uint8_t attrs_ = 0;
  int top_ = 0, bottom_ = 0;
  bool autoWrap_ = true, originMode_ = false, insertMode_ = false;
  bool bracketedPaste_ = false, appCursor_ = false, appKeypad_ = false, focusReporting_ = false, mouseSgr_ = false;
  int mouseTracking_ = 0;
  std::vector<uint8_t> tabs_;
  Saved saved_, savedAlt_;
  uint8_t g0_ = 0, g1_ = 0, gl_ = 0;  // 0 = ASCII, 1 = DEC special graphics
  char32_t lastChar_ = 0;
  std::string title_;

  // parser state
  St st_ = St::Ground;
  static constexpr size_t kMaxParams = 32;
  int params_[kMaxParams] = {};
  uint8_t colon_[kMaxParams] = {};  // 1 when the parameter was introduced by ':' (sub-parameter)
  size_t nParams_ = 0;
  bool paramSeen_ = false, curOk_ = false, sepLast_ = false, pendColon_ = false;
  char priv_ = 0;
  char inter_ = 0;
  std::string osc_;
  char32_t utf8Cp_ = 0;
  int utf8Need_ = 0;

  std::function<void(const std::string&)> reply_;

  std::vector<uint8_t> dirty_;
  bool allDirty_ = true;
  bool changed_ = false;
  uint64_t generation_ = 1;
};

}  // namespace tf
