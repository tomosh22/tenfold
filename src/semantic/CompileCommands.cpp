#include "semantic/CompileCommands.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
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
// MSBuild property / condition evaluation
// ---------------------------------------------------------------------------

std::string expandProperties(const std::string& s, const std::map<std::string, std::string>& props) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '$' && i + 1 < s.size() && s[i + 1] == '(') {
      size_t close = s.find(')', i + 2);
      if (close == std::string::npos) {
        out += s.substr(i);
        break;
      }
      std::string name = s.substr(i + 2, close - i - 2);
      // Property functions like $(Foo.Replace(...)) are not supported; leave empty.
      auto it = props.find(name);
      if (it != props.end()) out += it->second;
      i = close;
      continue;
    }
    out += s[i];
  }
  return out;
}

namespace {

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

// Normalise a Windows-style path fragment for use on the host.
std::string hostPath(std::string p) {
  std::replace(p.begin(), p.end(), '\\', '/');
  return p;
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

// Recursive-descent evaluator for MSBuild conditions.
struct CondParser {
  const std::string& s;
  size_t i = 0;
  const std::map<std::string, std::string>& props;
  const fs::path& base;

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
      size_t e = s.find('\'', i + 1);
      if (e == std::string::npos) e = s.size();
      std::string v = s.substr(i + 1, e - i - 1);
      i = std::min(e + 1, s.size());
      return expandProperties(v, props);
    }
    size_t st = i;
    while (i < s.size() && !std::isspace(static_cast<unsigned char>(s[i])) && s[i] != ')' && s[i] != '=' && s[i] != '!')
      ++i;
    return expandProperties(s.substr(st, i - st), props);
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
      ws();
      if (i < s.size() && s[i] == ')') ++i;
      return v;
    }
    if (kw("exists")) {
      ws();
      if (i < s.size() && s[i] == '(') ++i;
      std::string p = atom();
      ws();
      if (i < s.size() && s[i] == ')') ++i;
      fs::path fp = hostPath(p);
      std::error_code ec;
      return fs::exists(fp.is_absolute() ? fp : base / fp, ec);
    }
    if (kw("hastrailingslash")) {
      ws();
      if (i < s.size() && s[i] == '(') ++i;
      std::string p = atom();
      ws();
      if (i < s.size() && s[i] == ')') ++i;
      return !p.empty() && (p.back() == '\\' || p.back() == '/');
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

}  // namespace

bool evaluateCondition(const std::string& cond, const std::map<std::string, std::string>& props,
                       const fs::path& baseDir) {
  if (trim(cond).empty()) return true;
  CondParser p{cond, 0, props, baseDir};
  return p.orExpr();
}

// ---------------------------------------------------------------------------
// .sln / .vcxproj
// ---------------------------------------------------------------------------

std::vector<fs::path> projectsInSolution(const fs::path& sln, std::string* error) {
  std::vector<fs::path> out;
  std::ifstream in(sln);
  if (!in) {
    if (error) *error = "cannot open " + sln.string();
    return out;
  }
  std::string line;
  fs::path dir = sln.parent_path();
  while (std::getline(in, line)) {
    // Project("{GUID}") = "Name", "rel\path.vcxproj", "{GUID}"
    if (line.rfind("Project(", 0) != 0) continue;
    size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    std::vector<std::string> parts;
    size_t p = eq;
    while (true) {
      size_t q1 = line.find('"', p);
      if (q1 == std::string::npos) break;
      size_t q2 = line.find('"', q1 + 1);
      if (q2 == std::string::npos) break;
      parts.push_back(line.substr(q1 + 1, q2 - q1 - 1));
      p = q2 + 1;
    }
    if (parts.size() < 2) continue;
    fs::path rel = hostPath(parts[1]);
    if (lower(rel.extension().string()) != ".vcxproj") continue;
    out.push_back(fs::weakly_canonical(dir / rel));
  }
  return out;
}

namespace {

struct ProjectState {
  std::map<std::string, std::string> props;
  // ClCompile item-definition metadata (merged across groups).
  std::map<std::string, std::string> clMeta;
  struct Item {
    fs::path file;
    std::map<std::string, std::string> meta;
    bool excluded = false;
  };
  std::vector<Item> items;
  std::set<std::string> visited;
  std::string error;
};

// Resolve "%(Name)" self-references by substituting the current metadata value.
std::string mergeMeta(const std::string& value, const std::string& name,
                      const std::map<std::string, std::string>& current) {
  std::string out;
  std::string token = "%(" + name + ")";
  size_t pos = 0;
  while (true) {
    size_t f = value.find(token, pos);
    if (f == std::string::npos) {
      out += value.substr(pos);
      break;
    }
    out += value.substr(pos, f - pos);
    auto it = current.find(name);
    if (it != current.end()) out += it->second;
    pos = f + token.size();
  }
  // Drop any other %(...) references we can't resolve.
  std::string clean;
  for (size_t i = 0; i < out.size(); ++i) {
    if (out[i] == '%' && i + 1 < out.size() && out[i + 1] == '(') {
      size_t c = out.find(')', i);
      if (c == std::string::npos) break;
      i = c;
      continue;
    }
    clean += out[i];
  }
  return clean;
}

void processProject(const fs::path& file, ProjectState& st, const fs::path& projectDir);

void processNode(const xml::Node& n, ProjectState& st, const fs::path& fileDir, const fs::path& projectDir) {
  for (auto& childPtr : n.children) {
    const xml::Node& c = *childPtr;
    std::string cond = c.attr("Condition");
    if (!cond.empty() && !evaluateCondition(cond, st.props, fileDir)) continue;

    if (c.name == "PropertyGroup") {
      for (auto& pp : c.children) {
        std::string pc = pp->attr("Condition");
        if (!pc.empty() && !evaluateCondition(pc, st.props, fileDir)) continue;
        st.props[pp->name] = expandProperties(trim(pp->text), st.props);
      }
    } else if (c.name == "ItemDefinitionGroup") {
      if (const xml::Node* cl = c.child("ClCompile")) {
        for (auto& m : cl->children) {
          std::string mc = m->attr("Condition");
          if (!mc.empty() && !evaluateCondition(mc, st.props, fileDir)) continue;
          std::string v = expandProperties(trim(m->text), st.props);
          st.clMeta[m->name] = mergeMeta(v, m->name, st.clMeta);
        }
      }
    } else if (c.name == "ItemGroup") {
      for (auto& it : c.childrenNamed("ClCompile")) {
        std::string inc = it->attr("Include");
        if (inc.empty()) continue;
        ProjectState::Item item;
        fs::path rel = hostPath(expandProperties(inc, st.props));
        item.file = rel.is_absolute() ? rel : projectDir / rel;
        for (auto& m : it->children) {
          std::string mc = m->attr("Condition");
          if (!mc.empty() && !evaluateCondition(mc, st.props, fileDir)) continue;
          std::string v = expandProperties(trim(m->text), st.props);
          if (m->name == "ExcludedFromBuild") {
            item.excluded = lower(v) == "true";
            continue;
          }
          item.meta[m->name] = v;
        }
        st.items.push_back(std::move(item));
      }
    } else if (c.name == "Import") {
      std::string proj = hostPath(expandProperties(c.attr("Project"), st.props));
      if (proj.empty() || proj.find("$(") != std::string::npos) continue;  // unresolvable (VCTargetsPath etc.)
      fs::path p = fs::path(proj).is_absolute() ? fs::path(proj) : fileDir / proj;
      std::error_code ec;
      if (fs::is_regular_file(p, ec)) processProject(fs::weakly_canonical(p, ec), st, projectDir);
    } else if (c.name == "ImportGroup" || c.name == "Choose" || c.name == "When" || c.name == "Otherwise") {
      processNode(c, st, fileDir, projectDir);
    }
  }
}

void processProject(const fs::path& file, ProjectState& st, const fs::path& projectDir) {
  std::string key = file.generic_string();
  if (st.visited.count(key)) return;
  st.visited.insert(key);
  std::string err;
  auto root = xml::parseFile(file.string(), &err);
  if (!root) {
    st.error += file.string() + ": " + err + "\n";
    return;
  }
  st.props["MSBuildThisFileDirectory"] = file.parent_path().generic_string() + "/";
  st.props["MSBuildThisFile"] = file.filename().string();
  processNode(*root, st, file.parent_path(), projectDir);
}

bool isSourceExt(const fs::path& p) {
  std::string e = lower(p.extension().string());
  return e == ".cpp" || e == ".cc" || e == ".cxx" || e == ".c" || e == ".cu" || e == ".mm" || e == ".m";
}

}  // namespace

std::vector<CompileCommand> fromVcxproj(const fs::path& vcxproj, const VcxprojOptions& opt, std::string* error) {
  std::vector<CompileCommand> out;
  std::error_code ec;
  fs::path proj = fs::weakly_canonical(vcxproj, ec);
  fs::path projectDir = proj.parent_path();
  ProjectState st;
  st.props["Configuration"] = opt.configuration;
  st.props["Platform"] = opt.platform;
  st.props["ProjectDir"] = projectDir.generic_string() + "/";
  st.props["ProjectName"] = proj.stem().string();
  st.props["ProjectFileName"] = proj.filename().string();
  st.props["ProjectPath"] = proj.generic_string();
  st.props["ProjectExt"] = proj.extension().string();
  fs::path slnDir = opt.solutionDir.empty() ? projectDir : opt.solutionDir;
  st.props["SolutionDir"] = slnDir.generic_string() + "/";
  st.props["MSBuildProjectDirectory"] = projectDir.generic_string();
  st.props["MSBuildProjectName"] = proj.stem().string();
  st.props["PlatformTarget"] = lower(opt.platform) == "win32" ? "x86" : opt.platform;
  for (auto& [k, v] : opt.extraProperties) st.props[k] = v;

  processProject(proj, st, projectDir);
  if (!st.error.empty() && error) *error = st.error;

  auto metaOf = [&](const ProjectState::Item& item, const std::string& name) {
    auto it = item.meta.find(name);
    std::string base = st.clMeta.count(name) ? st.clMeta.at(name) : "";
    if (it == item.meta.end()) return base;
    std::map<std::string, std::string> cur{{name, base}};
    return mergeMeta(it->second, name, cur);
  };

  for (auto& item : st.items) {
    if (item.excluded || !isSourceExt(item.file)) continue;
    CompileCommand cmd;
    cmd.directory = projectDir;
    cmd.file = fs::weakly_canonical(item.file, ec);
    auto& a = cmd.arguments;
    a.push_back("clang-cl");
    std::string stdv = metaOf(item, "LanguageStandard");
    if (stdv == "stdcpp14") a.push_back("/std:c++14");
    else if (stdv == "stdcpp17") a.push_back("/std:c++17");
    else if (stdv == "stdcpp20") a.push_back("/std:c++20");
    else if (stdv == "stdcpplatest") a.push_back("/std:c++latest");
    else a.push_back("/std:c++17");
    a.push_back("/EHsc");
    a.push_back("/nologo");
    if (lower(opt.platform) == "x64") a.push_back("-m64");
    else if (lower(opt.platform) == "win32") a.push_back("-m32");
    for (auto& inc : splitSemis(metaOf(item, "AdditionalIncludeDirectories"))) {
      if (inc.find("$(") != std::string::npos || inc.find("%(") != std::string::npos) continue;
      fs::path ip = hostPath(inc);
      a.push_back("/I" + (ip.is_absolute() ? ip : projectDir / ip).lexically_normal().generic_string());
    }
    for (auto& def : splitSemis(metaOf(item, "PreprocessorDefinitions"))) {
      if (def.find("%(") != std::string::npos) continue;
      a.push_back("/D" + def);
    }
    for (auto& fi : splitSemis(metaOf(item, "ForcedIncludeFiles"))) {
      if (fi.find("%(") != std::string::npos) continue;
      a.push_back("/FI" + hostPath(fi));
    }
    std::string rtti = metaOf(item, "RuntimeTypeInfo");
    if (lower(rtti) == "false") a.push_back("/GR-");
    std::string rtl = metaOf(item, "RuntimeLibrary");
    if (rtl == "MultiThreadedDebugDLL") a.push_back("/MDd");
    else if (rtl == "MultiThreadedDLL") a.push_back("/MD");
    else if (rtl == "MultiThreadedDebug") a.push_back("/MTd");
    else if (rtl == "MultiThreaded") a.push_back("/MT");
    std::string extra = metaOf(item, "AdditionalOptions");
    for (auto& e : splitCommand(extra)) {
      if (e.find("%(") == std::string::npos && e != "%(AdditionalOptions)") a.push_back(e);
    }
    if (lower(item.file.extension().string()) == ".c") a.push_back("/TC");
    else a.push_back("/TP");
    a.push_back("/c");
    a.push_back(cmd.file.generic_string());
    out.push_back(std::move(cmd));
  }
  return out;
}

std::vector<CompileCommand> fromSolution(const fs::path& sln, const VcxprojOptions& optIn, std::string* error) {
  std::vector<CompileCommand> out;
  VcxprojOptions opt = optIn;
  std::error_code ec;
  if (opt.solutionDir.empty()) opt.solutionDir = fs::weakly_canonical(sln, ec).parent_path();
  std::string err;
  for (auto& p : projectsInSolution(sln, &err)) {
    if (!fs::is_regular_file(p, ec)) {
      err += "missing project " + p.string() + "\n";
      continue;
    }
    std::string perr;
    auto cmds = fromVcxproj(p, opt, &perr);
    err += perr;
    out.insert(out.end(), cmds.begin(), cmds.end());
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
    if (FILE* f = popen("xcrun --show-sdk-path 2>/dev/null", "r")) {
      char buf[1024];
      while (fgets(buf, sizeof buf, f)) sdk += buf;
      pclose(f);
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
