#pragma once
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "ui/features/PickerUtil.h"

namespace tf::ui {

// The small dialogs of the workspace explorer's file operations (VS-TREE X6 / X7 / C10), as modal ListPopups:
//
//  * askName: a one-row popup with an input holding a file or folder name (New File, New Folder, Rename). The row says
//    what Enter will do, or why it cannot ("already exists"). Enter with a problem keeps the box open with the same
//    text and says so in the status bar.
//  * confirm: the title and some dim lines, then OK / Cancel rows (Visual Studio's "'a.cpp' will be deleted
//    permanently.", "Are you sure you want to move ...?"). Enter takes OK, Escape or a click outside is Cancel.
//  * info: a read-only page of "label: value" rows (Properties); Escape or Enter closes it.
class ExplorerPrompt {
 public:
  using Validate = std::function<std::string(const std::string&)>;  // problem text, empty = fine
  using Named = std::function<void(const std::string&)>;

  explicit ExplorerPrompt(PickerEnv& env);
  ~ExplorerPrompt();
  ExplorerPrompt(const ExplorerPrompt&) = delete;
  ExplorerPrompt& operator=(const ExplorerPrompt&) = delete;

  // `verb` is the row text ("Rename to", "Create"). The stem of `initial` (without extension) is not pre-selected: the
  // whole text is.
  void askName(const std::string& title, const std::string& verb, const std::string& initial, Validate validate,
               Named ok);
  void confirm(const std::string& title, const std::vector<std::string>& details, const std::string& okLabel,
               std::function<void()> ok);
  void info(const std::string& title, const std::vector<std::pair<std::string, std::string>>& rows);
  void close();
  bool isOpen() const { return popup_ != nullptr; }
  ListPopup* popup() const { return popup_; }

 private:
  void updateName(const std::string& text);

  PickerEnv& env_;
  ListPopup* popup_ = nullptr;
  std::string title_, verb_, text_;
  Validate validate_;
  Named named_;
  std::shared_ptr<int> life_ = std::make_shared<int>(0);
};

}  // namespace tf::ui
