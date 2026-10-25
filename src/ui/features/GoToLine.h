#pragma once
#include "ui/features/PickerUtil.h"

namespace tf::ui {

// Go to line (Ctrl+G, B8): a one-row modal ListPopup. Typing "42" or "42:7" (line:column, 1-based) shows the target;
// Enter jumps through Editor::gotoLine, i.e. Editor::gotoLocation, so the jump is in the navigation history. Numbers
// beyond the document are clamped to its last line.
class GoToLine {
 public:
  explicit GoToLine(PickerEnv& env) : env_(env) {}
  void open();
  void close();
  bool isOpen() const { return popup_ != nullptr; }
  ListPopup* popup() const { return popup_; }
  // "42", "42:7" -> line, column (column 1 when omitted); false for anything else.
  static bool parse(const std::string& text, int& line, int& column);

 private:
  void update(const std::string& text);

  PickerEnv& env_;
  ListPopup* popup_ = nullptr;
  int line_ = 0, column_ = 1;
};

}  // namespace tf::ui
