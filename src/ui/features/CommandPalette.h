#pragma once
#include <functional>
#include <string>
#include <vector>

#include "ui/commands/CommandRegistry.h"
#include "ui/features/PickerUtil.h"

namespace tf::ui {

// App.CommandPalette: a picker of every registered command that has a handler (the front end bound it) except the
// palette itself and the commands that cannot work without an argument (Workbench.FocusPanel; Build.Build is listed and
// runs the detected build). Each row shows the human name (fuzzy-matched characters highlighted), the command id (dim)
// and the first key binding (right aligned). The query is matched against the name, else the id, like the other
// pickers: case-insensitive subsequence, blanks ignored, tighter and earlier matches first. Without a query the rows
// are sorted by name. A command that is disabled right now (no document open, ...) stays listed greyed out; Enter on it
// says so in the status bar. Enter closes the palette and runs the command through `run`, i.e. like its key binding
// would, so the commands that open their own popup (Go to Line, Show Compile Command, Select Configuration) work from
// here.
class CommandPalette {
 public:
  struct Hooks {
    std::function<std::string(const std::string& id)> binding;  // first key binding of a command, "" if none
    std::function<void(const std::string& id)> run;
  };
  CommandPalette(PickerEnv& env, CommandRegistry& registry, Hooks hooks)
      : env_(env), registry_(registry), hooks_(std::move(hooks)) {}
  void open();
  void close();
  bool isOpen() const { return popup_ != nullptr; }
  ListPopup* popup() const { return popup_; }
  // Ids in the order of the rows shown (for tests).
  const std::vector<std::string>& shown() const { return shown_; }
  static bool listed(const Command& c);

 private:
  void refresh();

  PickerEnv& env_;
  CommandRegistry& registry_;
  Hooks hooks_;
  ListPopup* popup_ = nullptr;
  std::vector<std::string> shown_;
};

}  // namespace tf::ui
