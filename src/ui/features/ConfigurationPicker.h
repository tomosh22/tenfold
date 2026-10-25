#pragma once
#include <memory>
#include <string>
#include <vector>

#include "semantic/CompileCommands.h"
#include "ui/features/PickerUtil.h"

namespace tf::ui {

// Active solution configuration (Build.SelectConfiguration, C5 UI): a modal ListPopup with a filter line listing the
// solution configurations of the workspace's .sln files ("Debug|x64", ...). The active one is marked (badge "active")
// and preselected. Enter / click calls Editor::selectVsConfiguration; the switch is asynchronous (compile commands are
// regenerated and clangd restarted), so statusText() reads "Configuration: Release|x64 (reloading...)" until the
// editor reports it applied, then the plain name. statusText() is empty for a workspace without a solution
// configuration, which is how the status bar omits the segment.
//
// The configuration list is read from the .sln files on construction, on open() and after a switch (not per frame).
class ConfigurationPicker {
 public:
  explicit ConfigurationPicker(PickerEnv& env);
  void open();
  void close();
  bool isOpen() const { return popup_ != nullptr; }
  ListPopup* popup() const { return popup_; }

  // Re-reads the solution configurations (a .sln added or changed since).
  void refresh();
  bool hasSolution() const { return !configs_.empty(); }
  // "Debug|x64", "Configuration: Release|x64 (reloading...)", or empty without a solution.
  std::string statusText() const;
  bool reloading() const { return !pending_.empty(); }

 private:
  void fill(const std::string& filter);
  void choose(const std::string& name);

  PickerEnv& env_;
  ListPopup* popup_ = nullptr;
  std::vector<compdb::SolutionConfiguration> configs_;
  std::vector<std::string> shown_;  // names aligned with the popup rows
  std::string active_;              // name of the active configuration
  std::string pending_;             // being switched to
  std::shared_ptr<int> life_ = std::make_shared<int>(0);
};

}  // namespace tf::ui
