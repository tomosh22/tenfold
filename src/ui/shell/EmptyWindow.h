#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

#include "ui/commands/KeyMap.h"
#include "ui/features/WorkspaceOpener.h"
#include "ui/settings/KeyBindings.h"
#include "ui/settings/Settings.h"
#include "ui/shell/StartWindow.h"
#include "ui/widgets/MenuBar.h"
#include "ui/widgets/StatusBar.h"
#include "ui/widgets/Widget.h"

namespace tf::ui {

// The window while no solution or folder is open (CLOSE-SLN, docs/VS_BEHAVIOUR.md section 18): what `tenfold-ui` shows
// when started without arguments and after File > Close Solution. It has NO Editor: nothing here can start clangd, a
// scan, a file watcher, a build, a debugger session or version control, and nothing writes into a workspace folder.
// What it has: the File / Edit / View menu bar (the same definitions as the editor window; everything that needs a
// workspace is greyed, `availableWithoutWorkspace`), the Start window as the client area, a status bar in the "no
// solution" colour (Theme::statusBackgroundNoSolution) and the key bindings of the user (Ctrl+Shift+O, Ctrl+Q, ...).
// File > Open Solution..., Open Folder..., Recent Projects and Solutions, drag and drop of a solution / project /
// folder and Exit work; the opening itself is the SLN-OPEN flow: `hooks.open` records the request and the program
// (AppHost) replaces this window between two events.
//
// The client area is the Start window (START-WINDOW, docs/VS_BEHAVIOUR.md section 20): recent solutions / folders with
// a search box and the two "Get started" actions (StartWindow). Its list is the same RecentSolutions as File > Recent.
class EmptyWindow : public RootWidget {
 public:
  struct Hooks {
    std::function<void(const std::filesystem::path& target)> open;  // File > Open ..., recent entry, drop
    // A plain file dropped on the window: its folder becomes the workspace and the file is opened in it.
    std::function<void(const std::filesystem::path& file)> openFile;
    FileDialogBackend dialog;
    RecentSolutions* recent = nullptr;
    std::function<void(bool on)> setFullscreen;  // View.ToggleFullscreen (the window is the program's)
  };
  EmptyWindow(gfx::TextRenderer& text, const Theme& theme, const std::filesystem::path& userDir, Hooks hooks,
              bool fullscreen = false);
  ~EmptyWindow() override;

  // The commands that run without a workspace (the others are greyed in the menus and do nothing).
  static bool availableWithoutWorkspace(const std::string& commandId);

  MenuBar& menuBar() { return *menuBar_; }
  StatusBar& status() { return *status_; }
  StartWindow& start() { return *start_; }
  WorkspaceOpener& opener() { return *opener_; }
  KeyMap& keys() { return keys_; }
  const std::string& message() const { return message_; }
  bool fullscreen() const { return fullscreen_; }
  bool quitRequested() const { return quit_; }

  // menu.hide / menu.showOnMouseHover / menu.toggleOnAlt, keys.import10x.
  void applySettings(const Settings& s);
  // Runs a command by id; false when it is not available here.
  bool runCommand(const std::string& id);
  bool handleEvent(const InputEvent& e, int64_t nowMs);
  // Expires the status message and a stale key chord; returns the ms until the next tick is wanted (-1 = none).
  int tick(int64_t nowMs);

 protected:
  void onLayout() override;
  void paint(PaintContext& ctx) override;

 private:
  void buildMenus();
  bool menuBarEvent(const InputEvent& e);
  bool dropFile(const std::string& path);
  void setMessage(const std::string& text, int ttlMs = 0);
  std::string shortcutFor(const std::string& command) const;
  void reloadKeys();

  StartWindow* start_ = nullptr;
  StatusBar* status_ = nullptr;
  MenuBar* menuBar_ = nullptr;
  Hooks hooks_;
  std::unique_ptr<WorkspaceOpener> opener_;
  KeyMap keys_;
  KeyLoadOptions keyOpts_;
  std::string message_;
  int ttlMs_ = 0;
  int64_t expiry_ = 0, now_ = 0;
  bool quit_ = false, fullscreen_ = false, altCandidate_ = false;
  bool menuToggleOnAlt_ = true, menuShowOnHover_ = true, pendingShown_ = false;
};

}  // namespace tf::ui
