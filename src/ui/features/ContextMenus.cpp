#include "ui/features/ContextMenus.h"

namespace tf::ui {

std::vector<MenuItem> buildMenu(const std::vector<MenuEntry>& entries, const MenuHooks& hooks, bool onVcsChange) {
  std::vector<MenuItem> out;
  auto pushSep = [&] {
    if (!out.empty() && !out.back().separator) out.push_back(MenuItem::sep());
  };
  for (const MenuEntry& e : entries) {
    if (e.command.empty()) {
      pushSep();
      continue;
    }
    if (e.onlyOnVcsChange && !onVcsChange) continue;
    const MenuCommandInfo info = hooks.info ? hooks.info(e.command) : MenuCommandInfo{};
    if (!info.available) continue;
    MenuItem m;
    m.label = e.label.empty() ? info.name : e.label;
    m.shortcut = info.shortcut;
    m.enabled = info.enabled;
    m.action = [run = hooks.run, cmd = e.command] {
      if (run) run(cmd);
    };
    out.push_back(std::move(m));
  }
  while (!out.empty() && out.back().separator) out.pop_back();
  return out;
}

std::vector<MenuEntry> editorMenuEntries() {
  return {
      {"Editor.Cut", "Cut"},
      {"Editor.Copy", "Copy"},
      {"Editor.Paste", "Paste"},
      {},
      {"Editor.GotoDefinition", "Go To Definition"},
      {"Editor.GotoDeclaration", "Go To Declaration"},
      {"Editor.FindReferences", "Find All References"},
      {"Editor.RenameSymbol", "Rename..."},
      {"Editor.ShowSymbolInfo", "Quick Info"},
      {"Editor.SwitchHeaderSource", "Switch Header/Source"},
      {},
      {"Editor.FormatSelection", "Format Selection"},
      {"Editor.ToggleComment", "Toggle Comment"},
      {"Editor.SelectAll", "Select All"},
      {},
      {"Debug.ToggleBreakpoint", "Toggle Breakpoint"},
      {"Debug.RunToCursor", "Run To Cursor"},
      {},
      {"Vcs.RevertHunk", "Revert Change", true},
      {"Vcs.ToggleInlineDiff", "Show Change", true},
      {},
      {"Debug.OpenInVisualStudio", "Open in Visual Studio"},
      {"ShowInWorkspaceTree", "Show in Workspace Tree"},
      {"OpenFolderForActivefile", "Open Containing Folder"},
      {"CopyActiveFilePath", "Copy Full Path"},
  };
}

std::vector<MenuEntry> tabMenuEntries(bool pinned) {
  return {
      {"Workbench.CloseTab", "Close"},
      {"CloseAllOtherTabs", "Close All But This"},
      {"CloseAllButPinnedTabs", "Close All But Pinned"},
      {},
      {"TogglePinTab", pinned ? "Unpin Tab" : "Pin Tab"},
      {},
      {"CopyActiveFilePath", "Copy Full Path"},
      {"OpenFolderForActivefile", "Open Containing Folder"},
      {"ShowInWorkspaceTree", "Show in Workspace Tree"},
  };
}

std::vector<MenuEntry> statusMenuEntries(const std::string& segment) {
  if (segment == "config") return {{"Build.SelectConfiguration", "Select Configuration..."}};
  if (segment == "pos") return {{"Editor.GotoLine", "Go To Line..."}};
  if (segment == "eol") return {{"Editor.LineEndingCRLF", "Windows (CRLF)"}, {"Editor.LineEndingLF", "Unix (LF)"}};
  if (segment == "flags") return {{"Help.ShowCompileCommand", "Show Compile Command"}};
  if (segment == "clangd") return {{"Clangd.Restart", "Restart clangd"}};
  if (segment == "build")
    return {{"View.ToggleBuildPanel", "Toggle Build Panel"},
            {},
            {"Build.Build", "Build"},
            {"Build.Rebuild", "Rebuild"},
            {"Build.Cancel", "Cancel Build"}};
  return {};
}

}  // namespace tf::ui
