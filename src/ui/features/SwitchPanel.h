#pragma once
#include <memory>
#include <vector>

#include "ui/features/PickerUtil.h"

namespace tf::ui {

class Workbench;
class Panel;

// The switch panel behind Ctrl+Tab / Ctrl+Shift+Tab (10x ShowSwitchPanelPanel / ShowSwitchPanelPrevItem; VS-TABS T1,
// the order and the commit follow Visual Studio's IDE Navigator). A small modal list of the active panel's tabs, most
// recently used first. Opening it with Ctrl+Tab selects the previous file (the second row), with Ctrl+Shift+Tab the
// oldest one; Tab / Ctrl+Tab move down, Shift+Tab / Ctrl+Shift+Tab up (wrapping), Up / Down likewise. Nothing is
// switched while the panel is up: releasing Ctrl commits (EditorApp calls commit()), Enter or a click commits the row,
// Escape cancels and leaves the active tab as it was. With fewer than two tabs it does not open.
class SwitchPanel {
 public:
  SwitchPanel(PickerEnv& env, Workbench& bench) : env_(env), bench_(bench) {}
  ~SwitchPanel() { close(); }
  SwitchPanel(const SwitchPanel&) = delete;
  SwitchPanel& operator=(const SwitchPanel&) = delete;

  // Ctrl+Tab (forward) / Ctrl+Shift+Tab: opens the panel, or moves the selection when it is already open. False when
  // there is nothing to switch to.
  bool step(bool forward);
  bool commit();  // activates the selected tab and closes; false when not open
  void close();   // cancel
  bool isOpen() const { return popup_ != nullptr; }
  ListPopup* popup() const { return popup_; }
  const std::vector<int>& tabIds() const { return ids_; }  // the tab ids of the rows, in row order

 private:
  void activate(int row);
  PickerEnv& env_;
  Workbench& bench_;
  ListPopup* popup_ = nullptr;
  Panel* panel_ = nullptr;
  std::vector<int> ids_;
  std::shared_ptr<int> life_ = std::make_shared<int>(0);
};

}  // namespace tf::ui
