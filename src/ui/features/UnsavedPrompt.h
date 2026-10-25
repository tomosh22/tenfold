#pragma once
#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

#include "ui/features/PickerUtil.h"

namespace tf::ui {

// "Save changes?" (Save / Don't Save / Cancel), a small modal ListPopup without an input. One dirty document: "Save
// changes to a.cpp?" with Save / Don't Save / Cancel. Several (quitting): "Save changes to 3 documents?" with Save All
// / Don't Save / Cancel and the document names listed under the three choices. Enter takes the first choice; Escape or
// a click outside is Cancel.
//
// Save writes every document through Editor::save. If one fails (a read-only Perforce file, a locked file) nothing
// proceeds: the error goes to the status bar ("Cannot save a.cpp: ..."), documents already written stay saved, the rest
// stay dirty and open. Otherwise `proceed(false)` runs. Don't Save runs `proceed(true)` ("discarded": the caller drops
// the changes, e.g. closes the document). Cancel runs nothing.
class UnsavedPrompt {
 public:
  using Proceed = std::function<void(bool discarded)>;
  explicit UnsavedPrompt(PickerEnv& env) : env_(env) {}
  // `paths` are open documents (dirty ones are listed; clean or closed ones are ignored). Nothing dirty: proceed(false)
  // runs at once.
  void ask(const std::vector<std::filesystem::path>& paths, Proceed proceed);
  void close();
  bool isOpen() const { return popup_ != nullptr; }
  ListPopup* popup() const { return popup_; }
  // Saves the dirty documents among `paths`; false (error shown) when one failed.
  bool saveAll(const std::vector<std::filesystem::path>& paths);

 private:
  PickerEnv& env_;
  ListPopup* popup_ = nullptr;
  std::shared_ptr<int> life_ = std::make_shared<int>(0);
};

}  // namespace tf::ui
