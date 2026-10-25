#pragma once

namespace tf::ui {

// Scroll state of a text view: pixel offsets of the top-left corner of the visible text area, plus the smooth-scroll
// animation toward a target. Pure value logic (no widgets, no time source): the owner calls step() with elapsed ms.
class Viewport {
 public:
  float x() const { return x_; }
  float y() const { return y_; }
  float targetX() const { return tx_; }
  float targetY() const { return ty_; }
  // First line that is at least partly visible, for a uniform line height.
  int firstLine(float lineHeight) const { return y_ <= 0 ? 0 : static_cast<int>(y_ / lineHeight); }

  // Move the target; with animate == false the view jumps there at once.
  void scrollTo(float x, float y, bool animate);
  void jumpTo(float x, float y) { scrollTo(x, y, false); }
  bool animating() const { return x_ != tx_ || y_ != ty_; }
  // Advance the animation by dtMs (exponential approach, 60 ms time constant, snaps within half a pixel).
  // Returns true if the position changed.
  bool step(float dtMs);

 private:
  float x_ = 0, y_ = 0, tx_ = 0, ty_ = 0;
};

}  // namespace tf::ui
