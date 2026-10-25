#pragma once
#include "ui/commands/CommandRegistry.h"

namespace tf::ui {

// Registers every command the engine can serve by itself (navigation, semantic jumps, caret movement, editing,
// undo/redo, multi-caret) against CommandContext::editor, plus the UI-only commands (file/symbol pickers,
// find-in-workspace, MRU cycling, clipboard, panel focus, the Vcs.* commands of VcsUi) WITHOUT a handler: running one
// of those before the front end called CommandRegistry::bind() yields RunStatus::Unbound.
void registerEditorCommands(CommandRegistry& r);

}  // namespace tf::ui
