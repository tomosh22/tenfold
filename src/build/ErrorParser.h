#pragma once
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace tf {

enum class BuildSeverity { Error, Warning, Note };

// One diagnostic found in build output. `file` is empty for messages without a source location (LINK, NMAKE,
// make, driver errors); `line` / `col` are 1-based, 0 when absent. `file` uses '/' separators (Windows drive
// letters kept: "C:/proj/a.cpp") and is made absolute against the parser's working directory when the compiler
// printed a relative path; compare files with tf::pathKey.
struct BuildMessage {
  std::string file;
  int line = 0, col = 0;
  BuildSeverity severity = BuildSeverity::Error;
  std::string code;  // "C2065", "LNK1104", "-Wunused-variable", "make"
  std::string text;
  // "In file included from ..." chain printed before the message (outermost-first as printed), as notes whose
  // text is "included from here".
  std::vector<BuildMessage> notes;
  bool operator==(const BuildMessage&) const = default;
  bool hasLocation() const { return !file.empty() && line > 0; }
};

// Incremental parser for MSVC / MSBuild / NMake, GCC / Clang and make output. Stateful (include chains, duplicate
// suppression: MSBuild repeats every error in its summary), so use one instance per build.
class ErrorParser {
 public:
  // `cwd`: relative paths in the output resolve against it.
  explicit ErrorParser(std::string cwd = {}) : cwd_(std::move(cwd)) {}

  // Feed one output line (without the newline). Returns true and fills `out` when the line is a diagnostic.
  bool parseLine(std::string_view line, BuildMessage* out);

  // Convenience: parse many lines.
  std::vector<BuildMessage> parseAll(const std::vector<std::string>& lines);

 private:
  std::string resolve(std::string path) const;
  bool parseMsvc(std::string_view s, BuildMessage* m) const;
  bool parseGcc(std::string_view s, BuildMessage* m) const;
  bool parseMake(std::string_view s, BuildMessage* m) const;
  bool parseIncludeChain(std::string_view s);

  std::string cwd_;
  std::vector<BuildMessage> chain_;
  std::set<std::string> seen_;
};

}  // namespace tf
