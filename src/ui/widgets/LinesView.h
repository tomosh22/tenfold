#pragma once
#include <memory>
#include <string>
#include <vector>

#include "ui/widgets/Widget.h"

namespace tf::ui {

// Placeholder content widget: read-only lines with a line-number gutter and wheel scrolling, drawn through the
// deferred text path. B2's TextView replaces it; it exists so panels have something real to host.
class LinesView : public Widget {
 public:
  explicit LinesView(std::shared_ptr<const std::vector<std::string>> lines) : lines_(std::move(lines)) {}
  float scrollY() const { return scroll_; }
  bool onEvent(const InputEvent& e) override;

 protected:
  void paint(PaintContext& ctx) override;

 private:
  std::shared_ptr<const std::vector<std::string>> lines_;
  float scroll_ = 0;
  std::string number_;
};

}  // namespace tf::ui
