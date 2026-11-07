#include "ui/commands/EditorCommands.h"

namespace tf::ui {

namespace {

bool hasEditor(const CommandContext& c) { return c.editor != nullptr; }
bool hasDocument(const CommandContext& c) { return c.editor && c.editor->currentDocument(); }

void addEd(CommandRegistry& r, const char* id, const char* name, CommandHandler h) {
  r.add({id, name, std::move(h), hasDocument});
}
void addUi(CommandRegistry& r, const char* id, const char* name) { r.add({id, name, {}, {}}); }

struct CaretCmd {
  const char* id;  // suffix after "Editor.Caret" / "Editor.Select"
  const char* name;
  Editor::Direction dir;
  Editor::Unit unit;
};

}  // namespace

void registerEditorCommands(CommandRegistry& r) {
  // --- navigation ---
  r.add({"Nav.Back", "Navigate Back", [](const CommandContext& c, std::string_view) { return c.editor->back(); },
         [](const CommandContext& c) { return hasEditor(c) && c.editor->navigation().canGoBack(); }});
  r.add({"Nav.Forward", "Navigate Forward",
         [](const CommandContext& c, std::string_view) { return c.editor->forward(); },
         [](const CommandContext& c) { return hasEditor(c) && c.editor->navigation().canGoForward(); }});
  r.add({"Nav.BackInFile", "Navigate Back in Current File",
         [](const CommandContext& c, std::string_view) { return c.editor->backInFile(); }, hasDocument});
  r.add({"Nav.ForwardInFile", "Navigate Forward in Current File",
         [](const CommandContext& c, std::string_view) { return c.editor->forwardInFile(); }, hasDocument});
  r.add({"Nav.LastEditLocation", "Go to Last Edit Location",
         [](const CommandContext& c, std::string_view) {
           const auto loc = c.editor->lastEditLocation();
           return loc && c.editor->gotoLocation(*loc, nullptr, "last edit");
         },
         [](const CommandContext& c) { return hasEditor(c) && c.editor->lastEditLocation().has_value(); }});

  // --- semantic jumps: results go to CommandContext::onNavResult (the UI shows a picker for several) ---
  addEd(r, "Editor.GotoDefinition", "Go to Definition", [](const CommandContext& c, std::string_view) {
    c.editor->gotoDefinition(c.onNavResult);
    return true;
  });
  addEd(r, "Editor.GotoDeclaration", "Go to Declaration", [](const CommandContext& c, std::string_view) {
    c.editor->gotoDeclaration(c.onNavResult);
    return true;
  });
  addEd(r, "Editor.FindReferences", "Find All References", [](const CommandContext& c, std::string_view) {
    c.editor->findReferences(c.onNavResult);
    return true;
  });
  addEd(r, "Editor.SwitchHeaderSource", "Switch Header/Source", [](const CommandContext& c, std::string_view) {
    c.editor->switchSourceHeader([](bool, const std::string&) {});
    return true;
  });

  // --- caret movement (Editor.Caret*) and selection growth (Editor.Select*) ---
  using D = Editor::Direction;
  using U = Editor::Unit;
  static const CaretCmd caretCmds[] = {
      {"Left", "Left", D::Left, U::Char},
      {"Right", "Right", D::Right, U::Char},
      {"Up", "Up", D::Up, U::Line},
      {"Down", "Down", D::Down, U::Line},
      {"WordLeft", "Word Left", D::Left, U::Word},
      {"WordRight", "Word Right", D::Right, U::Word},
      {"LineStart", "Line Start", D::LineStart, U::Char},
      {"LineEnd", "Line End", D::LineEnd, U::Char},
      {"DocStart", "Document Start", D::DocStart, U::Char},
      {"DocEnd", "Document End", D::DocEnd, U::Char},
      {"PageUp", "Page Up", D::PageUp, U::Page},
      {"PageDown", "Page Down", D::PageDown, U::Page},
  };
  for (const CaretCmd& cc : caretCmds)
    for (bool extend : {false, true}) {
      std::string id = std::string(extend ? "Editor.Select" : "Editor.Caret") + cc.id;
      std::string name = std::string(extend ? "Select " : "Move Caret ") + cc.name;
      r.add({id, name,
             [cc, extend](const CommandContext& c, std::string_view) {
               if (!c.moveCarets || !c.moveCarets(cc.dir, cc.unit, extend))
                 c.editor->moveCarets(cc.dir, cc.unit, extend);
               return true;
             },
             hasDocument});
    }

  // --- editing ---
  addEd(r, "Editor.Backspace", "Backspace", [](const CommandContext& c, std::string_view) {
    c.editor->backspace();
    return true;
  });
  addEd(r, "Editor.Delete", "Delete", [](const CommandContext& c, std::string_view) {
    c.editor->del();
    return true;
  });
  // Enter / Tab are smart (B8): auto-indent, indent unit, indent a multi-line selection. See Editor::EditingOptions.
  addEd(r, "Editor.Enter", "Insert New Line", [](const CommandContext& c, std::string_view) {
    c.editor->insertNewline();
    return true;
  });
  addEd(r, "Editor.Tab", "Insert Tab / Indent", [](const CommandContext& c, std::string_view) {
    c.editor->insertTab();
    return true;
  });
  // Ctrl+Backspace / Ctrl+Delete: without a selection extend each caret by a word first, then delete.
  for (bool left : {true, false})
    addEd(r, left ? "Editor.DeleteWordLeft" : "Editor.DeleteWordRight", left ? "Delete Word Left" : "Delete Word Right",
          [left](const CommandContext& c, std::string_view) {
            bool any = false;
            for (const Selection& s : c.editor->selections()) any = any || !s.empty();
            if (!any) c.editor->moveCarets(left ? D::Left : D::Right, U::Word, true);
            if (left) c.editor->backspace();
            else c.editor->del();
            return true;
          });
  addEd(r, "Editor.CollapseCarets", "Collapse to Primary Caret", [](const CommandContext& c, std::string_view) {
    c.editor->collapseToPrimary();
    return true;
  });
  addEd(r, "File.Save", "Save",
        [](const CommandContext& c, std::string_view) { return c.editor->save(*c.editor->currentDocument()); });
  addEd(r, "Editor.Undo", "Undo", [](const CommandContext& c, std::string_view) { return c.editor->undo(); });
  addEd(r, "Editor.Redo", "Redo", [](const CommandContext& c, std::string_view) { return c.editor->redo(); });
  addEd(r, "Editor.SelectAll", "Select All", [](const CommandContext& c, std::string_view) {
    const TextBuffer& b = c.editor->currentDocument()->buffer();
    const int last = b.lineCount() - 1;
    c.editor->setSelections({Selection{{0, 0}, {last, static_cast<int>(b.line(last).size())}}});
    return true;
  });
  addEd(r, "Editor.AddCaretAbove", "Add Caret Above", [](const CommandContext& c, std::string_view) {
    c.editor->addCaretAbove();
    return true;
  });
  addEd(r, "Editor.AddCaretBelow", "Add Caret Below", [](const CommandContext& c, std::string_view) {
    c.editor->addCaretBelow();
    return true;
  });
  addEd(r, "Editor.AddCaretAtNextOccurrence", "Add Next Occurrence", [](const CommandContext& c, std::string_view) {
    c.editor->addCaretAtNextOccurrence();
    return true;
  });
  // --- 10x multi-cursor commands (W8d) ---
  // Nothing to undo / align is a quiet no-op, not a failure message.
  addEd(r, "Editor.UndoAddCaretAtNextOccurrence", "Undo Add Next Occurrence",
        [](const CommandContext& c, std::string_view) {
          c.editor->undoAddCaretAtNextOccurrence();
          return true;
        });
  addEd(r, "Editor.AlignCarets", "Align Carets", [](const CommandContext& c, std::string_view) {
    c.editor->alignCarets();
    return true;
  });
  addEd(r, "Editor.ExtendSelectionEnds", "Extend Selection Ends (All Carets)",
        [](const CommandContext& c, std::string_view) {
          c.editor->adjustSelectionEnds(true);
          return true;
        });
  addEd(r, "Editor.ShrinkSelectionEnds", "Shrink Selection Ends (All Carets)",
        [](const CommandContext& c, std::string_view) {
          c.editor->adjustSelectionEnds(false);
          return true;
        });
  addUi(r, "Editor.SelectAllOccurrences", "Select All Occurrences");  // reads the find bar: bound by the front end
  addUi(r, "Editor.AddCaretAtMouse", "Add Caret at Mouse Position");  // needs the pointer: bound by the front end

  // --- editing conveniences (B8): each is one undo step at every caret ---
  struct Simple {
    const char* id;
    const char* name;
    void (Editor::*fn)();
  };
  static const Simple simple[] = {
      {"Editor.Outdent", "Outdent Lines", &Editor::outdentLines},
      {"Editor.Indent", "Indent Lines", &Editor::indentLines},
      {"Editor.ToggleLineComment", "Toggle Line Comment", &Editor::toggleLineComment},
      {"Editor.ToggleBlockComment", "Toggle Block Comment", &Editor::toggleBlockComment},
      {"Editor.Duplicate", "Duplicate Line / Selection", &Editor::duplicate},
      {"Editor.JoinLines", "Join Lines", &Editor::joinLines},
      {"Editor.DeleteLines", "Delete Line", &Editor::deleteLines},
  };
  for (const Simple& sc : simple)
    addEd(r, sc.id, sc.name, [fn = sc.fn](const CommandContext& c, std::string_view) {
      (c.editor->*fn)();
      return true;
    });
  // --- 10x editing commands (W8a) ---
  static const Simple w8a[] = {
      {"Editor.MakeSelectionUppercase", "Make Selection Uppercase", &Editor::makeSelectionUppercase},
      {"Editor.MakeSelectionLowercase", "Make Selection Lowercase", &Editor::makeSelectionLowercase},
      {"Editor.CommentLines", "Comment Lines", &Editor::commentLines},
      {"Editor.UncommentLines", "Uncomment Lines", &Editor::uncommentLines},
      {"Editor.ToggleComment", "Toggle Comment", &Editor::toggleComment},
      {"Editor.DeleteToEndOfLine", "Delete to End of Line", &Editor::deleteToEndOfLine},
      {"Editor.InsertLineAbove", "Insert Line", &Editor::insertLineAbove},
      {"Editor.InsertLineBelow", "Insert Line After", &Editor::insertLineBelow},
      {"Editor.SelectCurrentWord", "Select Current Word", &Editor::selectCurrentWord},
      {"Editor.SelectCurrentScope", "Select Current Scope", &Editor::selectCurrentScope},
      {"Editor.SelectCurrentBracketScope", "Select Current Bracket Scope", &Editor::selectCurrentBracketScope},
  };
  for (const Simple& sc : w8a)
    addEd(r, sc.id, sc.name, [fn = sc.fn](const CommandContext& c, std::string_view) {
      (c.editor->*fn)();
      return true;
    });
  addEd(r, "Editor.MoveToMatchingBracket", "Move to Matching Bracket",
        [](const CommandContext& c, std::string_view) { return c.editor->moveToMatchingBracket(); });
  r.add({"File.SaveAll", "Save All", [](const CommandContext& c, std::string_view) { return c.editor->saveAll(); },
         hasEditor});
  addUi(r, "Editor.CutLine", "Cut Line");
  addUi(r, "View.ToggleWhitespace", "Toggle Whitespace");
  addUi(r, "Editor.ScrollLineUp", "Scroll One Line Up");
  addUi(r, "Editor.ScrollLineDown", "Scroll One Line Down");
  addUi(r, "Editor.FindNextCurrentWord", "Find Next Occurrence of Current Word");
  addUi(r, "Editor.FindPrevCurrentWord", "Find Previous Occurrence of Current Word");
  for (bool down : {false, true})
    addEd(r, down ? "Editor.MoveLinesDown" : "Editor.MoveLinesUp", down ? "Move Lines Down" : "Move Lines Up",
          [down](const CommandContext& c, std::string_view) {
            c.editor->moveLines(down);
            return true;
          });
  addEd(r, "Editor.ToggleBookmark", "Toggle Bookmark", [](const CommandContext& c, std::string_view) {
    c.editor->toggleBookmark();
    return true;
  });
  for (bool fwd : {true, false})
    r.add({fwd ? "Editor.NextBookmark" : "Editor.PrevBookmark", fwd ? "Next Bookmark" : "Previous Bookmark",
           [fwd](const CommandContext& c, std::string_view) { return c.editor->gotoBookmark(fwd); },
           [](const CommandContext& c) { return hasEditor(c) && !c.editor->bookmarks().empty(); }});
  for (bool fwd : {true, false})
    addEd(r, fwd ? "Editor.NextBookmarkInFile" : "Editor.PrevBookmarkInFile",
          fwd ? "Next Bookmark in File" : "Previous Bookmark in File",
          [fwd](const CommandContext& c, std::string_view) { return c.editor->gotoBookmarkInFile(fwd); });
  addEd(r, "Editor.RemoveBookmarksInFile", "Remove All Bookmarks from File",
        [](const CommandContext& c, std::string_view) {
          c.editor->removeBookmarksInFile();
          return true;
        });
  // The status bar's line-ending menu: converts every line break of the document (one undo step, document modified).
  struct EolCmd {
    const char* id;
    const char* name;
    LineEnding e;
  };
  for (const EolCmd& ec :
       {EolCmd{"Editor.LineEndingLF", "Line Endings: Unix (LF)", LineEnding::LF},
        EolCmd{"Editor.LineEndingCRLF", "Line Endings: Windows (CR LF)", LineEnding::CRLF},
        EolCmd{"Editor.LineEndingCR", "Line Endings: Macintosh (CR)", LineEnding::CR},
        EolCmd{"Editor.LineEndingLS", "Line Endings: Unicode Line Separator (LS)", LineEnding::LS},
        EolCmd{"Editor.LineEndingPS", "Line Endings: Unicode Paragraph Separator (PS)", LineEnding::PS},
        EolCmd{"Editor.LineEndingNEL", "Line Endings: Next Line (NEL)", LineEnding::NEL}})
    addEd(r, ec.id, ec.name, [e = ec.e](const CommandContext& c, std::string_view) {
      c.editor->setLineEnding(*c.editor->currentDocument(), e);
      return true;
    });
  // Advanced Save Options (Line endings list): bound by the UI, which owns the list popup.
  r.add({"File.SaveWithLineEndings", "Save with Line Endings...", nullptr,
         [](const CommandContext& c) { return hasEditor(c); }});

  // Encoding widget (VS_BEHAVIOUR E3-E5): the pickers are bound by the UI, which owns the list popups.
  r.add({"File.ReopenWithEncoding", "Reopen with Encoding...", nullptr,
         [](const CommandContext& c) { return hasEditor(c); }});
  r.add({"File.SaveWithEncoding", "Save with Encoding...", nullptr,
         [](const CommandContext& c) { return hasEditor(c); }});

  // --- debugger (C2c): Visual Studio's commands; they queue work on the DebugSession (asynchronous, errors reach
  // lastError and the status bar) and edit the breakpoint store, which the session syncs to the backend ---
  struct DebugCmd {
    const char* id;
    const char* name;
    bool (DebugSession::*fn)(DebugSession::Done);
  };
  static const DebugCmd debugCmds[] = {
      {"Debug.Start", "Start Debugging / Continue", &DebugSession::start},
      {"Debug.StartWithoutDebugging", "Start Without Debugging", &DebugSession::startWithoutDebugging},
      {"Debug.Stop", "Stop Debugging", &DebugSession::stop},
      {"Debug.Restart", "Restart Debugging", &DebugSession::restart},
      {"Debug.BreakAll", "Break All", &DebugSession::breakAll},
      {"Debug.StepOver", "Step Over", &DebugSession::stepOver},
      {"Debug.StepInto", "Step Into", &DebugSession::stepInto},
      {"Debug.StepOut", "Step Out", &DebugSession::stepOut},
  };
  for (const DebugCmd& dc : debugCmds)
    r.add({dc.id, dc.name,
           [fn = dc.fn](const CommandContext& c, std::string_view) { return (c.editor->debug().*fn)({}); }, hasEditor});
  addEd(r, "Debug.RunToCursor", "Run to Cursor", [](const CommandContext& c, std::string_view) {
    return c.editor->debug().runToCursor(c.editor->currentDocument()->path(), c.editor->selections()[0].head.line);
  });
  addEd(r, "Debug.ToggleBreakpoint", "Toggle Breakpoint", [](const CommandContext& c, std::string_view) {
    c.editor->toggleBreakpoint();
    return true;
  });
  addEd(r, "Debug.ToggleBreakpointEnabled", "Enable / Disable Breakpoint",
        [](const CommandContext& c, std::string_view) {
          const auto bp =
              c.editor->breakpoints().get(c.editor->currentDocument()->path(), c.editor->selections()[0].head.line);
          if (!bp) return false;
          c.editor->breakpoints().setEnabled(bp->file, bp->line, !bp->enabled);
          c.editor->saveBreakpoints();
          return true;
        });
  r.add({"Debug.DeleteAllBreakpoints", "Delete All Breakpoints",
         [](const CommandContext& c, std::string_view) {
           c.editor->breakpoints().clear();
           c.editor->saveBreakpoints();
           return true;
         },
         [](const CommandContext& c) { return hasEditor(c) && !c.editor->breakpoints().empty(); }});

  addEd(r, "Debug.DeleteBreakpointsInFile", "Delete All Breakpoints in File",
        [](const CommandContext& c, std::string_view) {
          c.editor->removeBreakpointsInFile();
          return true;
        });

  // --- UI-only: the front end binds these (B3b / B5 / B8) ---
  addUi(r, "Debug.ShowBreakpoints", "Show Breakpoints...");
  addUi(r, "Debug.OpenInVisualStudio", "Open in Visual Studio");
  addUi(r, "Build.SelectConfiguration", "Select Solution Configuration...");
  addUi(r, "Debug.EditCondition", "Edit Breakpoint Condition...");
  addUi(r, "Editor.GotoLine", "Go to Line...");
  addUi(r, "Editor.Find", "Find in File...");
  addUi(r, "Editor.Replace", "Replace in File...");
  addUi(r, "Editor.FindNext", "Find Next");
  addUi(r, "Editor.FindPrev", "Find Previous");
  addUi(r, "Editor.ListBookmarks", "List Bookmarks...");
  addUi(r, "File.OpenFile", "Open File...");
  addUi(r, "Search.SymbolSearch", "Go to Symbol in Workspace...");
  addUi(r, "Search.FindInWorkspace", "Find in Workspace...");
  addUi(r, "Search.NewFindInFiles", "New Find in Files Results...");
  addUi(r, "Search.ReplaceInFiles", "Replace in Files...");
  addUi(r, "Editor.ReplaceNextInFile", "Replace Next in File");
  addUi(r, "Editor.ReplaceAllInFile", "Replace All in File");
  addUi(r, "Search.NextResult", "Next Search or Build Result");
  addUi(r, "Search.PrevResult", "Previous Search or Build Result");
  addUi(r, "View.ShowDiagnostics", "Show Diagnostics");
  addUi(r, "Build.Build", "Build (argument: custom command)");
  addUi(r, "Build.Rebuild", "Rebuild (argument: custom command)");
  addUi(r, "Build.BuildCurrentFile", "Build Current File");
  addUi(r, "Build.Cancel", "Cancel Build");
  addUi(r, "Build.NextError", "Next Build Error");
  addUi(r, "Build.PrevError", "Previous Build Error");
  addUi(r, "View.ToggleBuildPanel", "Toggle Build Panel");
  addUi(r, "View.ToggleWorkspaceExplorer", "Toggle Workspace Explorer");
  addUi(r, "View.ShowInWorkspaceTree", "Show Current File in Workspace Tree");
  addUi(r, "View.ShowInWorkspaceTreeWithToggle", "Show Current File in Workspace Tree (Close if Shown)");
  addUi(r, "View.ToggleExplorerFolderView", "Switch Workspace Explorer View (Solution / Folder)");
  addUi(r, "View.ToggleExplorerShowAllFiles", "Workspace Explorer: Show All Files");
  addUi(r, "Explorer.NewFile", "Workspace Explorer: New File");
  addUi(r, "Explorer.NewFolder", "Workspace Explorer: New Folder");
  addUi(r, "Explorer.Rename", "Workspace Explorer: Rename");
  addUi(r, "Explorer.Delete", "Workspace Explorer: Delete");
  addUi(r, "Explorer.Properties", "Workspace Explorer: Properties");
  addUi(r, "View.ToggleWordWrap", "Toggle Word Wrap");
  // Code folding (W8f): bound to the focused text view by EditorApp.
  addUi(r, "Editor.CollapseRegion", "Collapse Region");
  addUi(r, "Editor.ExpandRegion", "Expand Region");
  addUi(r, "Editor.ToggleCollapseExpandRegion", "Toggle Collapse/Expand Region");
  addUi(r, "Editor.ToggleCollapseExpandAllRegions", "Toggle Collapse/Expand All Regions");
  addUi(r, "Editor.CollapseAllRegions", "Collapse All Regions");
  addUi(r, "Editor.ExpandAllRegions", "Expand All Regions");
  addUi(r, "Editor.CollapseAllRegionsRecursive", "Collapse All Regions Recursively");
  addUi(r, "Editor.ExpandAllRegionsRecursive", "Expand All Regions Recursively");
  addUi(r, "Editor.CollapseToDefinitions", "Collapse to Definitions");
  addUi(r, "Help.ShowCompileCommand", "Show Compile Command for Current File");
  addUi(r, "Vcs.NextChange", "Next Change");
  addUi(r, "Vcs.PrevChange", "Previous Change");
  addUi(r, "Vcs.ToggleInlineDiff", "Toggle Inline Diff");
  addUi(r, "Vcs.RevertHunk", "Revert Change");
  addUi(r, "Vcs.RevertFile", "Revert File...");
  addUi(r, "Vcs.DiffFileExternal", "Diff File in External Tool");
  addUi(r, "Vcs.RefreshStatus", "Refresh Version Control Status");
  addUi(r, "Vcs.CheckOut", "Check Out (Perforce)");
  addUi(r, "Workbench.CycleMruNext", "Next Recent Document");
  addUi(r, "Workbench.CycleMruPrev", "Previous Recent Document");
  addUi(r, "Workbench.FocusPanel", "Focus Panel (argument: index)");
  addUi(r, "Workbench.CloseTab", "Close Tab");
  addUi(r, "Workbench.CloseOtherTabs", "Close All Other Tabs");
  addUi(r, "Workbench.CloseAllButPinned", "Close All But Pinned Tabs");
  addUi(r, "Workbench.CloseAllDocuments", "Close All Documents");
  addUi(r, "Workbench.ClosePanel", "Close Panel");
  addUi(r, "Workbench.PinTab", "Pin Tab");
  addUi(r, "Workbench.TogglePinTab", "Toggle Pin Tab");
  addUi(r, "File.CopyPath", "Copy Full Path");
  addUi(r, "App.ShowContextMenu", "Show Context Menu");
  addUi(r, "Workbench.ReopenClosedTab", "Reopen Closed Tab");
  // W8b: tabs, panels and view
  addUi(r, "Workbench.PrevTab", "Previous Tab in Panel");
  addUi(r, "Workbench.NextTab", "Next Tab in Panel");
  addUi(r, "Workbench.MoveTabPrev", "Move Tab Earlier in Panel");
  addUi(r, "Workbench.MoveTabNext", "Move Tab Later in Panel");
  addUi(r, "Workbench.FocusPanelLeft", "Focus Panel to the Left");
  addUi(r, "Workbench.FocusPanelRight", "Focus Panel to the Right");
  addUi(r, "Workbench.FocusPanelUp", "Focus Panel Above");
  addUi(r, "Workbench.FocusPanelDown", "Focus Panel Below");
  addUi(r, "Workbench.MovePanelLeft", "Move Panel Left");
  addUi(r, "Workbench.MovePanelRight", "Move Panel Right");
  addUi(r, "Workbench.MovePanelUp", "Move Panel Up");
  addUi(r, "Workbench.MovePanelDown", "Move Panel Down");
  addUi(r, "Workbench.DuplicatePanel", "Duplicate Panel");
  addUi(r, "Workbench.DuplicatePanelLeft", "Duplicate Panel to the Left");
  addUi(r, "Workbench.DuplicatePanelRight", "Duplicate Panel to the Right");
  addUi(r, "View.ZoomIn", "Zoom In");
  addUi(r, "View.ZoomOut", "Zoom Out");
  addUi(r, "View.ToggleFullscreen", "Toggle Fullscreen");
  addUi(r, "View.ToggleFocusMode", "Toggle Focus Mode");
  addUi(r, "View.ToggleFullscreenFocusMode", "Toggle Fullscreen Focus Mode");
  addUi(r, "File.New", "New Text File");
  addUi(r, "File.ShowInFolder", "Show Current File in File Manager");
  addUi(r, "Editor.InternetSearch", "Internet Search for Word or Selection");
  addUi(r, "App.Quit", "Quit");
  addUi(r, "App.CommandPalette", "Command Palette...");
  addUi(r, "Clangd.Restart", "Restart clangd");
  addUi(r, "Editor.Copy", "Copy");
  addUi(r, "Editor.Cut", "Cut");
  addUi(r, "Editor.Paste", "Paste");
  // W8f: paste history, key sequences, Perforce history / p4v
  addUi(r, "Editor.PasteHistory", "Paste from History...");
  addUi(r, "App.RecordKeySequence", "Start / Stop Recording Key Sequence");
  addUi(r, "App.PlaybackKeySequence", "Play Back Key Sequence");
  addUi(r, "Vcs.PerforceHistory", "Perforce History...");
  addUi(r, "Vcs.ShowInP4v", "Show in p4v");
  // W8f terminal panel
  addUi(r, "Terminal.New", "New Terminal");
  addUi(r, "Terminal.Show", "Show Terminal");
  addUi(r, "Terminal.NewClaude", "New Claude Code Terminal");
  addUi(r, "Terminal.NewCursor", "New Cursor Terminal");
  addUi(r, "Terminal.Copy", "Copy Terminal Selection");
  addUi(r, "Terminal.Paste", "Paste into Terminal");
  addUi(r, "Terminal.OpenAt", "Open in Terminal");
  // W8c parser commands
  addUi(r, "Editor.RenameSymbol", "Rename Symbol...");
  addUi(r, "Editor.SubmitRenameSymbol", "Submit Rename Symbol");
  addUi(r, "Editor.Autocomplete", "Show Autocomplete");
  addUi(r, "Editor.ShowFunctionArgs", "Show Function Arguments");
  addUi(r, "Editor.ShowSymbolInfo", "Show Symbol Info");
  addUi(r, "Editor.FindFunction", "Find Function in File...");
  addUi(r, "Editor.GotoDefinitionUnderMouse", "Go to Definition Under Mouse");
  addUi(r, "Editor.FormatSelection", "Format Selection or Scope");
}

}  // namespace tf::ui
