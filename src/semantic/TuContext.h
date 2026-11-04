#pragma once
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "semantic/CompileCommands.h"

namespace tf::tu {

// Translation-unit context for files that are only ever #included (.tpp / .ipp / .inl / .def X-macro lists, the
// parts of a unity build): such a file has no compile command of its own and is not self-contained, so clangd
// would parse it standalone (names undeclared) and the standalone parse would replace the file's entries in its
// index. The functions here work out which translation unit does include the file and what the compiler has seen
// at that point; SemanticEngine hands clangd a compile command that reproduces it (docs/ARCHITECTURE.md,
// "Translation-unit context for included files").
//
// Pure logic, no clangd and no engine state, so it is unit tested directly (tests/test_tu_context.cpp).

// One #include directive of a text. Lines are 0-based.
struct IncludeDirective {
  int line = 0;
  size_t offset = 0;  // byte offset of the start of `line`
  std::string spelling;
  bool angled = false;
  int condDepth = 0;  // #if / #ifdef / #ifndef groups open at this directive
};

// Every #include / #include_next / #import with a literal spelling, outside comments. Directives continued with a
// trailing backslash are read as one. Lines end at '\n'.
std::vector<IncludeDirective> scanIncludeDirectives(std::string_view text);

// Does `spelling` (as written in an #include of `includer`) name `target`? Quoted lookup starts in the includer's
// directory, then the include path: the first is exact, the second is a path-suffix test, rejected when the
// includer's own directory has a different file of that name.
bool includeSpellingResolves(const std::string& spelling, const std::filesystem::path& includer,
                             const std::filesystem::path& target);

// What the compiler has seen of `text` when it reaches `d`: the bytes before the directive's line, followed by one
// "#endif" for every conditional group still open there. Line numbers (and columns) of the kept lines do not
// change, which is what lets answers located in a prefix copy be mapped back to the original file.
std::string prefixBefore(std::string_view text, const IncludeDirective& d);

// .h .hh .hpp .hxx .h++ .hp .cuh (any case): clangd's own header heuristics apply to these.
bool isStandardHeaderExtension(const std::filesystem::path& p);

// One file on the way from the translation unit's main file down to the orphan: `file` includes the next link
// (or the orphan) at `directive`.
struct Link {
  std::filesystem::path file;
  IncludeDirective directive;
  std::string prefix;  // prefixBefore(text of file, directive)
};

// True when `prefix` ends inside a class / struct / enum / function body (an open `{` that is not a namespace or
// extern "C" block). A file included there (`class S { #include "members.inl" };`) is a fragment: parsed as a
// translation unit it ends inside an unfinished class and its declarations do not reach the index.
bool endsInsideBody(std::string_view prefix);

struct Plan {
  CompileCommand root;      // the compile database entry of links.front().file
  std::vector<Link> links;  // main file first
  bool fragment = false;    // some link includes the file inside a class / function body (endsInsideBody)
};

struct Env {
  // Files that may #include `target` (a superset is fine: candidates are verified against their text).
  std::function<std::vector<std::filesystem::path>(const std::filesystem::path& target)> includersOf;
  // Current text of a file (the editor's buffer when it is open, else the disk). False when unreadable.
  std::function<bool(const std::filesystem::path&, std::string& text)> readText;
  // The compile database entry of exactly this file, if any.
  std::function<const CompileCommand*(const std::filesystem::path&)> commandFor;
};

// Finds the shortest chain of #includes from a file with a compile command down to `orphan` (breadth first, at most
// `maxDepth` levels, candidates in path order so the answer is deterministic). nullopt when no such chain exists or
// when the orphan includes one of the files of the chain itself (then it brings its own context and is parsed on
// its own).
std::optional<Plan> planContext(const std::filesystem::path& orphan, const Env& env, int maxDepth = 8);

struct CommandOptions {
  bool clMode = false;     // clang-cl / cl driver arguments
  bool isC = false;        // the main file is C
  bool headerLike = true;  // the orphan is not a translation unit of its own: -x c++-header
};
// The compile arguments for the orphan: the root's, with the main file replaced by `orphan` (and its language
// forced), one forced include per link (`prefixFiles[i]` holds links[i].prefix) and the links' directories on the
// quote include path so relative #includes inside the copies still resolve.
std::vector<std::string> buildArguments(const Plan& plan, const std::filesystem::path& orphan,
                                        const std::vector<std::filesystem::path>& prefixFiles,
                                        const CommandOptions& opt);

}  // namespace tf::tu
