#include "ui/widgets/InputLine.h"

#include <algorithm>
#include <cmath>

#include "ui/platform/Clipboard.h"

namespace tf::ui {

namespace {

bool isCont(unsigned char c) { return (c & 0xC0) == 0x80; }
size_t prevCp(const std::string& s, size_t i) {
  if (i == 0) return 0;
  --i;
  while (i > 0 && isCont(static_cast<unsigned char>(s[i]))) --i;
  return i;
}
size_t nextCp(const std::string& s, size_t i) {
  if (i >= s.size()) return s.size();
  ++i;
  while (i < s.size() && isCont(static_cast<unsigned char>(s[i]))) ++i;
  return i;
}
// 0 whitespace, 1 identifier (ASCII alnum, '_', any non-ASCII), 2 punctuation
int charClass(unsigned char c) {
  if (c == ' ' || c == '\t') return 0;
  if (std::isalnum(c) || c == '_' || c >= 0x80) return 1;
  return 2;
}

}  // namespace

void InputLine::setText(std::string s, bool all) {
  text_ = std::move(s);
  caret_ = text_.size();
  anchor_ = all ? 0 : caret_;
  markDirty();
}

void InputLine::setPlaceholder(std::string s) {
  placeholder_ = std::move(s);
  markDirty();
}

void InputLine::setCaret(size_t i, bool extend) {
  i = std::min(i, text_.size());
  while (i > 0 && i < text_.size() && isCont(static_cast<unsigned char>(text_[i]))) --i;
  caret_ = i;
  if (!extend) anchor_ = i;
  markDirty();
}

void InputLine::selectAll() {
  anchor_ = 0;
  caret_ = text_.size();
  markDirty();
}

std::string InputLine::selectedText() const {
  return text_.substr(std::min(caret_, anchor_), caret_ > anchor_ ? caret_ - anchor_ : anchor_ - caret_);
}

float InputLine::preferredHeight() const {
  return (root() ? root()->text().lineHeight() : 14.0f) + 2 * theme().verticalPad;
}

size_t InputLine::prevWord(size_t i) const {
  while (i > 0 && charClass(static_cast<unsigned char>(text_[i - 1])) == 0) --i;
  if (i == 0) return 0;
  const int cls = charClass(static_cast<unsigned char>(text_[i - 1]));
  while (i > 0 && charClass(static_cast<unsigned char>(text_[i - 1])) == cls) --i;
  return i;
}

size_t InputLine::nextWord(size_t i) const {
  while (i < text_.size() && charClass(static_cast<unsigned char>(text_[i])) == 0) ++i;
  if (i >= text_.size()) return text_.size();
  const int cls = charClass(static_cast<unsigned char>(text_[i]));
  while (i < text_.size() && charClass(static_cast<unsigned char>(text_[i])) == cls) ++i;
  return i;
}

void InputLine::edited() {
  markDirty();
  if (changed_) changed_(text_);
}

void InputLine::replaceSelection(const std::string& s) {
  std::string clean;
  for (char c : s) clean += (c == '\n' || c == '\r' || c == '\t') ? ' ' : c;
  clean.erase(std::remove_if(clean.begin(), clean.end(), [](char c) { return static_cast<unsigned char>(c) < 0x20; }),
              clean.end());
  const size_t a = std::min(caret_, anchor_), b = std::max(caret_, anchor_);
  if (clean.empty() && a == b) return;
  text_.replace(a, b - a, clean);
  caret_ = anchor_ = a + clean.size();
  edited();
}

void InputLine::deleteBackward(bool word) {
  if (!hasSelection()) {
    const size_t to = word ? prevWord(caret_) : prevCp(text_, caret_);
    if (to == caret_) return;
    anchor_ = to;
  }
  replaceSelection("");
}

void InputLine::deleteForward(bool word) {
  if (!hasSelection()) {
    const size_t to = word ? nextWord(caret_) : nextCp(text_, caret_);
    if (to == caret_) return;
    anchor_ = to;
  }
  replaceSelection("");
}

float InputLine::textLeft() const { return bounds().x + theme().padding; }

float InputLine::caretX() const { return root()->text().measure(std::string_view(text_).substr(0, caret_)); }

size_t InputLine::offsetAt(float x) const {
  const float rel = x - textLeft() + scrollX_;
  size_t best = 0;
  float bestD = 1e30f;
  for (size_t i = 0;; i = nextCp(text_, i)) {
    const float d = std::abs(root()->text().measure(std::string_view(text_).substr(0, i)) - rel);
    if (d < bestD) bestD = d, best = i;
    if (i >= text_.size()) break;
  }
  return best;
}

bool InputLine::onEvent(const InputEvent& e) {
  const bool shift = e.mods & ModShift, ctrl = e.mods & ModCtrl;
  switch (e.type) {
    case InputType::Text: replaceSelection(e.text); return true;
    case InputType::MouseDown:
      if (e.button != MouseButton::Left) return false;
      if (e.clicks >= 3) selectAll();
      else if (e.clicks == 2) {
        const size_t at = offsetAt(e.x);
        anchor_ = prevWord(nextWord(at));
        caret_ = nextWord(anchor_);
        markDirty();
      } else setCaret(offsetAt(e.x), shift);
      captureMouse();
      return true;
    case InputType::MouseMove:
      if (hasCapture()) setCaret(offsetAt(e.x), true);
      return hasCapture();
    case InputType::KeyDown: break;
    default: return false;
  }
  switch (e.key) {
    case Key::Left:
      if (hasSelection() && !shift) setCaret(std::min(caret_, anchor_));
      else setCaret(ctrl ? prevWord(caret_) : prevCp(text_, caret_), shift);
      return true;
    case Key::Right:
      if (hasSelection() && !shift) setCaret(std::max(caret_, anchor_));
      else setCaret(ctrl ? nextWord(caret_) : nextCp(text_, caret_), shift);
      return true;
    case Key::Home:
    case Key::End:
      if (ctrl) return false;  // Ctrl+Home/End belong to the list behind the field
      setCaret(e.key == Key::Home ? 0 : text_.size(), shift);
      return true;
    case Key::Backspace: deleteBackward(ctrl); return true;
    case Key::Delete: deleteForward(ctrl); return true;
    default: break;
  }
  if (ctrl && !shift) {
    switch (static_cast<uint32_t>(e.key)) {
      case 'A': selectAll(); return true;
      case 'C':
        if (hasSelection()) clipboardSet(selectedText());
        return true;
      case 'X':
        if (hasSelection() && clipboardSet(selectedText())) replaceSelection("");
        return true;
      case 'V': replaceSelection(clipboardGet()); return true;
      default: break;
    }
  }
  return false;
}

void InputLine::paint(PaintContext& ctx) {
  const RectF& b = bounds();
  ctx.fill(b, ctx.theme.panelBackground);
  const float left = textLeft(), inner = b.w - 2 * ctx.theme.padding;
  const float cx = caretX();
  if (cx - scrollX_ > inner - 2) scrollX_ = cx - inner + 2;
  if (cx - scrollX_ < 0) scrollX_ = cx;
  if (scrollX_ < 0 || ctx.measure(text_) <= inner) scrollX_ = 0;
  const RectF clip{left, b.y, std::max(0.0f, inner), b.h};
  const float ty = b.y + (b.h - ctx.lineHeight()) / 2;
  if (hasSelection()) {
    const size_t a = std::min(caret_, anchor_), z = std::max(caret_, anchor_);
    const float x0 = ctx.measure(std::string_view(text_).substr(0, a));
    const float x1 = ctx.measure(std::string_view(text_).substr(0, z));
    ctx.fill(intersect({left + x0 - scrollX_, ty, x1 - x0, ctx.lineHeight()}, clip), ctx.theme.selection);
  }
  if (text_.empty()) ctx.text(left, ty, placeholder_, ctx.theme.textDim, clip);
  else ctx.text(left - scrollX_, ty, text_, ctx.theme.text, clip);
}

void InputLine::paintOverlay(PaintContext& ctx) {
  if (!hasFocus()) return;
  const RectF& b = bounds();
  const float x = textLeft() + caretX() - scrollX_;
  ctx.fill({x, b.y + (b.h - ctx.lineHeight()) / 2, 1.5f, ctx.lineHeight()}, ctx.theme.caret);
}

}  // namespace tf::ui
