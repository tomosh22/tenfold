#include "ui/widgets/CheckBox.h"

#include <algorithm>
#include <cmath>

namespace tf::ui {

float CheckBox::boxSize() const { return root() ? std::floor(root()->text().lineHeight() * 0.85f) : 14.0f; }

float CheckBox::preferredHeight() const {
  return (root() ? root()->text().lineHeight() : 16.0f) + 2 * theme().verticalPad;
}

float CheckBox::preferredWidth() const {
  const float cell = root() ? root()->text().cellWidth() : 8.0f;
  return boxSize() + cell + (root() ? root()->text().measure(label_) : 0.0f) + 2;
}

RectF CheckBox::boxRect() const {
  const RectF& b = bounds();
  const float s = boxSize();
  return {b.x + 1, b.y + (b.h - s) / 2, s, s};
}

void CheckBox::setLabel(std::string s) {
  label_ = std::move(s);
  markDirty();
}

void CheckBox::setChecked(bool on) {
  if (on == checked_) return;
  checked_ = on;
  markDirty();
}

void CheckBox::toggle() {
  checked_ = !checked_;
  markDirty();
  if (changed_) changed_(checked_);
}

void CheckBox::onHover(bool entered) {
  if (hover_ == entered) return;
  hover_ = entered;
  markDirty();
}

bool CheckBox::onEvent(const InputEvent& e) {
  switch (e.type) {
    case InputType::KeyDown:
      if (e.key == Key::Space && !(e.mods & (ModCtrl | ModAlt | ModSuper))) {
        toggle();
        return true;
      }
      return false;
    case InputType::MouseDown:
      if (e.button != MouseButton::Left) return false;
      toggle();
      return true;
    case InputType::MouseUp: return true;
    default: return false;
  }
}

void CheckBox::paint(PaintContext& ctx) {
  const Theme& t = ctx.theme;
  const RectF r = boxRect();
  ctx.fill(r, Theme::orElse(t.inputBackground, t.panelBackground));
  const Color border = hasFocus() ? t.tabAccent : hover_ ? t.text : Theme::orElse(t.inputBorder, t.textDim);
  ctx.fill({r.x, r.y, r.w, 1}, border);
  ctx.fill({r.x, r.y + r.h - 1, r.w, 1}, border);
  ctx.fill({r.x, r.y, 1, r.h}, border);
  ctx.fill({r.x + r.w - 1, r.y, 1, r.h}, border);
  if (checked_) {  // tick
    const float x = r.x, y = r.y, s = r.w;
    ctx.line(x + s * 0.22f, y + s * 0.52f, x + s * 0.43f, y + s * 0.74f, 1.8f, t.text);
    ctx.line(x + s * 0.43f, y + s * 0.74f, x + s * 0.80f, y + s * 0.28f, 1.8f, t.text);
  }
  const RectF& b = bounds();
  const float tx = r.x + r.w + ctx.textRenderer.cellWidth();
  ctx.text(tx, b.y + (b.h - ctx.lineHeight()) / 2, label_, t.text, {tx, b.y, std::max(0.0f, b.x + b.w - tx), b.h});
}

}  // namespace tf::ui
