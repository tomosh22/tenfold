#include "ui/view/Scrollbar.h"

#include <algorithm>
#include <cmath>

namespace tf::ui {

void Scrollbar::setMetrics(float range, float viewH, float scrollY, int lines) {
  lines = std::max(1, lines);
  if (range == range_ && viewH == viewH_ && scrollY == scrollY_ && lines == lines_) return;
  range_ = range;
  viewH_ = viewH;
  scrollY_ = scrollY;
  lines_ = lines;
  markDirty();
}

void Scrollbar::setMarkers(Lane lane, std::vector<int> lines) {
  std::sort(lines.begin(), lines.end());
  std::vector<int>& m = marks_[static_cast<int>(lane)];
  if (m == lines) return;
  m = std::move(lines);
  markDirty();
}

RectF Scrollbar::thumbRect() const {
  const RectF& b = bounds();
  if (range_ <= 0 || b.h <= 0) return {b.x, b.y, 0, 0};
  const float h = std::clamp(viewH_ / (range_ + viewH_) * b.h, std::min(24.0f, b.h), b.h);
  return {b.x, b.y + std::clamp(scrollY_ / range_, 0.0f, 1.0f) * (b.h - h), b.w, h};
}

float Scrollbar::scrollFor(float thumbY) const {
  const RectF t = thumbRect(), &b = bounds();
  const float free = b.h - t.h;
  return free <= 0 ? 0 : std::clamp((thumbY - b.y) / free, 0.0f, 1.0f) * range_;
}

RectF Scrollbar::markerRect(Lane lane, int line) const {
  const RectF& b = bounds();
  const float lw = b.w / 3, y = std::floor(b.y + (static_cast<float>(line) + 0.5f) / static_cast<float>(lines_) * b.h);
  const float my = std::min(y, b.y + b.h - 2);
  if (lane >= Lane::DiffAdded) return {b.x, my, std::max(2.0f, std::floor(lw * 0.6f)), 2};
  const int l = lane == Lane::Search ? 0 : lane == Lane::Caret ? 1 : 2;
  return {b.x + lw * static_cast<float>(l), my, lw, 2};
}

void Scrollbar::paint(PaintContext& ctx) {
  const Theme& th = ctx.theme;
  ctx.fill(bounds(), th.scrollbarTrack);
  const Color colors[kLanes] = {th.matchMarker,    th.textDim,     th.diagnosticError, th.diagnosticWarning,
                                th.diagnosticInfo, th.buildError,  th.buildWarning,    th.vcsAdded(),
                                th.vcsModified(),  th.vcsDeleted()};
  // Later ones on top: changes, then search, build, clangd info / warnings / errors, carets.
  for (int lane : {9, 8, 7, 0, 6, 5, 4, 3, 2, 1}) {
    float lastY = -1e9f;
    for (int line : marks_[lane]) {
      const RectF r = markerRect(static_cast<Lane>(lane), line);
      if (r.y == lastY) continue;
      lastY = r.y;
      ctx.fill(r, colors[lane]);
    }
  }
  const RectF t = thumbRect();
  if (t.w > 0) ctx.fill(t, th.scrollbarThumb);
}

bool Scrollbar::onEvent(const InputEvent& e) {
  switch (e.type) {
    case InputType::MouseDown: {
      if (e.button != MouseButton::Left) return false;
      const RectF t = thumbRect();
      if (t.w <= 0) return true;
      if (ui::contains(t, e.x, e.y)) {
        drag_ = true;
        dragOff_ = e.y - t.y;
        captureMouse();
      } else if (onScroll) {  // page toward the click
        const float page = std::max(viewH_ * 0.9f, 1.0f);
        onScroll(std::clamp(scrollY_ + (e.y < t.y ? -page : page), 0.0f, range_), true);
      }
      return true;
    }
    case InputType::MouseMove:
      if (!drag_) return false;
      if (onScroll) onScroll(scrollFor(e.y - dragOff_), false);
      return true;
    case InputType::MouseUp: drag_ = false; return e.button == MouseButton::Left;
    default: return false;
  }
}

}  // namespace tf::ui
