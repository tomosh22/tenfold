#include "ui/widgets/LinesView.h"

#include <algorithm>
#include <cmath>

namespace tf::ui {

bool LinesView::onEvent(const InputEvent& e) {
  if (e.type != InputType::Wheel || !root()) return false;
  const float lh = root()->text().lineHeight();
  const float maxScroll = std::max(0.0f, static_cast<float>(lines_->size()) * lh - bounds().h * 0.5f);
  const float next = std::clamp(scroll_ - e.dy * 3 * lh, 0.0f, maxScroll);
  if (next != scroll_) {
    scroll_ = next;
    markDirty();
  }
  return true;
}

void LinesView::paint(PaintContext& ctx) {
  const RectF& b = bounds();
  const Theme& th = ctx.theme;
  const float lh = ctx.lineHeight(), cw = ctx.textRenderer.cellWidth();
  int digits = 3;
  for (size_t n = lines_->size(); n >= 1000; n /= 10) ++digits;
  const float gutterW = (static_cast<float>(digits) + 2) * cw, textX = b.x + gutterW + cw / 2;
  ctx.fill(b, th.panelBackground);
  ctx.fill({b.x, b.y, gutterW, b.h}, th.gutterBackground);
  const float off = std::floor(scroll_);
  const size_t first = static_cast<size_t>(off / lh),
               last = std::min(lines_->size(), static_cast<size_t>((off + b.h) / lh) + 1);
  const RectF textClip = intersect({textX, b.y, b.x + b.w - textX, b.h}, b),
              gutterClip = intersect({b.x, b.y, gutterW, b.h}, b);
  for (size_t i = first; i < last; ++i) {
    const float y = b.y + static_cast<float>(i) * lh - off;
    number_ = std::to_string(i + 1);
    ctx.text(b.x + gutterW - cw - static_cast<float>(number_.size()) * cw, y, number_, th.gutterText, gutterClip);
    ctx.text(textX, y, (*lines_)[i], th.text, textClip);
  }
}

}  // namespace tf::ui
