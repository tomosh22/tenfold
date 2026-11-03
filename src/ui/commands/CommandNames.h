#pragma once
#include <span>
#include <string>
#include <string_view>

// 10x command names (from 10x's Commands.txt / KeyMappings.txt) and the tenfold commands that do the same thing.
//
// Key binding files name commands the 10x way ("GotoSymbolDefinition"); tenfold's own ids ("Editor.GotoDefinition")
// keep working everywhere. The alias table lists every 10x command: those tenfold implements point at a tenfold
// command id, the others have an empty id and report "not implemented yet: <Name>" when their key is pressed.
// 10x names are matched case-insensitively (10x itself accepts "Openfile").
namespace tf::ui {

struct CommandAlias {
  const char* name10x;
  const char* tenfoldId;  // empty: tenfold does not implement this 10x command
  const char* context;    // key context the binding belongs in: "global", "editor", "popup"
  const char* note;       // how the tenfold command differs from the 10x one; empty when it matches
};

// All 10x commands. For a tenfold id with several 10x names the first row is the name the palette shows.
std::span<const CommandAlias> commandAliases();

struct ResolvedCommand {
  std::string id;       // tenfold command id; for an unimplemented 10x command its canonical 10x name; else as written
  std::string name10x;  // canonical 10x spelling, empty if the command has no 10x name
  std::string context;  // key context for bindings of this command ("global" unless the table says otherwise)
  bool known10x = false;     // the text named a 10x command
  bool implemented = false;  // `id` is a tenfold command id (false: an unimplemented 10x command)
};
// Looks `name` up as a 10x command name (case-insensitive) and otherwise treats it as a tenfold id (kept as written).
ResolvedCommand resolveCommandName(std::string_view name);

// The 10x name the palette shows for a tenfold command id; empty if 10x has no such command.
std::string_view displayName10x(std::string_view tenfoldId);

// Pseudo command carried by bindings whose value is a Python statement (10x allows them, tenfold does not run
// them): the statement is the binding's argument.
inline constexpr const char* kPythonCommand = "Unsupported.Python";

}  // namespace tf::ui
