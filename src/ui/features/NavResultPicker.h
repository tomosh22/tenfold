#pragma once
#include <string>
#include <vector>

#include "ui/features/PickerUtil.h"

namespace tf::ui {

// Result list for the semantic navigation commands (GotoDefinition, FindReferences, ...) when the answer has several
// locations: rows "file:line" + the source line, greyed with a "verifying..." badge while the answer is Approximate,
// swapped (selection kept by location) when the Authoritative one arrives. Enter -> Editor::gotoLocation.
class NavResultPicker {
 public:
  explicit NavResultPicker(PickerEnv& env) : env_(env) {}

  // Shows or updates the picker for one command run. `title` names the command ("Go to Definition"). Fewer than
  // `minResults` locations closes an open picker instead (the Editor jumped by itself, or there is nothing to pick).
  void show(const std::string& title, Confidence c, const std::vector<SymbolLocation>& locs, const std::string& err,
            size_t minResults);
  void close();
  bool isOpen() const { return popup_ != nullptr; }
  ListPopup* popup() const { return popup_; }

 private:
  PickerEnv& env_;
  ListPopup* popup_ = nullptr;
  std::vector<SymbolLocation> locs_;
};

}  // namespace tf::ui
