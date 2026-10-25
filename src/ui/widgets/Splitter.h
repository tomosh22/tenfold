#pragma once
#include "ui/widgets/Widget.h"

namespace tf::ui {

// Two children separated by a draggable divider. `ratio` (0..1) is the first child's share of the space left after the
// divider; layout clamps it so both children keep at least their minimum size (if the bounds are too small for both
// minimums the first wins). Dragging stores the clamped ratio. Double-click on the divider resets to 50%.
// Horizontal = children side by side (divider is a vertical bar); Vertical = children stacked.
class Splitter : public Widget {
 public:
  enum class Orientation { Horizontal, Vertical };
  explicit Splitter(Orientation o = Orientation::Horizontal) : orient_(o) {}

  // Takes ownership; may be called once. Either child may be a Panel, another Splitter, ...
  void setChildren(std::unique_ptr<Widget> first, std::unique_ptr<Widget> second);
  Widget* first() const { return children().size() > 0 ? children()[0].get() : nullptr; }
  Widget* second() const { return children().size() > 1 ? children()[1].get() : nullptr; }

  Orientation orientation() const { return orient_; }
  float ratio() const { return ratio_; }
  void setRatio(float r);
  void setMinSizes(float first, float second);
  RectF dividerRect() const;  // window coordinates

  bool onEvent(const InputEvent& e) override;

 protected:
  void paint(PaintContext& ctx) override;
  void onLayout() override;

 private:
  float extent() const { return orient_ == Orientation::Horizontal ? bounds().w : bounds().h; }
  float thickness() const { return theme().splitterWidth; }
  float clampedRatio(float r) const;

  Orientation orient_;
  float ratio_ = 0.5f, minFirst_ = 80, minSecond_ = 80;
  bool dragging_ = false;
  float grab_ = 0;  // pointer offset inside the divider at press
};

}  // namespace tf::ui
