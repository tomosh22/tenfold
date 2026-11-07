#pragma once
#include <functional>
#include <string>

#include "ui/widgets/Widget.h"

namespace tf::ui {

// A check box with a text label, like the ones in Visual Studio's dialogs. Focusable. Space toggles it; a left click
// on the box or on the label toggles it (and gives it the focus). onChanged fires for user changes only.
class CheckBox : public Widget {
 public:
  using ChangedFn = std::function<void(bool checked)>;

  void setLabel(std::string s);
  const std::string& label() const { return label_; }
  bool checked() const { return checked_; }
  void setChecked(bool on);  // programmatic: does not call onChanged
  void toggle();             // as a user would: flips and calls onChanged
  void onChanged(ChangedFn fn) { changed_ = std::move(fn); }

  float preferredWidth() const;  // box + gap + label
  float preferredHeight() const;
  RectF boxRect() const;  // the square (valid after layout)

  bool acceptsFocus() const override { return true; }
  bool onEvent(const InputEvent& e) override;
  void onFocusChanged(bool) override { markDirty(); }
  void onHover(bool entered) override;

 protected:
  void paint(PaintContext& ctx) override;

 private:
  float boxSize() const;
  std::string label_;
  bool checked_ = false, hover_ = false;
  ChangedFn changed_;
};

}  // namespace tf::ui
