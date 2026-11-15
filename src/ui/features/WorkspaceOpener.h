#pragma once
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ui/features/PickerUtil.h"
#include "ui/features/UnsavedPrompt.h"
#include "ui/platform/FileDialog.h"
#include "ui/settings/RecentSolutions.h"

namespace tf::ui {

// Opening a solution, a project or a folder from inside the running editor (SLN-OPEN, docs/VS_BEHAVIOUR.md section 16):
// File > Open > Project/Solution (File.OpenSolution, Ctrl+Shift+O), Open Folder, Recent Projects and Solutions, and the
// files dropped on the window. Like Visual Studio, opening another solution closes the open one in the same window:
// the modified documents are asked about first (UnsavedPrompt, "Save changes to the following items?" Yes / No /
// Cancel; Cancel stops everything and the current solution stays), then `hooks.open` runs. That hook belongs to the
// program (AppHost): it records the request and, between two events, replaces Editor / Workbench / EditorApp, which
// destroys the object that called it, so nothing may touch this class after `hooks.open` returned.
//
// Close Solution (File.CloseSolution, VS's File > Close Solution, 10x's CloseWorkspace): the same save prompt, then
// `hooks.close` runs, and the program replaces the workspace by the empty window (AppHost). The empty window has its
// own WorkspaceOpener without an editor (no documents, nothing to ask, nothing to close): the same dialogs, recent list
// and request() entry, so opening from the empty state is the one flow.
//
// The native dialog is a FileDialogBackend (platform/FileDialog.h), injected so tests answer at once. Its answer can
// arrive after the window's workspace was replaced: the callback checks a life token.
class WorkspaceOpener {
 public:
  struct Hooks {
    std::function<void(const std::filesystem::path& target)> open;  // empty: the commands are unavailable
    std::function<void()> close;                                    // empty: Close Solution is unavailable
    FileDialogBackend dialog;                                       // empty: the dialog commands are unavailable
    RecentSolutions* recent = nullptr;                              // null: no recent list
    std::function<bool()> confirmUnsaved = [] { return true; };     // setting editor.confirmUnsaved
  };
  WorkspaceOpener(PickerEnv& env, UnsavedPrompt& unsaved);
  // The empty window: no workspace, no documents. `say` shows a message (status bar).
  WorkspaceOpener(RootWidget& root, std::function<void(const std::string&)> say);

  void setHooks(Hooks h) { hooks_ = std::move(h); }
  bool canOpen() const { return static_cast<bool>(hooks_.open); }
  bool canClose() const { return editor_ && static_cast<bool>(hooks_.close); }
  bool canShowDialog() const { return canOpen() && static_cast<bool>(hooks_.dialog); }

  // File.OpenSolution: the native "open" dialog, filtered like Visual Studio's, starting in the open solution's
  // folder. The chosen file goes to request().
  void openProjectOrSolutionDialog();
  // File.OpenFolder: the native folder dialog.
  void openFolderDialog();
  // File.RecentSolutions: the picker over the recent list.
  void openRecentPicker();
  void closePicker();
  // A recent entry chosen (the picker's row, the Start window's row): opens it like request(); an entry whose file or
  // folder is gone is dropped from the recent list with a message instead (nothing opens).
  void chooseRecent(const std::filesystem::path& p);
  bool pickerOpen() const { return popup_ != nullptr; }
  ListPopup* popup() const { return popup_; }

  // Opens `path` (a .sln / .slnx, another file whose folder becomes the workspace, or a folder) in this window,
  // asking about modified documents first. Opening what is already open only says so. False when unavailable.
  bool request(const std::filesystem::path& path);
  // File.CloseSolution: asks about modified documents (Cancel keeps the solution), then runs hooks.close. False when
  // unavailable (no hook, or this is the empty window's opener).
  bool closeSolution();
  // The solution or folder that is open now (what the recent picker marks "current").
  std::filesystem::path currentTarget() const;

 private:
  void fillRecent(const std::string& filter);

  RootWidget& root_;
  std::function<void(const std::string&)> say_;
  Editor* editor_ = nullptr;          // null: the empty window
  UnsavedPrompt* unsaved_ = nullptr;  // null: the empty window
  Hooks hooks_;
  ListPopup* popup_ = nullptr;
  std::vector<std::filesystem::path> shown_;  // aligned with the picker rows
  std::shared_ptr<int> life_ = std::make_shared<int>(0);
};

}  // namespace tf::ui
