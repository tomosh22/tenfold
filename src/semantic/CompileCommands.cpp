#include "semantic/CompileCommands.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <set>
#include <sstream>

#include <nlohmann/json.hpp>

#include "core/Platform.h"
#include "semantic/XmlLite.h"

namespace tf {

namespace fs = std::filesystem;
using Json = nlohmann::json;

// ---------------------------------------------------------------------------
// compile_commands.json I/O
// ---------------------------------------------------------------------------

std::optional<fs::path> CompileDatabase::find(const fs::path& root) {
  static const char* candidates[] = {"",       "build",   "out", "cmake-build-debug", "cmake-build-release",
                                     "_build", ".tenfold"};
  for (auto c : candidates) {
    fs::path p = root / c / "compile_commands.json";
    std::error_code ec;
    if (fs::is_regular_file(p, ec)) return fs::weakly_canonical(p, ec);
  }
  // One level of build directories: build/*/compile_commands.json (CMake multi-config layouts).
  std::error_code ec;
  for (auto base : {root / "build", root / "out"}) {
    if (!fs::is_directory(base, ec)) continue;
    for (auto& e : fs::directory_iterator(base, ec)) {
      fs::path p = e.path() / "compile_commands.json";
      if (fs::is_regular_file(p, ec)) return fs::weakly_canonical(p, ec);
    }
  }
  return std::nullopt;
}

namespace {
// Split a shell-ish command line into arguments (handles quotes and backslash escapes).
std::vector<std::string> splitCommand(const std::string& cmd) {
  std::vector<std::string> out;
  std::string cur;
  bool inArg = false;
  char quote = 0;
  for (size_t i = 0; i < cmd.size(); ++i) {
    char c = cmd[i];
    if (quote) {
      if (c == quote) quote = 0;
      else if (c == '\\' && quote == '"' && i + 1 < cmd.size() && (cmd[i + 1] == '"' || cmd[i + 1] == '\\'))
        cur += cmd[++i];
      else cur += c;
      continue;
    }
    if (c == '"' || c == '\'') {
      quote = c;
      inArg = true;
      continue;
    }
    if (c == '\\' && i + 1 < cmd.size()) {
      cur += cmd[++i];
      inArg = true;
      continue;
    }
    if (std::isspace(static_cast<unsigned char>(c))) {
      if (inArg) {
        out.push_back(cur);
        cur.clear();
        inArg = false;
      }
      continue;
    }
    cur += c;
    inArg = true;
  }
  if (inArg) out.push_back(cur);
  return out;
}

std::string quoteArg(const std::string& a) {
  if (!a.empty() && a.find_first_of(" \t\"'\\") == std::string::npos) return a;
  std::string out = "\"";
  for (char c : a) {
    if (c == '"' || c == '\\') out += '\\';
    out += c;
  }
  return out + "\"";
}
}  // namespace

std::vector<CompileCommand> CompileDatabase::load(const fs::path& jsonFile, std::string* error) {
  std::vector<CompileCommand> out;
  std::ifstream in(jsonFile);
  if (!in) {
    if (error) *error = "cannot open " + jsonFile.string();
    return out;
  }
  Json j = Json::parse(in, nullptr, false);
  if (!j.is_array()) {
    if (error) *error = "not a JSON array: " + jsonFile.string();
    return out;
  }
  for (auto& e : j) {
    CompileCommand c;
    c.directory = e.value("directory", "");
    fs::path file = e.value("file", "");
    c.file = file.is_absolute() ? file : c.directory / file;
    if (e.contains("arguments")) {
      for (auto& a : e["arguments"]) c.arguments.push_back(a.get<std::string>());
    } else if (e.contains("command")) {
      c.arguments = splitCommand(e["command"].get<std::string>());
    }
    out.push_back(std::move(c));
  }
  return out;
}

bool CompileDatabase::save(const std::vector<CompileCommand>& cmds, const fs::path& jsonFile, std::string* error) {
  Json arr = Json::array();
  for (auto& c : cmds) {
    Json e;
    e["directory"] = c.directory.generic_string();
    e["file"] = c.file.generic_string();
    e["arguments"] = c.arguments;
    std::string cmd;
    for (auto& a : c.arguments) {
      if (!cmd.empty()) cmd += ' ';
      cmd += quoteArg(a);
    }
    e["command"] = cmd;
    arr.push_back(std::move(e));
  }
  std::error_code ec;
  fs::create_directories(jsonFile.parent_path(), ec);
  std::ofstream out(jsonFile);
  if (!out) {
    if (error) *error = "cannot write " + jsonFile.string();
    return false;
  }
  out << arr.dump(2);
  return true;
}

namespace compdb {

// ---------------------------------------------------------------------------
// CMake
// ---------------------------------------------------------------------------

std::optional<fs::path> fromCMake(const fs::path& root, const fs::path& buildDir, std::string* log) {
  std::error_code ec;
  if (!fs::is_regular_file(root / "CMakeLists.txt", ec)) return std::nullopt;
  fs::create_directories(buildDir, ec);
  std::string cmd = "cmake -S " + quoteArg(root.string()) + " -B " + quoteArg(buildDir.string()) +
                    " -DCMAKE_EXPORT_COMPILE_COMMANDS=ON 2>&1";
  std::string output;
  if (FILE* p = openShellPipe(cmd, "r")) {
    char buf[4096];
    while (size_t n = std::fread(buf, 1, sizeof buf, p)) output.append(buf, n);
    closeShellPipe(p);
  }
  if (log) *log = output;
  fs::path cc = buildDir / "compile_commands.json";
  if (fs::is_regular_file(cc, ec)) return cc;
  return std::nullopt;
}

// ---------------------------------------------------------------------------
// String-level path helpers. The MSBuild files of a Windows project name paths with backslashes and drive
// letters; these helpers treat them the same on every host (fs::path on Linux would see "C:\x" as relative).
// ---------------------------------------------------------------------------

namespace {

constexpr char kPoison = '\x01';  // stands for "this value could not be resolved"; entries holding it are dropped

std::string trim(std::string s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
  size_t i = 0;
  while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
  return s.substr(i);
}

std::string lower(std::string s) {
  for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

bool poisoned(const std::string& s) { return s.find(kPoison) != std::string::npos; }

// Normalise a Windows-style path fragment for use on the host.
std::string hostPath(std::string p) {
  std::replace(p.begin(), p.end(), '\\', '/');
  return p;
}

bool isDriveRooted(const std::string& p) {
  return p.size() >= 2 && std::isalpha(static_cast<unsigned char>(p[0])) && p[1] == ':';
}

// "/x", "\x", "\\server\share" and "C:..." are absolute (rooted) in the MSBuild world, on every host.
bool isAbsolutePathStr(const std::string& p) { return !p.empty() && (p[0] == '/' || p[0] == '\\' || isDriveRooted(p)); }

// Forward slashes, "." / ".." / duplicate separators collapsed, prefix ("/", "//", "C:/") preserved, no trailing slash.
std::string normalizePathStr(const std::string& in) {
  std::string p = hostPath(in);
  std::string prefix;
  size_t i = 0;
  if (p.rfind("//", 0) == 0) {
    prefix = "//";
    i = 2;
  } else if (!p.empty() && p[0] == '/') {
    prefix = "/";
    i = 1;
  } else if (isDriveRooted(p)) {
    prefix = p.substr(0, 2);
    i = 2;
    if (i < p.size() && p[i] == '/') {
      prefix += "/";
      ++i;
    }
  }
  const bool rooted = !prefix.empty() && prefix.back() == '/';
  std::vector<std::string> seg;
  while (i <= p.size()) {
    size_t j = p.find('/', i);
    if (j == std::string::npos) j = p.size();
    std::string s = p.substr(i, j - i);
    i = j + 1;
    if (s.empty() || s == ".") continue;
    if (s == "..") {
      if (!seg.empty() && seg.back() != "..") seg.pop_back();
      else if (!rooted) seg.push_back(s);
      continue;
    }
    seg.push_back(std::move(s));
  }
  std::string out = prefix;
  for (size_t k = 0; k < seg.size(); ++k) {
    if (k) out += '/';
    out += seg[k];
  }
  if (out.empty()) out = ".";
  return out;
}

std::string joinPathStr(const std::string& base, const std::string& rel) {
  if (isAbsolutePathStr(rel)) return normalizePathStr(rel);
  return normalizePathStr(base.empty() ? rel : base + "/" + rel);
}

bool endsWithSlash(const std::string& s) { return !s.empty() && (s.back() == '/' || s.back() == '\\'); }

std::string parentDirStr(const std::string& p) {
  std::string n = normalizePathStr(p);
  size_t k = n.find_last_of('/');
  if (k == std::string::npos) return ".";
  if (k == 0) return "/";
  return n.substr(0, k);
}

std::vector<std::string> splitSemis(const std::string& s) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : s) {
    if (c == ';') {
      if (!trim(cur).empty()) out.push_back(trim(cur));
      cur.clear();
    } else cur += c;
  }
  if (!trim(cur).empty()) out.push_back(trim(cur));
  return out;
}

// Command-line splitting with Windows rules: backslashes are literal unless they precede a double quote.
std::vector<std::string> splitWindowsArgs(const std::string& cmd) {
  std::vector<std::string> out;
  std::string cur;
  bool inArg = false, inQuote = false;
  for (size_t i = 0; i < cmd.size(); ++i) {
    char c = cmd[i];
    if (c == '\\') {
      size_t n = 0;
      while (i < cmd.size() && cmd[i] == '\\') ++n, ++i;
      if (i < cmd.size() && cmd[i] == '"') {
        cur.append(n / 2, '\\');
        if (n % 2) cur += '"';
        else inQuote = !inQuote;
        inArg = true;
      } else {
        cur.append(n, '\\');
        inArg = true;
        --i;
        continue;
      }
      continue;
    }
    if (c == '"') {
      inQuote = !inQuote;
      inArg = true;
      continue;
    }
    if (std::isspace(static_cast<unsigned char>(c)) && !inQuote) {
      if (inArg) {
        out.push_back(cur);
        cur.clear();
        inArg = false;
      }
      continue;
    }
    cur += c;
    inArg = true;
  }
  if (inArg) out.push_back(cur);
  return out;
}

// '*' and '?' wildcards within one path segment.
bool wildcardMatch(const std::string& pat, const std::string& s) {
  size_t p = 0, i = 0, star = std::string::npos, mark = 0;
  while (i < s.size()) {
    if (p < pat.size() && (pat[p] == '?' || std::tolower(static_cast<unsigned char>(pat[p])) ==
                                                std::tolower(static_cast<unsigned char>(s[i])))) {
      ++p, ++i;
    } else if (p < pat.size() && pat[p] == '*') {
      star = p++;
      mark = i;
    } else if (star != std::string::npos) {
      p = star + 1;
      i = ++mark;
    } else return false;
  }
  while (p < pat.size() && pat[p] == '*') ++p;
  return p == pat.size();
}

void globInto(const std::string& dir, const std::vector<std::string>& segs, size_t k, std::vector<std::string>& out) {
  std::error_code ec;
  if (k == segs.size()) return;
  const std::string& seg = segs[k];
  const bool last = k + 1 == segs.size();
  if (seg == "**") {
    globInto(dir, segs, k + 1, out);
    for (auto& e : fs::directory_iterator(fs::path(dir), ec))
      if (e.is_directory(ec)) globInto(e.path().generic_string(), segs, k, out);
    return;
  }
  if (seg.find_first_of("*?") == std::string::npos) {
    std::string next = dir + "/" + seg;
    if (last) {
      if (fs::exists(fs::path(next), ec)) out.push_back(next);
    } else globInto(next, segs, k + 1, out);
    return;
  }
  std::vector<std::string> names;
  for (auto& e : fs::directory_iterator(fs::path(dir), ec)) {
    std::string n = e.path().filename().string();
    if (wildcardMatch(seg, n) && (last ? e.is_regular_file(ec) : e.is_directory(ec))) names.push_back(n);
  }
  std::sort(names.begin(), names.end());
  for (auto& n : names) {
    if (last) out.push_back(dir + "/" + n);
    else globInto(dir + "/" + n, segs, k + 1, out);
  }
}

// Expands a (possibly wildcarded) include path relative to `baseDir`; a plain path is returned as is (it need not
// exist).
std::vector<std::string> expandInclude(const std::string& baseDir, const std::string& include) {
  std::string full = joinPathStr(baseDir, include);
  if (full.find_first_of("*?") == std::string::npos) return {full};
  std::string root;
  std::vector<std::string> segs;
  size_t i = 0;
  if (full.rfind("//", 0) == 0) i = 2, root = "/";
  else if (!full.empty() && full[0] == '/') i = 1, root = "";
  else if (isDriveRooted(full)) i = full.size() > 2 && full[2] == '/' ? 3 : 2, root = full.substr(0, 2);
  std::string cur = full.substr(i);
  size_t pos = 0;
  while (pos <= cur.size()) {
    size_t j = cur.find('/', pos);
    if (j == std::string::npos) j = cur.size();
    if (j > pos) segs.push_back(cur.substr(pos, j - pos));
    pos = j + 1;
  }
  std::vector<std::string> out;
  globInto(root, segs, 0, out);
  return out;
}

// ---------------------------------------------------------------------------
// MSBuild property / condition evaluation
// ---------------------------------------------------------------------------

struct EvalCtx {
  std::map<std::string, std::string> props;  // keys lower-case (MSBuild property names are case-insensitive)
  std::map<std::string, std::string> env;    // injected environment variables (win over the process environment)
  bool processEnv = false;
};

enum class Missing { Empty, Poison };

std::string expandEx(const std::string& s, const EvalCtx& cx, Missing miss);

std::string prop(const EvalCtx& cx, const std::string& lowName) {
  auto it = cx.props.find(lowName);
  return it == cx.props.end() ? std::string() : it->second;
}

// Position of the ')' closing the "$(" whose body starts at `from`, ignoring parentheses inside quotes.
size_t matchingParen(const std::string& s, size_t from) {
  int depth = 1;
  char quote = 0;
  for (size_t i = from; i < s.size(); ++i) {
    char c = s[i];
    if (quote) {
      if (c == quote) quote = 0;
      continue;
    }
    if (c == '\'' || c == '`') quote = c;
    else if (c == '(') ++depth;
    else if (c == ')' && --depth == 0) return i;
  }
  return std::string::npos;
}

std::vector<std::string> splitArgs(const std::string& s) {
  std::vector<std::string> out;
  std::string cur;
  int depth = 0;
  char quote = 0;
  for (char c : s) {
    if (quote) {
      cur += c;
      if (c == quote) quote = 0;
      continue;
    }
    if (c == '\'' || c == '`' || c == '"') {
      quote = c;
      cur += c;
    } else if (c == '(') {
      ++depth, cur += c;
    } else if (c == ')') {
      --depth, cur += c;
    } else if (c == ',' && depth == 0) {
      out.push_back(trim(cur));
      cur.clear();
    } else cur += c;
  }
  if (!trim(cur).empty() || !out.empty()) out.push_back(trim(cur));
  return out;
}

std::string unquote(const std::string& s) {
  if (s.size() >= 2 && (s.front() == '\'' || s.front() == '`' || s.front() == '"') && s.back() == s.front())
    return s.substr(1, s.size() - 2);
  return s;
}

std::string fileAboveDir(const std::string& startDir, const std::string& file) {
  std::string dir = normalizePathStr(startDir);
  std::error_code ec;
  for (int guard = 0; guard < 64; ++guard) {
    if (fs::exists(fs::path(dir + "/" + file), ec)) return dir;
    std::string up = parentDirStr(dir);
    if (up == dir || up == ".") break;
    dir = up;
  }
  return {};
}

std::string evalMacro(const std::string& bodyIn, const EvalCtx& cx, Missing miss) {
  const std::string poison(1, kPoison);
  std::string body = trim(bodyIn);
  if (body.empty()) return poison;
  if (body[0] == '[') {  // static property function
    size_t colons = body.find("]::");
    size_t open = body.find('(');
    if (colons == std::string::npos || open == std::string::npos || body.back() != ')') return poison;
    std::string fn = lower(body.substr(0, open));
    std::vector<std::string> args;
    for (auto& a : splitArgs(body.substr(open + 1, body.size() - open - 2))) {
      std::string v = expandEx(unquote(a), cx, miss);
      if (poisoned(v)) return poison;
      args.push_back(std::move(v));
    }
    const std::string thisDir = hostPath(prop(cx, "msbuildthisfiledirectory"));
    const std::string projDir = hostPath(prop(cx, "msbuildprojectdirectory"));
    if (fn == "[msbuild]::ensuretrailingslash" && args.size() == 1)
      return args[0].empty() || endsWithSlash(args[0]) ? args[0] : args[0] + "/";
    if ((fn == "[msbuild]::normalizedirectory" || fn == "[msbuild]::normalizepath") && !args.empty()) {
      std::string acc;
      for (auto& a : args) acc = acc.empty() ? a : isAbsolutePathStr(a) ? a : acc + "/" + a;
      std::string r = joinPathStr(projDir, acc);
      return fn == "[msbuild]::normalizedirectory" ? r + "/" : r;
    }
    if (fn == "[msbuild]::getdirectorynameoffileabove" && args.size() == 2)
      return fileAboveDir(joinPathStr(thisDir, args[0]), args[1]);
    if (fn == "[msbuild]::getpathoffileabove" && !args.empty()) {
      std::string start = args.size() > 1 ? joinPathStr(thisDir, args[1]) : parentDirStr(thisDir);
      std::string d = fileAboveDir(start, args[0]);
      return d.empty() ? std::string() : d + "/" + args[0];
    }
    if (fn == "[system.io.path]::combine" && !args.empty()) {
      std::string acc;
      for (auto& a : args)
        acc = acc.empty() || isAbsolutePathStr(a) ? a : (endsWithSlash(acc) ? acc + a : acc + "/" + a);
      return acc;
    }
    if (fn == "[system.io.path]::getfullpath" && args.size() == 1) return joinPathStr(projDir, args[0]);
    if (fn == "[system.io.path]::getdirectoryname" && args.size() == 1) return parentDirStr(args[0]);
    return poison;  // other functions are not evaluated
  }
  if (lower(body.substr(0, 9)) == "registry:") return poison;
  if (body.find('(') != std::string::npos) return poison;  // $(Name.Method(...)) instance functions
  std::string low = lower(body);
  auto it = cx.props.find(low);
  if (it != cx.props.end()) return it->second;
  auto e = cx.env.find(body);
  if (e != cx.env.end()) return e->second;
  if (cx.processEnv) {
    if (const char* v = std::getenv(body.c_str())) return v;
  }
  return miss == Missing::Poison ? poison : std::string();
}

std::string expandEx(const std::string& s, const EvalCtx& cx, Missing miss) {
  if (s.find("$(") == std::string::npos) return s;
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '$' && i + 1 < s.size() && s[i + 1] == '(') {
      size_t close = matchingParen(s, i + 2);
      if (close == std::string::npos) {
        out += s.substr(i);
        break;
      }
      out += evalMacro(s.substr(i + 2, close - i - 2), cx, miss);
      i = close;
      continue;
    }
    out += s[i];
  }
  return out;
}

EvalCtx ctxFromMap(const std::map<std::string, std::string>& props) {
  EvalCtx cx;
  for (auto& [k, v] : props) cx.props[lower(k)] = v;
  return cx;
}

// Recursive-descent evaluator for MSBuild conditions.
struct CondParser {
  const std::string& s;
  size_t i = 0;
  const EvalCtx& cx;
  const std::string base;

  void ws() {
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
  }
  bool kw(const char* k) {
    ws();
    size_t n = std::strlen(k);
    if (lower(s.substr(i, n)) == k && (i + n >= s.size() || !std::isalnum(static_cast<unsigned char>(s[i + n])))) {
      i += n;
      return true;
    }
    return false;
  }
  std::string atom() {
    ws();
    if (i < s.size() && s[i] == '\'') {
      // The closing quote is the first one outside a $( ... ) macro.
      size_t e = i + 1;
      int depth = 0;
      for (; e < s.size(); ++e) {
        if (s[e] == '$' && e + 1 < s.size() && s[e + 1] == '(') ++depth, ++e;
        else if (s[e] == ')' && depth > 0) --depth;
        else if (s[e] == '\'' && depth == 0) break;
      }
      std::string v = s.substr(i + 1, e - i - 1);
      i = std::min(e + 1, s.size());
      return expandEx(v, cx, Missing::Empty);
    }
    size_t st = i;
    while (i < s.size() && !std::isspace(static_cast<unsigned char>(s[i])) && s[i] != ')' && s[i] != '=' && s[i] != '!')
      ++i;
    return expandEx(s.substr(st, i - st), cx, Missing::Empty);
  }
  void skipOpenParen() {
    ws();
    if (i < s.size() && s[i] == '(') ++i;
  }
  void skipCloseParen() {
    ws();
    if (i < s.size() && s[i] == ')') ++i;
  }
  bool primary() {
    ws();
    if (i < s.size() && s[i] == '!' && (i + 1 >= s.size() || s[i + 1] != '=')) {
      ++i;
      return !primary();
    }
    if (i < s.size() && s[i] == '(') {
      ++i;
      bool v = orExpr();
      skipCloseParen();
      return v;
    }
    if (kw("exists")) {
      skipOpenParen();
      std::string p = atom();
      skipCloseParen();
      if (p.empty() || poisoned(p)) return false;
      std::error_code ec;
      return fs::exists(fs::path(joinPathStr(base, p)), ec);
    }
    if (kw("hastrailingslash")) {
      skipOpenParen();
      std::string p = atom();
      skipCloseParen();
      return endsWithSlash(p);
    }
    std::string a = atom();
    ws();
    if (s.substr(i, 2) == "==") {
      i += 2;
      return lower(a) == lower(atom());
    }
    if (s.substr(i, 2) == "!=") {
      i += 2;
      return lower(a) != lower(atom());
    }
    return lower(a) == "true";
  }
  bool andExpr() {
    bool v = primary();
    while (kw("and")) v = primary() && v;
    return v;
  }
  bool orExpr() {
    bool v = andExpr();
    while (kw("or")) v = andExpr() || v;
    return v;
  }
};

bool evalCondition(const std::string& cond, const EvalCtx& cx, const std::string& baseDir) {
  if (trim(cond).empty()) return true;
  CondParser p{cond, 0, cx, baseDir};
  return p.orExpr();
}

}  // namespace

std::string expandProperties(const std::string& s, const std::map<std::string, std::string>& props) {
  std::string out = expandEx(s, ctxFromMap(props), Missing::Empty);
  out.erase(std::remove(out.begin(), out.end(), kPoison), out.end());  // unresolvable functions expand to ""
  return out;
}

bool evaluateCondition(const std::string& cond, const std::map<std::string, std::string>& props,
                       const fs::path& baseDir) {
  return evalCondition(cond, ctxFromMap(props), baseDir.generic_string());
}

// ---------------------------------------------------------------------------
// .sln
// ---------------------------------------------------------------------------

bool SolutionConfiguration::operator==(const SolutionConfiguration& o) const {
  return lower(configuration) == lower(o.configuration) && lower(platform) == lower(o.platform);
}

bool SolutionProject::isVcxproj() const { return lower(path.extension().string()) == ".vcxproj"; }

SolutionConfiguration parseSolutionConfiguration(const std::string& name) {
  size_t bar = name.find('|');
  if (bar == std::string::npos) return {trim(name), ""};
  return {trim(name.substr(0, bar)), trim(name.substr(bar + 1))};
}

SolutionConfiguration pickSolutionConfiguration(const std::vector<SolutionConfiguration>& available,
                                                const std::string& configuration, const std::string& platform,
                                                bool* exact) {
  SolutionConfiguration want{configuration, platform};
  if (exact) *exact = false;
  if (available.empty()) return want;
  for (auto& c : available)
    if (c == want) {
      if (exact) *exact = true;
      return c;
    }
  for (auto& c : available)
    if (lower(c.configuration) == "debug" && lower(c.platform) == "x64") return c;
  return available.front();
}

std::optional<SolutionProjectConfiguration> SolutionInfo::mapping(const SolutionProject& project,
                                                                  const SolutionConfiguration& solutionConfig) const {
  if (!hasProjectConfigurationMap) {
    SolutionProjectConfiguration pc;
    pc.configuration = solutionConfig.configuration;
    pc.platform = solutionConfig.platform;
    pc.build = true;
    return pc;
  }
  auto it = projectConfigurations.find(project.guid);
  if (it == projectConfigurations.end()) return std::nullopt;
  auto m = it->second.find(lower(solutionConfig.name()));
  if (m == it->second.end()) return std::nullopt;
  return m->second;
}

SolutionInfo readSolution(const fs::path& sln, std::string* error) {
  SolutionInfo info;
  info.path = sln;
  std::ifstream in(sln);
  if (!in) {
    if (error) *error = "cannot open " + sln.string();
    return info;
  }
  const std::string dir = hostPath(sln.parent_path().generic_string());
  std::string line;
  enum class Section { None, SolutionConfigs, ProjectConfigs } section = Section::None;
  bool first = true;
  while (std::getline(in, line)) {
    if (first && line.rfind("\xEF\xBB\xBF", 0) == 0) line.erase(0, 3);  // UTF-8 BOM
    first = false;
    line = trim(line);
    if (line.rfind("Project(", 0) == 0) {
      // Project("{TYPE-GUID}") = "Name", "rel\path.vcxproj", "{GUID}"
      size_t eq = line.find('=');
      if (eq == std::string::npos) continue;
      std::vector<std::string> parts;
      size_t p = 0;
      while (true) {
        size_t q1 = line.find('"', p);
        if (q1 == std::string::npos) break;
        size_t q2 = line.find('"', q1 + 1);
        if (q2 == std::string::npos) break;
        parts.push_back(line.substr(q1 + 1, q2 - q1 - 1));
        p = q2 + 1;
      }
      if (parts.size() < 4) continue;  // type guid, name, path, project guid
      SolutionProject pr;
      pr.typeGuid = parts[0];
      pr.name = parts[1];
      pr.path = fs::path(joinPathStr(dir, parts[2]));
      std::string g = parts[3];
      for (auto& c : g) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
      pr.guid = g;
      info.projects.push_back(std::move(pr));
      continue;
    }
    if (line.rfind("GlobalSection(", 0) == 0) {
      if (line.rfind("GlobalSection(SolutionConfigurationPlatforms)", 0) == 0) section = Section::SolutionConfigs;
      else if (line.rfind("GlobalSection(ProjectConfigurationPlatforms)", 0) == 0) {
        section = Section::ProjectConfigs;
        info.hasProjectConfigurationMap = true;
      } else section = Section::None;
      continue;
    }
    if (line.rfind("EndGlobalSection", 0) == 0) {
      section = Section::None;
      continue;
    }
    size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    std::string lhs = trim(line.substr(0, eq)), rhs = trim(line.substr(eq + 1));
    if (section == Section::SolutionConfigs) {
      SolutionConfiguration sc = parseSolutionConfiguration(lhs);
      if (sc.platform.empty()) continue;
      if (std::find(info.configurations.begin(), info.configurations.end(), sc) == info.configurations.end())
        info.configurations.push_back(std::move(sc));
    } else if (section == Section::ProjectConfigs) {
      // {GUID}.Debug|x64.ActiveCfg = Debug|x64      {GUID}.Debug|x64.Build.0 = Debug|x64
      if (lhs.empty() || lhs[0] != '{') continue;
      size_t close = lhs.find('}');
      if (close == std::string::npos || close + 1 >= lhs.size() || lhs[close + 1] != '.') continue;
      std::string guid = lhs.substr(0, close + 1);
      for (auto& c : guid) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
      std::string rest = lhs.substr(close + 2);
      bool active = false, build = false;
      const std::string a = ".ActiveCfg", b = ".Build.0";
      if (rest.size() > a.size() && rest.compare(rest.size() - a.size(), a.size(), a) == 0) {
        active = true;
        rest.resize(rest.size() - a.size());
      } else if (rest.size() > b.size() && rest.compare(rest.size() - b.size(), b.size(), b) == 0) {
        build = true;
        rest.resize(rest.size() - b.size());
      } else continue;  // Deploy.0 and friends
      SolutionConfiguration target = parseSolutionConfiguration(rhs);
      auto& pc = info.projectConfigurations[guid][lower(rest)];
      if (active) {
        pc.configuration = target.configuration;
        pc.platform = target.platform;
      } else if (build) {
        pc.build = true;
        if (pc.configuration.empty()) {
          pc.configuration = target.configuration;
          pc.platform = target.platform;
        }
      }
    }
  }
  return info;
}

std::vector<SolutionConfiguration> solutionConfigurations(const fs::path& sln, std::string* error) {
  return readSolution(sln, error).configurations;
}

std::vector<fs::path> projectsInSolution(const fs::path& sln, std::string* error) {
  std::vector<fs::path> out;
  std::string err;
  SolutionInfo info = readSolution(sln, &err);
  if (!err.empty() && error) *error = err;
  for (auto& p : info.projects)
    if (p.isVcxproj()) out.push_back(fs::weakly_canonical(p.path));
  return out;
}

// ---------------------------------------------------------------------------
// .vcxproj evaluation (MSBuild passes: properties and imports, item definitions, items)
// ---------------------------------------------------------------------------

namespace {

struct ProjectState {
  EvalCtx cx;
  std::string projectDir;                     // string-level, forward slashes
  std::map<std::string, std::string> clMeta;  // ItemDefinitionGroup ClCompile metadata (merged across groups)
  struct Scope {
    const xml::Node* node;
    std::string dir;   // directory of the file the node came from
    std::string file;  // that file (for $(MSBuildThisFile...) when the node is evaluated in pass 2 / 3)
  };
  std::vector<std::unique_ptr<xml::Node>> roots;  // keeps the imported documents alive
  std::vector<Scope> defGroups, itemGroups;       // pass 1 output, in import order
  struct Item {
    std::string file;
    std::map<std::string, std::string> meta;  // raw values; may still contain %(Name) references
  };
  std::vector<Item> items;
  std::set<std::string> visited;
  std::string error;
  bool dirProps = false, dirTargets = false;
};

// Substitute "%(name)" in `value` by `replacement`.
std::string substituteMeta(const std::string& value, const std::string& name, const std::string& replacement) {
  const std::string token = "%(" + name + ")";
  std::string out;
  size_t pos = 0;
  while (true) {
    size_t f = value.find(token, pos);
    if (f == std::string::npos) {
      out += value.substr(pos);
      break;
    }
    out += value.substr(pos, f - pos);
    out += replacement;
    pos = f + token.size();
  }
  return out;
}

// Any other %(...) reference cannot be resolved: it poisons the entry that holds it.
std::string poisonMetaRefs(const std::string& v) {
  std::string out;
  for (size_t i = 0; i < v.size(); ++i) {
    if (v[i] == '%' && i + 1 < v.size() && v[i + 1] == '(') {
      size_t c = v.find(')', i);
      if (c == std::string::npos) break;
      out += kPoison;
      i = c;
      continue;
    }
    out += v[i];
  }
  return out;
}

void importFile(const std::string& path, ProjectState& st);

std::string importPathOf(const xml::Node& c, ProjectState& st) {
  return expandEx(c.attr("Project"), st.cx, Missing::Poison);
}

void importProject(const std::string& projectAttr, ProjectState& st, const std::string& fileDir) {
  if (projectAttr.empty() || poisoned(projectAttr)) return;  // $(VCTargetsPath)\Microsoft.Cpp.props and the like
  std::error_code ec;
  for (auto& p : expandInclude(fileDir, projectAttr))
    if (fs::is_regular_file(fs::path(p), ec)) importFile(p, st);
}

void importDirectoryBuild(ProjectState& st, const char* fileName) {
  std::string dir = st.projectDir;
  std::error_code ec;
  for (int guard = 0; guard < 64; ++guard) {
    std::string cand = dir + "/" + fileName;
    if (fs::is_regular_file(fs::path(cand), ec)) {
      importFile(cand, st);
      return;
    }
    std::string up = parentDirStr(dir);
    if (up == dir || up == ".") return;
    dir = up;
  }
}

void collect(const xml::Node& n, ProjectState& st, const std::string& fileDir, const std::string& filePath, bool root) {
  for (auto& childPtr : n.children) {
    const xml::Node& c = *childPtr;
    const std::string cond = c.attr("Condition");
    if (c.name == "PropertyGroup") {
      if (!cond.empty() && !evalCondition(cond, st.cx, fileDir)) continue;
      for (auto& pp : c.children) {
        std::string pc = pp->attr("Condition");
        if (!pc.empty() && !evalCondition(pc, st.cx, fileDir)) continue;
        st.cx.props[lower(pp->name)] = expandEx(trim(pp->text), st.cx, Missing::Poison);
      }
    } else if (c.name == "ItemDefinitionGroup") {
      st.defGroups.push_back({&c, fileDir, filePath});  // Condition is evaluated against the final properties (pass 2)
    } else if (c.name == "ItemGroup") {
      st.itemGroups.push_back({&c, fileDir, filePath});  // ... and pass 3
    } else if (c.name == "Import") {
      if (!cond.empty() && !evalCondition(cond, st.cx, fileDir)) continue;
      std::string proj = importPathOf(c, st);
      const std::string rawProj = lower(c.attr("Project"));
      if (root && !st.dirProps && rawProj.find("microsoft.cpp.default.props") != std::string::npos) {
        // Microsoft.Common.props (pulled in by Microsoft.Cpp.Default.props) imports Directory.Build.props first.
        st.dirProps = true;
        importDirectoryBuild(st, "Directory.Build.props");
      }
      // The toolset's own files are never evaluated: their content is the toolchain's business (clangd's
      // --query-driver), and they rely on properties and functions that are not available here.
      if (rawProj.find("vctargetspath") != std::string::npos || rawProj.find("msbuildtoolspath") != std::string::npos ||
          rawProj.find("msbuildbinpath") != std::string::npos ||
          rawProj.find("msbuildextensionspath") != std::string::npos)
        continue;
      importProject(proj, st, fileDir);
    } else if (c.name == "ImportGroup") {
      if (!cond.empty() && !evalCondition(cond, st.cx, fileDir)) continue;
      collect(c, st, fileDir, filePath, false);
    } else if (c.name == "Choose") {
      if (!cond.empty() && !evalCondition(cond, st.cx, fileDir)) continue;
      const xml::Node* chosen = nullptr;
      for (auto& w : c.children) {
        if (w->name == "When" && evalCondition(w->attr("Condition"), st.cx, fileDir)) {
          chosen = w.get();
          break;
        }
      }
      if (!chosen) {
        for (auto& w : c.children)
          if (w->name == "Otherwise") chosen = w.get();
      }
      if (chosen) collect(*chosen, st, fileDir, filePath, false);
    }
  }
}

void importFile(const std::string& path, ProjectState& st) {
  std::string key = normalizePathStr(path);
  if (st.visited.count(key)) return;
  st.visited.insert(key);
  std::string err;
  auto doc = xml::parseFile(key, &err);
  if (!doc) {
    st.error += key + ": " + err + "\n";
    return;
  }
  const std::string dir = parentDirStr(key);
  const std::string prevDir = prop(st.cx, "msbuildthisfiledirectory");
  const std::string prevFile = prop(st.cx, "msbuildthisfile");
  st.cx.props["msbuildthisfiledirectory"] = dir + "/";
  st.cx.props["msbuildthisfile"] = key.substr(key.find_last_of('/') + 1);
  st.cx.props["msbuildthisfilefullpath"] = key;
  const xml::Node* rootNode = doc.get();
  st.roots.push_back(std::move(doc));
  collect(*rootNode, st, dir, key, false);
  st.cx.props["msbuildthisfiledirectory"] = prevDir;
  st.cx.props["msbuildthisfile"] = prevFile;
}

// MSBuild evaluates item definitions and items in the context of the file they are written in.
void enterScope(ProjectState& st, const ProjectState::Scope& g) {
  st.cx.props["msbuildthisfiledirectory"] = g.dir + "/";
  st.cx.props["msbuildthisfile"] = g.file.substr(g.file.find_last_of('/') + 1);
  st.cx.props["msbuildthisfilefullpath"] = g.file;
}

bool isSourceExt(const std::string& path) {
  std::string e = lower(fs::path(path).extension().string());
  return e == ".cpp" || e == ".cc" || e == ".cxx" || e == ".c" || e == ".cu" || e == ".mm" || e == ".m" ||
         e == ".ixx" || e == ".cppm";
}

bool isTrue(const std::string& v) { return lower(trim(v)) == "true"; }

void defineOnce(std::vector<std::string>& defs, const std::string& d) {
  const std::string name = d.substr(0, d.find('='));
  for (auto& e : defs)
    if (e.substr(0, e.find('=')) == name) return;
  defs.push_back(d);
}

}  // namespace

std::vector<CompileCommand> fromVcxproj(const fs::path& vcxproj, const VcxprojOptions& opt, std::string* error) {
  std::vector<CompileCommand> out;
  std::error_code ec;
  fs::path proj = fs::weakly_canonical(vcxproj, ec);
  const fs::path projectDirPath = proj.parent_path();
  ProjectState st;
  st.cx.env = opt.environment;
  st.cx.processEnv = opt.useProcessEnvironment;
  st.projectDir = normalizePathStr(projectDirPath.generic_string());
  const std::string projPath = normalizePathStr(proj.generic_string());
  const std::string platformLow = lower(opt.platform);
  auto& P = st.cx.props;
  P["configuration"] = opt.configuration;
  P["platform"] = opt.platform;
  P["projectdir"] = st.projectDir + "/";
  P["projectname"] = proj.stem().string();
  P["projectfilename"] = proj.filename().string();
  P["projectpath"] = projPath;
  P["projectext"] = proj.extension().string();
  const std::string slnDir =
      opt.solutionDir.empty() ? st.projectDir : normalizePathStr(opt.solutionDir.generic_string());
  P["solutiondir"] = slnDir + "/";
  if (!opt.solutionFile.empty()) {
    const std::string slnPath = normalizePathStr(opt.solutionFile.generic_string());
    P["solutionpath"] = slnPath;
    P["solutionfilename"] = opt.solutionFile.filename().string();
    P["solutionname"] = opt.solutionFile.stem().string();
    P["solutionext"] = opt.solutionFile.extension().string();
  }
  P["msbuildprojectdirectory"] = st.projectDir;
  P["msbuildprojectname"] = proj.stem().string();
  P["msbuildprojectfile"] = proj.filename().string();
  P["msbuildprojectfullpath"] = projPath;
  P["msbuildprojectextension"] = proj.extension().string();
  P["platformtarget"] = platformLow == "win32" ? "x86" : opt.platform;
  P["outdir"] = slnDir + "/" + (platformLow == "win32" ? "" : opt.platform + "/") + opt.configuration + "/";
  P["intdir"] = (platformLow == "win32" ? "" : opt.platform + "/") + opt.configuration + "/";
  P["targetname"] = proj.stem().string();
  for (auto& [k, v] : opt.extraProperties) P[lower(k)] = v;

  // Pass 1: properties and imports, in document order (the project body between Directory.Build.props and .targets).
  {
    std::string err;
    auto doc = xml::parseFile(projPath, &err);
    if (!doc) {
      if (error) *error = projPath + ": " + err + "\n";
      return out;
    }
    st.visited.insert(projPath);
    st.cx.props["msbuildthisfiledirectory"] = st.projectDir + "/";
    st.cx.props["msbuildthisfile"] = proj.filename().string();
    st.cx.props["msbuildthisfilefullpath"] = projPath;
    const xml::Node* rootNode = doc.get();
    st.roots.push_back(std::move(doc));
    // Directory.Build.props goes in where Microsoft.Cpp.Default.props sits; a project without that import (hand-made
    // or SDK-style) gets it at the very top.
    bool hasDefaultProps = false;
    for (auto& c : rootNode->children) {
      if (c->name == "Import" && lower(c->attr("Project")).find("microsoft.cpp.default.props") != std::string::npos)
        hasDefaultProps = true;
    }
    if (opt.directoryBuildFiles) {
      if (!hasDefaultProps) {
        st.dirProps = true;
        importDirectoryBuild(st, "Directory.Build.props");
      }
    } else {
      st.dirProps = true;  // suppress the import in collect()
    }
    collect(*rootNode, st, st.projectDir, projPath, true);
    st.cx.props["msbuildthisfiledirectory"] = st.projectDir + "/";
    st.cx.props["msbuildthisfile"] = proj.filename().string();
    if (opt.directoryBuildFiles) importDirectoryBuild(st, "Directory.Build.targets");
  }

  // Pass 2: item definitions with the final property values.
  for (auto& g : st.defGroups) {
    enterScope(st, g);
    const std::string cond = g.node->attr("Condition");
    if (!cond.empty() && !evalCondition(cond, st.cx, g.dir)) continue;
    for (auto* cl : g.node->childrenNamed("ClCompile")) {
      const std::string cc = cl->attr("Condition");
      if (!cc.empty() && !evalCondition(cc, st.cx, g.dir)) continue;
      for (auto& m : cl->children) {
        const std::string mc = m->attr("Condition");
        if (!mc.empty() && !evalCondition(mc, st.cx, g.dir)) continue;
        std::string v = expandEx(trim(m->text), st.cx, Missing::Poison);
        auto cur = st.clMeta.find(m->name);
        st.clMeta[m->name] = poisonMetaRefs(substituteMeta(v, m->name, cur == st.clMeta.end() ? "" : cur->second));
      }
    }
  }

  // Pass 3: items.
  for (auto& g : st.itemGroups) {
    enterScope(st, g);
    const std::string cond = g.node->attr("Condition");
    if (!cond.empty() && !evalCondition(cond, st.cx, g.dir)) continue;
    for (auto* it : g.node->childrenNamed("ClCompile")) {
      const std::string ic = it->attr("Condition");
      if (!ic.empty() && !evalCondition(ic, st.cx, g.dir)) continue;
      const std::string inc = it->attr("Include");
      if (inc.empty()) continue;  // Update= / Remove= are not supported
      std::map<std::string, std::string> rawMeta;
      for (auto& m : it->children) {
        const std::string mc = m->attr("Condition");
        if (!mc.empty() && !evalCondition(mc, st.cx, g.dir)) continue;
        std::string v = expandEx(trim(m->text), st.cx, Missing::Poison);
        auto cur = rawMeta.find(m->name);
        if (cur != rawMeta.end()) v = substituteMeta(v, m->name, cur->second);
        rawMeta[m->name] = v;
      }
      for (auto& entry : splitSemis(expandEx(inc, st.cx, Missing::Poison))) {
        if (poisoned(entry)) continue;
        for (auto& file : expandInclude(st.projectDir, entry)) {
          ProjectState::Item item;
          item.file = file;
          item.meta = rawMeta;
          st.items.push_back(std::move(item));
        }
      }
    }
  }
  if (!st.error.empty() && error) *error = st.error;

  auto metaOf = [&](const ProjectState::Item& item, const std::string& name) {
    auto b = st.clMeta.find(name);
    const std::string base = b == st.clMeta.end() ? "" : b->second;
    auto it = item.meta.find(name);
    if (it == item.meta.end()) return base;
    return poisonMetaRefs(substituteMeta(it->second, name, base));
  };
  auto listOf = [&](const ProjectState::Item& item, const std::string& name) {
    std::vector<std::string> l;
    for (auto& e : splitSemis(metaOf(item, name)))
      if (!poisoned(e)) l.push_back(e);
    return l;
  };
  auto propList = [&](const std::string& lowName) {
    std::vector<std::string> l;
    for (auto& e : splitSemis(prop(st.cx, lowName)))
      if (!poisoned(e)) l.push_back(e);
    return l;
  };

  const std::string charSet = lower(prop(st.cx, "characterset"));
  const bool debugLibs = isTrue(prop(st.cx, "usedebuglibraries"));
  const bool is64 = platformLow == "x64" || platformLow == "arm64" || platformLow == "arm64ec";

  for (auto& item : st.items) {
    if (!isSourceExt(item.file)) continue;
    if (isTrue(metaOf(item, "ExcludedFromBuild"))) continue;
    CompileCommand cmd;
    cmd.directory = projectDirPath;
    {
      fs::path f(item.file);
      cmd.file = f.is_absolute() ? fs::weakly_canonical(f, ec) : f;
    }
    const std::string compileAs = lower(metaOf(item, "CompileAs"));
    const std::string ext = lower(fs::path(item.file).extension().string());
    bool isC = ext == ".c";
    if (compileAs == "compileasc") isC = true;
    else if (compileAs.rfind("compileascpp", 0) == 0) isC = false;

    std::vector<std::string> a;
    a.push_back("clang-cl");
    const std::string stdv = lower(metaOf(item, "LanguageStandard"));
    const std::string stdc = lower(metaOf(item, "LanguageStandard_C"));
    if (!isC) {
      if (stdv == "stdcpp14") a.push_back("/std:c++14");
      else if (stdv == "stdcpp17") a.push_back("/std:c++17");
      else if (stdv == "stdcpp20") a.push_back("/std:c++20");
      else if (stdv == "stdcpp23" || stdv == "stdcpp23preview" || stdv == "stdcpplatest") a.push_back("/std:c++latest");
      else a.push_back("/std:c++17");
      const std::string eh = lower(metaOf(item, "ExceptionHandling"));
      if (eh == "" || eh == "sync") a.push_back("/EHsc");
      else if (eh == "async") a.push_back("/EHa");
      else if (eh == "synccthrow") a.push_back("/EHs");
      // "false": no /EH
    } else {
      if (stdc == "stdc11") a.push_back("/std:c11");
      else if (stdc == "stdc17") a.push_back("/std:c17");
      else if (stdc == "stdclatest") a.push_back("/std:clatest");
    }
    a.push_back("/nologo");
    if (platformLow == "x64") a.push_back("-m64");
    else if (platformLow == "win32" || platformLow == "x86") a.push_back("-m32");
    else if (platformLow == "arm64") a.push_back("--target=aarch64-pc-windows-msvc");

    // Include directories: /I in project order, then IncludePath (cl searches those after /I via INCLUDE), then the
    // external ones as system directories.
    auto addDirs = [&](const char* flag, const std::vector<std::string>& dirs) {
      for (auto& d : dirs) a.push_back(std::string(flag) + joinPathStr(st.projectDir, d));
    };
    addDirs("/I", listOf(item, "AdditionalIncludeDirectories"));
    addDirs("/I", propList("includepath"));
    {
      std::vector<std::string> ext = propList("externalincludepath");
      for (auto& e : listOf(item, "ExternalIncludePath")) ext.push_back(e);
      for (auto& e : listOf(item, "AdditionalExternalIncludeDirectories")) ext.push_back(e);
      addDirs("/imsvc", ext);
    }
    const std::string extWarn = lower(metaOf(item, "ExternalWarningLevel"));
    if (extWarn == "turnoffallwarnings" || extWarn == "level0") a.push_back("-Wno-system-headers");
    else if (extWarn.rfind("level", 0) == 0 && extWarn.size() == 6) a.push_back("-Wsystem-headers");

    // Runtime library: explicit, else what the toolset derives from UseDebugLibraries.
    std::string rtl = metaOf(item, "RuntimeLibrary");
    if (rtl.empty()) rtl = debugLibs ? "MultiThreadedDebugDLL" : "MultiThreadedDLL";
    const std::string rtlLow = lower(rtl);
    const bool rtlDebug = rtlLow == "multithreadeddebugdll" || rtlLow == "multithreadeddebug";
    const bool rtlDll = rtlLow == "multithreadeddebugdll" || rtlLow == "multithreadeddll";
    const bool rtlKnown = rtlDebug || rtlDll || rtlLow == "multithreaded";

    // Defines: PreprocessorDefinitions as written, then what cl/MSBuild add on their own, then /U.
    std::vector<std::string> defs;
    for (auto& d : listOf(item, "PreprocessorDefinitions")) defs.push_back(d);
    if (opt.implicitDefines) {
      defineOnce(defs, "_WIN32");
      if (is64) defineOnce(defs, "_WIN64");
      if (rtlKnown) {
        if (rtlDebug) defineOnce(defs, "_DEBUG");
        if (rtlDll) defineOnce(defs, "_DLL");
        defineOnce(defs, "_MT");
      }
      if (charSet == "unicode") {
        defineOnce(defs, "UNICODE");
        defineOnce(defs, "_UNICODE");
      } else if (charSet == "multibyte") {
        defineOnce(defs, "_MBCS");
      }
    }
    for (auto& d : defs) a.push_back("/D" + d);
    for (auto& u : listOf(item, "UndefinePreprocessorDefinitions")) a.push_back("/U" + u);

    // Precompiled header (/Yu, /Yc): parse with the header forced in, as the build effectively does.
    const std::string pch = lower(metaOf(item, "PrecompiledHeader"));
    std::string pchHeader = trim(metaOf(item, "PrecompiledHeaderFile"));
    if ((pch == "use" || pch == "create") && !pchHeader.empty() && !poisoned(pchHeader)) {
      std::string resolved = hostPath(pchHeader);
      if (!isAbsolutePathStr(pchHeader)) {
        std::vector<std::string> dirs{parentDirStr(item.file), st.projectDir};
        for (auto& d : listOf(item, "AdditionalIncludeDirectories")) dirs.push_back(joinPathStr(st.projectDir, d));
        for (auto& d : dirs) {
          std::string cand = joinPathStr(d, pchHeader);
          if (fs::exists(fs::path(cand), ec)) {
            resolved = cand;
            break;
          }
        }
      } else {
        resolved = normalizePathStr(pchHeader);
      }
      // Only the forced include: clang-cl's /Yu makes the parser skip everything before the through header (a whole
      // file that does not include it), and clangd has no .pch to load anyway.
      a.push_back("/FI" + resolved);
    }
    for (auto& fi : listOf(item, "ForcedIncludeFiles"))
      a.push_back("/FI" + (isAbsolutePathStr(fi) ? normalizePathStr(fi) : hostPath(fi)));

    if (!isC) {
      if (lower(metaOf(item, "RuntimeTypeInfo")) == "false") a.push_back("/GR-");
      if (lower(metaOf(item, "TreatWChar_tAsBuiltInType")) == "false") a.push_back("/Zc:wchar_t-");
    }
    if (isTrue(metaOf(item, "ConformanceMode"))) a.push_back("/permissive-");
    if (isTrue(metaOf(item, "UseStandardPreprocessor"))) a.push_back("/Zc:preprocessor");
    if (rtlLow == "multithreadeddebugdll") a.push_back("/MDd");
    else if (rtlLow == "multithreadeddll") a.push_back("/MD");
    else if (rtlLow == "multithreadeddebug") a.push_back("/MTd");
    else if (rtlLow == "multithreaded") a.push_back("/MT");
    {
      const std::string simd = lower(metaOf(item, "EnableEnhancedInstructionSet"));
      if (simd == "advancedvectorextensions") a.push_back("/arch:AVX");
      else if (simd == "advancedvectorextensions2") a.push_back("/arch:AVX2");
      else if (simd == "advancedvectorextensions512") a.push_back("/arch:AVX512");
      else if (simd == "streamingsimdextensions" && !is64) a.push_back("/arch:SSE");
      else if (simd == "streamingsimdextensions2" && !is64) a.push_back("/arch:SSE2");
      if (isTrue(metaOf(item, "OpenMPSupport"))) a.push_back("/openmp");
    }
    for (auto& e : splitWindowsArgs(metaOf(item, "AdditionalOptions"))) {
      if (!poisoned(e)) a.push_back(e);
    }
    a.push_back(isC ? "/TC" : "/TP");
    a.push_back("/c");
    a.push_back(cmd.file.generic_string());
    cmd.arguments = std::move(a);
    out.push_back(std::move(cmd));
  }
  return out;
}

std::vector<CompileCommand> fromSolution(const fs::path& sln, const VcxprojOptions& optIn, std::string* error) {
  std::vector<CompileCommand> out;
  VcxprojOptions opt = optIn;
  std::error_code ec;
  opt.solutionFile = fs::weakly_canonical(sln, ec);
  if (opt.solutionDir.empty()) opt.solutionDir = opt.solutionFile.parent_path();
  std::string err;
  SolutionInfo info = readSolution(sln, &err);
  const SolutionConfiguration active =
      pickSolutionConfiguration(info.configurations, optIn.configuration, optIn.platform);
  for (auto& p : info.projects) {
    if (!p.isVcxproj()) continue;
    auto map = info.mapping(p, active);
    if (!map || !map->build) continue;  // not part of this configuration's build
    fs::path path = fs::weakly_canonical(p.path, ec);
    if (!fs::is_regular_file(path, ec)) {
      err += "missing project " + path.string() + "\n";
      continue;
    }
    VcxprojOptions po = opt;
    po.configuration = map->configuration;
    po.platform = map->platform;
    std::string perr;
    auto cmds = fromVcxproj(path, po, &perr);
    err += perr;
    out.insert(out.end(), std::make_move_iterator(cmds.begin()), std::make_move_iterator(cmds.end()));
  }
  if (error) *error = err;
  return out;
}

// ---------------------------------------------------------------------------
// Heuristic
// ---------------------------------------------------------------------------

std::vector<fs::path> inferIncludeDirs(const std::vector<fs::path>& headers,
                                       const std::vector<std::string>& includePaths) {
  // filename -> header paths with that filename
  std::map<std::string, std::vector<std::string>> byName;
  for (auto& h : headers) byName[h.filename().string()].push_back(h.generic_string());

  // For each include path, the candidate directories that would satisfy it.
  std::map<std::string, std::vector<std::string>> candidatesFor;  // include -> dirs
  std::map<std::string, std::set<std::string>> coverage;          // dir -> includes
  for (auto inc : includePaths) {
    std::replace(inc.begin(), inc.end(), '\\', '/');
    while (inc.rfind("./", 0) == 0) inc.erase(0, 2);
    if (inc.empty()) continue;
    std::string fname = fs::path(inc).filename().string();
    auto it = byName.find(fname);
    if (it == byName.end()) continue;
    std::string suffix = "/" + inc;
    for (auto& h : it->second) {
      if (h.size() <= suffix.size() || h.compare(h.size() - suffix.size(), suffix.size(), suffix) != 0) continue;
      std::string dir = h.substr(0, h.size() - suffix.size());
      candidatesFor[inc].push_back(dir);
      coverage[dir].insert(inc);
    }
  }
  // Greedy set cover.
  std::vector<fs::path> out;
  std::set<std::string> uncovered;
  for (auto& [inc, dirs] : candidatesFor) uncovered.insert(inc);
  while (!uncovered.empty() && !coverage.empty()) {
    std::string best;
    size_t bestCount = 0;
    for (auto& [dir, incs] : coverage) {
      size_t n = 0;
      for (auto& i : incs) n += uncovered.count(i);
      // Tie-break towards shallower directories.
      if (n > bestCount || (n == bestCount && n > 0 &&
                            std::count(dir.begin(), dir.end(), '/') < std::count(best.begin(), best.end(), '/'))) {
        best = dir;
        bestCount = n;
      }
    }
    if (bestCount == 0) break;
    out.emplace_back(best);
    for (auto& i : coverage[best]) uncovered.erase(i);
    coverage.erase(best);
  }
  return out;
}

std::vector<CompileCommand> heuristic(const fs::path& root, const std::vector<fs::path>& sources,
                                      const std::vector<fs::path>& headers, const std::string& stdVersion,
                                      const std::vector<std::string>& includePaths, bool clDriver) {
  std::vector<std::string> incs;
  if (!includePaths.empty()) {
    for (auto& d : inferIncludeDirs(headers, includePaths)) incs.push_back(d.generic_string());
  } else {
    // No include information: every directory that contains a header. Capped.
    std::set<std::string> dirs;
    for (auto& h : headers) dirs.insert(h.parent_path().generic_string());
    incs.assign(dirs.begin(), dirs.end());
    std::stable_sort(incs.begin(), incs.end(), [](const std::string& a, const std::string& b) {
      return std::count(a.begin(), a.end(), '/') < std::count(b.begin(), b.end(), '/');
    });
    if (incs.size() > 400) incs.resize(400);
  }
  // Conventional roots are cheap to add and often right.
  for (auto name : {"", "include", "inc", "src", "source", "public"}) {
    std::error_code ec;
    fs::path d = *name ? root / name : root;
    std::string ds = d.generic_string();
    if (fs::is_directory(d, ec) && std::find(incs.begin(), incs.end(), ds) == incs.end()) incs.push_back(ds);
  }

  std::vector<CompileCommand> out;
  for (auto& s : sources) {
    CompileCommand c;
    c.directory = root;
    c.file = s;
    bool isC = lower(s.extension().string()) == ".c";
    if (clDriver) {
      c.arguments.push_back("clang-cl");
      c.arguments.push_back(isC ? "/std:c11" : "/std:" + stdVersion);
      if (!isC) c.arguments.push_back("/EHsc");
    } else {
      c.arguments.push_back(isC ? "clang" : "clang++");
      c.arguments.push_back(isC ? "-std=c11" : "-std=" + stdVersion);
    }
    for (auto& d : incs) c.arguments.push_back("-I" + d);
    c.arguments.push_back(clDriver ? "/c" : "-c");
    c.arguments.push_back(s.generic_string());
    out.push_back(std::move(c));
  }
  return out;
}

std::optional<fs::path> clExeInToolsDir(const fs::path& toolsDir) {
  std::error_code ec;
  fs::path cl = toolsDir / "bin" / "Hostx64" / "x64" / "cl.exe";
  if (fs::is_regular_file(cl, ec)) return cl;
  return std::nullopt;
}

namespace {
std::vector<int> versionParts(const std::string& v) {
  std::vector<int> out;
  size_t i = 0;
  while (i < v.size()) {
    size_t j = i;
    int n = 0;
    while (j < v.size() && std::isdigit(static_cast<unsigned char>(v[j]))) n = n * 10 + (v[j++] - '0');
    out.push_back(n);
    i = (j == i) ? i + 1 : j + 1;
  }
  return out;
}
std::string trimLine(std::string s) {
  size_t nl = s.find_first_of("\r\n");
  if (nl != std::string::npos) s.resize(nl);
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
  size_t b = 0;
  while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
  return s.substr(b);
}
}  // namespace

std::optional<fs::path> clExeInVsInstall(const fs::path& installationPath) {
  std::error_code ec;
  fs::path msvc = installationPath / "VC" / "Tools" / "MSVC";
  std::vector<std::pair<std::vector<int>, fs::path>> versions;
  for (auto& e : fs::directory_iterator(msvc, ec)) {
    if (e.is_directory(ec)) versions.emplace_back(versionParts(e.path().filename().string()), e.path());
  }
  std::sort(versions.begin(), versions.end(), [](auto& a, auto& b) { return a.first > b.first; });
  for (auto& v : versions)
    if (auto cl = clExeInToolsDir(v.second)) return cl;  // newest that actually has cl.exe
  return std::nullopt;
}

std::optional<fs::path> discoverCl(const std::string& configured, const MsvcProbe& probe) {
  if (!configured.empty()) {
    fs::path c(configured);
    std::error_code ec;
    if (fs::is_directory(c, ec)) {
      if (fs::is_regular_file(c / "cl.exe", ec)) return c / "cl.exe";
      if (auto cl = clExeInToolsDir(c)) return cl;
    }
    return c;  // user override is trusted as given
  }
  if (!probe.vcToolsInstallDir.empty()) {
    if (auto cl = clExeInToolsDir(fs::path(probe.vcToolsInstallDir))) return cl;
  }
  if (probe.vswhere) {
    std::string install = trimLine(probe.vswhere());
    if (!install.empty()) {
      if (auto cl = clExeInVsInstall(fs::path(install))) return cl;
    }
  }
  return std::nullopt;
}

MsvcProbe systemMsvcProbe() {
  MsvcProbe p;
  if (const char* v = std::getenv("VCToolsInstallDir")) p.vcToolsInstallDir = v;
#ifdef _WIN32
  p.vswhere = [] {
    std::string exe = "vswhere.exe";
    if (const char* pf = std::getenv("ProgramFiles(x86)")) {
      fs::path cand = fs::path(pf) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe";
      std::error_code ec;
      if (fs::is_regular_file(cand, ec)) exe = cand.string();
    }
    std::string cmd = "\"" + exe +
                      "\" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 "
                      "-property installationPath";
    std::string out;
    if (FILE* f = _popen(("\"" + cmd + "\"").c_str(), "r")) {
      char buf[512];
      while (fgets(buf, sizeof buf, f)) out += buf;
      _pclose(f);
    }
    return out;
  };
#endif
  return p;
}

std::vector<std::string> platformSystemFlags() {
#ifdef __APPLE__
  std::string sdk;
  if (const char* e = std::getenv("SDKROOT")) sdk = e;
  if (sdk.empty()) {
    if (FILE* f = openShellPipe("xcrun --show-sdk-path 2>/dev/null", "r")) {
      char buf[1024];
      while (fgets(buf, sizeof buf, f)) sdk += buf;
      closeShellPipe(f);
    }
    sdk = trimLine(sdk);
  }
  if (!sdk.empty()) return {"-isysroot", sdk};
#endif
  return {};
}

std::string queryDriverArg(const fs::path& clExe) {
  return "--query-driver=" + clExe.parent_path().generic_string() + "/**";
}

std::vector<std::string> msvcClangdArgs(const fs::path& clExe) {
  if (clExe.empty()) return {};
  return {queryDriverArg(clExe)};
}

}  // namespace compdb
}  // namespace tf
