#pragma once
#include <functional>
#include <string>

#include "ui/commands/CommandRegistry.h"
#include "ui/features/PickerUtil.h"

namespace tf::ui {

// Help.ShowCompileCommand (D4): a read-only, modal popup that explains the compile flags clangd uses for the current
// file: where the compile database came from, the exact command (or the fallback it gets), the active Visual Studio
// configuration, and whether clangd has parsed the file (its answers are authoritative) or not. The text is
// Editor::compileCommandInfo(file).text(), one popup row per line; Escape or a click outside closes it. Without a
// current document the command only says so in the status bar.
class CompileInfo {
 public:
  explicit CompileInfo(PickerEnv& env) : env_(env) {}
  // Gives the command (registered without a handler by registerEditorCommands) its handler. `beforeOpen` runs first
  // (EditorApp closes the other pickers).
  void bind(CommandRegistry& r, std::function<void()> beforeOpen = {});
  bool open();
  void close();
  bool isOpen() const { return popup_ != nullptr; }
  ListPopup* popup() const { return popup_; }
  const std::string& text() const { return text_; }  // what the popup shows (as of open())

 private:
  PickerEnv& env_;
  ListPopup* popup_ = nullptr;
  std::string text_;
};

}  // namespace tf::ui
