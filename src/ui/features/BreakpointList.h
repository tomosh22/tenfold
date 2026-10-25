#pragma once
#include <filesystem>
#include <string>
#include <vector>

#include "ui/features/PickerUtil.h"

namespace tf::ui {

// Breakpoint list (Debug.ShowBreakpoints, Ctrl+Alt+B, C2c): a modal, input-less ListPopup of every breakpoint of the
// workspace: "[x] path:line" (enabled) or "[ ] path:line" (disabled), then the condition ("when <condition>") or the
// line's text. Enter / click -> Editor::gotoLocation (navigation history); Space toggles the selected breakpoint's
// enabled flag; Delete removes it. All three go through Breakpoints, so the DebugSession pushes them to the backend.
// The list follows changes made meanwhile (refresh(), also from the backend) and keeps the selection by breakpoint.
//
// Debug.EditCondition (openCondition) is a one-row ListPopup with an input line holding the condition of the
// breakpoint on the caret's line (empty removes the condition); Enter applies it, Escape cancels. When the caret's
// line has no breakpoint it only says so in the status bar message.
class BreakpointList {
 public:
  explicit BreakpointList(PickerEnv& env) : env_(env) {}
  void open();
  void openCondition();
  void close();
  void refresh();  // rebuilds the list when Breakpoints changed since the last build
  bool isOpen() const { return popup_ != nullptr; }
  bool conditionOpen() const { return cond_ != nullptr; }
  ListPopup* popup() const { return popup_; }
  ListPopup* conditionPopup() const { return cond_; }
  const std::vector<Breakpoint>& shown() const { return shown_; }

 private:
  void rebuild(const std::string& keepKey);
  void toggleSelected();
  void removeSelected();

  PickerEnv& env_;
  ListPopup* popup_ = nullptr;
  ListPopup* cond_ = nullptr;
  std::vector<Breakpoint> shown_;  // aligned with the popup rows
  uint64_t gen_ = 0;
  std::filesystem::path condFile_;
  int condLine_ = 0;
  std::string condText_;
};

}  // namespace tf::ui
