#pragma once
#include <deque>
#include <functional>
#include <string>
#include <vector>

#include "ui/features/PickerUtil.h"

namespace tf::ui {

class Settings;

// PasteHistory (Control Shift V, W8f): the recent clipboard texts, newest first, in a modal ListPopup. EditorApp
// records what Copy, Cut and CutLine put on the clipboard; a change made by another program is picked up when the
// window regains focus (syncFromClipboard). The same text is never listed twice (copying it again moves it to the top).
// Enter pastes the entry through `paste` (the normal Paste rules: one line per caret when the counts match), moves it
// to the top and makes it the clipboard text as well, so a following Control V pastes it again. The input filters the
// entries by substring. 10x documents no size for the history: the default is 20 (setting editor.pasteHistorySize).
class PasteHistory {
 public:
  struct Hooks {
    std::function<bool(const std::string& text)> paste;  // inserts at the carets like Paste; false: nothing pasted
    std::function<std::string()> clipboardGet;           // the system clipboard (null: no focus sync)
    std::function<bool(const std::string&)> clipboardSet;
  };
  PasteHistory(PickerEnv& env, Hooks hooks) : env_(env), hooks_(std::move(hooks)) {}

  void applySettings(const Settings& s);
  void setLimit(size_t n);
  size_t limit() const { return limit_; }

  void record(const std::string& text);  // empty text is ignored
  // The window regained focus: the clipboard holds something tenfold did not put there -> record it.
  void syncFromClipboard();
  const std::deque<std::string>& entries() const { return entries_; }

  void open();
  void close();
  bool isOpen() const { return popup_ != nullptr; }
  ListPopup* popup() const { return popup_; }

  // "first line" (with " ..." when more follow), at most `max` bytes.
  static std::string preview(const std::string& text, size_t max = 120);

 private:
  void rebuild();

  PickerEnv& env_;
  Hooks hooks_;
  std::deque<std::string> entries_;
  std::string lastClipboard_;
  size_t limit_ = 20;
  ListPopup* popup_ = nullptr;
  std::vector<size_t> shown_;  // entry index per popup row
};

}  // namespace tf::ui
