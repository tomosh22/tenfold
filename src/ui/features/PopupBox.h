#pragma once
#include <functional>
#include <string>
#include <vector>

#include "ui/widgets/Widget.h"

namespace tf::ui {

// One run of text in a PopupBox line.
struct PopupSpan {
  enum class Style { Text, Dim, Hit, Error, Warn, Info };
  std::string text;
  Style style = Style::Text;
  bool mark = false;  // drawn on a highlighted background (the active signature parameter)
};

struct PopupLine {
  std::vector<PopupSpan> spans;
  std::string right;  // dim, right aligned (completion detail / return type)
  bool selected = false;
};

// Non-modal top-layer box for completion, hover and signature help (B5b): lines of styled spans, an optional side pane
// on the right (documentation), anchored under a rectangle (the caret or the hovered word) or above it when there is no
// room. It never takes focus; only mouse events inside the box reach it (a click on a line fires onPick).
class PopupBox : public Widget {
 public:
  PopupBox();
  // Replaces the content. The first visible line follows `selected` (-1 = none). Rows beyond maxRows scroll.
  void setContent(std::vector<PopupLine> lines, int selected = -1, std::vector<std::string> side = {});
  void setMaxRows(int n) { maxRows_ = n; }
  void showAt(const RectF& anchor);  // below `anchor`, above it when the window has no room; makes it visible
  void hide();
  RectF box() const { return box_; }
  const std::vector<PopupLine>& lines() const { return lines_; }
  const std::vector<std::string>& side() const { return side_; }
  int firstRow() const { return first_; }
  void onPick(std::function<void(int)> fn) { pick_ = std::move(fn); }
  // Word wrap to at most `cols` columns (long words are cut); keeps existing line breaks.
  static std::vector<std::string> wrap(const std::string& text, size_t cols);

  bool onEvent(const InputEvent& e) override;

 protected:
  void onLayout() override;
  void paint(PaintContext& ctx) override;

 private:
  float rowH() const;
  void measure();

  std::vector<PopupLine> lines_;
  std::vector<std::string> side_;
  int selected_ = -1, first_ = 0, maxRows_ = 10;
  RectF anchor_{0, 0, 0, 0}, box_{0, 0, 0, 0};
  float listW_ = 0, sideW_ = 0;
  bool inLayout_ = false;
  std::function<void(int)> pick_;
};

}  // namespace tf::ui
