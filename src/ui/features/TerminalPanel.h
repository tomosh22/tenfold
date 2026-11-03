#pragma once
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "app/Editor.h"
#include "ui/settings/Settings.h"
#include "ui/shell/Workbench.h"
#include "ui/widgets/TerminalView.h"

namespace tf::ui {

// The terminal panel feature (W8f; 10x NewTerminal, ShowTerminal, ClaudeCodeTerminal, CursorTerminal): creates terminal
// views as tabs of the workbench's editor panels, finds them again to cycle through, and applies the terminal.*
// settings. See docs/ARCHITECTURE.md ("Terminal panel") and docs/OPEN_QUESTIONS.md ("W8f detail: terminal panel").
class TerminalPanel {
 public:
  enum class Kind { Shell, Claude, Cursor };

  TerminalPanel(Editor& editor, Workbench& bench);
  ~TerminalPanel();
  TerminalPanel(const TerminalPanel&) = delete;
  TerminalPanel& operator=(const TerminalPanel&) = delete;

  // Reads the terminal.* settings (and editor.wheelLines); `dpiScale` sizes a terminal.font / terminal.fontSize font.
  // Existing terminals pick up the font, the scrollback limit and the wheel speed; the commands apply to new ones.
  void applySettings(const Settings& s, float dpiScale);
  // The program a terminal of `kind` runs, after the settings and the off-Windows translation of 10x's Windows
  // defaults ("cmd.exe" = the user's shell, a leading "cmd /c" dropped): program + arguments.
  std::vector<std::string> commandFor(Kind kind) const;
  // The working directory of a new terminal: the workspace root, else the current file's folder, else the home folder.
  std::filesystem::path workingDirectory() const;

  // NewTerminal: a terminal tab in the active panel, focused. Returns its tab id, or -1 with *error.
  int create(Kind kind, std::string* error = nullptr);
  // ShowTerminal: with no terminal creates a shell; if the active tab is a terminal shows the next one (wrapping round,
  // in creation order); otherwise shows the one shown last. Returns the tab id shown, or -1 with *error.
  int show(std::string* error = nullptr);

  // Live terminals (tab ids) in creation order.
  std::vector<int> terminals() const;
  TerminalView* view(int id) const;
  int shownLast() const { return lastShown_; }

  // Per-frame upkeep (EditorApp::tick): tab titles follow the program's title, closeOnExit closes finished terminals.
  void tick();
  // The window gained / lost the focus: the focused terminal tells its program (focus reporting).
  void setWindowFocused(bool on);
  void closeAll();

  // Text for the status bar (failures); may be empty.
  std::function<void(const std::string&)> message;
  // Makes an idle UI loop turn when terminal output arrives (default: the SDL wake event; tests may replace it).
  std::function<void()> wake;

 private:
  struct Info {
    int id;
    Kind kind;
    std::string baseTitle;
    std::string shownTitle;
    int serial;
  };
  void prune();
  void rebuildFont(const Settings& s, float dpiScale);
  std::string baseTitle(Kind k, const std::vector<std::string>& cmd, int serial) const;
  void focusTerminal(int id);

  Editor& editor_;
  Workbench& bench_;
  std::vector<Info> infos_;
  std::shared_ptr<TerminalFont> font_ = std::make_shared<TerminalFont>();
  std::string command_ = "cmd.exe", claude_ = "claude", cursor_ = "cmd /c agent";
  size_t scrollback_ = 10000;
  bool closeOnExit_ = false;
  int wheelLines_ = 3;
  FontSpec fontSpec_;
  float fontSize_ = 0;
  float editorPt_ = 10;
  std::string fontKey_;
  int lastShown_ = -1;
  int serial_ = 0;
  std::vector<int> closeSoon_;
  std::shared_ptr<int> life_ = std::make_shared<int>(0);
};

}  // namespace tf::ui
