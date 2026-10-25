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
  // Plain "\n" / "\t": auto-indent and tab-width/spaces policy come with the editing settings (later).
  addEd(r, "Editor.Enter", "Insert New Line", [](const CommandContext& c, std::string_view) {
    c.editor->insertText("\n");
    return true;
  });
  addEd(r, "Editor.Tab", "Insert Tab", [](const CommandContext& c, std::string_view) {
    c.editor->insertText("\t");
    return true;
  });
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

  // --- UI-only: the front end binds these (B3b / B5) ---
  addUi(r, "File.OpenFile", "Open File...");
  addUi(r, "Search.SymbolSearch", "Go to Symbol in Workspace...");
  addUi(r, "Search.FindInWorkspace", "Find in Workspace...");
  addUi(r, "Workbench.CycleMruNext", "Next Recent Document");
  addUi(r, "Workbench.CycleMruPrev", "Previous Recent Document");
  addUi(r, "Workbench.FocusPanel", "Focus Panel (argument: index)");
  addUi(r, "Editor.Copy", "Copy");
  addUi(r, "Editor.Cut", "Cut");
  addUi(r, "Editor.Paste", "Paste");
}

}  // namespace tf::ui
