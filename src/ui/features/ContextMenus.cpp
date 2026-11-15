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
    m.action = [run = hooks.run, cmd = e.command, arg = e.arg] {
      if (run) run(cmd, arg);
    };
    out.push_back(std::move(m));
  }
  while (!out.empty() && out.back().separator) out.pop_back();
  return out;
}

std::vector<MenuItem> buildMenuSections(const std::vector<MenuSection>& sections, const MenuHooks& hooks) {
  std::vector<MenuItem> out;
  for (const MenuSection& s : sections) {
    std::vector<MenuItem> items = buildMenu(s.entries, hooks);
    if (items.empty()) continue;
    if (s.separated && !out.empty()) out.push_back(MenuItem::sep());
    if (s.submenu.empty()) {
      for (MenuItem& m : items) out.push_back(std::move(m));
    } else {
      MenuItem m;
      m.label = s.submenu;
      m.sub = std::move(items);
      out.push_back(std::move(m));
    }
  }
  return out;
}

// The order and labels follow Visual Studio 2022's File / Edit / View menus where tenfold has the command
// (VS_BEHAVIOUR section 17); "Close Solution" follows "Close" like VS (section 18).
std::vector<MenuSection> fileMenuSections() {
  return {{"",
           {{"File.OpenSolution", "Open Solution..."},
            {"File.OpenFolder", "Open Folder..."},
            {"File.OpenFile", "Open File..."},
            {"File.RecentSolutions", "Recent Projects and Solutions"},
            {},
            {"File.New", "New File"},
            {},
            {"Workbench.CloseTab", "Close"},
            {"Workbench.CloseAllDocuments", "Close All Documents"},
            {"File.CloseSolution", "Close Solution"},
            {},
            {"File.Save", "Save"},
            {"File.SaveAll", "Save All"},
            {},
            {"File.CopyPath", "Copy Full Path"},
            {"File.ShowInFolder", "Open Containing Folder"},
            {},
            {"App.Quit", "Exit"}},
           false}};
}

std::vector<MenuSection> editMenuSections() {
  return {{"", {{"Editor.Undo", "Undo"}, {"Editor.Redo", "Redo"}}},
          {"",
           {{"Editor.Cut", "Cut"},
            {"Editor.Copy", "Copy"},
            {"Editor.Paste", "Paste"},
            {"Editor.PasteHistory", "Paste from History..."},
            {"Editor.Delete", "Delete"},
            {"Editor.SelectAll", "Select All"}}},
          {"Find and Replace",
           {{"Editor.Find", "Quick Find"},
            {"Editor.Replace", "Quick Replace"},
            {"Search.FindInWorkspace", "Find in Files"},
            {"Search.ReplaceInFiles", "Replace in Files"},
            {},
            {"Editor.FindNext", "Find Next"},
            {"Editor.FindPrev", "Find Previous"}}},
          {"Go To",
           {{"Editor.GotoLine", "Go To Line..."},
            {"Search.SymbolSearch", "Go To All..."},
            {"Editor.FindFunction", "Go To Member..."},
            {},
            {"Editor.GotoDefinition", "Go To Definition"},
            {"Editor.GotoDeclaration", "Go To Declaration"},
            {"Editor.FindReferences", "Find All References"},
            {"Editor.SwitchHeaderSource", "Go To Header/Source"}},
           false},
          {"Bookmarks",
           {{"Editor.ToggleBookmark", "Toggle Bookmark"},
            {"Editor.NextBookmark", "Next Bookmark"},
            {"Editor.PrevBookmark", "Previous Bookmark"},
            {"Editor.ListBookmarks", "Bookmark Window"}},
           false},
          {"Outlining",
           {{"Editor.ToggleCollapseExpandRegion", "Toggle Outlining Expansion"},
            {"Editor.ToggleCollapseExpandAllRegions", "Toggle All Outlining"},
            {"Editor.CollapseToDefinitions", "Collapse to Definitions"}},
           false},
          {"Advanced",
           {{"Editor.ToggleComment", "Toggle Comment"},
            {"Editor.FormatSelection", "Format Selection"},
            {"Editor.JoinLines", "Join Lines"},
            {"View.ToggleWordWrap", "Word Wrap"},
            {"View.ToggleWhitespace", "View White Space"}},
           false}};
}

std::vector<MenuSection> viewMenuSections() {
  return {{"",
           {{"View.ToggleWorkspaceExplorer", "Solution Explorer"},
            {"View.ShowDiagnostics", "Error List"},
            {"View.ToggleBuildPanel", "Output"},
            {"Terminal.Show", "Terminal"},
            {"View.CallHierarchy", "Call Hierarchy"},
            {"View.TypeHierarchy", "Type Hierarchy"}}},
          {"", {{"App.CommandPalette", "Command Palette..."}}},
          {"", {{"View.ZoomIn", "Zoom In"}, {"View.ZoomOut", "Zoom Out"}, {"View.ToggleFullscreen", "Full Screen"}}}};
}

std::vector<MenuEntry> editorMenuEntries() {
  return {
      {"Editor.Cut", "Cut"},
      {"Editor.Copy", "Copy"},
      {"Editor.Paste", "Paste"},
      {},
      {"Editor.QuickActions", "Quick Actions and Refactorings..."},
      {},
      {"Editor.GotoDefinition", "Go To Definition"},
      {"Editor.GotoDeclaration", "Go To Declaration"},
      {"Editor.FindReferences", "Find All References"},
      {"View.CallHierarchy", "View Call Hierarchy"},
      {"View.TypeHierarchy", "View Type Hierarchy"},
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
  if (segment == "eol")
    return {{"Editor.LineEndingCRLF", "Windows (CR LF)"},
            {"Editor.LineEndingLF", "Unix (LF)"},
            {"Editor.LineEndingCR", "Macintosh (CR)"},
            {"Editor.LineEndingLS", "Unicode Line Separator (LS)"},
            {"Editor.LineEndingPS", "Unicode Paragraph Separator (PS)"},
            {"Editor.LineEndingNEL", "Next Line (NEL)"},
            {},
            {"File.SaveWithLineEndings", "Save with Line Endings..."}};
  if (segment == "encoding")
    return {{"File.ReopenWithEncoding", "Reopen with Encoding..."}, {"File.SaveWithEncoding", "Save with Encoding..."}};
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
