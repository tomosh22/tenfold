#pragma once
// Text and file-system helpers behind two navigation answers (VS-NAV):
//   * Go To Document on an `#include` line (VS_BEHAVIOUR N1): which directive the caret is on, and which file the
//     compiler would pick for it (the includer's directory for "x.h", then the -iquote / -I / -isystem directories of
//     the includer's compile command). SemanticEngine::gotoDefinition / gotoDeclaration answer with the file.
//   * Find All References columns (N5): whether a reference writes, reads or merely declares the name.
// Pure functions over one line of source or over directories; no clangd, no engine state.
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "semantic/CompileCommands.h"

namespace tf::incnav {

// An `#include` / `#include_next` / `#import` directive with a literal spelling.
struct IncludeRef {
  std::string spelling;  // "x.h" / "sys/types.h" without the delimiters
  bool angled = false;   // <x.h> (true) or "x.h" (false)
  int begin = 0;         // byte column of the '#'
  int end = 0;           // byte column just after the closing delimiter
};
// The directive on `line` when byte column `col` lies on it (from the '#' up to and including the position right after
// the closing delimiter). Comments are ignored; a macro operand (`#include HEADER`) or an unterminated spelling gives
// nothing.
std::optional<IncludeRef> includeAt(std::string_view line, int col);

// The directories a compile command searches, in the order the compiler tries them (relative ones are made absolute
// against the command's directory; backslashes are read as separators).
struct SearchPath {
  std::vector<std::filesystem::path> quote;  // -iquote
  std::vector<std::filesystem::path> angle;  // -I, /I, -isystem, /imsvc, -idirafter
};
SearchPath searchPathOf(const CompileCommand& cmd);

// The file `ref` names as seen from `includer`: quoted spellings look in the includer's directory first; then
// `path.quote` (quoted only) and `path.angle`. Nothing when no such regular file exists.
std::optional<std::filesystem::path> resolveInclude(const IncludeRef& ref, const std::filesystem::path& includer,
                                                    const SearchPath& path);

// What a reference to an identifier does at byte column `col` of `line` (the token is `len` bytes long).
enum class RefRole { Read, Write, Other };
// Write: assigned (`x = 1`, `x += 1`, also through `x[i] =`), incremented or decremented. Other: the line is a
// preprocessor directive other than a condition, or a using / typedef / namespace / class / struct / enum line (a
// spelling, not a use of a value). Read: everything else.
RefRole classifyReference(std::string_view line, int col, int len);
const char* roleName(RefRole r);  // "Read" / "Write" / "Other"

}  // namespace tf::incnav
