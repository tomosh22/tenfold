#pragma once
#include <functional>
#include <string>
#include <vector>

#include "ui/widgets/Widget.h"

namespace tf::ui {

// A single-line bar of text segments identified by string keys ("pos" -> "Ln 12, Col 5", "encoding" -> "UTF-8",
// "eol", "clangd", "fastindex", "workspace", ...). Left segments are laid out from the left edge in the order they were
// first declared, right segments from the right edge (the first declared right segment is outermost). A segment with
// empty text takes no space. A segment with a click callback highlights on hover and calls it on left click.
class StatusBar : public Widget {
 public:
  enum class Align { Left, Right };

  // Declares a segment (fixes its order and alignment); a no-op for an existing key apart from updating the callback.
  void addSegment(const std::string& key, Align align, std::function<void(const std::string& key)> onClick = {});
  // Sets the text; an undeclared key is declared as a Left segment.
  void setText(const std::string& key, std::string text);
  const std::string& text(const std::string& key) const;
  void setOnClick(const std::string& key, std::function<void(const std::string& key)> onClick);

  // While true (debugger Running / Break) the bar uses Theme::statusBackgroundDebugging / statusTextDebugging when the
  // theme sets them.
  void setDebugging(bool on);
  bool debugging() const { return debugging_; }

  float preferredHeight() const;
  // Window-coordinate rect of a segment as of the last layout/paint (zero-size if empty or unknown).
  RectF segmentRect(const std::string& key);

  bool onEvent(const InputEvent& e) override;
  void onHover(bool entered) override;

 protected:
  void paint(PaintContext& ctx) override;
  void onLayout() override { markStale(); }

 private:
  struct Segment {
    std::string key, text;
    Align align;
    std::function<void(const std::string&)> onClick;
    RectF rect{0, 0, 0, 0};
  };
  Segment* find(const std::string& key);
  const Segment* find(const std::string& key) const;
  void markStale() { stale_ = true; }
  void place();
  Segment* segmentAt(float x, float y);

  std::vector<Segment> segs_;
  bool stale_ = true, debugging_ = false;
  Segment* hot_ = nullptr;
};

}  // namespace tf::ui
