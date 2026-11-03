#pragma once
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "ui/commands/KeyMap.h"

// Key binding files use 10x's format (a regular .10x_settings file, see 10x's KeyBindings / SettingsFiles docs):
//
//   <keys>: <command>
//
// `<keys>` is one or more chords separated by a comma; a chord is its modifiers then its key, separated by spaces:
//   Control Shift F: Search
//   Control S, Control S: SaveAll
//   Shift Alt O: FindFile
// Modifiers: Control, Shift, Alt. Keys: A-Z, 0-9, F1-F24, Up Down Left Right Home End PageUp PageDown Enter Tab
// Space Backspace Delete Insert Cancel AppsKey Escape, the punctuation keys ' . / = [ ] \ - ; ` (a comma is written
// Comma), and the mouse buttons MouseMiddleButton MouseBackButton MouseForwardButton. Names are case-insensitive.
// A space after the colon is required (as in every 10x settings file); `#` starts a comment when it is the first
// non-blank character.
//
// `<command>` is a 10x command name (CommandNames.h maps it to the tenfold command) or a tenfold command id,
// optionally followed by an argument (tenfold extension, e.g. "Workbench.FocusPanel 2"). An empty value removes the
// binding of an earlier layer. 10x also accepts Python statements there; those load as a binding that reports "not
// supported" when pressed, with a warning. Each binding's key context ("global", "editor", "popup") comes from its
// command (CommandNames.h).
//
// The parser never throws and never stops at an error: bad lines are reported in `diags` and skipped. A later layer
// replaces earlier bindings of the same keys in every context.
namespace tf::ui {

struct KeyBindingDiag {
  std::string source;  // file name / label
  int line = 0;        // 1-based; 0 = whole file (e.g. unreadable)
  bool warning = false;
  std::string message;
};
std::string formatDiag(const KeyBindingDiag& d);  // "KeyMappings.10x_settings:12: error: ..." (status bar / log)

inline constexpr const char* kKeyMappingsFileName = "KeyMappings.10x_settings";

// One stroke such as "Control Shift -" or "MouseBackButton". On failure returns false and sets `error`.
bool parseKeyChord(std::string_view text, KeyChord& out, std::string& error);
// A chord sequence such as "Control K, Control C" (one stroke is a sequence of one).
bool parseKeyChords(std::string_view text, std::vector<KeyChord>& out, std::string& error);

// Adds the bindings of `text` to `into` (later lines and later calls win). A duplicate inside this one text is a
// warning; overriding a binding from an earlier layer is not.
void parseKeyBindings(std::string_view text, std::string_view source, KeyMap& into, std::vector<KeyBindingDiag>& diags);
// Missing file: nothing happens (no diagnostic). Unreadable file: one diagnostic.
void loadKeyBindingFile(const std::filesystem::path& file, KeyMap& into, std::vector<KeyBindingDiag>& diags);

// assets/KeyMappings.default.10x_settings (10x's default key map plus a marked "tenfold additions" section),
// embedded in the binary.
std::string_view defaultKeyBindingsText();
// Marks the start of the tenfold-only section of the defaults file.
inline constexpr const char* kTenfoldAdditionsMarker = "# ===== tenfold additions";
// User-global config dir (%APPDATA%/tenfold, ~/Library/Application Support/tenfold, $XDG_CONFIG_HOME/tenfold or
// ~/.config/tenfold); empty if it cannot be determined.
std::filesystem::path userConfigDir();
// An installed 10x's key file: %APPDATA%\10x\Settings\KeyMappings.10x_settings on Windows; empty elsewhere.
std::filesystem::path default10xKeyMappingsPath();

struct KeyLoadOptions {
  std::filesystem::path workspaceRoot;  // `<root>/.tenfold/KeyMappings.10x_settings`
  std::filesystem::path userDir;        // `<userDir>/KeyMappings.10x_settings`
  std::filesystem::path import10xFile;  // a 10x key file to import (default10xKeyMappingsPath()); empty = none
  bool import10x = true;                // setting keys.import10x
};

// Layers, later wins: the embedded defaults, the imported 10x file, the user's tenfold file, the workspace file.
// Empty paths are skipped, missing files are silent. A leftover `keybindings.txt` (the old tenfold format, no longer
// read) produces a warning that points at the new file.
KeyMap loadKeyBindings(const KeyLoadOptions& opt, std::vector<KeyBindingDiag>& diags);
// Same without the 10x import.
KeyMap loadKeyBindings(const std::filesystem::path& workspaceRoot, const std::filesystem::path& userDir,
                       std::vector<KeyBindingDiag>& diags);

}  // namespace tf::ui
