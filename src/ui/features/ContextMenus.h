#pragma once
#include <functional>
#include <string>
#include <vector>

#include "ui/widgets/ContextMenu.h"

// Menu definitions for the context menus (W8f). A menu is a list of entries, each naming a command (a 10x name or a
// tenfold id); buildMenu turns it into ContextMenu items through hooks that know the command registry and the key map.
// Menus never carry behaviour of their own: every item runs a command. An entry whose command is unknown, not
// implemented or not bound by the front end is left out, and a command that is disabled right now is shown greyed out.
// The models are Visual Studio's context menus (OPEN_QUESTIONS #W8f).
namespace tf::ui {

struct MenuEntry {
  std::string command;           // empty = separator
  std::string label;             // empty = the command's own name
  bool onlyOnVcsChange = false;  // shown only when the click / caret is on a version-control change
  std::string arg;               // passed to the command (a path, for the explorer tree)
};

struct MenuCommandInfo {
  bool available = false;  // false: the entry is left out
  bool enabled = true;
  std::string name;      // the command's own name (the label when the entry has none)
  std::string shortcut;  // first key binding in 10x notation, may be empty
};

struct MenuHooks {
  std::function<MenuCommandInfo(const std::string& command)> info;
  std::function<void(const std::string& command, const std::string& arg)> run;
};

// Items for `entries`; unavailable entries are dropped and separators that would lead, trail or repeat are dropped.
std::vector<MenuItem> buildMenu(const std::vector<MenuEntry>& entries, const MenuHooks& hooks,
                                bool onVcsChange = false);

// One block of a menu-bar menu (MENUBAR): a flat group of entries (`submenu` empty) or one submenu. `separated`: a
// separator goes before it when something precedes it.
struct MenuSection {
  std::string submenu;  // the submenu's label; empty: the entries are items of the menu itself
  std::vector<MenuEntry> entries;
  bool separated = true;
};
// The File / Edit / View menus of the menu bar, one definition for the editor window and for the empty window (no
// solution open: the same menus, the commands that need a workspace greyed).
std::vector<MenuSection> fileMenuSections();
std::vector<MenuSection> editMenuSections();
std::vector<MenuSection> viewMenuSections();
// Items for the sections: unavailable entries are dropped, a section or submenu left without items is dropped.
std::vector<MenuItem> buildMenuSections(const std::vector<MenuSection>& sections, const MenuHooks& hooks);

// The text editor menu (right-click in a TextView, Shift F10 / AppsKey with the editor focused).
std::vector<MenuEntry> editorMenuEntries();
// A tab of a TabStrip; the pin entry reads "Pin Tab" or "Unpin Tab". The tab is made the active one before the menu
// opens, so the commands (which act on the active tab) apply to it.
std::vector<MenuEntry> tabMenuEntries(bool pinned);
// A status bar segment (key as in Workbench::status()); empty for segments without a menu.
std::vector<MenuEntry> statusMenuEntries(const std::string& segment);
}  // namespace tf::ui
