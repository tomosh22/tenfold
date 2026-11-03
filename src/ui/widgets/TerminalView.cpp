#include "ui/widgets/TerminalView.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>

#include "ui/platform/Clipboard.h"

namespace tf::ui {

namespace {

constexpr int64_t kBlinkMs = 530;
constexpr VtColor kFgDefaultAsBg =
    0xFF000000u;  // an inverse cell with the default foreground: the background is the foreground
constexpr VtColor kBgDefaultAsFg = 0xFE000000u;

using gfx::Color;

Color mix(const Color& a, const Color& b, float t) {
  return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, 1.0f};
}

Color rgb8(unsigned r, unsigned g, unsigned b) { return {r / 255.0f, g / 255.0f, b / 255.0f, 1.0f}; }

// xterm palette entries 16-255: the 6x6x6 cube, then 24 grays.
Color paletteColor(const Theme& th, unsigned n) {
  if (n < 16) return th.terminalAnsi(static_cast<int>(n));
  if (n < 232) {
    static const unsigned lv[6] = {0, 95, 135, 175, 215, 255};
    const unsigned i = n - 16;
    return rgb8(lv[i / 36], lv[(i / 6) % 6], lv[i % 6]);
  }
  const unsigned g = 8 + 10 * (n - 232);
  return rgb8(g, g, g);
}

Color resolve(const Theme& th, VtColor c, bool bold) {
  if (c == kFgDefaultAsBg) return th.terminalFg();
  if (c == kBgDefaultAsFg) return th.terminalBg();
  if (vtIsRgb(c)) return rgb8(vtRed(c), vtGreen(c), vtBlue(c));
  if (vtIsIndexed(c)) {
    unsigned n = vtIndex(c);
    if (bold && n < 8) n += 8;  // bold as bright, like xterm and the Windows console
    return paletteColor(th, n);
  }
  return th.terminalFg();  // default foreground; the default background is never resolved (it is not drawn)
}

struct Eff {
  VtColor fg, bg;
  bool bold, dim, under, strike, hidden;
};

Eff effective(const VtCell& c) {
  Eff e;
  e.bold = c.attrs & kVtBold;
  e.dim = c.attrs & kVtDim;
  e.under = c.attrs & kVtUnderline;
  e.strike = c.attrs & kVtStrike;
  e.hidden = c.attrs & kVtInvisible;
  if (c.attrs & kVtInverse) {
    e.fg = c.bg == kVtDefaultColor ? kBgDefaultAsFg : c.bg;
    e.bg = c.fg == kVtDefaultColor ? kFgDefaultAsBg : c.fg;
  } else {
    e.fg = c.fg;
    e.bg = c.bg;
  }
  return e;
}

bool isWordChar(char32_t cp) {
  if (cp <= U' ' || cp == 0) return false;
  switch (cp) {
    case U'(':
    case U')':
    case U'[':
    case U']':
    case U'{':
    case U'}':
    case U'<':
    case U'>':
    case U'\'':
    case U'"':
    case U'`':
    case U',':
    case U';':
    case U'|':
    case U'&': return false;
    default: return true;
  }
}

const VtCell kBlank;

}  // namespace

std::string encodeTermMouse(const TermMouse& m, bool sgr) {
  int b = 0;
  switch (m.kind) {
    case TermMouse::WheelUp: b = 64; break;
    case TermMouse::WheelDown: b = 65; break;
    case TermMouse::Release: b = sgr ? m.button : 3; break;  // legacy encoding cannot say which button was released
    default: b = m.button; break;
  }
  if (m.kind == TermMouse::Motion) b += 32;
  if (m.shift) b += 4;
  if (m.alt) b += 8;
  if (m.ctrl) b += 16;
  const int x = m.col + 1, y = m.row + 1;
  if (sgr)
    return "\x1b[<" + std::to_string(b) + ";" + std::to_string(x) + ";" + std::to_string(y) +
           (m.kind == TermMouse::Release ? "m" : "M");
  if (x > 223 || y > 223) return {};  // the legacy encoding cannot address cells beyond 223
  std::string s = "\x1b[M";
  s += static_cast<char>(32 + b);
  s += static_cast<char>(32 + x);
  s += static_cast<char>(32 + y);
  return s;
}

TerminalView::TerminalView() = default;
TerminalView::~TerminalView() = default;

void TerminalView::setSession(std::unique_ptr<TerminalSession> s) {
  session_ = std::move(s);
  if (session_) {
    session_->onOutput = [this] { noteOutput(); };
    lastScrollback_ = session_->screen().scrollbackSize();
  }
  markDirty();
}

void TerminalView::setFont(std::shared_ptr<TerminalFont> f) {
  font_ = std::move(f);
  if (RootWidget* r = root()) r->requestLayout();
}

const gfx::TextRenderer* TerminalView::renderer() const {
  if (font_ && font_->tr) return font_->tr.get();
  const RootWidget* r = root();
  return r ? &r->text() : nullptr;
}

TerminalView::Metrics TerminalView::metrics() const {
  Metrics m;
  if (const gfx::TextRenderer* tr = renderer()) {
    m.cw = std::max(1.0f, tr->cellWidth());
    m.ch = std::max(1.0f, tr->lineHeight());
  }
  const float pad = theme().padding;
  m.ox = bounds().x + pad * 0.5f;
  m.oy = bounds().y + pad * 0.25f;
  m.sbW = pad;
  return m;
}

std::pair<int, int> TerminalView::gridFor(const RectF& area) const {
  Metrics m = metrics();
  const float pad = theme().padding;
  const int cols = static_cast<int>(std::floor((area.w - pad - m.sbW) / m.cw));
  const int rows = static_cast<int>(std::floor((area.h - pad * 0.5f) / m.ch));
  return {std::max(1, rows), std::max(2, cols)};
}

void TerminalView::onLayout() {
  if (!session_ || !root()) return;
  const auto [rows, cols] = gridFor(bounds());
  if (rows != session_->screen().rows() || cols != session_->screen().cols()) {
    session_->resize(rows, cols);
    offset_ = std::min<int>(offset_, static_cast<int>(session_->screen().scrollbackSize()));
    clearSelection();
    markDirty();
  }
}

// ---- helpers over the screen ----

const VtRow& TerminalView::absRow(long long abs) const {
  const VtScreen& s = session_->screen();
  const long long sb = static_cast<long long>(s.scrollbackSize());
  if (abs < sb) return s.scrollbackRow(static_cast<size_t>(std::max(0LL, abs)));
  return s.row(static_cast<int>(std::min<long long>(abs - sb, s.rows() - 1)));
}

TerminalView::SelPos TerminalView::cellAt(float x, float y, bool clampToView) const {
  const Metrics m = metrics();
  const VtScreen& s = session_->screen();
  int vr = static_cast<int>(std::floor((y - m.oy) / m.ch));
  int c = static_cast<int>(std::floor((x - m.ox) / m.cw));
  if (clampToView) {
    vr = std::clamp(vr, 0, s.rows() - 1);
    c = std::clamp(c, 0, s.cols() - 1);
  }
  const long long sb = static_cast<long long>(s.scrollbackSize());
  const int off = s.altScreen() ? 0 : offset_;
  SelPos p;
  p.row = sb - off + vr;
  p.col = c;
  return p;
}

RectF TerminalView::cellRect(int row, int col) const {
  const Metrics m = metrics();
  return {m.ox + static_cast<float>(col) * m.cw, m.oy + static_cast<float>(row) * m.ch, m.cw, m.ch};
}

std::string TerminalView::visibleRowText(int r) const {
  if (!session_) return {};
  const VtScreen& s = session_->screen();
  const long long sb = static_cast<long long>(s.scrollbackSize());
  const int off = s.altScreen() ? 0 : offset_;
  const VtRow& row = absRow(sb - off + r);
  std::string out;
  for (const VtCell& c : row.cells)
    if (c.width != 0) vtAppendUtf8(out, c.cp ? c.cp : U' ');
  while (!out.empty() && out.back() == ' ') out.pop_back();
  return out;
}

// ---- scrollback ----

void TerminalView::scrollBy(int lines) {
  if (!session_ || session_->screen().altScreen()) return;
  const int maxOff = static_cast<int>(session_->screen().scrollbackSize());
  const int next = std::clamp(offset_ + lines, 0, maxOff);
  if (next == offset_) return;
  offset_ = next;
  markDirty();
}

void TerminalView::scrollToBottom() {
  if (offset_ == 0) return;
  offset_ = 0;
  markDirty();
}

void TerminalView::noteOutput() {
  if (!session_) return;
  const size_t now = session_->screen().scrollbackSize();
  if (offset_ > 0 && now > lastScrollback_) offset_ += static_cast<int>(now - lastScrollback_);  // keep the place
  lastScrollback_ = now;
  offset_ = std::min<int>(offset_, static_cast<int>(now));
  resetBlink();
  markDirty();
}

void TerminalView::resetBlink() {
  blinkOn_ = true;
  blinkAtMs_ = 0;
}

// ---- selection ----

void TerminalView::clearSelection() {
  if (selActive_) markDirty();
  selActive_ = selecting_ = false;
}

void TerminalView::selectAll() {
  if (!session_) return;
  const VtScreen& s = session_->screen();
  selAnchor_ = {0, 0};
  selHead_ = {static_cast<long long>(s.scrollbackSize()) + s.rows() - 1, s.cols() - 1};
  selActive_ = true;
  markDirty();
}

// The word (a run of non-separator cells) under p; p itself when it is on a separator.
std::pair<TerminalView::SelPos, TerminalView::SelPos> TerminalView::wordRange(const SelPos& p) const {
  const VtRow& row = absRow(p.row);
  const int n = static_cast<int>(row.cells.size());
  const int c = std::min(p.col, n - 1);
  auto wordAt = [&](int i) {
    const VtCell& cell = row.cells[static_cast<size_t>(i)];
    return cell.width == 0 || isWordChar(cell.cp);
  };
  if (c < 0 || !wordAt(c)) return {p, p};
  int a = c, b = c;
  while (a > 0 && wordAt(a - 1)) --a;
  while (b + 1 < n && wordAt(b + 1)) ++b;
  return {SelPos{p.row, a}, SelPos{p.row, b}};
}

// A soft-wrapped line is one logical line: the range extends over the rows that continue it.
std::pair<TerminalView::SelPos, TerminalView::SelPos> TerminalView::lineRange(long long row) const {
  long long a = row, b = row;
  const VtScreen& s = session_->screen();
  const long long total = static_cast<long long>(s.scrollbackSize()) + s.rows();
  while (a > 0 && absRow(a - 1).wrapped) --a;
  while (b + 1 < total && absRow(b).wrapped) ++b;
  return {SelPos{a, 0}, SelPos{b, std::max(0, s.cols() - 1)}};
}

std::string TerminalView::selectedText() const {
  if (!session_ || !selActive_) return {};
  SelPos a = selAnchor_, b = selHead_;
  if (b < a) std::swap(a, b);
  std::string out;
  for (long long r = a.row; r <= b.row; ++r) {
    const VtRow& row = absRow(r);
    const int from = r == a.row ? a.col : 0;
    const int to = r == b.row ? b.col : std::numeric_limits<int>::max() - 1;  // inclusive
    std::string line;
    for (int c = from; c <= to && c < static_cast<int>(row.cells.size()); ++c) {
      const VtCell& cell = row.cells[static_cast<size_t>(c)];
      if (cell.width == 0) continue;
      vtAppendUtf8(line, cell.cp ? cell.cp : U' ');
    }
    const bool continues = row.wrapped && r != b.row;
    if (!continues) {
      while (!line.empty() && line.back() == ' ') line.pop_back();
    }
    out += line;
    if (r != b.row && !continues) out += '\n';
  }
  return out;
}

bool TerminalView::copySelection(bool clear) {
  if (!selActive_) return false;
  const std::string t = selectedText();
  if (!t.empty()) clipboardSet(t);
  if (clear) clearSelection();
  return true;
}

void TerminalView::pasteText(const std::string& text) {
  if (!session_ || text.empty() || session_->exited()) return;
  scrollToBottom();
  session_->write(encodeTermPaste(text, session_->screen().bracketedPaste()));
}

void TerminalView::paste() { pasteText(clipboardGet()); }

// ---- input ----

void TerminalView::sendBytes(std::string s) {
  if (!session_ || s.empty()) return;
  session_->write(std::move(s));
}

void TerminalView::sendKey(const TermKeyEvent& k) {
  if (!session_) return;
  TermKeyModes modes;
  modes.applicationCursorKeys = session_->screen().applicationCursorKeys();
  std::string bytes = encodeTermKey(k, modes);
  if (bytes.empty()) return;
  scrollToBottom();
  resetBlink();
  sendBytes(std::move(bytes));
  markDirty();
}

// Focus can be lost without a callback (the tab is hidden and the root forgets the widget), so the state the program
// was last told is compared with the real one whenever it might have changed, and every frame (TerminalPanel::tick).
void TerminalView::syncFocusReport() {
  const bool a = hasFocus() && windowFocused_;
  if (a == reportedFocus_) return;
  reportedFocus_ = a;
  if (session_ && !session_->exited() && session_->screen().focusReporting()) sendBytes(a ? "\x1b[I" : "\x1b[O");
  resetBlink();
  markDirty();
}

void TerminalView::setWindowFocused(bool on) {
  windowFocused_ = on;
  syncFocusReport();
}

void TerminalView::onFocusChanged(bool) { syncFocusReport(); }

bool TerminalView::onKey(const InputEvent& e) {
  const bool ctrl = e.mods & ModCtrl, alt = e.mods & ModAlt, shift = e.mods & ModShift;
  const uint32_t k = static_cast<uint32_t>(e.key);
  // Clipboard.
  if (ctrl && !alt && (e.key == Key('C'))) {
    if (shift) {
      copySelection(false);
      return true;  // Ctrl+Shift+C never reaches the program
    }
    if (selActive_) {
      copySelection(true);
      return true;
    }
    // no selection: falls through, ^C goes to the program
  }
  if (ctrl && !alt && !shift && e.key == Key::Insert) {
    copySelection(false);
    return true;
  }
  if ((ctrl && !alt && e.key == Key('V')) || (shift && !ctrl && !alt && e.key == Key::Insert)) {
    paste();
    return true;
  }
  if (!session_ || session_->exited()) return false;
  // Scrollback keys.
  if (shift && !ctrl && !alt && (e.key == Key::PageUp || e.key == Key::PageDown) && !session_->screen().altScreen()) {
    const int page = std::max(1, session_->screen().rows() - 1);
    scrollBy(e.key == Key::PageUp ? page : -page);
    return true;
  }
  TermKeyEvent t;
  t.ctrl = ctrl;
  t.alt = alt;
  t.shift = shift;
  switch (e.key) {
    case Key::Enter:
    case Key::KpEnter: t.key = TermKey::Enter; break;
    case Key::Backspace: t.key = TermKey::Backspace; break;
    case Key::Tab: t.key = TermKey::Tab; break;
    case Key::Escape: t.key = TermKey::Escape; break;
    case Key::Up: t.key = TermKey::Up; break;
    case Key::Down: t.key = TermKey::Down; break;
    case Key::Left: t.key = TermKey::Left; break;
    case Key::Right: t.key = TermKey::Right; break;
    case Key::Home: t.key = TermKey::Home; break;
    case Key::End: t.key = TermKey::End; break;
    case Key::PageUp: t.key = TermKey::PageUp; break;
    case Key::PageDown: t.key = TermKey::PageDown; break;
    case Key::Insert: t.key = TermKey::Insert; break;
    case Key::Delete: t.key = TermKey::Delete; break;
    default:
      if (k >= static_cast<uint32_t>(Key::F1) && k <= static_cast<uint32_t>(Key::F12)) {
        t.key = static_cast<TermKey>(static_cast<uint32_t>(TermKey::F1) + (k - static_cast<uint32_t>(Key::F1)));
        break;
      }
      if (k >= 0x20 && k < 0x7f) {
        // Printable keys arrive as Text events; only Ctrl / Alt combinations are encoded here. Ctrl+Alt is how
        // Windows reports AltGr, whose characters also arrive as Text.
        if ((!ctrl && !alt) || (ctrl && alt)) return false;
        char ch = static_cast<char>(k);
        if (ch >= 'A' && ch <= 'Z' && !(alt && shift)) ch = static_cast<char>(ch - 'A' + 'a');
        t.key = TermKey::Text;
        t.text.assign(1, ch);
        break;
      }
      return false;
  }
  sendKey(t);
  return true;
}

void TerminalView::sendMouse(const InputEvent& e, TermMouse::Kind kind) {
  const VtScreen& s = session_->screen();
  const SelPos p = cellAt(e.x, e.y, true);
  TermMouse m;
  m.kind = kind;
  m.button = kind == TermMouse::Motion && !(e.buttons & 7) ? 3 : reportButton_;
  m.col = p.col;
  m.row = static_cast<int>(p.row - static_cast<long long>(s.scrollbackSize()) + (s.altScreen() ? 0 : offset_));
  m.row = std::clamp(m.row, 0, s.rows() - 1);
  m.shift = e.mods & ModShift;
  m.alt = e.mods & ModAlt;
  m.ctrl = e.mods & ModCtrl;
  sendBytes(encodeTermMouse(m, s.mouseSgr()));
}

RectF TerminalView::scrollbarThumb() const {
  const Metrics m = metrics();
  const VtScreen& s = session_->screen();
  const float trackH = bounds().h;
  const float total = static_cast<float>(s.scrollbackSize() + static_cast<size_t>(s.rows()));
  const float visible = static_cast<float>(s.rows());
  const float thumbH = std::max(m.ch, trackH * visible / total);
  const float maxOff = static_cast<float>(s.scrollbackSize());
  const float frac = maxOff > 0 ? static_cast<float>(offset_) / maxOff : 0;  // 0 = bottom
  const float y = bounds().y + (trackH - thumbH) * (1.0f - frac);
  return {bounds().x + bounds().w - m.sbW, y, m.sbW, thumbH};
}

bool TerminalView::mouseDown(const InputEvent& e) {
  if (!session_) return false;
  VtScreen& s = session_->screen();
  const bool track = s.mouseTracking() > 0 && !(e.mods & ModShift);
  if (e.button == MouseButton::Left && !s.altScreen() && s.scrollbackSize() > 0 &&
      e.x >= bounds().x + bounds().w - metrics().sbW) {  // the scrollbar
    const RectF th = scrollbarThumb();
    if (e.y >= th.y && e.y < th.y + th.h) barGrab_ = e.y - th.y;
    else {
      barGrab_ = th.h / 2;  // a click on the track jumps there
    }
    draggingBar_ = true;
    captureMouse();
    InputEvent mv = e;
    mouseMove(mv);
    return true;
  }
  if (track && e.button <= MouseButton::Right) {
    reportButton_ = static_cast<int>(e.button);
    reporting_ = true;
    sendMouse(e, TermMouse::Press);
    captureMouse();
    return true;
  }
  if (e.button == MouseButton::Right) {
    if (selActive_) copySelection(true);
    else paste();
    return true;
  }
  if (e.button != MouseButton::Left) return false;
  const SelPos p = cellAt(e.x, e.y, true);
  selClicks_ = std::max(1, e.clicks);
  if (selClicks_ >= 2) {
    std::tie(selAnchor_, selHead_) = selClicks_ == 2 ? wordRange(p) : lineRange(p.row);
    selOrigA_ = selAnchor_;
    selOrigB_ = selHead_;
    selActive_ = true;
  } else if ((e.mods & ModShift) && selActive_) {
    selHead_ = p;
  } else {
    selAnchor_ = selHead_ = p;
    selActive_ = false;  // becomes a selection once the pointer moves
  }
  selecting_ = true;
  captureMouse();
  markDirty();
  return true;
}

bool TerminalView::mouseMove(const InputEvent& e) {
  if (!session_) return false;
  VtScreen& s = session_->screen();
  if (draggingBar_) {
    const float maxOff = static_cast<float>(s.scrollbackSize());
    const float trackH = bounds().h;
    const RectF th = scrollbarThumb();
    const float range = std::max(1.0f, trackH - th.h);
    const float top = std::clamp(e.y - barGrab_ - bounds().y, 0.0f, range);
    const float frac = 1.0f - top / range;
    const int next = static_cast<int>(std::lround(frac * maxOff));
    if (next != offset_) {
      offset_ = next;
      markDirty();
    }
    return true;
  }
  if (reporting_) {
    if (s.mouseTracking() >= 1002) sendMouse(e, TermMouse::Motion);
    return true;
  }
  if (!selecting_) {
    if (s.mouseTracking() == 1003 && !(e.mods & ModShift)) {
      reportButton_ = 3;
      sendMouse(e, TermMouse::Motion);
      return true;
    }
    return false;
  }
  // Dragging above / below the view scrolls the history one line per move event.
  if (e.y < bounds().y) scrollBy(1);
  else if (e.y > bounds().y + bounds().h) scrollBy(-1);
  const SelPos p = cellAt(e.x, e.y, true);
  if (!selActive_ && p == selAnchor_) return true;
  if (selClicks_ <= 1) {
    selHead_ = p;
    selActive_ = true;
  } else {
    // Word / line selection grows by whole words / lines around the one that was double / triple clicked.
    const auto [ua, ub] = selClicks_ == 2 ? wordRange(p) : lineRange(p.row);
    if (p < selOrigA_) {
      selAnchor_ = selOrigB_;
      selHead_ = ua;
    } else {
      selAnchor_ = selOrigA_;
      selHead_ = ub;
    }
  }
  markDirty();
  return true;
}

bool TerminalView::mouseUp(const InputEvent& e) {
  if (draggingBar_) {
    draggingBar_ = false;
    return true;
  }
  if (reporting_) {
    reporting_ = false;
    if (session_) sendMouse(e, TermMouse::Release);
    return true;
  }
  if (selecting_) {
    selecting_ = false;
    return true;
  }
  return e.button == MouseButton::Right;
}

bool TerminalView::onEvent(const InputEvent& e) {
  if (!session_) return false;
  VtScreen& s = session_->screen();
  switch (e.type) {
    case InputType::KeyDown: return onKey(e);
    case InputType::Text:
      if (e.text.empty() || session_->exited()) return false;
      scrollToBottom();
      resetBlink();
      sendBytes(e.text);
      markDirty();
      return true;
    case InputType::MouseDown: return mouseDown(e);
    case InputType::MouseMove: return mouseMove(e);
    case InputType::MouseUp: return mouseUp(e);
    case InputType::Wheel: {
      if (e.dy == 0) return false;
      const bool shift = e.mods & ModShift;
      if (s.mouseTracking() > 0 && !shift) {
        const int n = std::max(1, static_cast<int>(std::lround(std::fabs(e.dy))));
        for (int i = 0; i < n; ++i) sendMouse(e, e.dy > 0 ? TermMouse::WheelUp : TermMouse::WheelDown);
        return true;
      }
      if (s.altScreen()) {  // alternate scroll: the wheel moves the cursor in full-screen programs (less, man)
        TermKeyEvent t;
        t.key = e.dy > 0 ? TermKey::Up : TermKey::Down;
        const int n = std::max(1, static_cast<int>(std::lround(std::fabs(e.dy)))) * 3;
        for (int i = 0; i < n; ++i) sendKey(t);
        return true;
      }
      wheelFrac_ += e.dy * static_cast<float>(wheelLines);
      const int whole = static_cast<int>(wheelFrac_);
      wheelFrac_ -= static_cast<float>(whole);
      if (whole != 0) scrollBy(whole);
      return true;
    }
    default: return false;
  }
}

int TerminalView::onTick(int64_t nowMs) {
  if (!session_ || !hasFocus() || !windowFocused_ || offset_ > 0) return -1;
  const VtScreen& s = session_->screen();
  const VtCursorShape sh = s.cursorShape();
  const bool blinks =
      sh == VtCursorShape::BlinkBlock || sh == VtCursorShape::BlinkUnderline || sh == VtCursorShape::BlinkBar;
  if (!blinks || !s.cursorVisible()) {
    if (!blinkOn_) {
      blinkOn_ = true;
      markDirty();
    }
    return -1;
  }
  if (blinkAtMs_ == 0) blinkAtMs_ = nowMs + kBlinkMs;
  if (nowMs >= blinkAtMs_) {
    blinkOn_ = !blinkOn_;
    blinkAtMs_ = nowMs + kBlinkMs;
    markDirty();
  }
  return static_cast<int>(std::max<int64_t>(1, blinkAtMs_ - nowMs));
}

// ---- painting ----

void TerminalView::paint(PaintContext& ctx) {
  const Theme& th = ctx.theme;
  const Color bgc = th.terminalBg();
  ctx.fill(bounds(), bgc);
  if (!session_) return;
  VtScreen& s = session_->screen();
  gfx::TextRenderer& tr = (font_ && font_->tr) ? *font_->tr : ctx.textRenderer;
  if (font_) {  // a replaced renderer can give its texture back now that a live Renderer is at hand
    for (auto& old : font_->retired) old->releaseTexture(ctx.renderer);
    font_->retired.clear();
  }
  const Metrics m = metrics();
  const int rows = s.rows(), cols = s.cols();
  const long long sb = static_cast<long long>(s.scrollbackSize());
  const int off = s.altScreen() ? 0 : std::min<int>(offset_, static_cast<int>(sb));
  const RectF clip = bounds();
  const bool showCursor = off == 0 && s.cursorVisible() && blinkOn_;
  const bool cursorFilled = hasFocus() && windowFocused_;
  const VtCursorShape shape = s.cursorShape();
  const bool blockCursor =
      shape == VtCursorShape::Default || shape == VtCursorShape::BlinkBlock || shape == VtCursorShape::Block;
  SelPos sa = selAnchor_, sbp = selHead_;
  if (sbp < sa) std::swap(sa, sbp);
  const Color selColor = Theme::orElse(th.terminalSelection, Theme::orElse(th.selectionActive, th.selection));
  const Color curColor = th.terminalCursorColor();
  const float scale = std::max(1.0f, std::round(m.ch / 16.0f));

  for (int vr = 0; vr < rows; ++vr) {
    const long long abs = sb - off + vr;
    const VtRow& row = absRow(abs);
    const float y = m.oy + static_cast<float>(vr) * m.ch;
    const int n = std::min<int>(cols, static_cast<int>(row.cells.size()));
    const int curCol = (showCursor && abs == sb + s.cursorRow()) ? s.cursorCol() : -1;

    // Cell backgrounds, merged into runs.
    {
      int c = 0;
      while (c < n) {
        const VtCell& cell = row.cells[static_cast<size_t>(c)];
        VtColor key = effective(cell).bg;
        const bool isCur = c == curCol && blockCursor && cursorFilled;
        if (key == kVtDefaultColor && !isCur) {
          ++c;
          continue;
        }
        int e = c + 1;
        while (e < n && e != curCol && c != curCol && effective(row.cells[static_cast<size_t>(e)]).bg == key) ++e;
        if (isCur) ctx.fill({m.ox + static_cast<float>(c) * m.cw, y, m.cw * (cell.width == 2 ? 2 : 1), m.ch}, curColor);
        else
          ctx.fill({m.ox + static_cast<float>(c) * m.cw, y, static_cast<float>(e - c) * m.cw, m.ch},
                   resolve(th, key, false));
        c = e;
      }
    }
    // Selection.
    if (selActive_ && abs >= sa.row && abs <= sbp.row) {
      const int from = abs == sa.row ? sa.col : 0;
      int to = abs == sbp.row ? sbp.col : cols - 1;  // inclusive
      if (to < cols && to + 1 < n && row.cells[static_cast<size_t>(to + 1)].width == 0) ++to;
      if (to >= from)
        ctx.fill({m.ox + static_cast<float>(from) * m.cw, y,
                  static_cast<float>(std::min(to, cols - 1) - from + 1) * m.cw, m.ch},
                 selColor);
    }
    // Cursor shapes other than the block (and the block outline of an unfocused terminal).
    if (curCol >= 0 && !(blockCursor && cursorFilled)) {
      const float x = m.ox + static_cast<float>(curCol) * m.cw;
      if (blockCursor) {
        ctx.fill({x, y, m.cw, scale}, curColor);
        ctx.fill({x, y + m.ch - scale, m.cw, scale}, curColor);
        ctx.fill({x, y, scale, m.ch}, curColor);
        ctx.fill({x + m.cw - scale, y, scale, m.ch}, curColor);
      } else if (shape == VtCursorShape::Underline || shape == VtCursorShape::BlinkUnderline) {
        ctx.fill({x, y + m.ch - 2 * scale, m.cw, 2 * scale}, curColor);
      } else {
        ctx.fill({x, y, 2 * scale, m.ch}, curColor);
      }
    } else if (curCol >= 0 && !blockCursor) {
      const float x = m.ox + static_cast<float>(curCol) * m.cw;
      if (shape == VtCursorShape::Underline || shape == VtCursorShape::BlinkUnderline)
        ctx.fill({x, y + m.ch - 2 * scale, m.cw, 2 * scale}, curColor);
      else ctx.fill({x, y, 2 * scale, m.ch}, curColor);
    }

    // Text and decorations.
    int c = 0;
    while (c < n) {
      const VtCell& cell = row.cells[static_cast<size_t>(c)];
      if (cell.width == 0) {
        ++c;
        continue;
      }
      Eff ef = effective(cell);
      const bool isCur = c == curCol && blockCursor && cursorFilled;
      Color fg = isCur ? bgc : resolve(th, ef.fg, ef.bold);
      if (ef.dim) fg = mix(fg, bgc, 0.4f);
      const float x = m.ox + static_cast<float>(c) * m.cw;
      const int span = cell.width == 2 ? 2 : 1;
      if (ef.under) ctx.fill({x, y + m.ch - scale - 1, m.cw * static_cast<float>(span), scale}, fg);
      if (ef.strike) ctx.fill({x, y + m.ch * 0.5f, m.cw * static_cast<float>(span), scale}, fg);
      const char32_t cp = cell.cp;
      if (ef.hidden || cp == 0 || cp == U' ') {
        ++c;
        continue;
      }
      if (cp < 0x80) {
        // A run of printable ASCII cells in one colour.
        runBuf_.clear();
        runBuf_.push_back(static_cast<char>(cp));
        int e = c + 1;
        while (e < n) {
          const VtCell& nx = row.cells[static_cast<size_t>(e)];
          if (nx.cp < 0x21 || nx.cp >= 0x7f || e == curCol || c == curCol) break;
          const Eff en = effective(nx);
          if (en.fg != ef.fg || en.bold != ef.bold || en.dim != ef.dim || en.hidden || en.under != ef.under ||
              en.strike != ef.strike)
            break;
          runBuf_.push_back(static_cast<char>(nx.cp));
          ++e;
        }
        if (cp > U' ' && cp < 0x7f) {
          tr.drawText(x, y, runBuf_, fg, clip);
          if (ef.bold) tr.drawText(x + 1, y, runBuf_, fg, clip);
        }
        // Underline / strike for the cells folded into the run.
        if (e - c > 1 && (ef.under || ef.strike)) {
          const float w = m.cw * static_cast<float>(e - c - 1);
          if (ef.under) ctx.fill({x + m.cw, y + m.ch - scale - 1, w, scale}, fg);
          if (ef.strike) ctx.fill({x + m.cw, y + m.ch * 0.5f, w, scale}, fg);
        }
        c = e;
        continue;
      }
      runBuf_.clear();
      vtAppendUtf8(runBuf_, cp);
      tr.drawText(x, y, runBuf_, fg, clip);
      if (ef.bold) tr.drawText(x + 1, y, runBuf_, fg, clip);
      c += span;
    }
  }

  // Scrollbar: only when there is history to scroll through.
  if (!s.altScreen() && sb > 0) {
    const RectF track{bounds().x + bounds().w - m.sbW, bounds().y, m.sbW, bounds().h};
    ctx.fill(track, th.scrollbarTrack);
    ctx.fill(scrollbarThumb(), th.scrollbarThumb);
  }
  s.clearDirty();
}

void TerminalView::paintOverlay(PaintContext& ctx) {
  // Text drawn with the terminal's own font is emitted here, after the root's glyph batches.
  if (font_ && font_->tr && !font_->tr->flushText(ctx.renderer)) markDirty();  // atlas grew: repaint once
}

}  // namespace tf::ui
