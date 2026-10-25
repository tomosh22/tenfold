#pragma once
#include <functional>
#include <vector>

#include "ui/widgets/Widget.h"

namespace tf::ui {

// Vertical scrollbar of a TextView (B2b): a track with a draggable thumb and a marker lane. The owner feeds the
// scroll metrics (it stays the source of truth for the scroll position) and the marker lines; the bar reports user
// scrolling through onScroll. Click on the track outside the thumb pages toward the click; wheel events are not
// handled here (they bubble to the TextView). The track is split into three lanes, left to right: search matches,
// carets, diagnostics (error / warning / info colours). Markers are at least 2 px tall and drawn at most once per
// pixel row and lane.
class Scrollbar : public Widget {
 public:
  // BuildError / BuildWarning (C1) share the diagnostics lane and are drawn under the clangd marks.
  enum class Lane { Search, Caret, Error, Warning, Info, BuildError, BuildWarning };

  // scrollRange = maximum scroll offset, viewH = visible height, both in pixels; lines = document line count.
  void setMetrics(float scrollRange, float viewH, float scrollY, int lines);
  void setMarkers(Lane lane, std::vector<int> lines);  // 0-based document lines, any order
  std::function<void(float scrollY, bool animate)> onScroll;

  RectF thumbRect() const;  // empty (w == 0) when there is nothing to scroll
  // Marker rectangle for a line in `lane` (what paint draws), for tests and hit-testing.
  RectF markerRect(Lane lane, int line) const;
  bool dragging() const { return drag_; }

  bool onEvent(const InputEvent& e) override;

 protected:
  void paint(PaintContext& ctx) override;

 private:
  float scrollFor(float thumbY) const;
  float range_ = 0, viewH_ = 0, scrollY_ = 0, dragOff_ = 0;
  int lines_ = 1;
  bool drag_ = false;
  std::vector<int> marks_[7];
};

}  // namespace tf::ui
