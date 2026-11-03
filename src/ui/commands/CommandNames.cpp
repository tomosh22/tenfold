#include "ui/commands/CommandNames.h"

#include <algorithm>
#include <cctype>
#include <map>

namespace tf::ui {

namespace {

// Generated once from the 10x documentation (Commands.txt / KeyMappings.txt); maintained by hand afterwards.
// Rows with an empty tenfold id are 10x commands tenfold does not implement. The first row per tenfold id is the
// name the command palette shows.
const CommandAlias kAliases[] = {
    {"FindFile", "File.OpenFile", "global", ""},
    {"OpenFile", "File.OpenFile", "global", "opens tenfold's file picker, not a native open dialog"},
    {"CloseFile", "Workbench.CloseTab", "global", "closes the tab (asks about unsaved changes)"},
    {"SaveFile", "File.Save", "global", ""},
    {"ShowCommandPanel", "App.CommandPalette", "global", "a list to pick from; it does not take a typed command line"},
    {"OpenCurrentFileInDebugger", "Debug.OpenInVisualStudio", "global", ""},
    {"Cut", "Editor.Cut", "editor", ""},
    {"Copy", "Editor.Copy", "editor", ""},
    {"Paste", "Editor.Paste", "editor", ""},
    {"Undo", "Editor.Undo", "editor", ""},
    {"Redo", "Editor.Redo", "editor", ""},
    {"Delete", "Editor.Delete", "editor", ""},
    {"DeleteLine", "Editor.DeleteLines", "editor", ""},
    {"MoveLineUp", "Editor.MoveLinesUp", "editor", "also moves every line a selection touches"},
    {"MoveLineDown", "Editor.MoveLinesDown", "editor", "also moves every line a selection touches"},
    {"DuplicateLine", "Editor.Duplicate", "editor", "with a selection it duplicates the selected text, not the line"},
    {"SelectAll", "Editor.SelectAll", "editor", ""},
    {"ToggleWordWrap", "View.ToggleWordWrap", "global", ""},
    {"ToggleComment", "Editor.ToggleComment", "editor",
     "a selection follows editor.commentBlockUsesLineComments (default: line comments); no selection: line comment"},
    {"IndentLine", "Editor.Indent", "editor", ""},
    {"UnindentLine", "Editor.Outdent", "editor", ""},
    {"MultiCursorMoveUp", "Editor.AddCaretAbove", "editor", "adds the caret at the same column"},
    {"MultiCursorMoveDown", "Editor.AddCaretBelow", "editor", "adds the caret at the same column"},
    {"SelectNextMultiCursor", "Editor.AddCaretAtNextOccurrence", "editor",
     "UndoSelectNextMultiCursor takes the last step back"},
    {"MoveCursorUp", "Editor.CaretUp", "editor", ""},
    {"MoveCursorDown", "Editor.CaretDown", "editor", ""},
    {"MoveCursorLeft", "Editor.CaretLeft", "editor", ""},
    {"MoveCursorRight", "Editor.CaretRight", "editor", ""},
    {"SelectUp", "Editor.SelectUp", "editor", ""},
    {"SelectDown", "Editor.SelectDown", "editor", ""},
    {"SelectLeft", "Editor.SelectLeft", "editor", ""},
    {"SelectRight", "Editor.SelectRight", "editor", ""},
    {"MoveToLineStart", "Editor.CaretLineStart", "editor", ""},
    {"MoveToLineEnd", "Editor.CaretLineEnd", "editor", ""},
    {"SelectToLineStart", "Editor.SelectLineStart", "editor", ""},
    {"SelectToLineEnd", "Editor.SelectLineEnd", "editor", ""},
    {"MoveToFileStart", "Editor.CaretDocStart", "editor", ""},
    {"MoveToFileEnd", "Editor.CaretDocEnd", "editor", ""},
    {"SelectToFileStart", "Editor.SelectDocStart", "editor", ""},
    {"SelectToFileEnd", "Editor.SelectDocEnd", "editor", ""},
    {"MoveCursorPrevWord", "Editor.CaretWordLeft", "editor", ""},
    {"MoveCursorNextWord", "Editor.CaretWordRight", "editor", ""},
    {"MoveCursorPrevWordSelect", "Editor.SelectWordLeft", "editor", ""},
    {"MoveCursorNextWordSelect", "Editor.SelectWordRight", "editor", ""},
    {"PageUp", "Editor.CaretPageUp", "editor", ""},
    {"PageDown", "Editor.CaretPageDown", "editor", ""},
    {"SelectPageUp", "Editor.SelectPageUp", "editor", ""},
    {"SelectPageDown", "Editor.SelectPageDown", "editor", ""},
    {"PrevLocation", "Nav.Back", "global", ""},
    {"NextLocation", "Nav.Forward", "global", ""},
    {"PrevEditLocation", "Nav.LastEditLocation", "global", ""},
    {"FindInFile", "Editor.Find", "global", ""},
    {"FindInFilePrev", "Editor.FindPrev", "global", ""},
    {"FindInFileNext", "Editor.FindNext", "global", ""},
    {"FindReplaceInFile", "Editor.Replace", "global", ""},
    {"GotoPrevResult", "Search.PrevResult", "global",
     "walks the results of whichever came last, a find-in-files search or a build"},
    {"GotoNextResult", "Search.NextResult", "global",
     "walks the results of whichever came last, a find-in-files search or a build"},
    {"GoToLine", "Editor.GotoLine", "global", ""},
    {"Search", "Search.FindInWorkspace", "global",
     "tenfold's find-in-files picker (NewSearchPanel keeps a previous result list)"},
    {"FindInFiles", "Search.FindInWorkspace", "global", "tenfold's find-in-files picker"},
    {"ToggleBookmark", "Editor.ToggleBookmark", "global", ""},
    {"PrevBookmark", "Editor.PrevBookmark", "global", ""},
    {"NextBookmark", "Editor.NextBookmark", "global", ""},
    {"ShowSwitchPanelPanel", "Workbench.CycleMruNext", "global",
     "tenfold's recent-documents switcher; releasing Control ends the cycle"},
    {"ShowSwitchPanelPrevItem", "Workbench.CycleMruPrev", "global", "tenfold's recent-documents switcher"},
    {"BuildActiveWorkspace", "Build.Build", "global", "runs the detected build (solution, CMake or custom command)"},
    {"CancelBuild", "Build.Cancel", "global", ""},
    {"ToggleBuildOutputPanel", "View.ToggleBuildPanel", "global", ""},
    {"GotoPrevBuildWarningOrError", "Build.PrevError", "global", ""},
    {"GotoNextBuildWarningOrError", "Build.NextError", "global", ""},
    {"StartDebugging", "Debug.Start", "global", "drives the Visual Studio debugger"},
    {"RestartDebugging", "Debug.Restart", "global", ""},
    {"StopDebugging", "Debug.Stop", "global", ""},
    {"DebugStepOver", "Debug.StepOver", "global", ""},
    {"DebugStepInto", "Debug.StepInto", "global", ""},
    {"DebugStepOut", "Debug.StepOut", "global", ""},
    {"Run", "Debug.StartWithoutDebugging", "global", ""},
    {"ToggleBreakpoint", "Debug.ToggleBreakpoint", "global", ""},
    {"RemoveAllBreakpoints", "Debug.DeleteAllBreakpoints", "global", ""},
    {"ListBreakpoints", "Debug.ShowBreakpoints", "global",
     "opens the breakpoint picker instead of writing to the output panel"},
    {"GotoSymbolDefinition", "Editor.GotoDefinition", "global", ""},
    {"FindSymbol", "Search.SymbolSearch", "global", ""},
    {"FindSymbolReferences", "Editor.FindReferences", "global", ""},
    {"CppParser.ToggleSourceHeader", "Editor.SwitchHeaderSource", "global", ""},
    {"PerforceOpenForEdit", "Vcs.CheckOut", "global", ""},
    {"PerforceDiff", "Vcs.DiffFileExternal", "global",
     "diffs against the version control base revision in the external diff tool"},
    {"PerforceRevert", "Vcs.RevertFile", "global", "asks for confirmation first"},
    {"GitRestore", "Vcs.RevertFile", "global", "same command as PerforceRevert (works for any version control system)"},
    {"Exit", "App.Quit", "global", ""},
    {"SaveFileAs", "", "global", ""},
    {"SaveAll", "File.SaveAll", "global", ""},
    {"CloseAll", "", "global", ""},
    {"NewTextFile", "File.New", "global",
     "creates an untitled file named Untitled N; Save asks for its path in an input line"},
    {"CopyActiveFilePath", "", "global", ""},
    {"OpenFolderForActivefile", "File.ShowInFolder", "global",
     "opens the file manager (explorer, open -R, or xdg-open on the folder)"},
    {"ShowInWorkspaceTree", "View.ShowInWorkspaceTree", "global", "opens the explorer panel when it is closed"},
    {"ShowInWorkspaceTreeWithToggle", "View.ShowInWorkspaceTreeWithToggle", "global", ""},
    {"ToggleReadOnly", "", "global", ""},
    {"CreateWorkspace", "", "global", ""},
    {"OpenWorkspace", "", "global", ""},
    {"SaveWorkspace", "", "global", ""},
    {"CloseWorkspace", "", "global", ""},
    {"OpenSolution", "", "global", ""},
    {"OpenFolder", "", "global", ""},
    {"WorkspaceSettings", "", "global", ""},
    {"ActiveWorkspaceSettings", "", "global", ""},
    {"LogWorkspaceSettings", "", "global", ""},
    {"SyncWorkspace", "", "global", ""},
    {"CreateSecondaryWindow", "", "global", ""},
    {"Show10XOutput", "", "global", ""},
    {"NewTerminal", "", "global", ""},
    {"ShowTerminal", "", "global", ""},
    {"ClaudeCodeTerminal", "", "global", ""},
    {"CursorTerminal", "", "global", ""},
    {"CloseOutputPanel", "", "global", ""},
    {"CloseFindFilePanel", "", "global", ""},
    {"CloseCommandPanel", "", "global", ""},
    {"ToggleOutputPanel", "", "global", ""},
    {"ToggleWorkspaceExplorer", "View.ToggleWorkspaceExplorer", "global", ""},
    {"Settings", "", "global", ""},
    {"LocalSettings", "", "global", ""},
    {"EditKeyBindings", "", "global", ""},
    {"EditCurrentColourScheme", "", "global", ""},
    {"Colours", "", "global", ""},
    {"InternetSearch", "Editor.InternetSearch", "global", ""},
    {"ZoomIn", "View.ZoomIn", "global",
     "zooms the whole UI font (editor, tabs, status bar) for the session; one point per step"},
    {"ZoomOut", "View.ZoomOut", "global",
     "zooms the whole UI font (editor, tabs, status bar) for the session; one point per step"},
    {"ToggleFullscreen", "View.ToggleFullscreen", "global", ""},
    {"ToggleFocusMode", "View.ToggleFocusMode", "global",
     "shows only the current panel (no status bar, other panels or bottom dock)"},
    {"ToggleFullScreenFocusMode", "View.ToggleFullscreenFocusMode", "global", "fullscreen and focus mode together"},
    {"ToggleAccessibility", "", "global", ""},
    {"ToggleTextToSpeech", "", "global", ""},
    {"ImportKeyBindingsFromVS", "", "global", ""},
    {"PasteHistory", "", "editor", ""},
    {"CutLine", "Editor.CutLine", "editor", ""},
    {"InsertLine", "Editor.InsertLineAbove", "editor", "a blank line above the current line, caret on it"},
    {"InsertLineAfter", "Editor.InsertLineBelow", "editor", "a blank line below the current line, caret on it"},
    {"SelectCurrentWord", "Editor.SelectCurrentWord", "editor", ""},
    {"SelectCurrentScope", "Editor.SelectCurrentScope", "editor",
     "selects inside the enclosing braces, then with them, then the next scope out"},
    {"MakeSelectionUppercase", "Editor.MakeSelectionUppercase", "editor", "changes ASCII letters only"},
    {"MakeSelectionLowercase", "Editor.MakeSelectionLowercase", "editor", "changes ASCII letters only"},
    {"SelectCurrentBracketScope", "Editor.SelectCurrentBracketScope", "editor",
     "()[]{} scopes, growing like SelectCurrentScope"},
    {"ToggleWhitespace", "View.ToggleWhitespace", "global",
     "for this session; editor.showWhitespace sets the start-up state"},
    {"ToggleLineEndings", "", "global", ""},
    {"ToggleWordWrapForCurrentPanel", "", "global", ""},
    {"Commentline", "Editor.CommentLines", "editor", ""},
    {"Uncommentline", "Editor.UncommentLines", "editor", ""},
    {"DeleteToEndOfLine", "Editor.DeleteToEndOfLine", "editor", ""},
    {"ClearUpdatedLines", "", "global", ""},
    {"RefreshUpdatedLines", "", "global", ""},
    {"ToggleIndentMode", "", "global", ""},
    {"FormatFile", "", "editor", ""},
    {"FormatSelection", "Editor.FormatSelection", "editor",
     "formats the primary selection (else the first other one); with no selection the innermost { } block around the "
     "caret, or the caret line outside braces"},
    {"MultiCursorMoveLeft", "Editor.ExtendSelectionEnds", "editor",
     "as 10x describes it: moves the later end of every selection one character later (grows it); a caret becomes a "
     "one-character selection"},
    {"MultiCursorMoveRight", "Editor.ShrinkSelectionEnds", "editor",
     "as 10x describes it: moves the later end of every selection one character earlier (shrinks it, never past the "
     "start)"},
    {"UndoSelectNextMultiCursor", "Editor.UndoAddCaretAtNextOccurrence", "editor",
     "undoes one SelectNextMultiCursor step while the carets and text are unchanged since it"},
    {"SelectAllOccurrences", "Editor.SelectAllOccurrences", "global",
     "the find bar's text and options when the bar is open, else the selected text or whole word at the caret"},
    {"AddCursorAtMousePos", "Editor.AddCaretAtMouse", "editor", "Control + middle click in an editor view"},
    {"StartDraggingRectSelect", "", "editor", ""},
    {"AlignMultiCursor", "Editor.AlignCarets", "editor",
     "pads with spaces; of several carets on a line the first moves"},
    {"CollapseRegion", "", "editor", ""},
    {"ExpandRegion", "", "editor", ""},
    {"CollapseAllRegions", "", "editor", ""},
    {"ExpandAllRegions", "", "editor", ""},
    {"CollapseAllRegionsRecursive", "", "editor", ""},
    {"ExpandAllRegionsRecursive", "", "editor", ""},
    {"ToggleCollapseExpandRegion", "", "editor", ""},
    {"ToggleCollapseExpandAllRegions", "", "editor", ""},
    {"RecordKeySequence", "", "global", ""},
    {"PlaybackKeySequence", "", "global", ""},
    {"ScrollOneLineUp", "Editor.ScrollLineUp", "editor", ""},
    {"ScrollOneLineDown", "Editor.ScrollLineDown", "editor", ""},
    {"PrevLocationCurrentFile", "Nav.BackInFile", "global", "skips the navigation history entries of other files"},
    {"NextLocationCurrentFile", "Nav.ForwardInFile", "global", "skips the navigation history entries of other files"},
    {"NextEditLocation", "", "global", ""},
    {"FindInFilePrevCurrentWord", "Editor.FindPrevCurrentWord", "global",
     "the word is matched as a whole word, case-sensitively"},
    {"FindInFileNextCurrentWord", "Editor.FindNextCurrentWord", "global",
     "the word is matched as a whole word, case-sensitively"},
    {"RestoreRecentlyClosedFile", "Workbench.ReopenClosedTab", "global",
     "reopens the last file whose last tab was closed, at its caret; repeats go further back"},
    {"MoveToMatchingBracket", "Editor.MoveToMatchingBracket", "editor", ""},
    {"NewSearchPanel", "Search.NewFindInFiles", "global",
     "keeps the current find-in-files results aside (the last three) and starts an empty list; Alt PageUp/PageDown "
     "in the picker bring them back"},
    {"PrevBookmarkInFile", "Editor.PrevBookmarkInFile", "global", "wraps within the current file"},
    {"NextBookmarkInFile", "Editor.NextBookmarkInFile", "global", "wraps within the current file"},
    {"RemoveAllBookmarks", "", "global", ""},
    {"RemoveAllBookmarksFromFile", "Editor.RemoveBookmarksInFile", "global", ""},
    {"ReplaceInFiles", "Search.ReplaceInFiles", "global",
     "a modal picker with a replace field; the replacement is literal text, closed files are written after one "
     "confirmation"},
    {"ShowFindInFilesResults1", "", "global", ""},
    {"ShowFindInFilesResults2", "", "global", ""},
    {"ShowFindInFilesResults3", "", "global", ""},
    {"ShowFindInFilesResults4", "", "global", ""},
    {"ToggleFindInFilesResultsPanel1", "", "global", ""},
    {"ToggleFindInFilesResultsPanel2", "", "global", ""},
    {"ToggleFindInFilesResultsPanel3", "", "global", ""},
    {"ToggleFindInFilesResultsPanel4", "", "global", ""},
    {"ReplaceAllInFile", "Editor.ReplaceAllInFile", "global",
     "uses the last query and replacement of the find/replace bar; one undo step"},
    {"ReplaceNextInFile", "Editor.ReplaceNextInFile", "global",
     "uses the last query and replacement of the find/replace bar; in the find-only bar Alt R still toggles regex"},
    {"MovePanelFocusLeft", "Workbench.FocusPanelLeft", "global", ""},
    {"MovePanelFocusRight", "Workbench.FocusPanelRight", "global", ""},
    {"MovePanelFocusUp", "Workbench.FocusPanelUp", "global", ""},
    {"MovePanelFocusDown", "Workbench.FocusPanelDown", "global", ""},
    {"MovePanelLeft", "Workbench.MovePanelLeft", "global", "swaps places with the neighbouring panel"},
    {"MovePanelRight", "Workbench.MovePanelRight", "global", "swaps places with the neighbouring panel"},
    {"MovePanelUp", "Workbench.MovePanelUp", "global", "swaps places with the neighbouring panel"},
    {"MovePanelDown", "Workbench.MovePanelDown", "global", "swaps places with the neighbouring panel"},
    {"DuplicatePanel", "Workbench.DuplicatePanel", "global", "the copy opens to the right and the focus stays"},
    {"DuplicatePanelLeft", "Workbench.DuplicatePanelLeft", "global", ""},
    {"DuplicatePanelRight", "Workbench.DuplicatePanelRight", "global", ""},
    {"CloseAllTabs", "", "global", ""},
    {"CloseAllOtherTabs", "", "global", ""},
    {"CloseAllButPinnedTabs", "", "global", ""},
    {"CloseTabsLeft", "", "global", ""},
    {"CloseTabsRight", "", "global", ""},
    {"CloseUnmodifiedTabs", "", "global", ""},
    {"PrevPanelTab", "Workbench.PrevTab", "global", ""},
    {"NextPanelTab", "Workbench.NextTab", "global", ""},
    {"MovePrevPanelTab", "Workbench.MoveTabPrev", "global", ""},
    {"MoveNextPanelTab", "Workbench.MoveTabNext", "global", ""},
    {"PinTab", "", "global", ""},
    {"TogglePinTab", "", "global", ""},
    {"ToggleTabs", "", "global", ""},
    {"SetColumnCountAuto", "", "global", ""},
    {"SetColumnCount1", "", "global", ""},
    {"SetColumnCount2", "", "global", ""},
    {"SetColumnCount3", "", "global", ""},
    {"SetColumnCount4", "", "global", ""},
    {"SetColumnCount5", "", "global", ""},
    {"SetColumnCount6", "", "global", ""},
    {"SetColumnCount7", "", "global", ""},
    {"SetColumnCount8", "", "global", ""},
    {"SetRowCountAuto", "", "global", ""},
    {"SetRowCount1", "", "global", ""},
    {"SetRowCount2", "", "global", ""},
    {"SetRowCount3", "", "global", ""},
    {"SetRowCount4", "", "global", ""},
    {"SetRowCount5", "", "global", ""},
    {"SetRowCount6", "", "global", ""},
    {"SetRowCount7", "", "global", ""},
    {"SetRowCount8", "", "global", ""},
    {"BuildAll", "", "global", ""},
    {"RebuildAll", "", "global", ""},
    {"CleanAll", "", "global", ""},
    {"RebuildActiveWorkspace", "Build.Rebuild", "global",
     "msbuild /t:Rebuild, cmake --build --clean-first, make clean && make, or the rebuildCommand setting"},
    {"CleanActiveWorkspace", "", "global", ""},
    {"BuildCurrentFile", "Build.BuildCurrentFile", "global",
     "compiles only the current saved file: msbuild /t:ClCompile, ninja object target or the compile database command"},
    {"ShowBuildOutput", "", "global", ""},
    {"CloseBuildPanel", "", "global", ""},
    {"GotoPrevBuildError", "", "global", ""},
    {"GotoNextBuildError", "", "global", ""},
    {"GotoPrevBuildWarning", "", "global", ""},
    {"GotoNextBuildWarning", "", "global", ""},
    {"VSDebuggerReconnect", "", "global", ""},
    {"PrevBreakpoint", "", "global", ""},
    {"NextBreakpoint", "", "global", ""},
    {"PrevBreakpointInFile", "", "global", ""},
    {"NextBreakpointInFile", "", "global", ""},
    {"RemoveAllBreakpointsFromFile", "Debug.DeleteBreakpointsInFile", "global", ""},
    {"CommandLineArgSelector", "", "global", ""},
    {"ToggleCommandLineArgSelector", "", "global", ""},
    {"OpenDebugPanels", "", "global", ""},
    {"OpenCallstackPanel", "", "global", ""},
    {"OpenWatchPanel", "", "global", ""},
    {"OpenAttachDebuggerPanel", "", "global", ""},
    {"ReparseFile", "", "global", ""},
    {"ReparseAll", "", "global", ""},
    {"GotoSymbolDefinitionUnderMouse", "Editor.GotoDefinitionUnderMouse", "global", ""},
    {"Autocomplete", "Editor.Autocomplete", "editor", ""},
    {"AutocompleteUp", "", "popup", ""},
    {"AutocompleteDown", "", "popup", ""},
    {"AutocompletePageUp", "", "popup", ""},
    {"AutocompletePageDown", "", "popup", ""},
    {"CancelAutocomplete", "", "global", ""},
    {"InsertPrevAutocomplete", "", "editor", ""},
    {"InsertNextAutocomplete", "", "editor", ""},
    {"ShowFunctionArgsInfo", "Editor.ShowFunctionArgs", "editor", ""},
    {"FindFunction", "Editor.FindFunction", "global", ""},
    {"RenameSymbol", "Editor.RenameSymbol", "editor", ""},
    {"SubmitRenameSymbol", "Editor.SubmitRenameSymbol", "popup", ""},
    {"ToggleParserVerboseLogging", "", "global", ""},
    {"ShowSymbolInfo", "Editor.ShowSymbolInfo", "global", ""},
    {"CppParser.LogParseStatus", "", "global", ""},
    {"CppParser.ListWorkspaceFiles", "", "global", ""},
    {"CppParser.FindBrokenDef", "", "global", ""},
    {"CppParser.DumpStringTable", "", "global", ""},
    {"CppParser.WriteFileStatsCsv", "", "global", ""},
    {"CppParser.LogIncludePaths", "", "global", ""},
    {"PerforceAdd", "", "global", ""},
    {"PerforceHistory", "", "global", ""},
    {"PerforceShowInP4v", "", "global", ""},
    {"PerforceAnnotate", "", "global", ""},
    {"PerforceTimelapse", "", "global", ""},
    {"PerforceRevisionGraph", "", "global", ""},
    {"PerforceRevertIfUnchanged", "", "global", ""},
    {"SVNDiff", "", "global", ""},
    {"SVNAdd", "", "global", ""},
    {"SVNUpdateFile", "", "global", ""},
    {"SVNUpdateWorkspace", "", "global", ""},
    {"GitDiff", "", "global", ""},
    {"GitAdd", "", "global", ""},
    {"GitBlame", "", "global", ""},
    {"ListCommands", "", "global", ""},
    {"Update", "", "global", ""},
    {"ToggleFPS", "", "global", ""},
    {"ShowContextMenu", "", "global", ""},
    {"ListWorkspaceFiles", "", "global", ""},
    {"ListFileStatusMonitorFiles", "", "global", ""},
    {"LogMemory", "", "global", ""},
    {"LogMemoryStats", "", "global", ""},
    {"CheckParserIntegrity", "", "global", ""},
};

// Commands tenfold has that 10x does not: the key context their bindings need.
struct NativeContext {
  const char* id;
  const char* context;
};
const NativeContext kNativeContexts[] = {
    {"Editor.Enter", "editor"},
    {"Editor.Tab", "editor"},
    {"Editor.Backspace", "editor"},
    {"Editor.CollapseCarets", "editor"},
    {"Editor.DeleteWordLeft", "editor"},
    {"Editor.DeleteWordRight", "editor"},
    {"Editor.JoinLines", "editor"},
    {"Editor.ToggleBlockComment", "editor"},
    {"Editor.ToggleLineComment", "editor"},
};

std::string lowered(std::string_view s) {
  std::string r(s);
  for (char& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return r;
}

struct Index {
  std::map<std::string, const CommandAlias*> by10x;      // lower-case 10x name
  std::map<std::string, const CommandAlias*> byTenfold;  // tenfold id -> first (preferred) row
  Index() {
    for (const CommandAlias& a : kAliases) {
      by10x.emplace(lowered(a.name10x), &a);
      if (*a.tenfoldId) byTenfold.emplace(a.tenfoldId, &a);
    }
  }
};
const Index& index() {
  static const Index i;
  return i;
}

}  // namespace

std::span<const CommandAlias> commandAliases() { return kAliases; }

ResolvedCommand resolveCommandName(std::string_view name) {
  const Index& ix = index();
  ResolvedCommand r;
  if (auto it = ix.by10x.find(lowered(name)); it != ix.by10x.end()) {
    const CommandAlias& a = *it->second;
    r.known10x = true;
    r.name10x = a.name10x;
    r.context = a.context;
    r.implemented = *a.tenfoldId != 0;
    r.id = r.implemented ? a.tenfoldId : a.name10x;
    return r;
  }
  r.id = std::string(name);
  r.context = "global";
  r.implemented = true;  // a tenfold id (or an unknown name; the registry decides)
  if (auto it = ix.byTenfold.find(r.id); it != ix.byTenfold.end()) {
    r.name10x = it->second->name10x;
    r.context = it->second->context;
  } else {
    for (const NativeContext& n : kNativeContexts)
      if (r.id == n.id) r.context = n.context;
  }
  return r;
}

std::string_view displayName10x(std::string_view tenfoldId) {
  const Index& ix = index();
  auto it = ix.byTenfold.find(std::string(tenfoldId));
  return it == ix.byTenfold.end() ? std::string_view{} : std::string_view(it->second->name10x);
}

}  // namespace tf::ui
