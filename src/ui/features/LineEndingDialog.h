#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ui/widgets/CheckBox.h"
#include "ui/widgets/DropDown.h"
#include "ui/widgets/Widget.h"

namespace tf::ui {

// Visual Studio's "The line endings in the following files are not consistent" dialog (VS_BEHAVIOUR L4), a modal
// message box laid out like VS's:
//
//   Line Endings                                               (caption)
//   The line endings in the following files are not consistent. Do you want to normalize the line endings?
//   +------------------------------------------------------+
//   | src/a.cpp                          3 CR LF, 1 LF     |   the files (scrolls with the wheel past 6 rows)
//   +------------------------------------------------------+
//   Normalize line endings:  [ Windows (CR LF)         v ]
//   [x] Do not show this message again
//                                              [ Yes ] [ No ]
//
// Keys: Tab / Shift+Tab move the focus over drop-down, check box, Yes and No (Yes has it first); Space toggles the
// check box or presses the focused button; Alt+Down / F4 / Space open the drop-down (Enter presses the default button
// instead, as in Windows dialogs), Up / Down choose in it; Enter = Yes (No when No has the focus); Escape = No. Mouse:
// the two buttons, the drop-down and the check box; a click outside the dialog does nothing (it is modal), the wheel
// scrolls a long file list. The dialog is only the widget; LineEndingPrompt fills it and acts on the answer.
class LineEndingDialog : public Widget {
 public:
  struct File {
    std::string label;   // path shown on the left
    std::string detail;  // dim, right aligned ("3 CR LF, 1 LF")
  };
  // Called once with the answer. `entry` = the drop-down's selected index, `doNotShowAgain` = the check box.
  using AnswerFn = std::function<void(bool yes, int entry, bool doNotShowAgain)>;

  LineEndingDialog();

  void setCaption(std::string s);
  void setMessage(std::string s);
  void setFiles(std::vector<File> files);
  void setLabel(std::string s);  // text in front of the drop-down
  DropDown* dropDown() const { return dropDown_; }
  CheckBox* checkBox() const { return checkBox_; }
  const std::string& message() const { return message_; }
  const std::vector<File>& files() const { return files_; }

  void onAnswer(AnswerFn fn) { answer_ = std::move(fn); }
  void onClosed(std::function<void()> fn) { closed_ = std::move(fn); }  // after an answer, or a close()

  // Shows `d` on `root` as the modal widget (Yes has the focus) and returns it; ownership moves to the tree.
  static LineEndingDialog* open(RootWidget& root, std::unique_ptr<LineEndingDialog> d);
  // Closes the modal and fires onClosed once, without an answer; no-op when it is not shown.
  void close();
  // Answers as the buttons do (closes first, then calls onAnswer).
  void answer(bool yes);

  // Which control has the keyboard focus: 0 drop-down, 1 check box, 2 Yes, 3 No.
  int focusIndex() const;
  void focusControl(int index);

  // Geometry in window pixels (valid after layout).
  RectF dialogRect() const { return dialog_; }
  RectF fileListRect() const { return list_; }
  RectF yesRect() const { return yes_; }
  RectF noRect() const { return no_; }
  int visibleFileRows() const { return kMaxFileRows; }
  int firstFileRow() const { return firstFile_; }
  const std::vector<std::string>& messageLines() const { return msgLines_; }

  bool onEvent(const InputEvent& e) override;
  bool acceptsFocus() const override { return true; }
  void onFocusChanged(bool) override { markDirty(); }

 protected:
  void onLayout() override;
  void paint(PaintContext& ctx) override;

 private:
  static constexpr int kMaxFileRows = 6;
  void press(int button);  // 2 Yes, 3 No
  bool coveredByList(const RectF& r) const;
  void paintButton(PaintContext& ctx, const RectF& r, const char* text, int idx);

  std::string caption_ = "Line Endings", message_, label_ = "Normalize line endings:";
  std::vector<File> files_;
  DropDown* dropDown_ = nullptr;
  CheckBox* checkBox_ = nullptr;
  AnswerFn answer_;
  std::function<void()> closed_;
  bool closedFired_ = false, answered_ = false;
  int buttonFocus_ = 2, pressed_ = -1, hover_ = -1, firstFile_ = 0;
  RectF dialog_{}, captionR_{}, list_{}, label_r_{}, yes_{}, no_{};
  std::vector<std::string> msgLines_;
  float msgTop_ = 0, rowH_ = 0;
};

}  // namespace tf::ui
