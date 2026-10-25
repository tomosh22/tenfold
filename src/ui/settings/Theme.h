#pragma once
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "ui/settings/Settings.h"
#include "ui/widgets/Theme.h"

// Theme files (B7): same `key = value` format as settings. Keys: `color.<member of ui::Theme>` (e.g.
// color.statusBackground) and `token.<kind>[.<modifier>]`. Kinds: lexical (keyword identifier number string char
// comment preprocessor operator punctuation) and clangd semantic token types (namespace type class struct interface
// enum enumMember typeParameter concept function method property variable parameter macro event decorator modifier
// regexp); modifiers: declaration definition readonly static deprecated abstract async modification documentation
// defaultLibrary. A theme file only overrides what it names; the rest comes from `base`.
namespace tf::ui {

// Applies `text` on top of `base`. Unknown keys / bad colours are diagnostics (never fatal).
Theme parseTheme(std::string_view text, std::string_view source, std::vector<SettingsDiag>& diags,
                 const Theme& base = Theme::darkDefault());
// Every colour member name accepted after `color.`, and the token kinds / modifiers.
const std::vector<std::string>& themeColorNames();
const std::vector<std::string>& themeTokenKinds();
const std::vector<std::string>& themeTokenModifiers();
// Keys a shipped theme must define: all non-optional color.* (the optional ones are listed in themeOptionalColorNames)
// plus the lexical kinds and the common semantic types.
std::vector<std::string> requiredThemeKeys();
// Colour names whose transparent default means "legacy look"; vs-dark defines them all.
std::vector<std::string> themeOptionalColorNames();

std::string_view builtinThemeText(std::string_view name);  // "dark", "light", "vs-dark"; empty if unknown
// Path of the file a named theme resolves to (workspace .tenfold/themes, then userDir/themes); empty = built-in.
std::filesystem::path findThemeFile(std::string_view name, const std::filesystem::path& workspaceRoot,
                                    const std::filesystem::path& userDir);
// Resolves `name` and parses it; unknown name -> diagnostic and the dark built-in. `fingerprint` (optional) receives
// a string that changes whenever the resolved theme text does.
Theme loadTheme(std::string_view name, const std::filesystem::path& workspaceRoot, const std::filesystem::path& userDir,
                std::vector<SettingsDiag>& diags, std::string* fingerprint = nullptr);

}  // namespace tf::ui
