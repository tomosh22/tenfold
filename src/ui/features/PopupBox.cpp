#include "ui/features/PopupBox.h"

#include <algorithm>

namespace tf::ui {

PopupBox::PopupBox() {
  setTopLayer(true);
  setVisible(false);
}

float PopupBox::rowH() const { return root() ? root()->text().lineHeight() + 2 : 14; }

void PopupBox::setContent(std::vector<PopupLine> lines, int selected, std::vector<std::string> side) {
  lines_ = std::move(lines);
  side_ = std::move(side);
  selected_ = selected;
  const int n = static_cast<int>(lines_.size());
  first_ = std::clamp(first_, 0, std::max(0, n - maxRows_));
  if (selected_ >= 0) {
    if (selected_ < first_) first_ = selected_;
    if (selected_ >= first_ + maxRows_) first_ = selected_ - maxRows_ + 1;
  }
  if (root() && visible()) showAt(anchor_);
}

void PopupBox::measure() {
  gfx::TextRenderer& t = root()->text();
  const float pad = 8, cell = t.cellWidth();
  listW_ = 0;
  for (const PopupLine& l : lines_) {
    float w = 0;
    for (const PopupSpan& s : l.spans) w += t.measure(s.text);
    if (!l.right.empty()) w += t.measure(l.right) + 2 * cell;
    listW_ = std::max(listW_, w);
  }
  listW_ = std::min(listW_ + 2 * pad, cell * 100);
  sideW_ = 0;
  for (const std::string& s : side_) sideW_ = std::max(sideW_, t.measure(s));
  if (sideW_ > 0) sideW_ += 2 * pad;
}

void PopupBox::showAt(const RectF& anchor) {
  if (!root()) return;
  anchor_ = anchor;
  measure();
  const float rh = rowH();
  const RectF win = root()->bounds();
  const float w = win.w > 16 ? std::min(listW_ + sideW_, win.w - 8) : listW_ + sideW_;  // before the first layout
  const int rows = std::min<int>(maxRows_, static_cast<int>(lines_.size()));
  const float sideH = side_.empty() ? 0 : static_cast<float>(side_.size()) * rh + 8;
  const float h = std::max(static_cast<float>(rows) * rh + 4, sideH);
  const float x = std::clamp(anchor.x, win.x + 4, std::max(win.x + 4, win.x + win.w - w - 4));
  float y = anchor.y + anchor.h + 2;
  if (y + h > win.y + win.h && anchor.y - h - 2 >= win.y) y = anchor.y - h - 2;
  box_ = {x, y, w, h};
  setVisible(true);
  layout(box_);
  markDirty();
}

void PopupBox::hide() {
  if (!visible()) return;
  setVisible(false);
  markDirty();
}

void PopupBox::onLayout() {
  const RectF& b = bounds();
  if (inLayout_ || (b.x == box_.x && b.y == box_.y && b.w == box_.w && b.h == box_.h)) return;
  inLayout_ = true;  // RootWidget gives every child the full window; a popup keeps its own box
  layout(box_);
  inLayout_ = false;
}

bool PopupBox::onEvent(const InputEvent& e) {
  if (e.type == InputType::MouseDown && e.button == MouseButton::Left && ui::contains(box_, e.x, e.y)) {
    const int i = first_ + static_cast<int>((e.y - box_.y - 2) / rowH());
    if (e.x < box_.x + listW_ && i >= 0 && i < static_cast<int>(lines_.size()) && pick_) pick_(i);
    return true;
  }
  return e.type == InputType::MouseDown || e.type == InputType::MouseUp || e.type == InputType::Wheel;
}

static Color styleColor(const Theme& t, PopupSpan::Style s) {
  switch (s) {
    case PopupSpan::Style::Dim: return t.textDim;
    case PopupSpan::Style::Hit: return t.tabAccent;
    case PopupSpan::Style::Error: return t.diagnosticError;
    case PopupSpan::Style::Warn: return t.diagnosticWarning;
    case PopupSpan::Style::Info: return t.diagnosticInfo;
    default: return t.text;
  }
}

void PopupBox::paint(PaintContext& ctx) {
  const Theme& t = ctx.theme;
  ctx.fill({box_.x - 1, box_.y - 1, box_.w + 2, box_.h + 2}, Theme::orElse(t.popupBorder, t.splitterHot));
  ctx.fill(box_, Theme::orElse(t.popupBackground, t.tabInactive));
  const float rh = rowH(), lh = ctx.lineHeight(), pad = 8, ty = (rh - lh) / 2;
  const RectF listClip{box_.x, box_.y, std::min(box_.w, listW_), box_.h};
  for (int i = first_, row = 0; i < static_cast<int>(lines_.size()) && row < maxRows_; ++i, ++row) {
    const PopupLine& l = lines_[static_cast<size_t>(i)];
    const float y = box_.y + 2 + static_cast<float>(row) * rh;
    if (l.selected || i == selected_) ctx.fill({box_.x, y, listClip.w, rh}, t.selection);
    float x = box_.x + pad;
    float rightEdge = listClip.x + listClip.w - pad;
    if (!l.right.empty()) {
      const float w = ctx.measure(l.right);
      ctx.text(rightEdge - w, y + ty, l.right, t.textDim, listClip);
      rightEdge -= w + 2 * ctx.textRenderer.cellWidth();
    }
    const RectF clip{box_.x, y, std::max(0.0f, rightEdge - box_.x), rh};
    for (const PopupSpan& s : l.spans) {
      const float w = ctx.measure(s.text);
      if (s.mark) ctx.fill({x - 1, y + 1, w + 2, rh - 2}, t.bracketMatch);
      ctx.text(x, y + ty, s.text, styleColor(t, s.style), clip);
      if (s.mark) ctx.fill({x, y + rh - 2, w, 1}, t.tabAccent);
      x += w;
    }
  }
  if (!side_.empty()) {
    const float sx = box_.x + listW_;
    if (listW_ > 0) ctx.fill({sx, box_.y, 1, box_.h}, t.splitterHot);
    const RectF clip{sx, box_.y, box_.w - listW_, box_.h};
    for (size_t i = 0; i < side_.size(); ++i)
      ctx.text(sx + pad, box_.y + 4 + static_cast<float>(i) * rh + ty, side_[i], t.textDim, clip);
  }
}

std::vector<std::string> PopupBox::wrap(const std::string& text, size_t cols) {
  std::vector<std::string> out;
  size_t from = 0;
  for (;;) {
    size_t nl = text.find('\n', from);
    if (nl == std::string::npos) nl = text.size();
    std::string line = text.substr(from, nl - from);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    while (line.size() > cols) {
      size_t cut = line.rfind(' ', cols);
      if (cut == std::string::npos || cut == 0) cut = cols;
      while (cut > 0 && (static_cast<unsigned char>(line[cut]) & 0xC0) == 0x80) --cut;  // do not split UTF-8
      out.push_back(line.substr(0, cut));
      const size_t next = line.find_first_not_of(' ', cut);
      line.erase(0, next == std::string::npos ? line.size() : next);
    }
    out.push_back(line);
    if (nl == text.size()) break;
    from = nl + 1;
  }
  return out;
}

}  // namespace tf::ui
