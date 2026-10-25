#pragma once
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "ui/commands/KeyMap.h"

// Key binding files: plain text, one binding per line.
//
//   [context] chord [chord ...] = Command.Id [argument]
//
// `[context]` is optional (default "global"); `#` starts a comment when it is the first non-blank character;
// key names are case-insensitive. A command of `-` removes the binding (to drop a default). The parser never
// throws and never stops at an error: bad lines are reported in `diags` and skipped. A future importer for
// 10x's KeyMappings.10x_settings only has to emit this text (or call KeyMap::add).
namespace tf::ui {

struct KeyBindingDiag {
  std::string source;  // file name / label
  int line = 0;        // 1-based; 0 = whole file (e.g. unreadable)
  bool warning = false;
  std::string message;
};
std::string formatDiag(const KeyBindingDiag& d);  // "keybindings.txt:12: error: ..." (status bar / log)

// One key or mouse stroke such as "Ctrl+Shift+-" or "Mouse4". On failure returns false and sets `error`.
bool parseKeyChord(std::string_view text, KeyChord& out, std::string& error);

// Adds the bindings of `text` to `into` (later lines and later calls win). A duplicate inside this one text is a
// warning; overriding a binding from an earlier layer is not.
void parseKeyBindings(std::string_view text, std::string_view source, KeyMap& into, std::vector<KeyBindingDiag>& diags);
// Missing file: nothing happens (no diagnostic). Unreadable file: one diagnostic.
void loadKeyBindingFile(const std::filesystem::path& file, KeyMap& into, std::vector<KeyBindingDiag>& diags);

// assets/keybindings.default.txt, embedded in the binary.
std::string_view defaultKeyBindingsText();
// User-global config dir (%APPDATA%/tenfold, ~/Library/Application Support/tenfold, $XDG_CONFIG_HOME/tenfold or
// ~/.config/tenfold); empty if it cannot be determined.
std::filesystem::path userConfigDir();

// defaults, then `<userConfigDir>/keybindings.txt`, then `<workspaceRoot>/.tenfold/keybindings.txt`.
// Empty paths are skipped.
KeyMap loadKeyBindings(const std::filesystem::path& workspaceRoot, const std::filesystem::path& userDir,
                       std::vector<KeyBindingDiag>& diags);

}  // namespace tf::ui
