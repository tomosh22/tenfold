#include "ui/view/Layout.h"

#include <algorithm>
#include <cmath>

namespace tf::ui {

float Layout::gutterWidth(int lineCount) const {
  int digits = 3;
  for (int n = lineCount; n >= 1000; n /= 10) ++digits;
  return static_cast<float>(digits + 2) * cellWidth();
}

float Layout::diffStripWidth() const { return std::max(3.0f, std::round(cellWidth() * 0.4f)); }

int Layout::lineAt(float yInView, float scrollY, int lineCount) const {
  const int l = static_cast<int>(std::floor((yInView + scrollY) / lineHeight()));
  return std::clamp(l, 0, std::max(0, lineCount - 1));
}

void Layout::visibleLines(float scrollY, float heightPx, int lineCount, int& first, int& last) const {
  first = std::clamp(static_cast<int>(std::floor(scrollY / lineHeight())), 0, lineCount);
  last = std::clamp(static_cast<int>(std::floor((scrollY + heightPx) / lineHeight())) + 1, first, lineCount);
}

}  // namespace tf::ui
