#include "ui/widgets/Splitter.h"

#include <algorithm>

namespace tf::ui {

void Splitter::setChildren(std::unique_ptr<Widget> a, std::unique_ptr<Widget> b) {
  addChild(std::move(a));
  addChild(std::move(b));
}

std::unique_ptr<Widget> Splitter::replaceChild(Widget* old, std::unique_ptr<Widget> nw) {
  if (old != first() && old != second()) return nullptr;
  const bool isFirst = old == first();
  auto a = removeChild(first());
  auto b = removeChild(first());
  std::swap(isFirst ? a : b, nw);  // nw now holds the old child
  addChild(std::move(a));
  addChild(std::move(b));
  return nw;
}

float Splitter::clampedRatio(float r) const {
  const float avail = extent() - thickness();
  if (avail <= 0) return 0.5f;
  const float hi = std::max(minFirst_, avail - minSecond_);
  const float first = std::clamp(r * avail, std::min(minFirst_, avail), std::min(hi, avail));
  return first / avail;
}

void Splitter::setRatio(float r) {
  r = std::clamp(r, 0.0f, 1.0f);
  if (r == ratio_) return;
  ratio_ = r;
  if (RootWidget* rt = root()) rt->requestLayout();
}

void Splitter::setMinSizes(float a, float b) {
  minFirst_ = a;
  minSecond_ = b;
  if (RootWidget* rt = root()) rt->requestLayout();
}

RectF Splitter::dividerRect() const {
  const RectF& b = bounds();
  const float first = clampedRatio(ratio_) * (extent() - thickness());
  return orient_ == Orientation::Horizontal ? RectF{b.x + first, b.y, thickness(), b.h}
                                            : RectF{b.x, b.y + first, b.w, thickness()};
}

void Splitter::onLayout() {
  if (!first() || !second()) return;
  const RectF &b = bounds(), d = dividerRect();
  if (orient_ == Orientation::Horizontal) {
    first()->layout({b.x, b.y, d.x - b.x, b.h});
    second()->layout({d.x + d.w, b.y, b.x + b.w - d.x - d.w, b.h});
  } else {
    first()->layout({b.x, b.y, b.w, d.y - b.y});
    second()->layout({b.x, d.y + d.h, b.w, b.y + b.h - d.y - d.h});
  }
}

bool Splitter::onEvent(const InputEvent& e) {
  const bool horiz = orient_ == Orientation::Horizontal;
  switch (e.type) {
    case InputType::MouseDown: {
      RectF d = dividerRect();
      const float slop = 2;  // grab a little outside a thin divider
      (horiz ? d.x : d.y) -= slop;
      (horiz ? d.w : d.h) += 2 * slop;
      if (e.button != MouseButton::Left || !ui::contains(d, e.x, e.y)) return false;
      if (e.clicks >= 2) {
        setRatio(0.5f);  // layout clamps the displayed position if the minimum sizes require it
        return true;
      }
      dragging_ = true;
      grab_ = (horiz ? e.x - dividerRect().x : e.y - dividerRect().y);
      captureMouse();
      markDirty();
      return true;
    }
    case InputType::MouseMove: {
      if (!dragging_) return false;
      const float start = horiz ? bounds().x : bounds().y;
      const float avail = extent() - thickness();
      if (avail <= 0) return true;
      const float pos = (horiz ? e.x : e.y) - grab_ - start;
      ratio_ = clampedRatio(pos / avail);
      if (RootWidget* rt = root()) rt->requestLayout();
      return true;
    }
    case InputType::MouseUp:
      if (!dragging_) return false;
      dragging_ = false;
      markDirty();
      return true;
    default: return false;
  }
}

void Splitter::paint(PaintContext& ctx) {
  ctx.fill(dividerRect(), dragging_ ? ctx.theme.splitterHot : ctx.theme.splitter);
}

}  // namespace tf::ui
