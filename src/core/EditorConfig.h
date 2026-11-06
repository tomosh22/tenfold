#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace tf {

// The `.editorconfig` properties tenfold reads (VS_BEHAVIOUR Z19: Visual Studio honours .editorconfig; tenfold takes
// the two whitespace properties as extra defaults for saving).
struct EditorConfigProps {
  std::optional<bool> trimTrailingWhitespace;  // trim_trailing_whitespace
  std::optional<bool> insertFinalNewline;      // insert_final_newline
};

// The properties that apply to `file`: every `.editorconfig` from the file's folder up to the first one with
// `root = true` (or the filesystem root) is read; the closer file wins and, inside a file, the later matching section.
// Section globs: `*`, `**`, `?`, `[abc]`, `[!abc]`, `{a,b}`; a glob without '/' matches the file name in any folder,
// one with '/' is relative to the folder of the .editorconfig. Unknown properties and values are ignored. Missing or
// unreadable files give an empty result.
EditorConfigProps editorConfigFor(const std::filesystem::path& file);

// The glob match alone (exposed for tests): `glob` from a section header, `relPath` of the file below the
// .editorconfig's folder with '/' separators.
bool editorConfigGlobMatches(std::string_view glob, std::string_view relPath);

}  // namespace tf
