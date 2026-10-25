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
  for (bool crlf : {false, true})
    addEd(r, crlf ? "Editor.LineEndingCRLF" : "Editor.LineEndingLF", crlf ? "Line Endings: CRLF" : "Line Endings: LF",
          [crlf](const CommandContext& c, std::string_view) {
            c.editor->setLineEnding(*c.editor->currentDocument(), crlf ? LineEnding::CRLF : LineEnding::LF);
            return true;
          });

  // --- UI-only: the front end binds these (B3b / B5 / B8) ---
  addUi(r, "Editor.GotoLine", "Go to Line...");
  addUi(r, "Editor.Find", "Find in File...");
  addUi(r, "Editor.Replace", "Replace in File...");
  addUi(r, "Editor.FindNext", "Find Next");
  addUi(r, "Editor.FindPrev", "Find Previous");
  addUi(r, "Editor.ListBookmarks", "List Bookmarks...");
  addUi(r, "File.OpenFile", "Open File...");
  addUi(r, "Search.SymbolSearch", "Go to Symbol in Workspace...");
  addUi(r, "Search.FindInWorkspace", "Find in Workspace...");
  addUi(r, "Search.NextResult", "Next Search Result");
  addUi(r, "Search.PrevResult", "Previous Search Result");
  addUi(r, "View.ShowDiagnostics", "Show Diagnostics");
  addUi(r, "Build.Build", "Build (argument: custom command)");
  addUi(r, "Build.Cancel", "Cancel Build");
  addUi(r, "Build.NextError", "Next Build Error");
  addUi(r, "Build.PrevError", "Previous Build Error");
  addUi(r, "View.ToggleBuildPanel", "Toggle Build Panel");
  addUi(r, "View.ToggleWordWrap", "Toggle Word Wrap");
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
  addUi(r, "App.Quit", "Quit");
  addUi(r, "Editor.Copy", "Copy");
  addUi(r, "Editor.Cut", "Cut");
  addUi(r, "Editor.Paste", "Paste");
}

}  // namespace tf::ui
