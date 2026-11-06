#pragma once
#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

#include "ui/features/PickerUtil.h"

namespace tf::ui {

// "Save changes?", a small modal ListPopup without an input. One dirty document: "Save changes to a.cpp?" with Save /
// Don't Save / Cancel; Enter takes the first choice, Escape or a click outside is Cancel. Several (Close All, closing a
// panel, quitting; VS_BEHAVIOUR T6, as Visual Studio): one list dialog "Save changes to the following items?" with a
// row per document, all checked (Enter / Space / a click toggles the row under the selection), then Yes / No / Cancel.
//
// Save / Yes writes the checked documents through Editor::save. If one fails (a read-only Perforce file, a locked
// file) nothing proceeds: the error goes to the status bar ("Cannot save a.cpp: ..."), documents already written stay
// saved, the rest stay dirty and open. Otherwise `proceed(false)` runs (`proceed(true)` when some documents were left
// unchecked: the caller closes those without saving). A document whose file encoding cannot hold one of its
// characters is refused the same way (nothing proceeds) and SaveRefusedDialog shows the characters. Don't Save / No
// runs `proceed(true)` ("discarded": the caller drops the changes, e.g. closes the document). Cancel runs nothing.
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
  void askList(const std::vector<std::filesystem::path>& dirty, Proceed proceed);
  PickerEnv& env_;
  ListPopup* popup_ = nullptr;
  std::shared_ptr<int> life_ = std::make_shared<int>(0);
};

}  // namespace tf::ui
