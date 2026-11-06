#include "core/EditorConfig.h"

#include <cctype>
#include <cstring>
#include <fstream>
#include <regex>
#include <sstream>
#include <vector>

#include "core/PathUtil.h"

namespace tf {
namespace fs = std::filesystem;

namespace {

std::string trim(std::string_view s) {
  size_t a = 0, b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
  return std::string(s.substr(a, b - a));
}

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

// Glob -> ECMAScript regex source.
std::string globToRegex(std::string_view g) {
  std::string out;
  int braces = 0;
  for (size_t i = 0; i < g.size(); ++i) {
    const char c = g[i];
    switch (c) {
      case '*':
        if (i + 1 < g.size() && g[i + 1] == '*') {
          out += ".*";
          ++i;
        } else {
          out += "[^/]*";
        }
        break;
      case '?': out += "[^/]"; break;
      case '[': {
        const size_t close = g.find(']', i + 2);
        if (close == std::string_view::npos) {
          out += "\\[";
          break;
        }
        std::string_view body = g.substr(i + 1, close - i - 1);
        out += '[';
        if (!body.empty() && body[0] == '!') {
          out += '^';
          body.remove_prefix(1);
        }
        for (char b : body) {
          if (b == '\\' || b == '^' || b == '[') out += '\\';
          out += b;
        }
        out += ']';
        i = close;
        break;
      }
      case '{': {
        // {a,b} alternatives; {1..3} numeric ranges are not supported and match literally.
        const size_t close = g.find('}', i);
        if (close == std::string_view::npos || g.substr(i, close - i).find(',') == std::string_view::npos) {
          out += "\\{";
        } else {
          out += "(?:";
          ++braces;
        }
        break;
      }
      case '}':
        if (braces > 0) {
          out += ')';
          --braces;
        } else {
          out += "\\}";
        }
        break;
      case ',': out += braces > 0 ? "|" : ","; break;
      case '\\':
        if (i + 1 < g.size()) {
          ++i;
          out += '\\';
          out += g[i];
        }
        break;
      default:
        if (std::strchr(".^$|()+", c)) out += '\\';
        out += c;
    }
  }
  return out;
}

struct Section {
  std::string glob;
  std::optional<bool> trim, finalNewline;
};

std::optional<bool> boolValue(const std::string& v) {
  const std::string l = lower(v);
  if (l == "true") return true;
  if (l == "false") return false;
  return std::nullopt;
}

}  // namespace

bool editorConfigGlobMatches(std::string_view glob, std::string_view relPath) {
  std::string g(glob);
  if (g.find('/') == std::string::npos) g = "**/" + g;
  else if (g[0] == '/') g.erase(0, 1);
  std::string pat = globToRegex(g);
  // "**/x" also matches "x" at the top
  if (pat.rfind(".*/", 0) == 0) pat = "(?:.*/)?" + pat.substr(3);
  try {
    return std::regex_match(std::string(relPath), std::regex(pat));
  } catch (const std::regex_error&) { return false; }
}

EditorConfigProps editorConfigFor(const fs::path& fileIn) {
  std::error_code ec;
  const fs::path file = fs::absolute(fileIn, ec).lexically_normal();
  EditorConfigProps result;
  // Closest file first: the first value found for a property is the one that counts.
  for (fs::path dir = file.parent_path(); !dir.empty(); dir = dir.parent_path()) {
    std::ifstream in(dir / ".editorconfig", std::ios::binary);
    bool root = false;
    if (in) {
      std::vector<Section> sections;
      bool preamble = true;
      for (std::string line; std::getline(in, line);) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        if (line[0] == '[' && line.back() == ']') {
          sections.push_back({line.substr(1, line.size() - 2), {}, {}});
          preamble = false;
          continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = lower(trim(line.substr(0, eq))), val = trim(line.substr(eq + 1));
        if (preamble) {
          if (key == "root") root = lower(val) == "true";
          continue;
        }
        if (key == "trim_trailing_whitespace") sections.back().trim = boolValue(val);
        else if (key == "insert_final_newline") sections.back().finalNewline = boolValue(val);
      }
      const std::string rel = pathKeyUtf8(file.lexically_relative(dir));
      std::optional<bool> trimHere, finalHere;
      for (const Section& s : sections) {  // later sections win
        if (!editorConfigGlobMatches(s.glob, rel)) continue;
        if (s.trim) trimHere = s.trim;
        if (s.finalNewline) finalHere = s.finalNewline;
      }
      if (!result.trimTrailingWhitespace) result.trimTrailingWhitespace = trimHere;
      if (!result.insertFinalNewline) result.insertFinalNewline = finalHere;
    }
    if (root || dir == dir.parent_path()) break;
  }
  return result;
}

}  // namespace tf
