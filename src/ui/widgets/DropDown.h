#pragma once
#include <functional>
#include <string>
#include <vector>

#include "ui/widgets/Widget.h"

namespace tf::ui {

// One entry of a DropDown. A disabled entry is listed (dim) but can be neither chosen nor highlighted.
struct DropDownItem {
  std::string label;
  bool enabled = true;
};

// A drop-down list (combo box without text entry), like the one in Visual Studio's dialogs: a box showing the chosen
// entry and an arrow; opening it lists all entries below the box (above it when there is no room below), inside the
// window. Focusable.
//
// Keys, closed: Up / Down choose the previous / next enabled entry (onChanged fires), Home / End the first / last,
// Alt+Down, F4 and Space open the list, Enter opens it too while `enterOpens` (default; a dialog turns it off so
// Enter reaches its default button, as in Windows). Keys, open: Up / Down / Home / End / PageUp / PageDown move the
// highlight over the enabled entries, Enter / Space / Alt+Up choose it, Escape closes without choosing (and is
// consumed, so a dialog around it stays open). Tab closes the list and bubbles. Mouse: a click on the box toggles the
// list, a click on an enabled entry chooses it, hovering highlights, a click on a disabled entry does nothing. The
// list also closes when the widget loses the focus (a click elsewhere in a dialog).
//
// The list is painted beyond the widget's bounds: hitRect() covers it. Text underneath it that is painted by an
// earlier widget would show through (text is drawn after all rects), so a parent hides what the list covers while
// isOpen() (listRect()).
class DropDown : public Widget {
 public:
  using ChangedFn = std::function<void(int index)>;

  void setItems(std::vector<DropDownItem> items, int selected = 0);
  const std::vector<DropDownItem>& items() const { return items_; }
  void setEnabled(int index, bool enabled);
  int selected() const { return selected_; }
  std::string selectedLabel() const;
  void setSelected(int index);  // programmatic: does not call onChanged; ignores disabled / out of range entries
  void onChanged(ChangedFn fn) { changed_ = std::move(fn); }  // user choices only
  void setEnterOpens(bool on) { enterOpens_ = on; }

  bool isOpen() const { return open_; }
  void open();
  void closeList();
  int highlighted() const { return highlight_; }  // while open

  // Natural size: the widest label plus the arrow, one text line plus padding high.
  float preferredWidth() const;
  float preferredHeight() const;
  // Geometry in window pixels (valid after layout; the list rect is empty while closed).
  RectF listRect() const;
  RectF itemRect(int i) const;

  RectF hitRect() const override;
  bool acceptsFocus() const override { return true; }
  bool onEvent(const InputEvent& e) override;
  void onFocusChanged(bool gained) override;
  void onHover(bool entered) override;

 protected:
  void paint(PaintContext& ctx) override;

 private:
  float rowHeight() const;
  int itemAt(float x, float y) const;
  int step(int from, int dir) const;  // next enabled entry after `from` in `dir`, -1 if none
  int firstEnabled() const { return step(-1, 1); }
  int lastEnabled() const { return step(static_cast<int>(items_.size()), -1); }
  void choose(int index);

  std::vector<DropDownItem> items_;
  int selected_ = -1, highlight_ = -1;
  bool open_ = false, enterOpens_ = true, hoverBox_ = false;
  ChangedFn changed_;
};

}  // namespace tf::ui
