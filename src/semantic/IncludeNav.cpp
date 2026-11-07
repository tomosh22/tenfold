#include "semantic/IncludeNav.h"

#include <algorithm>
#include <cctype>
#include <system_error>

#include "core/PathUtil.h"
#include "semantic/DefChoice.h"

namespace tf::incnav {

namespace fs = std::filesystem;

namespace {
bool isBlank(char c) { return c == ' ' || c == '\t'; }
bool isIdChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

fs::path absoluteDir(const std::string& d, const fs::path& base) {
  std::string s = d;
  std::replace(s.begin(), s.end(), '\\', '/');
  fs::path p = pathFromUtf8(s);
  return (p.is_absolute() ? p : base / p).lexically_normal();
}
}  // namespace

std::optional<IncludeRef> includeAt(std::string_view rawLine, int col) {
  if (col < 0) return std::nullopt;
  const std::string line = defchoice::blankComments(rawLine);
  size_t i = 0;
  while (i < line.size() && isBlank(line[i])) ++i;
  if (i >= line.size() || line[i] != '#') return std::nullopt;
  const size_t hash = i++;
  while (i < line.size() && isBlank(line[i])) ++i;
  size_t w = i;
  while (w < line.size() && isIdChar(line[w])) ++w;
  const std::string_view word = std::string_view(line).substr(i, w - i);
  if (word != "include" && word != "include_next" && word != "import") return std::nullopt;
  i = w;
  while (i < line.size() && isBlank(line[i])) ++i;
  if (i >= line.size() || (line[i] != '"' && line[i] != '<')) return std::nullopt;
  const char close = line[i] == '"' ? '"' : '>';
  const size_t e = line.find(close, i + 1);
  if (e == std::string::npos || e == i + 1) return std::nullopt;
  IncludeRef r;
  r.spelling = line.substr(i + 1, e - i - 1);
  r.angled = close == '>';
  r.begin = static_cast<int>(hash);
  r.end = static_cast<int>(e + 1);
  if (col < r.begin || col > r.end) return std::nullopt;
  return r;
}

SearchPath searchPathOf(const CompileCommand& cmd) {
  SearchPath sp;
  const auto& a = cmd.arguments;
  for (size_t i = 1; i < a.size(); ++i) {
    const std::string& x = a[i];
    // flag, value attached or in the next argument
    auto value = [&](const char* flag, bool& took) -> std::string {
      const size_t n = std::char_traits<char>::length(flag);
      if (x.compare(0, n, flag) != 0) return {};
      took = true;
      if (x.size() > n) return x.substr(n);
      return i + 1 < a.size() ? a[++i] : std::string();
    };
    bool took = false;
    std::string v = value("-iquote", took);
    if (took) {
      if (!v.empty()) sp.quote.push_back(absoluteDir(v, cmd.directory));
      continue;
    }
    for (const char* f : {"-isystem", "-idirafter", "/imsvc", "-I", "/I"}) {
      v = value(f, took);
      if (took) break;
    }
    if (took && !v.empty()) sp.angle.push_back(absoluteDir(v, cmd.directory));
  }
  return sp;
}

std::optional<fs::path> resolveInclude(const IncludeRef& ref, const fs::path& includer, const SearchPath& path) {
  std::string s = ref.spelling;
  std::replace(s.begin(), s.end(), '\\', '/');
  const fs::path rel = pathFromUtf8(s);
  std::error_code ec;
  auto tryDir = [&](const fs::path& dir) -> std::optional<fs::path> {
    const fs::path f = (dir / rel).lexically_normal();
    if (fs::is_regular_file(f, ec)) return f;
    return std::nullopt;
  };
  if (rel.is_absolute()) return fs::is_regular_file(rel, ec) ? std::optional<fs::path>(rel) : std::nullopt;
  if (!ref.angled) {
    if (auto f = tryDir(includer.parent_path())) return f;
    for (const fs::path& d : path.quote)
      if (auto f = tryDir(d)) return f;
  }
  for (const fs::path& d : path.angle)
    if (auto f = tryDir(d)) return f;
  return std::nullopt;
}

RefRole classifyReference(std::string_view rawLine, int col, int len) {
  const std::string line = defchoice::blankComments(rawLine);
  const size_t b = static_cast<size_t>(std::max(col, 0)), e = std::min(line.size(), b + static_cast<size_t>(len));
  // Not a use of a value: preprocessor lines (conditions excepted) and the declaration keywords that only spell a name.
  size_t f = 0;
  while (f < line.size() && isBlank(line[f])) ++f;
  if (f < line.size() && line[f] == '#') {
    size_t w = f + 1;
    while (w < line.size() && isBlank(line[w])) ++w;
    size_t we = w;
    while (we < line.size() && isIdChar(line[we])) ++we;
    const std::string_view dir = std::string_view(line).substr(w, we - w);
    if (dir != "if" && dir != "elif" && dir != "ifdef" && dir != "ifndef" && dir != "pragma") return RefRole::Other;
  }
  size_t ws = f;
  while (ws < line.size() && isIdChar(line[ws])) ++ws;
  const std::string_view first = std::string_view(line).substr(f, ws - f);
  if (first == "using" || first == "typedef" || first == "namespace" || first == "class" || first == "struct" ||
      first == "enum" || first == "union")
    return RefRole::Other;
  // Before the name: ++x / --x.
  size_t p = b;
  while (p > 0 && isBlank(line[p - 1])) --p;
  if (p >= 2 && ((line[p - 1] == '+' && line[p - 2] == '+') || (line[p - 1] == '-' && line[p - 2] == '-')))
    return RefRole::Write;
  // After the name (and any [subscripts]): x++ / x-- / x = / x op= .
  size_t q = e;
  auto skipBlank = [&] {
    while (q < line.size() && isBlank(line[q])) ++q;
  };
  skipBlank();
  while (q < line.size() && line[q] == '[') {
    int depth = 0;
    for (; q < line.size(); ++q) {
      if (line[q] == '[') ++depth;
      else if (line[q] == ']' && --depth == 0) {
        ++q;
        break;
      }
    }
    skipBlank();
  }
  if (q + 1 < line.size() + 1 && q < line.size()) {
    const char c = line[q], n = q + 1 < line.size() ? line[q + 1] : '\0', n2 = q + 2 < line.size() ? line[q + 2] : '\0';
    if ((c == '+' && n == '+') || (c == '-' && n == '-')) return RefRole::Write;
    if (c == '=' && n != '=') return RefRole::Write;
    if (n == '=' && std::string_view("+-*/%&|^").find(c) != std::string_view::npos) return RefRole::Write;
    if (((c == '<' && n == '<') || (c == '>' && n == '>')) && n2 == '=') return RefRole::Write;
  }
  return RefRole::Read;
}

const char* roleName(RefRole r) {
  switch (r) {
    case RefRole::Write: return "Write";
    case RefRole::Other: return "Other";
    default: return "Read";
  }
}

}  // namespace tf::incnav
