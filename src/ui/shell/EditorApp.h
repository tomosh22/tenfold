#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "app/Editor.h"
#include "ui/commands/CommandRegistry.h"
#include "ui/commands/KeyMap.h"
#include "ui/features/NavigationUi.h"
#include "ui/settings/KeyBindings.h"
#include "ui/shell/Workbench.h"

namespace tf::ui {

// Glue between the window's input events and the command system (B4): maps KeyDown / thumb-button MouseDown to a
// KeyChord, picks the active context ("editor" when a TextView has focus, else "global"), runs KeyMap::feed and
// CommandRegistry::run, and reports through the status bar: the pending chord ("Ctrl+K..."), key-binding file
// diagnostics, "<command> is not available yet" for commands whose UI does not exist yet (pickers, B5) and failures.
// Everything else (text input, mouse, keys without a binding) goes to the widget tree unchanged.
//
// `bench` may be null (any other RootWidget with TextViews, e.g. ShellDemo or a test); then the workbench commands
// stay unbound and messages are only available through message(). Bindings come from loadKeyBindings(workspace root,
// userDir). The caller owns time: pass a monotonic millisecond clock to handleEvent and tick.
class EditorApp {
 public:
  EditorApp(RootWidget& root, Editor& editor, Workbench* bench = nullptr, const std::filesystem::path& userDir = {});
  ~EditorApp();

  // Returns true if the event was consumed (a binding, a swallowed chord stroke, or a widget).
  bool handleEvent(const InputEvent& e, int64_t nowMs);
  // Call after each event batch and when waiting times out: Workbench::sync, key-chord/message expiry, the root's
  // tick. Returns the ms until the next tick is wanted (-1 = none).
  int tick(int64_t nowMs);

  bool quitRequested() const { return quit_; }
  const std::string& message() const { return message_; }  // what the status bar "message" segment shows
  CommandRegistry& commands() { return registry_; }
  KeyMap& keys() { return keys_; }
  const std::vector<KeyBindingDiag>& diagnostics() const { return diags_; }
  // B6: back/forward toolbar + dropdown (null without a Workbench). The status-bar "← 3 | 1 →" segment follows the
  // nav.statusCounts setting; until settings are wired into the app, whoever owns the Settings calls this (default
  // off).
  NavigationUi* navigationUi() { return nav_.get(); }
  void setNavStatusCounts(bool on);

 private:
  void setMessage(const std::string& text, int ttlMs = 0, bool pending = false);
  void bindUiCommands();
  void runCommandId(const std::string& id, const std::string& arg, const char* context);
  std::string shortcutFor(const std::string& command) const;
  const char* activeContext() const;

  RootWidget& root_;
  Editor& editor_;
  Workbench* bench_;
  CommandRegistry registry_;
  KeyMap keys_;
  std::vector<KeyBindingDiag> diags_;
  std::string message_;
  int ttlMs_ = 0;       // set by setMessage, converted to expiry_ on the next call that knows the time
  int64_t expiry_ = 0;  // 0 = never
  std::unique_ptr<NavigationUi> nav_;
  bool pendingShown_ = false, cycling_ = false, quit_ = false;
};

}  // namespace tf::ui
