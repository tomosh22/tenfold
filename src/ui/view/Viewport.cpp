#include "ui/view/Viewport.h"

#include <cmath>

namespace tf::ui {

void Viewport::scrollTo(float x, float y, bool animate) {
  tx_ = x;
  ty_ = y;
  if (!animate) {
    x_ = x;
    y_ = y;
  }
}

bool Viewport::step(float dtMs) {
  if (!animating()) return false;
  const float k = 1.0f - std::exp(-dtMs / 60.0f);
  auto approach = [k](float& cur, float target) {
    cur += (target - cur) * k;
    if (std::fabs(target - cur) < 0.5f) cur = target;
  };
  approach(x_, tx_);
  approach(y_, ty_);
  return true;
}

}  // namespace tf::ui
