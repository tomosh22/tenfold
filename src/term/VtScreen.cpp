#include "term/VtScreen.h"

#include <algorithm>
#include <utility>

namespace tf {

namespace {

constexpr char32_t kReplacement = 0xFFFD;
constexpr size_t kMaxOsc = 4096;
constexpr int kMaxRep = 100000;

// Column width of a code point: 0 for combining / zero-width characters (dropped), 2 for East Asian wide and emoji
// blocks, else 1. A small range table, not the full Unicode tables.
int cpWidth(char32_t cp) {
  if (cp < 0x300) return (cp >= 0x80 && cp < 0xA0) ? 0 : 1;
  struct R {
    char32_t lo, hi;
  };
  static const R zero[] = {{0x300, 0x36F},   {0x483, 0x489},   {0x591, 0x5BD},    {0x1AB0, 0x1AFF}, {0x1DC0, 0x1DFF},
                           {0x200B, 0x200F}, {0x2028, 0x202E}, {0x2060, 0x2064},  {0x20D0, 0x20FF}, {0xFE00, 0xFE0F},
                           {0xFE20, 0xFE2F}, {0xFEFF, 0xFEFF}, {0xE0100, 0xE01EF}};
  static const R wide[] = {{0x1100, 0x115F},   {0x2E80, 0x303E},   {0x3041, 0x33FF},  {0x3400, 0x4DBF},
                           {0x4E00, 0x9FFF},   {0xA000, 0xA4CF},   {0xAC00, 0xD7A3},  {0xF900, 0xFAFF},
                           {0xFE30, 0xFE6F},   {0xFF00, 0xFF60},   {0xFFE0, 0xFFE6},  {0x1F300, 0x1F64F},
                           {0x1F680, 0x1F6FF}, {0x1F900, 0x1F9FF}, {0x20000, 0x3FFFD}};
  for (const R& r : zero)
    if (cp >= r.lo && cp <= r.hi) return 0;
  for (const R& r : wide)
    if (cp >= r.lo && cp <= r.hi) return 2;
  return 1;
}

// DEC special graphics (ESC ( 0): 0x5f..0x7e.
const char32_t kGraphics[32] = {0x00A0, 0x25C6, 0x2592, 0x2409, 0x240C, 0x240D, 0x240A, 0x00B0, 0x00B1, 0x2424, 0x240B,
                                0x2518, 0x2510, 0x250C, 0x2514, 0x253C, 0x23BA, 0x23BB, 0x2500, 0x23BC, 0x23BD, 0x251C,
                                0x2524, 0x2534, 0x252C, 0x2502, 0x2264, 0x2265, 0x03C0, 0x2260, 0x00A3, 0x00B7};

}  // namespace

void vtAppendUtf8(std::string& out, char32_t cp) {
  if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = kReplacement;
  if (cp < 0x80) {
    out += static_cast<char>(cp);
  } else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

VtScreen::VtScreen(int rows, int cols, size_t scrollbackLines)
    : rows_(std::max(1, rows)), cols_(std::max(1, cols)), scrollbackLimit_(scrollbackLines) {
  grid_.resize(static_cast<size_t>(rows_));
  altGrid_.resize(static_cast<size_t>(rows_));
  for (auto& r : grid_) r.cells.assign(static_cast<size_t>(cols_), VtCell{});
  for (auto& r : altGrid_) r.cells.assign(static_cast<size_t>(cols_), VtCell{});
  dirty_.assign(static_cast<size_t>(rows_), 1);
  top_ = 0;
  bottom_ = rows_ - 1;
  defaultTabs();
}

void VtScreen::defaultTabs() {
  tabs_.assign(static_cast<size_t>(cols_), 0);
  for (int c = 8; c < cols_; c += 8) tabs_[static_cast<size_t>(c)] = 1;
}

VtCell VtScreen::blank() const {
  VtCell c;
  c.bg = bg_;
  return c;
}

VtRow VtScreen::blankRow() const {
  VtRow r;
  r.cells.assign(static_cast<size_t>(cols_), blank());
  return r;
}

bool VtScreen::anyDirty() const {
  if (allDirty_) return true;
  return std::any_of(dirty_.begin(), dirty_.end(), [](uint8_t d) { return d != 0; });
}

void VtScreen::clearDirty() {
  std::fill(dirty_.begin(), dirty_.end(), uint8_t{0});
  allDirty_ = false;
}

void VtScreen::touchRegion(int r0, int r1) {
  for (int r = std::max(0, r0); r <= std::min(rows_ - 1, r1); ++r) dirty_[static_cast<size_t>(r)] = 1;
  changed_ = true;
}

std::string VtScreen::rowText(int r) const {
  std::string out;
  const VtRow& row = grid_[static_cast<size_t>(r)];
  for (const VtCell& c : row.cells)
    if (c.width != 0) vtAppendUtf8(out, c.cp ? c.cp : U' ');
  while (!out.empty() && out.back() == ' ') out.pop_back();
  return out;
}

void VtScreen::setScrollbackLimit(size_t lines) {
  scrollbackLimit_ = lines;
  while (scrollback_.size() > scrollbackLimit_) scrollback_.pop_front();
  generation_++;
}

void VtScreen::clearScrollback() {
  scrollback_.clear();
  generation_++;
}

// ---------------------------------------------------------------------------------------------------------------
// Feeding

void VtScreen::feed(const char* data, size_t n) {
  changed_ = false;
  for (size_t i = 0; i < n; ++i) {
    unsigned char b = static_cast<unsigned char>(data[i]);
    // Fast path: printable ASCII in the ground state, not at the right margin.
    if (st_ == St::Ground && utf8Need_ == 0 && b >= 0x20 && b < 0x7F && gl_ == 0 && g0_ == 0 && !wrapPending_ &&
        !insertMode_ && curCol_ < cols_ - 1) {
      VtRow& row = grid_[static_cast<size_t>(curRow_)];
      VtCell& c = row.cells[static_cast<size_t>(curCol_)];
      if (c.width != 1 || row.cells[static_cast<size_t>(curCol_) + 1].width == 0) {
        put(b);
        continue;
      }
      c.cp = b;
      c.fg = fg_;
      c.bg = bg_;
      c.attrs = attrs_;
      c.width = 1;
      ++curCol_;
      lastChar_ = b;
      dirty_[static_cast<size_t>(curRow_)] = 1;
      changed_ = true;
      continue;
    }
    handleByte(b);
  }
  if (changed_) generation_++;
}

void VtScreen::startSequence(St s) {
  st_ = s;
  nParams_ = 0;
  paramSeen_ = false;
  curOk_ = false;
  sepLast_ = false;
  pendColon_ = false;
  priv_ = 0;
  inter_ = 0;
}

bool VtScreen::newParam(bool colon) {
  if (nParams_ >= kMaxParams) return curOk_ = false;
  params_[nParams_] = -1;
  colon_[nParams_] = colon ? 1 : 0;
  ++nParams_;
  return curOk_ = true;
}

int VtScreen::param(size_t i, int def) const { return (i < nParams_ && params_[i] >= 0) ? params_[i] : def; }

void VtScreen::handleByte(unsigned char b) {
  switch (st_) {
    case St::Ground: groundByte(b); return;
    case St::Osc:
      if (b == 0x07) {
        oscDispatch();
        st_ = St::Ground;
      } else if (b == 0x1B) {
        st_ = St::OscEsc;
      } else if (b == 0x18 || b == 0x1A) {
        st_ = St::Ground;
      } else if (b >= 0x20 && osc_.size() < kMaxOsc) {
        osc_ += static_cast<char>(b);
      }
      return;
    case St::OscEsc:
      if (b == '\\') {
        oscDispatch();
        st_ = St::Ground;
      } else {
        startSequence(St::Escape);
        handleByte(b);
      }
      return;
    case St::StrIgnore:
      if (b == 0x1B) st_ = St::StrEsc;
      else if (b == 0x18 || b == 0x1A) st_ = St::Ground;
      return;
    case St::StrEsc:
      if (b == '\\') {
        st_ = St::Ground;
      } else {
        startSequence(St::Escape);
        handleByte(b);
      }
      return;
    default: break;
  }
  // Escape / CSI states: controls act immediately.
  if (b == 0x1B) {
    startSequence(St::Escape);
    return;
  }
  if (b == 0x18 || b == 0x1A) {
    st_ = St::Ground;
    return;
  }
  if (b < 0x20) {
    c0(b);
    return;
  }
  if (b == 0x7F) return;
  switch (st_) {
    case St::Escape:
      if (b >= 0x20 && b <= 0x2F) {
        inter_ = static_cast<char>(b);
        st_ = St::EscInter;
      } else if (b == '[') {
        startSequence(St::CsiParam);
      } else if (b == ']') {
        st_ = St::Osc;
        osc_.clear();
      } else if (b == 'P' || b == 'X' || b == '^' || b == '_') {
        st_ = St::StrIgnore;
      } else if (b >= 0x30 && b <= 0x7E) {
        escDispatch(b);
        st_ = St::Ground;
      } else {
        st_ = St::Ground;
      }
      return;
    case St::EscInter:
      if (b >= 0x20 && b <= 0x2F) {
        // second intermediate: keep the first
      } else if (b >= 0x30 && b <= 0x7E) {
        escDispatch(b);
        st_ = St::Ground;
      } else {
        st_ = St::Ground;
      }
      return;
    case St::CsiParam:
      if (b >= '0' && b <= '9') {
        if (!paramSeen_) {
          newParam(pendColon_);
          paramSeen_ = true;
          pendColon_ = false;
        }
        sepLast_ = false;
        if (curOk_) {
          int& p = params_[nParams_ - 1];
          p = std::min((p < 0 ? 0 : p) * 10 + (b - '0'), 1000000);
        }
      } else if (b == ';' || b == ':') {
        if (!paramSeen_) newParam(pendColon_);
        paramSeen_ = false;
        sepLast_ = true;
        pendColon_ = (b == ':');
      } else if (b >= 0x3C && b <= 0x3F) {
        if (nParams_ == 0 && !paramSeen_ && !sepLast_ && priv_ == 0) priv_ = static_cast<char>(b);
        else st_ = St::CsiIgnore;
      } else if (b >= 0x20 && b <= 0x2F) {
        inter_ = static_cast<char>(b);
        st_ = St::CsiInter;
        if (sepLast_) newParam(pendColon_);
        sepLast_ = false;
      } else if (b >= 0x40 && b <= 0x7E) {
        if (sepLast_) newParam(pendColon_);
        csiDispatch(b);
        if (st_ == St::CsiParam) st_ = St::Ground;
      } else {
        st_ = St::CsiIgnore;
      }
      return;
    case St::CsiInter:
      if (b >= 0x20 && b <= 0x2F) {
        // more intermediates: keep the first
      } else if (b >= 0x40 && b <= 0x7E) {
        csiDispatch(b);
        if (st_ == St::CsiInter) st_ = St::Ground;
      } else {
        st_ = St::CsiIgnore;
      }
      return;
    case St::CsiIgnore:
      if (b >= 0x40 && b <= 0x7E) st_ = St::Ground;
      return;
    default: return;
  }
}

void VtScreen::groundByte(unsigned char b) {
  if (utf8Need_ > 0) {
    if ((b & 0xC0) == 0x80) {
      utf8Cp_ = (utf8Cp_ << 6) | (b & 0x3F);
      if (--utf8Need_ == 0) {
        char32_t cp = utf8Cp_;
        // Overlong forms and surrogates are replaced; the encoder-side minimum is tracked by the lead byte below.
        if (cp < 0x80 || (cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF) cp = kReplacement;
        put(cp);
      }
      return;
    }
    utf8Need_ = 0;
    put(kReplacement);
    // fall through: b starts something new
  }
  if (b < 0x20) {
    if (b == 0x1B) startSequence(St::Escape);
    else c0(b);
    return;
  }
  if (b < 0x7F) {
    put(b);
    return;
  }
  if (b == 0x7F) return;
  if (b >= 0xC2 && b <= 0xDF) {
    utf8Cp_ = b & 0x1F;
    utf8Need_ = 1;
  } else if (b >= 0xE0 && b <= 0xEF) {
    utf8Cp_ = b & 0x0F;
    utf8Need_ = 2;
  } else if (b >= 0xF0 && b <= 0xF4) {
    utf8Cp_ = b & 0x07;
    utf8Need_ = 3;
  } else {
    put(kReplacement);
  }
}

void VtScreen::c0(unsigned char b) {
  switch (b) {
    case 0x08:  // BS
      if (curCol_ > 0) --curCol_;
      wrapPending_ = false;
      changed_ = true;
      break;
    case 0x09:  // HT
      tab(1);
      break;
    case 0x0A:  // LF
    case 0x0B:  // VT
    case 0x0C:  // FF
      lineFeed();
      break;
    case 0x0D:  // CR
      carriageReturn();
      break;
    case 0x0E:  // SO
      gl_ = 1;
      break;
    case 0x0F:  // SI
      gl_ = 0;
      break;
    default:  // BEL, ENQ, ... : nothing to show
      break;
  }
}

// ---------------------------------------------------------------------------------------------------------------
// Printing

void VtScreen::put(char32_t cp) {
  uint8_t cs = gl_ == 0 ? g0_ : g1_;
  if (cs == 1 && cp >= 0x5F && cp <= 0x7E) cp = kGraphics[cp - 0x5F];
  int w = cp < 0x7F ? 1 : cpWidth(cp);
  if (w == 0) return;
  if (w == 2 && cols_ < 2) w = 1;
  putCell(cp, w);
}

void VtScreen::putCell(char32_t cp, int w) {
  if (wrapPending_) {
    wrapPending_ = false;
    if (autoWrap_) {
      grid_[static_cast<size_t>(curRow_)].wrapped = true;
      curCol_ = 0;
      lineFeed();
    }
  }
  if (w == 2 && curCol_ > cols_ - 2) {
    if (!autoWrap_) return;
    VtRow& r = grid_[static_cast<size_t>(curRow_)];
    fixWide(r, curCol_);
    r.cells[static_cast<size_t>(curCol_)] = blank();
    r.wrapped = true;
    touch(curRow_);
    curCol_ = 0;
    lineFeed();
  }
  VtRow& row = grid_[static_cast<size_t>(curRow_)];
  if (insertMode_) insertChars(w);
  fixWide(row, curCol_);
  if (w == 2) fixWide(row, curCol_ + 1);
  VtCell& c = row.cells[static_cast<size_t>(curCol_)];
  c.cp = cp;
  c.fg = fg_;
  c.bg = bg_;
  c.attrs = attrs_;
  c.width = static_cast<uint8_t>(w);
  if (w == 2) {
    VtCell& d = row.cells[static_cast<size_t>(curCol_) + 1];
    d.cp = 0;
    d.fg = fg_;
    d.bg = bg_;
    d.attrs = attrs_;
    d.width = 0;
  }
  lastChar_ = cp;
  touch(curRow_);
  curCol_ += w;
  if (curCol_ >= cols_) {
    curCol_ = cols_ - 1;
    wrapPending_ = true;
  }
}

// Called before overwriting or cutting at col: blanks the other half of a wide character that col belongs to (and col).
void VtScreen::fixWide(VtRow& r, int col) {
  if (col < 0 || col >= static_cast<int>(r.cells.size())) return;
  VtCell& c = r.cells[static_cast<size_t>(col)];
  if (c.width == 0) {
    if (col > 0 && r.cells[static_cast<size_t>(col) - 1].width == 2) r.cells[static_cast<size_t>(col) - 1] = blank();
    c = blank();
  } else if (c.width == 2) {
    if (col + 1 < static_cast<int>(r.cells.size())) r.cells[static_cast<size_t>(col) + 1] = blank();
    c = blank();
  }
}

// ---------------------------------------------------------------------------------------------------------------
// Cursor movement and scrolling

void VtScreen::carriageReturn() {
  curCol_ = 0;
  wrapPending_ = false;
  changed_ = true;
}

void VtScreen::lineFeed() {
  wrapPending_ = false;
  if (curRow_ == bottom_) scrollUp(1);
  else if (curRow_ < rows_ - 1) ++curRow_;
  changed_ = true;
}

void VtScreen::reverseIndex() {
  wrapPending_ = false;
  if (curRow_ == top_) scrollDown(1);
  else if (curRow_ > 0) --curRow_;
  changed_ = true;
}

void VtScreen::scrollUp(int n) {
  n = std::min(n, bottom_ - top_ + 1);
  if (n <= 0) return;
  bool save = !alt_ && top_ == 0 && scrollbackLimit_ > 0;
  if (save) {
    for (int i = 0; i < n; ++i) {
      scrollback_.push_back(std::move(grid_[static_cast<size_t>(i)]));
      if (scrollback_.size() > scrollbackLimit_) {
        grid_[static_cast<size_t>(i)] = std::move(scrollback_.front());  // recycle the evicted row's storage
        scrollback_.pop_front();
      } else {
        grid_[static_cast<size_t>(i)] = VtRow{};
      }
    }
  }
  auto first = grid_.begin() + top_;
  std::rotate(first, first + n, grid_.begin() + bottom_ + 1);
  VtCell b = blank();
  for (int r = bottom_ - n + 1; r <= bottom_; ++r) {
    VtRow& row = grid_[static_cast<size_t>(r)];
    row.cells.assign(static_cast<size_t>(cols_), b);
    row.wrapped = false;
  }
  touchRegion(top_, bottom_);
}

void VtScreen::scrollDown(int n) {
  n = std::min(n, bottom_ - top_ + 1);
  if (n <= 0) return;
  auto first = grid_.begin() + top_;
  std::rotate(first, grid_.begin() + bottom_ + 1 - n, grid_.begin() + bottom_ + 1);
  VtCell b = blank();
  for (int r = top_; r < top_ + n; ++r) {
    VtRow& row = grid_[static_cast<size_t>(r)];
    row.cells.assign(static_cast<size_t>(cols_), b);
    row.wrapped = false;
  }
  touchRegion(top_, bottom_);
}

void VtScreen::moveTo(int row, int col) {
  if (originMode_) {
    row = std::clamp(row + top_, top_, bottom_);
  } else {
    row = std::clamp(row, 0, rows_ - 1);
  }
  curRow_ = row;
  curCol_ = std::clamp(col, 0, cols_ - 1);
  wrapPending_ = false;
  changed_ = true;
}

void VtScreen::tab(int n) {
  while (n-- > 0 && curCol_ < cols_ - 1) {  // stop at the last column: "CSI 999999999 I" otherwise spins for seconds
    int c = curCol_ + 1;
    while (c < cols_ - 1 && !tabs_[static_cast<size_t>(c)]) ++c;
    curCol_ = std::min(c, cols_ - 1);
  }
  wrapPending_ = false;
  changed_ = true;
}

void VtScreen::backTab(int n) {
  while (n-- > 0 && curCol_ > 0) {
    int c = curCol_ - 1;
    while (c > 0 && !tabs_[static_cast<size_t>(c)]) --c;
    curCol_ = std::max(c, 0);
  }
  wrapPending_ = false;
  changed_ = true;
}

// ---------------------------------------------------------------------------------------------------------------
// Erasing and inserting

void VtScreen::eraseCells(int row, int c0, int c1) {
  c0 = std::max(c0, 0);
  c1 = std::min(c1, cols_);
  if (c0 >= c1) return;
  VtRow& r = grid_[static_cast<size_t>(row)];
  fixWide(r, c0);
  fixWide(r, c1 - 1);
  VtCell b = blank();
  std::fill(r.cells.begin() + c0, r.cells.begin() + c1, b);
  if (c1 >= cols_) r.wrapped = false;
  touch(row);
}

void VtScreen::eraseRows(int r0, int r1) {
  VtCell b = blank();
  for (int r = std::max(r0, 0); r < std::min(r1, rows_); ++r) {
    VtRow& row = grid_[static_cast<size_t>(r)];
    row.cells.assign(static_cast<size_t>(cols_), b);
    row.wrapped = false;
    touch(r);
  }
}

void VtScreen::insertChars(int n) {
  VtRow& r = grid_[static_cast<size_t>(curRow_)];
  n = std::min(n, cols_ - curCol_);
  if (n <= 0) return;
  fixWide(r, curCol_);
  auto c = r.cells.begin();
  std::move_backward(c + curCol_, c + (cols_ - n), c + cols_);
  std::fill(c + curCol_, c + curCol_ + n, blank());
  if (r.cells.back().width == 2) r.cells.back() = blank();
  if (curCol_ + n < cols_ && r.cells[static_cast<size_t>(curCol_ + n)].width == 0)
    r.cells[static_cast<size_t>(curCol_ + n)] = blank();
  touch(curRow_);
}

void VtScreen::deleteChars(int n) {
  VtRow& r = grid_[static_cast<size_t>(curRow_)];
  n = std::min(n, cols_ - curCol_);
  if (n <= 0) return;
  fixWide(r, curCol_);
  if (curCol_ + n < cols_) fixWide(r, curCol_ + n);
  auto c = r.cells.begin();
  std::move(c + curCol_ + n, c + cols_, c + curCol_);
  std::fill(c + (cols_ - n), c + cols_, blank());
  touch(curRow_);
}

void VtScreen::insertLines(int n) {
  if (curRow_ < top_ || curRow_ > bottom_) return;
  n = std::min(n, bottom_ - curRow_ + 1);
  auto first = grid_.begin() + curRow_;
  std::rotate(first, grid_.begin() + bottom_ + 1 - n, grid_.begin() + bottom_ + 1);
  VtCell b = blank();
  for (int r = curRow_; r < curRow_ + n; ++r) {
    grid_[static_cast<size_t>(r)].cells.assign(static_cast<size_t>(cols_), b);
    grid_[static_cast<size_t>(r)].wrapped = false;
  }
  curCol_ = 0;
  wrapPending_ = false;
  touchRegion(curRow_, bottom_);
}

void VtScreen::deleteLines(int n) {
  if (curRow_ < top_ || curRow_ > bottom_) return;
  n = std::min(n, bottom_ - curRow_ + 1);
  auto first = grid_.begin() + curRow_;
  std::rotate(first, first + n, grid_.begin() + bottom_ + 1);
  VtCell b = blank();
  for (int r = bottom_ - n + 1; r <= bottom_; ++r) {
    grid_[static_cast<size_t>(r)].cells.assign(static_cast<size_t>(cols_), b);
    grid_[static_cast<size_t>(r)].wrapped = false;
  }
  curCol_ = 0;
  wrapPending_ = false;
  touchRegion(curRow_, bottom_);
}

// ---------------------------------------------------------------------------------------------------------------
// Escape sequences

void VtScreen::saveCursor(Saved& s) const {
  s.row = curRow_;
  s.col = curCol_;
  s.fg = fg_;
  s.bg = bg_;
  s.attrs = attrs_;
  s.wrapPending = wrapPending_;
  s.originMode = originMode_;
  s.g0 = g0_;
  s.g1 = g1_;
  s.gl = gl_;
}

void VtScreen::restoreCursor(const Saved& s) {
  curRow_ = std::clamp(s.row, 0, rows_ - 1);
  curCol_ = std::clamp(s.col, 0, cols_ - 1);
  fg_ = s.fg;
  bg_ = s.bg;
  attrs_ = s.attrs;
  wrapPending_ = s.wrapPending;
  originMode_ = s.originMode;
  g0_ = s.g0;
  g1_ = s.g1;
  gl_ = s.gl;
  changed_ = true;
}

void VtScreen::escDispatch(unsigned char f) {
  changed_ = true;
  if (inter_ == '(' || inter_ == ')') {
    uint8_t cs = (f == '0') ? 1 : 0;
    if (inter_ == '(') g0_ = cs;
    else g1_ = cs;
    return;
  }
  if (inter_ != 0) return;  // '#', ' ', '%', '*', '+' ...: ignored
  switch (f) {
    case '7': saveCursor(saved_); break;
    case '8': restoreCursor(saved_); break;
    case 'c': fullReset(); break;
    case 'D': lineFeed(); break;
    case 'E':
      curCol_ = 0;
      lineFeed();
      break;
    case 'M': reverseIndex(); break;
    case 'H': tabs_[static_cast<size_t>(curCol_)] = 1; break;
    case '=': appKeypad_ = true; break;
    case '>': appKeypad_ = false; break;
    case 'Z': reply("\x1b[?1;2c"); break;
    default: break;
  }
}

void VtScreen::fullReset() {
  if (alt_) setAlt(false, false, false);
  softReset();
  fg_ = bg_ = 0;
  attrs_ = 0;
  defaultTabs();
  cursorShape_ = VtCursorShape::Default;
  appKeypad_ = false;
  mouseTracking_ = 0;
  mouseSgr_ = false;
  focusReporting_ = false;
  bracketedPaste_ = false;
  saved_ = Saved{};
  savedAlt_ = Saved{};
  eraseRows(0, rows_);
  curRow_ = curCol_ = 0;
  lastChar_ = 0;
  allDirty_ = true;
  changed_ = true;
}

void VtScreen::softReset() {
  cursorVisible_ = true;
  autoWrap_ = true;
  originMode_ = false;
  insertMode_ = false;
  appCursor_ = false;
  top_ = 0;
  bottom_ = rows_ - 1;
  fg_ = bg_ = 0;
  attrs_ = 0;
  g0_ = g1_ = gl_ = 0;
  wrapPending_ = false;
  saved_ = Saved{};
  changed_ = true;
}

void VtScreen::setAlt(bool on, bool clearOnEnter, bool saveCur) {
  if (on == alt_) return;
  if (on) {
    if (saveCur) saveCursor(savedAlt_);
    std::swap(grid_, altGrid_);
    alt_ = true;
    if (clearOnEnter) {
      VtColor oldBg = bg_;
      bg_ = 0;
      eraseRows(0, rows_);
      bg_ = oldBg;
    }
  } else {
    std::swap(grid_, altGrid_);
    alt_ = false;
    if (saveCur) restoreCursor(savedAlt_);
  }
  top_ = 0;
  bottom_ = rows_ - 1;
  wrapPending_ = false;
  allDirty_ = true;
  changed_ = true;
}

void VtScreen::setMode(bool priv, int mode, bool on) {
  changed_ = true;
  if (!priv) {
    if (mode == 4) insertMode_ = on;
    return;
  }
  switch (mode) {
    case 1: appCursor_ = on; break;
    case 6:
      originMode_ = on;
      moveTo(0, 0);
      break;
    case 7: autoWrap_ = on; break;
    case 25: cursorVisible_ = on; break;
    case 47: setAlt(on, false, false); break;
    case 1047:
      if (!on && alt_) {
        VtColor oldBg = bg_;
        bg_ = 0;
        eraseRows(0, rows_);
        bg_ = oldBg;
      }
      setAlt(on, on, false);
      break;
    case 1048:
      if (on) saveCursor(savedAlt_);
      else restoreCursor(savedAlt_);
      break;
    case 1049: setAlt(on, true, true); break;
    case 1000:
    case 1002:
    case 1003:
      if (on) mouseTracking_ = mode;
      else if (mouseTracking_ == mode) mouseTracking_ = 0;
      break;
    case 1004: focusReporting_ = on; break;
    case 1006: mouseSgr_ = on; break;
    case 2004: bracketedPaste_ = on; break;
    default: break;  // 3, 5, 12, 1005, 1015, 9001 ... accepted and ignored
  }
}

void VtScreen::sgr() {
  if (nParams_ == 0) {
    fg_ = bg_ = 0;
    attrs_ = 0;
    return;
  }
  auto val = [&](size_t i) { return i < nParams_ ? std::clamp(params_[i], 0, 255) : 0; };
  for (size_t i = 0; i < nParams_; ++i) {
    int p = params_[i] < 0 ? 0 : params_[i];
    size_t k = 0;  // colon sub-parameters following this one
    while (i + 1 + k < nParams_ && colon_[i + 1 + k]) ++k;
    switch (p) {
      case 0:
        fg_ = bg_ = 0;
        attrs_ = 0;
        break;
      case 1: attrs_ |= kVtBold; break;
      case 2: attrs_ |= kVtDim; break;
      case 3: attrs_ |= kVtItalic; break;
      case 4:
        if (k >= 1 && params_[i + 1] == 0) attrs_ &= static_cast<uint8_t>(~kVtUnderline);
        else attrs_ |= kVtUnderline;
        break;
      case 5:
      case 6: attrs_ |= kVtBlink; break;
      case 7: attrs_ |= kVtInverse; break;
      case 8: attrs_ |= kVtInvisible; break;
      case 9: attrs_ |= kVtStrike; break;
      case 21: attrs_ |= kVtUnderline; break;
      case 22: attrs_ &= static_cast<uint8_t>(~(kVtBold | kVtDim)); break;
      case 23: attrs_ &= static_cast<uint8_t>(~kVtItalic); break;
      case 24: attrs_ &= static_cast<uint8_t>(~kVtUnderline); break;
      case 25: attrs_ &= static_cast<uint8_t>(~kVtBlink); break;
      case 27: attrs_ &= static_cast<uint8_t>(~kVtInverse); break;
      case 28: attrs_ &= static_cast<uint8_t>(~kVtInvisible); break;
      case 29: attrs_ &= static_cast<uint8_t>(~kVtStrike); break;
      case 39: fg_ = 0; break;
      case 49: bg_ = 0; break;
      case 38:
      case 48:
      case 58: {
        VtColor col = 0;
        bool ok = false;
        size_t used = 0;
        if (k >= 1) {  // 38:5:n / 38:2:r:g:b / 38:2:cs:r:g:b
          int mode = params_[i + 1];
          if (mode == 5 && k >= 2) {
            col = vtIndexed(static_cast<unsigned>(val(i + 2)));
            ok = true;
          } else if (mode == 2 && k >= 4) {
            size_t b = (k >= 5) ? i + 3 : i + 2;
            col = vtRgb(static_cast<unsigned>(val(b)), static_cast<unsigned>(val(b + 1)),
                        static_cast<unsigned>(val(b + 2)));
            ok = true;
          }
          used = k;
        } else if (i + 1 < nParams_) {  // 38;5;n / 38;2;r;g;b
          int mode = params_[i + 1];
          if (mode == 5) {
            used = 2;
            if (i + 2 < nParams_) {
              col = vtIndexed(static_cast<unsigned>(val(i + 2)));
              ok = true;
            }
          } else if (mode == 2) {
            used = 4;
            if (i + 4 < nParams_) {
              col = vtRgb(static_cast<unsigned>(val(i + 2)), static_cast<unsigned>(val(i + 3)),
                          static_cast<unsigned>(val(i + 4)));
              ok = true;
            }
          } else {
            used = 1;
          }
        }
        if (ok) {
          if (p == 38) fg_ = col;
          else if (p == 48) bg_ = col;
        }
        i += std::min(used, nParams_ - 1 - i);
        k = 0;
        break;
      }
      default:
        if (p >= 30 && p <= 37) fg_ = vtIndexed(static_cast<unsigned>(p - 30));
        else if (p >= 40 && p <= 47) bg_ = vtIndexed(static_cast<unsigned>(p - 40));
        else if (p >= 90 && p <= 97) fg_ = vtIndexed(static_cast<unsigned>(p - 90 + 8));
        else if (p >= 100 && p <= 107) bg_ = vtIndexed(static_cast<unsigned>(p - 100 + 8));
        break;
    }
    i += k;
  }
}

void VtScreen::csiDispatch(unsigned char f) {
  changed_ = true;
  const bool priv = priv_ == '?';
  if (inter_ != 0) {
    if (inter_ == ' ' && f == 'q' && priv_ == 0) {
      int s = param(0, 0);
      cursorShape_ = s >= 0 && s <= 6 ? static_cast<VtCursorShape>(s) : VtCursorShape::Default;
    } else if (inter_ == '!' && f == 'p') {
      softReset();
    }
    return;
  }
  if (priv_ != 0 && !priv) {
    // Only the secondary device attributes query is answered; other '>' '=' '<' sequences are ignored.
    if (priv_ == '>' && f == 'c' && param(0, 0) == 0) reply("\x1b[>0;0;0c");
    return;
  }
  auto n1 = [&](size_t i) { return std::max(1, param(i, 1)); };
  auto up = [&](int n) {
    int lim = curRow_ >= top_ ? top_ : 0;
    curRow_ = std::max(lim, curRow_ - n);
    wrapPending_ = false;
  };
  auto down = [&](int n) {
    int lim = curRow_ <= bottom_ ? bottom_ : rows_ - 1;
    curRow_ = std::min(lim, curRow_ + n);
    wrapPending_ = false;
  };
  auto right = [&](int n) {
    curCol_ = std::min(cols_ - 1, curCol_ + n);
    wrapPending_ = false;
  };
  switch (f) {
    case '@':
      if (!priv) insertChars(n1(0));
      break;
    case 'A': up(n1(0)); break;
    case 'B': down(n1(0)); break;
    case 'C': right(n1(0)); break;
    case 'D':
      curCol_ = std::max(0, curCol_ - n1(0));
      wrapPending_ = false;
      break;
    case 'E':
      down(n1(0));
      curCol_ = 0;
      break;
    case 'F':
      up(n1(0));
      curCol_ = 0;
      break;
    case 'G':
    case '`':
      curCol_ = std::clamp(n1(0) - 1, 0, cols_ - 1);
      wrapPending_ = false;
      break;
    case 'a': right(n1(0)); break;
    case 'H':
    case 'f': moveTo(n1(0) - 1, n1(1) - 1); break;
    case 'd': moveTo(n1(0) - 1, curCol_); break;
    case 'e': down(n1(0)); break;
    case 'I': tab(n1(0)); break;
    case 'Z': backTab(n1(0)); break;
    case 'J': {
      int m = param(0, 0);
      if (m == 0) {
        eraseCells(curRow_, curCol_, cols_);
        eraseRows(curRow_ + 1, rows_);
      } else if (m == 1) {
        eraseRows(0, curRow_);
        eraseCells(curRow_, 0, curCol_ + 1);
      } else if (m == 2) {
        eraseRows(0, rows_);
      } else if (m == 3) {
        clearScrollback();
      }
      break;
    }
    case 'K': {
      int m = param(0, 0);
      if (m == 0) eraseCells(curRow_, curCol_, cols_);
      else if (m == 1) eraseCells(curRow_, 0, curCol_ + 1);
      else if (m == 2) eraseCells(curRow_, 0, cols_);
      break;
    }
    case 'L': insertLines(n1(0)); break;
    case 'M': deleteLines(n1(0)); break;
    case 'P': deleteChars(n1(0)); break;
    case 'S':
      if (!priv) scrollUp(n1(0));
      break;
    case 'T':
      if (!priv && nParams_ <= 1) scrollDown(n1(0));
      break;
    case 'X': eraseCells(curRow_, curCol_, curCol_ + n1(0)); break;
    case 'b':
      if (lastChar_ != 0) {
        int n = std::min(n1(0), kMaxRep);
        for (int i = 0; i < n; ++i) put(lastChar_);
      }
      break;
    case 'c':
      if (param(0, 0) == 0) reply("\x1b[?1;2c");
      break;
    case 'g': {
      int m = param(0, 0);
      if (m == 0) tabs_[static_cast<size_t>(curCol_)] = 0;
      else if (m == 3) std::fill(tabs_.begin(), tabs_.end(), uint8_t{0});
      break;
    }
    case 'h':
    case 'l':
      for (size_t i = 0; i < std::max<size_t>(nParams_, 1); ++i) setMode(priv, param(i, 0), f == 'h');
      break;
    case 'm':
      if (!priv) sgr();
      break;
    case 'n': {
      int m = param(0, 0);
      if (m == 5 && !priv) {
        reply("\x1b[0n");
      } else if (m == 6) {
        int r = curRow_ + 1 - (originMode_ ? top_ : 0);
        std::string out = priv ? "\x1b[?" : "\x1b[";
        out += std::to_string(r) + ";" + std::to_string(curCol_ + 1) + "R";
        reply(out);
      }
      break;
    }
    case 'r':
      if (!priv) {
        int t = param(0, 1) < 1 ? 1 : param(0, 1);
        int b = param(1, rows_);
        if (b < 1 || b > rows_) b = rows_;
        if (t < b) {
          top_ = t - 1;
          bottom_ = b - 1;
          moveTo(0, 0);
        }
      }
      break;
    case 's':
      if (!priv && nParams_ == 0) saveCursor(saved_);
      break;
    case 'u':
      if (!priv && nParams_ == 0) restoreCursor(saved_);
      break;
    case 't':
      if (!priv && param(0, 0) == 18) reply("\x1b[8;" + std::to_string(rows_) + ";" + std::to_string(cols_) + "t");
      break;
    default: break;  // unknown final byte: the sequence is consumed and ignored
  }
}

void VtScreen::oscDispatch() {
  changed_ = true;
  size_t i = 0;
  int cmd = 0;
  bool digits = false;
  while (i < osc_.size() && osc_[i] >= '0' && osc_[i] <= '9') {
    cmd = std::min(cmd * 10 + (osc_[i] - '0'), 100000);
    digits = true;
    ++i;
  }
  if (!digits) return;
  if (i < osc_.size() && osc_[i] == ';') ++i;
  else if (i < osc_.size()) return;
  if (cmd == 0 || cmd == 2) title_ = osc_.substr(i);
}

// ---------------------------------------------------------------------------------------------------------------
// Resize

void VtScreen::resizeGrid(std::vector<VtRow>& g, int rows, int cols) {
  g.resize(static_cast<size_t>(rows));
  for (auto& r : g) {
    if (static_cast<int>(r.cells.size()) > cols) {
      r.cells.resize(static_cast<size_t>(cols));
      if (r.cells.back().width == 2) r.cells.back() = VtCell{};
    } else {
      r.cells.resize(static_cast<size_t>(cols), VtCell{});
    }
  }
}

void VtScreen::resize(int rows, int cols) {
  rows = std::max(1, rows);
  cols = std::max(1, cols);
  if (rows == rows_ && cols == cols_) return;
  if (!alt_ && rows < rows_ && curRow_ > rows - 1) {
    // Keep the cursor line visible: the lines above scroll into the scrollback.
    int shift = curRow_ - (rows - 1);
    for (int i = 0; i < shift; ++i) {
      if (scrollbackLimit_ > 0) {
        scrollback_.push_back(std::move(grid_[static_cast<size_t>(i)]));
        if (scrollback_.size() > scrollbackLimit_) scrollback_.pop_front();
      }
    }
    grid_.erase(grid_.begin(), grid_.begin() + shift);
    curRow_ -= shift;
  }
  resizeGrid(grid_, rows, cols);
  resizeGrid(altGrid_, rows, cols);
  rows_ = rows;
  cols_ = cols;
  top_ = 0;
  bottom_ = rows_ - 1;
  curRow_ = std::clamp(curRow_, 0, rows_ - 1);
  curCol_ = std::clamp(curCol_, 0, cols_ - 1);
  wrapPending_ = false;
  for (Saved* s : {&saved_, &savedAlt_}) {
    s->row = std::clamp(s->row, 0, rows_ - 1);
    s->col = std::clamp(s->col, 0, cols_ - 1);
  }
  defaultTabs();
  dirty_.assign(static_cast<size_t>(rows_), 1);
  allDirty_ = true;
  generation_++;
}

}  // namespace tf
