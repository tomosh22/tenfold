#pragma once
#include <functional>
#include <memory>
#include <string>

#include "ui/commands/CommandRegistry.h"
#include "ui/features/DiffTool.h"
#include "ui/features/PickerUtil.h"

namespace tf::ui {

class Settings;
class TextView;

// Version-control commands and their small UI (C3c), on top of Editor::vcs() (C3a engine; Perforce C3b plugs in behind
// the same interface). The change strip, the scrollbar marks and the inline diff block live in TextView; the tab
// markers and the "vcs" status segment in Workbench. What is here:
//  * Vcs.NextChange / Vcs.PrevChange: jump to the next / previous hunk of the current file through
//    Editor::gotoLocation (so Back returns), wrapping around at the ends.
//  * Vcs.ToggleInlineDiff: opens / closes the inline block of the hunk at the caret in the focused view.
//  * Vcs.RevertHunk: reverts the hunk at the caret (one undo step). Vcs.RevertFile: asks first (a modal two-row
//    ListPopup: "Discard changes" / "Cancel"), then VcsManager::revertFile reloads the document from disk.
//  * Vcs.DiffFileExternal: DiffTool on the committed text and the buffer. Vcs.RefreshStatus. Vcs.CheckOut: the explicit
//    form of Perforce's auto-checkout (VcsManager::checkOut).
//  * VcsManager::onError (a failed auto-checkout, revert, status) is shown in the status bar message.
//  * Window focus: refreshStatus() (the engine does not watch .git); EditorApp calls refreshOnFocus().
// Key bindings are deliberately absent from the defaults: assets/keybindings.default.txt lists commented examples.
class VcsUi {
 public:
  struct Hooks {
    std::function<TextView*()> activeView;                            // the view commands act on (null: none)
    std::function<void(const std::string& text, int ttlMs)> message;  // status-bar message
  };
  VcsUi(PickerEnv& env, Hooks hooks);
  ~VcsUi();
  VcsUi(const VcsUi&) = delete;
  VcsUi& operator=(const VcsUi&) = delete;

  // Gives the Vcs.* commands (registered without handlers by registerEditorCommands) their handlers.
  void bind(CommandRegistry& r);
  void applySettings(const Settings& s);  // vcs.diffTool, vcs.diffToolArgs
  void tick();                            // collects finished diff tools
  void refreshOnFocus();                  // window regained focus: re-read the status
  void closeConfirm();
  ListPopup* confirm() const { return confirm_; }  // the open revert confirmation
  DiffTool& diffTool() { return tool_; }

  // The commands as methods (tests call them directly too).
  bool gotoChange(bool next);
  bool toggleInlineDiff();
  bool revertHunk();
  void revertFile();
  bool diffExternal();
  void refreshStatus();
  void checkOut();

 private:
  void say(const std::string& text, int ttlMs = 6000) const;
  std::string relName(const std::filesystem::path& p) const;

  PickerEnv& env_;
  Hooks hooks_;
  DiffTool tool_;
  DiffTool::Config cfg_;
  ListPopup* confirm_ = nullptr;
  std::shared_ptr<int> life_ = std::make_shared<int>(0);
};

}  // namespace tf::ui
