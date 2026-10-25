#pragma once
#include <functional>
#include <string>
#include <string_view>

#include "app/Editor.h"

// A command is a named user action. Key bindings, menus, the command palette and (later) scripting all run
// commands through the CommandRegistry, never by calling UI code directly.
namespace tf::ui {

struct CommandContext {
  Editor* editor = nullptr;
  std::string_view context;  // the active key context ("editor", "picker", ...); informational for handlers
  // Receives the result of the semantic navigation commands (GotoDefinition, FindReferences, ...). The editor
  // jumps by itself when the answer is a single location; with several candidates only this callback sees them
  // (the UI shows a picker). Null: results are dropped.
  Editor::NavResult onNavResult;
  // Caret movement by the view's own geometry (soft wrap: Up / Down / Home / End move by visual row). The caret
  // commands try it first; true = handled, false or null = Editor::moveCarets does it.
  std::function<bool(Editor::Direction, Editor::Unit, bool extend)> moveCarets;
};

using CommandHandler = std::function<bool(const CommandContext&, std::string_view arg)>;
using CommandEnabled = std::function<bool(const CommandContext&)>;

struct Command {
  std::string id;          // "Editor.GotoDefinition"
  std::string name;        // "Go to Definition" (command palette label)
  CommandHandler handler;  // empty: registered but not bound by the front end yet
  CommandEnabled enabled;  // empty: always enabled

  bool isEnabled(const CommandContext& ctx) const { return !enabled || enabled(ctx); }
};

enum class RunStatus { Ok, UnknownCommand, Disabled, Unbound, Failed };

struct RunResult {
  RunStatus status = RunStatus::Ok;
  std::string message;  // empty when Ok
  bool ok() const { return status == RunStatus::Ok; }
};

// Runs one command: a missing handler is Unbound, enabled() false is Disabled, a false return is Failed.
RunResult runCommand(const Command& c, const CommandContext& ctx, std::string_view arg);

}  // namespace tf::ui
