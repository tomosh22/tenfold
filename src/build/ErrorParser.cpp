#include "build/ErrorParser.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace tf {

namespace {

bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isAlpha(char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0; }
char lowerAscii(char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }

std::string_view trim(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
  return s;
}

bool startsWithNoCase(std::string_view s, std::string_view prefix) {
  if (s.size() < prefix.size()) return false;
  for (size_t i = 0; i < prefix.size(); ++i)
    if (lowerAscii(s[i]) != lowerAscii(prefix[i])) return false;
  return true;
}

bool endsWithNoCase(std::string_view s, std::string_view suffix) {
  return s.size() >= suffix.size() && startsWithNoCase(s.substr(s.size() - suffix.size()), suffix);
}

// Removes ANSI escape sequences (gcc -fdiagnostics-color).
std::string stripAnsi(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\x1b' && i + 1 < s.size() && s[i + 1] == '[') {
      i += 2;
      while (i < s.size() && !isAlpha(s[i])) ++i;
      continue;
    }
    out += s[i];
  }
  return out;
}

// "  1>" / "12>" / "1:7>" MSBuild node prefix.
std::string_view stripNodePrefix(std::string_view s) {
  size_t i = 0;
  while (i < s.size() && s[i] == ' ') ++i;
  size_t d = i;
  while (d < s.size() && isDigit(s[d])) ++d;
  if (d == i) return s;
  if (d + 1 < s.size() && s[d] == ':' && isDigit(s[d + 1])) {
    ++d;
    while (d < s.size() && isDigit(s[d])) ++d;
  }
  if (d < s.size() && s[d] == '>') return s.substr(d + 1);
  return s;
}

// " [C:\path\proj.vcxproj]" suffix MSBuild appends to every diagnostic.
std::string_view stripProjectSuffix(std::string_view s) {
  if (s.empty() || s.back() != ']') return s;
  size_t open = s.rfind(" [");
  if (open == std::string_view::npos) return s;
  std::string_view in = s.substr(open + 2, s.size() - open - 3);
  for (std::string_view ext : {".vcxproj", ".csproj", ".proj", ".sln", ".targets", ".props", ".vcproj", ".wixproj"})
    if (endsWithNoCase(in, ext)) return trim(s.substr(0, open));
  return s;
}

bool hasDrive(std::string_view p) { return p.size() >= 2 && isAlpha(p[0]) && p[1] == ':'; }
bool looksAbsolute(std::string_view p) { return !p.empty() && (p[0] == '/' || p[0] == '\\' || hasDrive(p)); }

// Parses "error" / "fatal error" / "warning" / "note" (+ "remark", "info") at the start of `s`.
bool parseSeverity(std::string_view s, BuildSeverity* sev, size_t* len) {
  struct Entry {
    const char* word;
    BuildSeverity sev;
  };
  static const Entry table[] = {{"fatal error", BuildSeverity::Error}, {"error", BuildSeverity::Error},
                                {"warning", BuildSeverity::Warning},   {"note", BuildSeverity::Note},
                                {"remark", BuildSeverity::Note},       {"info", BuildSeverity::Note}};
  for (auto& e : table) {
    std::string_view w = e.word;
    if (startsWithNoCase(s, w) && (s.size() == w.size() || !std::isalnum(static_cast<unsigned char>(s[w.size()])))) {
      *sev = e.sev;
      *len = w.size();
      return true;
    }
  }
  return false;
}

bool parseInt(std::string_view s, size_t& i, int* out) {
  size_t st = i;
  long v = 0;
  while (i < s.size() && isDigit(s[i])) {
    v = std::min(v * 10 + (s[i] - '0'), 100000000L);
    ++i;
  }
  if (i == st) return false;
  *out = static_cast<int>(v);
  return true;
}

// Message code such as C2065, LNK1104, MSB3073, U1077, D9002: letters followed by digits.
bool parseCode(std::string_view s, size_t& i, std::string* code) {
  size_t j = i;
  while (j < s.size() && isAlpha(s[j])) ++j;
  size_t d = j;
  while (d < s.size() && isDigit(s[d])) ++d;
  if (j == i || d == j || j - i > 6) return false;
  *code = std::string(s.substr(i, d - i));
  i = d;
  return true;
}

// After a severity word: " C2065: text", " : text" or ": text".
bool parseTail(std::string_view s, size_t i, BuildMessage* m) {
  size_t j = i;
  while (j < s.size() && s[j] == ' ') ++j;
  std::string code;
  size_t k = j;
  if (parseCode(s, k, &code)) {
    size_t c = k;
    while (c < s.size() && s[c] == ' ') ++c;
    if (c < s.size() && s[c] == ':') {
      m->code = code;
      m->text = std::string(trim(s.substr(c + 1)));
      return true;
    }
  }
  if (j < s.size() && s[j] == ':') {
    m->text = std::string(trim(s.substr(j + 1)));
    return true;
  }
  return false;
}

}  // namespace

std::string ErrorParser::resolve(std::string path) const {
  if (hasDrive(path) || path.starts_with("\\\\") || path.find('\\') != std::string::npos)
    std::replace(path.begin(), path.end(), '\\', '/');
  if (!looksAbsolute(path) && !cwd_.empty()) {
    std::string base = cwd_;
    std::replace(base.begin(), base.end(), '\\', '/');
    if (base.back() != '/') base += '/';
    path = base + path;
  }
  // Normalise "./" and "../" segments without touching the file system.
  std::string prefix;
  std::string rest = path;
  if (hasDrive(rest)) {
    prefix = rest.substr(0, 2);
    rest = rest.substr(2);
  }
  const bool abs = !rest.empty() && rest[0] == '/';
  const bool unc = rest.starts_with("//");
  std::vector<std::string> parts;
  size_t i = 0;
  while (i <= rest.size()) {
    size_t e = rest.find('/', i);
    if (e == std::string::npos) e = rest.size();
    std::string seg = rest.substr(i, e - i);
    if (seg == "..") {
      if (!parts.empty() && parts.back() != "..") parts.pop_back();
      else if (!abs) parts.push_back(seg);
    } else if (!seg.empty() && seg != ".") {
      parts.push_back(seg);
    }
    i = e + 1;
  }
  std::string out = prefix + (unc ? "//" : abs ? "/" : "");
  for (size_t k = 0; k < parts.size(); ++k) out += (k ? "/" : "") + parts[k];
  return out;
}

// path(12,5): error C2065: text      path(12): error C2065      LINK : fatal error LNK1104: text
bool ErrorParser::parseMsvc(std::string_view s, BuildMessage* m) const {
  for (size_t i = s.find('('); i != std::string_view::npos; i = s.find('(', i + 1)) {
    size_t j = i + 1;
    int line = 0, col = 0, ignored = 0;
    if (!parseInt(s, j, &line)) continue;
    if (j < s.size() && s[j] == ',') {
      ++j;
      if (!parseInt(s, j, &col)) continue;
      if (j < s.size() && s[j] == ',') {  // (line,col,endLine,endCol)
        ++j;
        if (!parseInt(s, j, &ignored) || j >= s.size() || s[j] != ',') continue;
        ++j;
        if (!parseInt(s, j, &ignored)) continue;
      }
    } else if (j < s.size() && s[j] == '-') {  // (line-endLine)
      ++j;
      if (!parseInt(s, j, &ignored)) continue;
    }
    if (j >= s.size() || s[j] != ')') continue;
    ++j;
    while (j < s.size() && s[j] == ' ') ++j;
    if (j >= s.size() || s[j] != ':') continue;
    ++j;
    while (j < s.size() && s[j] == ' ') ++j;
    BuildSeverity sev;
    size_t sl;
    if (!parseSeverity(s.substr(j), &sev, &sl)) continue;
    BuildMessage tmp;
    if (!parseTail(s, j + sl, &tmp)) continue;
    m->file = resolve(std::string(trim(s.substr(0, i))));
    m->line = line;
    m->col = col;
    m->severity = sev;
    m->code = tmp.code;
    m->text = tmp.text;
    return true;
  }
  // "origin : [fatal] error CODE: text" without a location (LINK, NMAKE, cl, MSBUILD, x.obj).
  for (size_t i = s.find(" : "); i != std::string_view::npos; i = s.find(" : ", i + 1)) {
    size_t at = i + 3;
    if (startsWithNoCase(s.substr(at), "Command line ")) at += 13;  // cl : Command line warning D9002
    BuildSeverity sev;
    size_t sl;
    if (!parseSeverity(s.substr(at), &sev, &sl)) continue;
    BuildMessage tmp;
    if (!parseTail(s, at + sl, &tmp) || tmp.code.empty()) continue;
    std::string_view origin = trim(s.substr(0, i));
    m->severity = sev;
    m->code = tmp.code;
    m->text = tmp.text;
    if (origin.find('.') != std::string_view::npos) m->text = std::string(origin) + ": " + m->text;  // x.obj
    return true;
  }
  return false;
}

// path:12:5: error: text [-Wfoo]   path:12: warning: text   C:/x/y.cpp:3:1: fatal error: text   gcc: error: text
bool ErrorParser::parseGcc(std::string_view s, BuildMessage* m) const {
  auto finish = [&](size_t pos, BuildSeverity sev, size_t sl) {
    std::string_view text = trim(s.substr(pos + sl));
    if (text.empty() || text[0] != ':') return false;
    text = trim(text.substr(1));
    m->severity = sev;
    if (text.size() > 3 && text.back() == ']') {  // trailing [-Wunused-variable]
      size_t open = text.rfind('[');
      if (open != std::string_view::npos && open > 0 && text[open - 1] == ' ' && text[open + 1] == '-') {
        m->code = std::string(text.substr(open + 1, text.size() - open - 2));
        text = trim(text.substr(0, open));
      }
    }
    m->text = std::string(text);
    return true;
  };
  for (size_t i = s.find(':'); i != std::string_view::npos; i = s.find(':', i + 1)) {
    if (i + 1 >= s.size() || !isDigit(s[i + 1])) continue;
    size_t j = i + 1;
    int line = 0, col = 0;
    parseInt(s, j, &line);
    if (j + 1 < s.size() && s[j] == ':' && isDigit(s[j + 1])) {
      ++j;
      parseInt(s, j, &col);
    }
    if (j >= s.size() || s[j] != ':') continue;
    size_t k = j + 1;
    while (k < s.size() && s[k] == ' ') ++k;
    BuildSeverity sev;
    size_t sl;
    if (!parseSeverity(s.substr(k), &sev, &sl) || !finish(k, sev, sl)) continue;
    m->file = resolve(std::string(trim(s.substr(0, i))));
    m->line = line;
    m->col = col;
    return true;
  }
  // Driver / linker messages without a location: "clang: error: linker command failed", "collect2: error: ...".
  size_t c = s.find(": ");
  if (c != std::string_view::npos && c > 0 && c < 24 && s.find(' ') > c) {
    BuildSeverity sev;
    size_t sl;
    if (parseSeverity(s.substr(c + 2), &sev, &sl) && finish(c + 2, sev, sl)) return true;
  }
  return false;
}

// make: *** [Makefile:5: all] Error 1     Makefile:12: *** missing separator.  Stop.
bool ErrorParser::parseMake(std::string_view s, BuildMessage* m) const {
  size_t p = s.find("*** ");
  if (p == std::string_view::npos) return false;
  std::string_view head = trim(s.substr(0, p));
  m->severity = BuildSeverity::Error;
  m->code = "make";
  m->text = std::string(trim(s.substr(p + 4)));
  if (head.size() < 2 || head.back() != ':') return false;
  std::string_view h = head.substr(0, head.size() - 1);
  // "make" / "make[2]" / "gmake" / "mingw32-make": no location.
  for (std::string_view prog : {"make", "gmake", "mingw32-make"})
    if (startsWithNoCase(h, prog) && (h.size() == prog.size() || h[prog.size()] == '[')) return true;
  size_t colon = h.rfind(':');  // "Makefile:12"
  if (colon == std::string_view::npos) return false;
  size_t i = colon + 1;
  int line = 0;
  if (!parseInt(h, i, &line) || i != h.size()) return false;
  m->file = resolve(std::string(h.substr(0, colon)));
  m->line = line;
  return true;
}

// "In file included from a.h:3:"   "                 from b.cpp:5,"
bool ErrorParser::parseIncludeChain(std::string_view s) {
  std::string_view t = trim(s);
  bool first = false;
  if (startsWithNoCase(t, "In file included from ")) {
    t.remove_prefix(22);
    first = true;
  } else if (!s.empty() && s[0] == ' ' && startsWithNoCase(t, "from ")) {
    t.remove_prefix(5);
  } else {
    return false;
  }
  while (!t.empty() && (t.back() == ',' || t.back() == ':')) t.remove_suffix(1);
  // path:line[:col]
  size_t end = t.size();
  int nums[2] = {0, 0};
  int n = 0;
  while (n < 2 && end > 0) {
    size_t colon = t.rfind(':', end - 1);
    if (colon == std::string_view::npos || colon + 1 >= end) break;
    bool digits = true;
    for (size_t i = colon + 1; i < end; ++i) digits = digits && isDigit(t[i]);
    if (!digits) break;
    nums[n++] = std::atoi(std::string(t.substr(colon + 1, end - colon - 1)).c_str());
    end = colon;
  }
  if (n == 0 || end == 0) return false;
  if (first) chain_.clear();
  BuildMessage note;
  note.severity = BuildSeverity::Note;
  note.file = resolve(std::string(t.substr(0, end)));
  note.line = n == 2 ? nums[1] : nums[0];
  note.col = n == 2 ? nums[0] : 0;
  note.text = "included from here";
  chain_.push_back(std::move(note));
  return true;
}

bool ErrorParser::parseLine(std::string_view raw, BuildMessage* out) {
  std::string line = stripAnsi(raw);
  while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
  if (parseIncludeChain(line)) return false;
  std::string_view s = stripNodePrefix(line);
  s = stripProjectSuffix(trim(s));
  BuildMessage m;
  bool ok = parseMsvc(s, &m) || parseGcc(s, &m) || parseMake(s, &m);
  if (!ok) {
    chain_.clear();
    return false;
  }
  if (m.severity != BuildSeverity::Note) m.notes = std::move(chain_);
  chain_.clear();
  std::string key = m.file + '\n' + std::to_string(m.line) + ':' + std::to_string(m.col) + '\n' +
                    std::to_string(static_cast<int>(m.severity)) + m.code + '\n' + m.text;
  if (!seen_.insert(key).second) return false;  // MSBuild's summary repeats every diagnostic
  *out = std::move(m);
  return true;
}

std::vector<BuildMessage> ErrorParser::parseAll(const std::vector<std::string>& lines) {
  std::vector<BuildMessage> out;
  BuildMessage m;
  for (auto& l : lines)
    if (parseLine(l, &m)) out.push_back(std::move(m));
  return out;
}

}  // namespace tf
