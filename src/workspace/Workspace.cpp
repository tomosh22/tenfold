#include "workspace/Workspace.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <mutex>
#include <shared_mutex>
#include <unordered_set>

#include "core/JsonUtil.h"
#include "core/Log.h"
#include "core/PathUtil.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#include <nlohmann/json.hpp>

namespace tf {

namespace fs = std::filesystem;
using Json = nlohmann::json;

namespace {
std::string lower(std::string s) {
  for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}
bool hasExt(const fs::path& p, const std::vector<std::string>& exts) {
  std::string e = lower(pathUtf8(p.extension()));
  return std::find(exts.begin(), exts.end(), e) != exts.end();
}

// The ignore rules of a folder workspace for a directory's name (Workspace::isIgnoredDir).
bool ignoredDirName(const Workspace::Settings& st, const std::string& name) {
  if (st.ignoreDirs.count(name)) return true;
  const std::string ln = lower(name);
  for (auto& n : st.ignoreDirNames)
    if (ln == lower(n)) return true;
  for (auto& pre : st.ignoreDirPrefixes)
    if (!pre.empty() && ln.rfind(lower(pre), 0) == 0) return true;
  return false;
}

std::string trimBlanks(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
  return s.substr(a, b - a);
}

// '*' (any run) and '?' (one character) within one name; `icase` folds ASCII letters.
bool globName(const std::string& pat, const std::string& name, bool icase) {
  auto eq = [&](char a, char b) {
    return icase ? std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)) : a == b;
  };
  size_t p = 0, i = 0, star = std::string::npos, mark = 0;
  while (i < name.size()) {
    if (p < pat.size() && (pat[p] == '?' || eq(pat[p], name[i]))) {
      ++p, ++i;
    } else if (p < pat.size() && pat[p] == '*') {
      star = p++;
      mark = i;
    } else if (star != std::string::npos) {
      p = star + 1;
      i = ++mark;
    } else {
      return false;
    }
  }
  while (p < pat.size() && pat[p] == '*') ++p;
  return p == pat.size();
}

bool matchSegments(const std::vector<std::string>& names, size_t i, const std::vector<std::string>& pats, size_t j,
                   bool icase) {
  if (j == pats.size()) return i == names.size();
  if (pats[j] == "**") {  // zero or more directories; the last name is the file and is matched by what follows
    for (size_t k = i; k < names.size(); ++k)
      if (matchSegments(names, k, pats, j + 1, icase)) return true;
    return false;
  }
  if (i >= names.size()) return false;
  return globName(pats[j], names[i], icase) && matchSegments(names, i + 1, pats, j + 1, icase);
}

// The names of a path, whatever its separators ("C:\\a/b" and "C:/a\\b" are the same): split the '/' form, so the root
// directory element of one spelling ("\\") and of the other ("/") cannot make equal paths differ (Windows).
std::vector<std::string> components(const fs::path& p) {
  std::vector<std::string> out;
  const std::string s = pathKeyUtf8(p);
  for (size_t pos = 0; pos <= s.size();) {
    size_t j = s.find('/', pos);
    if (j == std::string::npos) j = s.size();
    if (j > pos && s.compare(pos, j - pos, ".") != 0) out.push_back(s.substr(pos, j - pos));
    pos = j + 1;
  }
  return out;
}

// Whether `dir` is `base` or below it (component-wise; Windows: case-insensitive).
bool isUnder(const fs::path& dir, const fs::path& base, PathStyle style) {
  const auto d = components(dir), b = components(base);
  if (b.size() > d.size()) return false;
  for (size_t i = 0; i < b.size(); ++i)
    if (style == PathStyle::Windows ? lower(d[i]) != lower(b[i]) : d[i] != b[i]) return false;
  return true;
}
}  // namespace

std::vector<std::string> splitFolderList(const std::string& commaSeparated) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : commaSeparated + ",") {
    if (c == ',') {
      if (!trimBlanks(cur).empty()) out.push_back(trimBlanks(cur));
      cur.clear();
    } else {
      cur += c;
    }
  }
  return out;
}

FolderRule parseFolderRule(const std::string& entryIn, const fs::path& root, PathStyle style) {
  (void)style;
  FolderRule r;
  std::string e = trimBlanks(entryIn);
  if (e.empty()) return r;
  std::replace(e.begin(), e.end(), '\\', '/');
  const bool drive = e.size() >= 2 && std::isalpha(static_cast<unsigned char>(e[0])) && e[1] == ':';
  const bool rooted = e[0] == '/';
  std::vector<std::string> segs;
  for (size_t pos = 0; pos <= e.size();) {
    size_t j = e.find('/', pos);
    if (j == std::string::npos) j = e.size();
    if (j > pos) {
      std::string seg = e.substr(pos, j - pos);
      if (seg != ".") segs.push_back(std::move(seg));
    }
    pos = j + 1;
  }
  if (segs.empty()) return r;
  size_t firstWild = segs.size();
  for (size_t i = 0; i < segs.size(); ++i)
    if (segs[i].find_first_of("*?") != std::string::npos) {
      firstWild = i;
      break;
    }
  std::string base = rooted ? (e.rfind("//", 0) == 0 ? "//" : "/") : std::string();
  for (size_t i = 0; i < firstWild; ++i) {
    if (i) base += '/';
    base += segs[i];
  }
  if (drive && firstWild == 1) base += '/';  // "C:" alone would be the drive's current directory
  r.rest.assign(segs.begin() + static_cast<std::ptrdiff_t>(firstWild), segs.end());
  if (r.rest.empty()) r.rest.push_back("**");  // a folder written without a wildcard: everything below it
  if (r.rest.back() == "**") r.rest.push_back("*");
  for (std::string& seg : r.rest) {
    if (seg == "*.*") seg = "*";  // Windows: every name, with or without a dot
    if (seg == "**") r.recursive = true;
  }
  fs::path bp = pathFromUtf8(base);
  if (!rooted && !drive) bp = root / bp;
  bp = bp.lexically_normal();
  if (!bp.has_filename()) bp = bp.parent_path();
  r.base = std::move(bp);
  r.valid = !r.base.empty();
  return r;
}

bool FolderRule::matches(const fs::path& file, const std::function<bool(const std::string&)>& ignoredDirName,
                         PathStyle style) const {
  if (!valid) return false;
  const auto f = components(file), b = components(base);
  if (f.size() <= b.size()) return false;
  const bool icase = style == PathStyle::Windows;
  for (size_t i = 0; i < b.size(); ++i)
    if (icase ? lower(f[i]) != lower(b[i]) : f[i] != b[i]) return false;
  std::vector<std::string> names(f.begin() + static_cast<std::ptrdiff_t>(b.size()), f.end());
  if (ignoredDirName)
    for (size_t i = 0; i + 1 < names.size(); ++i)
      if (ignoredDirName(names[i])) return false;
  return matchSegments(names, 0, rest, 0, icase);
}

bool isSolutionFile(const fs::path& p) { return compdb::isSolutionFile(p); }

WorkspaceTarget resolveWorkspaceTarget(const fs::path& arg) {
  WorkspaceTarget t;
  std::error_code ec;
  fs::path abs = fs::absolute(arg, ec);
  if (ec) abs = arg;
  abs = abs.lexically_normal();
  if (abs.has_filename() && fs::is_regular_file(abs, ec)) {
    t.file = abs;
    t.root = abs.parent_path();
    if (isSolutionFile(abs)) t.solution = abs;
  } else {
    t.root = abs.has_filename() ? abs : abs.parent_path();  // "dir/" normalises to "dir/": drop the trailing slash
  }
  return t;
}

struct Workspace::Shared {
  mutable std::shared_mutex mu;
  bool solution = false;
  std::unordered_set<std::string>
      keys;  // pathKey of every project item (listed or not yet created) and additional file
  std::vector<FolderRule> rules;
  Settings settings;  // the ignore rules the rules above were scanned with
  std::vector<std::string> rootKeys;
};

Workspace::~Workspace() = default;
Workspace::Workspace(Workspace&&) noexcept = default;
Workspace& Workspace::operator=(Workspace&&) noexcept = default;

Workspace::Workspace(fs::path root) : shared_(std::make_unique<Shared>()) {
  std::error_code ec;
  root_ = fs::weakly_canonical(root, ec);
  if (ec) root_ = fs::absolute(root);
  // Allow pointing at a .sln or CMakeLists.txt directly.
  if (fs::is_regular_file(root_, ec)) root_ = root_.parent_path();
}

bool Workspace::loadSettings(std::string* error) {
  std::ifstream in(cacheDir() / "workspace.json");
  if (!in) return false;
  Json j = Json::parse(in, nullptr, false);
  if (!j.is_object()) {
    if (error) *error = "workspace.json is not an object";
    return false;
  }
  // workspace.json is hand-edited: a value of the wrong type is ignored (value() and get<>() throw on it, and
  // loadSettings runs in the Editor constructor).
  auto getv = [&](const char* k, std::vector<std::string>& out) {
    if (!j.contains(k) || !j[k].is_array()) return;
    out.clear();
    for (const Json& e : j[k])
      if (e.is_string()) out.push_back(e.get<std::string>());
  };
  auto str = [&](const char* k, const std::string& def) {
    return j.contains(k) && j[k].is_string() ? j[k].get<std::string>() : def;
  };
  auto flag = [&](const char* k, bool def) { return j.contains(k) && j[k].is_boolean() ? j[k].get<bool>() : def; };
  settings_.solution = str("solution", settings_.solution);
  for (const char* key : {"additionalWorkspaceFolders", "AdditionalWorkspaceFolders"}) {
    if (!j.contains(key)) continue;
    std::vector<std::string> entries;
    if (j[key].is_string()) {
      entries = splitFolderList(j[key].get<std::string>());
    } else if (j[key].is_array()) {
      for (const Json& e : j[key])
        if (e.is_string())
          for (std::string& one : splitFolderList(e.get<std::string>())) entries.push_back(std::move(one));
    } else {
      continue;
    }
    settings_.additionalWorkspaceFolders = std::move(entries);
    break;
  }
  getv("sourceExtensions", settings_.sourceExtensions);
  getv("headerExtensions", settings_.headerExtensions);
  getv("otherExtensions", settings_.otherExtensions);
  getv("ignoreDirNames", settings_.ignoreDirNames);
  getv("ignoreDirPrefixes", settings_.ignoreDirPrefixes);
  {  // workspace.json files saved before the exact-name rule carry the old prefix defaults: move them to the new ones
    const std::vector<std::string> legacy = {"build", "out", "cmake-build", "bin", "obj"};
    if (settings_.ignoreDirPrefixes == legacy) settings_.ignoreDirPrefixes = Settings().ignoreDirPrefixes;
  }
  getv("clangdExtraArgs", settings_.clangdExtraArgs);
  if (j.contains("ignoreDirs") && j["ignoreDirs"].is_array()) {
    settings_.ignoreDirs.clear();
    for (auto& d : j["ignoreDirs"])
      if (d.is_string()) settings_.ignoreDirs.insert(d.get<std::string>());
  }
  settings_.cppStandard = str("cppStandard", settings_.cppStandard);
  settings_.clangdPath = str("clangdPath", settings_.clangdPath);
  settings_.vsConfiguration = str("vsConfiguration", settings_.vsConfiguration);
  settings_.vsPlatform = str("vsPlatform", settings_.vsPlatform);
  settings_.runCMake = flag("runCMake", settings_.runCMake);
  settings_.buildCommand = str("buildCommand", settings_.buildCommand);
  settings_.rebuildCommand = str("rebuildCommand", settings_.rebuildCommand);
  settings_.buildDir = str("buildDir", settings_.buildDir);
  settings_.msvcToolchain = str("msvcToolchain", settings_.msvcToolchain);
  settings_.followSymlinks = flag("followSymlinks", settings_.followSymlinks);
  settings_.watchFiles = flag("watchFiles", settings_.watchFiles);
  settings_.debugAttachAtStartup = flag("debug.attachAtStartup", settings_.debugAttachAtStartup);
  settings_.visualStudioSync = flag("debug.visualStudioSync", settings_.visualStudioSync);
  if (j.contains("undoGrouping") && j["undoGrouping"].is_string()) {
    std::string g = j["undoGrouping"].get<std::string>();
    if (g == "word" || g == "char") settings_.undoGrouping = g;
  }
  if (j.contains("undoGroupPauseMs") && j["undoGroupPauseMs"].is_number_integer())
    settings_.undoGroupPauseMs = static_cast<int>(
        std::clamp<long long>(j["undoGroupPauseMs"].get<long long>(), 0, std::numeric_limits<int>::max()));
  return true;
}

bool Workspace::saveSettings(std::string* error) const {
  // Merge into what is on disk: keys this class does not know (vcs.provider, vcs.p4Path, a nested "vcs" or "debug"
  // object, anything a newer version or the user added) survive. A file that cannot be parsed is left untouched.
  const fs::path file = cacheDir() / "workspace.json";
  Json j = Json::object();
  {
    std::error_code ec;
    if (fs::is_regular_file(file, ec)) {
      std::ifstream in(file, std::ios::binary);
      std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
      const bool blank = text.find_first_not_of(" \t\r\n") == std::string::npos;
      if (!blank) {
        Json old = Json::parse(text, nullptr, false);
        if (!old.is_object()) {
          if (error) *error = "workspace.json is not a valid JSON object; not overwriting it";
          return false;
        }
        j = std::move(old);
      }
    }
  }
  j["sourceExtensions"] = settings_.sourceExtensions;
  j["headerExtensions"] = settings_.headerExtensions;
  j["otherExtensions"] = settings_.otherExtensions;
  j["ignoreDirs"] = std::vector<std::string>(settings_.ignoreDirs.begin(), settings_.ignoreDirs.end());
  j["ignoreDirNames"] = settings_.ignoreDirNames;
  j["ignoreDirPrefixes"] = settings_.ignoreDirPrefixes;
  j["cppStandard"] = settings_.cppStandard;
  j["clangdPath"] = settings_.clangdPath;
  j["clangdExtraArgs"] = settings_.clangdExtraArgs;
  j["vsConfiguration"] = settings_.vsConfiguration;
  j["vsPlatform"] = settings_.vsPlatform;
  j["runCMake"] = settings_.runCMake;
  j["buildCommand"] = settings_.buildCommand;
  j["rebuildCommand"] = settings_.rebuildCommand;
  j["buildDir"] = settings_.buildDir;
  j["msvcToolchain"] = settings_.msvcToolchain;
  j["followSymlinks"] = settings_.followSymlinks;
  j["watchFiles"] = settings_.watchFiles;
  j["debug.attachAtStartup"] = settings_.debugAttachAtStartup;
  j["debug.visualStudioSync"] = settings_.visualStudioSync;
  j["undoGrouping"] = settings_.undoGrouping;
  j["undoGroupPauseMs"] = settings_.undoGroupPauseMs;
  if (settings_.solution.empty()) j.erase("solution");
  else j["solution"] = settings_.solution;
  j.erase("AdditionalWorkspaceFolders");  // 10x's spelling is read, ours is written
  if (settings_.additionalWorkspaceFolders.empty()) j.erase("additionalWorkspaceFolders");
  else j["additionalWorkspaceFolders"] = settings_.additionalWorkspaceFolders;
  std::error_code ec;
  fs::create_directories(cacheDir(), ec);
  // Write a temporary file next to it and rename over the target, so a crash never leaves a half-written file.
  const fs::path tmp = cacheDir() / "workspace.json.tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) {
      if (error) *error = "cannot write workspace.json";
      return false;
    }
    out << dumpSafe(j, 2);
    out.flush();
    if (!out) {
      fs::remove(tmp, ec);
      if (error) *error = "cannot write workspace.json";
      return false;
    }
  }
  fs::rename(tmp, file, ec);
  if (ec) {
    fs::remove(tmp, ec);
    if (error) *error = "cannot replace workspace.json";
    return false;
  }
  return true;
}

bool Workspace::isSource(const fs::path& p) const { return hasExt(p, settings_.sourceExtensions); }
bool Workspace::isHeader(const fs::path& p) const {
  if (p.extension().empty()) {
    // Extensionless files are headers only in include-ish directories (e.g. <vector>-style).
    std::string parent = lower(pathUtf8(p.parent_path().filename()));
    return parent == "include" || parent.find("inc") != std::string::npos;
  }
  return hasExt(p, settings_.headerExtensions);
}

bool Workspace::isIgnoredDir(const fs::path& dir) const { return ignoredDirName(settings_, pathUtf8(dir.filename())); }

// One directory_iterator per directory instead of a single recursive_directory_iterator: when the recursive one
// fails to descend (a path past MAX_PATH on Windows, a directory deleted mid-scan, ...) the MSVC STL empties its
// stack, so the iterator becomes `end` and the rest of the tree is silently lost. Here a directory that cannot be
// read costs only itself (and is logged).
void Workspace::walkTree(const fs::path& start, const std::function<void(const fs::path&)>& onFile,
                         size_t maxDepth) const {
  const size_t kMaxDepth = std::min<size_t>(maxDepth, 256);  // also ends symlink cycles when followSymlinks is on
  std::vector<std::pair<fs::path, size_t>> stack;
  stack.emplace_back(start, 0);
  while (!stack.empty()) {
    auto [dir, depth] = std::move(stack.back());
    stack.pop_back();
    std::error_code ec;
    fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
    if (ec) {
      Log::warn("workspace", "scan: cannot read " + pathUtf8(dir) + ": " + ec.message());
      continue;
    }
    for (; it != end; it.increment(ec)) {
      if (ec) {
        ec.clear();
        continue;
      }
      const fs::directory_entry& e = *it;
      std::error_code ec2;
      if (e.is_directory(ec2)) {
        // A symlink or (Windows) junction to a directory is entered only when followSymlinks says so.
        const bool plain = e.symlink_status(ec2).type() == fs::file_type::directory;
        if (!isIgnoredDir(e.path()) && (plain || settings_.followSymlinks) && depth < kMaxDepth)
          stack.emplace_back(e.path(), depth + 1);
      } else if (e.is_regular_file(ec2)) {
        onFile(e.path());
      }
    }
  }
}

std::vector<fs::path> Workspace::noteTreeAdded(const fs::path& dir) {
  std::vector<fs::path> added;
  walkTree(dir, [&](const fs::path& p) {
    if (noteFileAdded(p)) added.push_back(p);
  });
  return added;
}

Workspace::Kind Workspace::detectKind() const {
  if (solutionFiles().empty()) return Kind::Folder;
  // The same precedence as the compile flags (SemanticEngine::prepareCompileCommands): a compile_commands.json of the
  // user's (not the one tenfold generates in .tenfold) or a CMake project wins over a solution.
  if (hasCMake() && settings_.runCMake) return Kind::Folder;
  if (auto db = CompileDatabase::find(root_)) {
    std::error_code ec;
    if (db->parent_path() != fs::weakly_canonical(cacheDir(), ec)) return Kind::Folder;
  }
  return Kind::Solution;
}

void Workspace::listFile(const fs::path& p, bool anyExtension, bool* added) {
  std::vector<fs::path>* list = nullptr;
  if (isSource(p)) list = &sources_;
  else if (isHeader(p)) list = &headers_;
  else if (anyExtension || hasExt(p, settings_.otherExtensions)) list = &others_;
  if (!list) {
    if (added) *added = false;
    return;
  }
  list->push_back(p);
  if (added) *added = true;
}

void Workspace::publishShared() {
  if (!shared_) shared_ = std::make_unique<Shared>();
  std::unique_lock<std::shared_mutex> lk(shared_->mu);
  shared_->solution = kind_ == Kind::Solution;
  shared_->settings = settings_;
  shared_->rootKeys = memberRootKeys_;
  shared_->rules.clear();
  shared_->keys.clear();
  if (kind_ != Kind::Solution) return;
  for (const std::string& e : settings_.additionalWorkspaceFolders) {
    FolderRule r = parseFolderRule(e, root_);
    if (r.valid) shared_->rules.push_back(std::move(r));
  }
  for (auto* list : {&sources_, &headers_, &others_})
    for (const fs::path& f : *list) shared_->keys.insert(pathKey(f));
  for (const std::string& k : expectedKeys_) shared_->keys.insert(k);
}

void Workspace::scanSolution(const std::function<void(size_t)>& progress) {
  size_t n = 0;
  auto tick = [&] {
    if (progress && (++n % 1000) == 0) progress(n);
  };
  const PathStyle style = nativePathStyle();
  // 1. The folder scan of the root: the files the explorer's folder view lists. Members are taken out below.
  std::vector<fs::path> rootFiles;
  walkTree(root_, [&](const fs::path& p) {
    if (isSource(p) || isHeader(p) || hasExt(p, settings_.otherExtensions)) rootFiles.push_back(p);
  });
  // 2. The files of the projects of every solution.
  std::set<std::string> member;
  std::vector<fs::path> inputs, projectDirs;
  std::vector<fs::path> declared;  // project items, in solution order
  compdb::VcxprojOptions vo;
  for (const fs::path& sln : solutionFiles()) {
    compdb::SolutionFiles sf = compdb::solutionFiles(sln, vo);
    for (fs::path& f : sf.files) declared.push_back(std::move(f));
    for (fs::path& f : sf.inputs) inputs.push_back(std::move(f));
    for (fs::path& d : sf.projectDirs) projectDirs.push_back(std::move(d));
  }
  for (const fs::path& f : declared) {
    if (!member.insert(pathKey(f)).second) continue;
    std::error_code ec;
    if (fs::is_regular_file(f, ec)) {
      listFile(f, true);
      tick();
    } else {
      expectedKeys_.push_back(pathKey(f));
    }
  }
  // 3. Additional workspace folders (10x AdditionalWorkspaceFolders): the folder rules, below the pattern's folder.
  std::vector<FolderRule> rules;
  for (const std::string& e : settings_.additionalWorkspaceFolders) {
    FolderRule r = parseFolderRule(e, root_);
    if (r.valid) rules.push_back(std::move(r));
  }
  const auto ignored = [this](const std::string& name) { return ignoredDirName(settings_, name); };
  for (const FolderRule& r : rules) {
    std::error_code ec;
    if (!fs::is_directory(r.base, ec)) continue;
    walkTree(
        r.base,
        [&](const fs::path& p) {
          if (!(isSource(p) || isHeader(p) || hasExt(p, settings_.otherExtensions))) return;
          if (!r.matches(p, ignored, style)) return;
          if (!member.insert(pathKey(p)).second) return;
          listFile(p, false);
          tick();
        },
        r.recursive ? 256 : (r.rest.size() > 0 ? r.rest.size() - 1 : 0));
  }
  // 4. What is left of the folder scan is loose.
  for (fs::path& p : rootFiles)
    if (!member.count(pathKey(p))) loose_.push_back(std::move(p));
  std::sort(loose_.begin(), loose_.end());
  std::sort(expectedKeys_.begin(), expectedKeys_.end());
  // 5. Files whose change re-reads the list, and where the watcher has to look.
  for (const fs::path& f : inputs) inputKeys_.insert(pathKey(f));
  std::vector<WatchRoot> cands;
  for (const fs::path& d : projectDirs)
    if (!isUnder(d, root_, style)) cands.push_back({d, true});
  for (const FolderRule& r : rules)
    if (!isUnder(r.base, root_, style)) cands.push_back({r.base, r.recursive || r.rest.size() > 1});
  const std::string rootPrefix = pathKey(root_) + "/";
  auto underRoot = [&](const fs::path& f) { return pathKey(f).compare(0, rootPrefix.size(), rootPrefix) == 0; };
  std::set<std::string> outsideDirs;  // one candidate per directory
  auto outsideDir = [&](const fs::path& f) {
    if (underRoot(f)) return;
    const fs::path d = f.parent_path();
    if (outsideDirs.insert(pathKey(d)).second) cands.push_back({d, false});
  };
  for (auto* list : {&sources_, &headers_, &others_})
    for (const fs::path& f : *list) outsideDir(f);
  for (const fs::path& f : inputs) outsideDir(f);
  std::stable_sort(cands.begin(), cands.end(), [](const WatchRoot& a, const WatchRoot& b) {
    return std::distance(a.dir.begin(), a.dir.end()) < std::distance(b.dir.begin(), b.dir.end());
  });
  for (const WatchRoot& c : cands) {
    bool covered = false;
    for (WatchRoot& k : watchRoots_) {
      if (pathKey(k.dir) == pathKey(c.dir)) {
        k.recursive = k.recursive || c.recursive;
        covered = true;
        break;
      }
      if (k.recursive && isUnder(c.dir, k.dir, style)) {
        covered = true;
        break;
      }
    }
    if (!covered) watchRoots_.push_back(c);
  }
  constexpr size_t kMaxWatchRoots = 256;  // a layout with more scattered directories is watched at its first 256
  if (watchRoots_.size() > kMaxWatchRoots) {
    Log::warn("workspace", "solution files in " + std::to_string(watchRoots_.size()) +
                               " directories outside the root; " + "watching the first " +
                               std::to_string(kMaxWatchRoots));
    watchRoots_.resize(kMaxWatchRoots);
  }
  memberRootKeys_.push_back(pathKeyUtf8(root_));
  for (const WatchRoot& w : watchRoots_) memberRootKeys_.push_back(pathKeyUtf8(w.dir));
}

void Workspace::scan(std::function<void(size_t)> progress) {
  sources_.clear();
  headers_.clear();
  others_.clear();
  loose_.clear();
  expectedKeys_.clear();
  inputKeys_.clear();
  watchRoots_.clear();
  memberRootKeys_.clear();
  ++revision_;
  kind_ = detectKind();
  if (kind_ == Kind::Solution) {
    scanSolution(progress);
  } else {
    size_t n = 0;
    walkTree(root_, [&](const fs::path& p) {
      if (isSource(p)) sources_.push_back(p);
      else if (isHeader(p)) headers_.push_back(p);
      else if (hasExt(p, settings_.otherExtensions)) others_.push_back(p);
      else return;
      if (progress && (++n % 1000) == 0) progress(n);
    });
  }
  std::sort(sources_.begin(), sources_.end());
  std::sort(headers_.begin(), headers_.end());
  std::sort(others_.begin(), others_.end());
  publishShared();
}

Workspace Workspace::scanCopy() const {
  Workspace w(root_);
  w.settings_ = settings_;
  return w;
}

Workspace::Changes Workspace::adoptScan(Workspace&& scanned) {
  Changes ch;
  std::vector<fs::path> before = allFiles(), after = scanned.allFiles();
  std::sort(before.begin(), before.end());
  std::sort(after.begin(), after.end());
  std::set_difference(before.begin(), before.end(), after.begin(), after.end(), std::back_inserter(ch.removed));
  std::set_difference(after.begin(), after.end(), before.begin(), before.end(), std::back_inserter(ch.added));
  kind_ = scanned.kind_;
  sources_ = std::move(scanned.sources_);
  headers_ = std::move(scanned.headers_);
  others_ = std::move(scanned.others_);
  loose_ = std::move(scanned.loose_);
  expectedKeys_ = std::move(scanned.expectedKeys_);
  inputKeys_ = std::move(scanned.inputKeys_);
  watchRoots_ = std::move(scanned.watchRoots_);
  memberRootKeys_ = std::move(scanned.memberRootKeys_);
  publishShared();
  ++revision_;
  return ch;
}

namespace {
bool eraseSorted(std::vector<fs::path>& list, const fs::path& p) {
  auto it = std::lower_bound(list.begin(), list.end(), p);
  if (it == list.end() || !(*it == p)) return false;
  list.erase(it);
  return true;
}
bool insertSorted(std::vector<fs::path>& list, const fs::path& p) {
  auto it = std::lower_bound(list.begin(), list.end(), p);
  if (it != list.end() && *it == p) return false;
  list.insert(it, p);
  return true;
}
}  // namespace

bool Workspace::isSolutionWorkspace() const {
  if (!shared_) return false;
  std::shared_lock<std::shared_mutex> lk(shared_->mu);
  return shared_->solution;
}

bool Workspace::isMember(const fs::path& file) const {
  if (!shared_) return true;
  std::shared_lock<std::shared_mutex> lk(shared_->mu);
  if (!shared_->solution) return true;
  if (shared_->keys.count(pathKey(file))) return true;
  const auto ignored = [this](const std::string& name) { return ignoredDirName(shared_->settings, name); };
  for (const FolderRule& r : shared_->rules)
    if (r.matches(file, ignored)) return true;
  return false;
}

std::vector<std::string> Workspace::memberRootKeys() const {
  if (!shared_) return {};
  std::shared_lock<std::shared_mutex> lk(shared_->mu);
  return shared_->rootKeys;
}

bool Workspace::isSolutionInput(const fs::path& file) const {
  return kind_ == Kind::Solution && inputKeys_.count(pathKey(file)) != 0;
}

bool Workspace::noteFileAdded(const fs::path& p) {
  if (kind_ == Kind::Solution) {
    // A project item (whatever its extension), a file an additional folder covers (the extension lists apply), else
    // a loose file below the root (the explorer's view; never a workspace file).
    const bool classified = isSource(p) || isHeader(p) || hasExt(p, settings_.otherExtensions);
    bool projectItem = false;
    {
      std::shared_lock<std::shared_mutex> lk(shared_->mu);
      projectItem = shared_->keys.count(pathKey(p)) != 0;
    }
    if (projectItem || (classified && isMember(p))) {
      std::vector<fs::path>* list = isSource(p) ? &sources_ : isHeader(p) ? &headers_ : &others_;
      if (!insertSorted(*list, p)) return false;
      {
        std::unique_lock<std::shared_mutex> lk(shared_->mu);
        shared_->keys.insert(pathKey(p));
      }
      eraseSorted(loose_, p);
      ++revision_;
      return true;
    }
    if (classified && isUnder(p, root_, nativePathStyle())) {
      if (!insertSorted(loose_, p)) return false;
      ++revision_;
      return true;
    }
    return false;
  }
  std::vector<fs::path>* list = nullptr;
  if (isSource(p)) list = &sources_;
  else if (isHeader(p)) list = &headers_;
  else if (hasExt(p, settings_.otherExtensions)) list = &others_;
  if (!list) return false;
  auto it = std::lower_bound(list->begin(), list->end(), p);
  if (it != list->end() && *it == p) return false;
  list->insert(it, p);
  ++revision_;
  return true;
}

bool Workspace::noteFileRemoved(const fs::path& p) {
  bool changed = false;
  for (auto* list : {&sources_, &headers_, &others_}) changed = eraseSorted(*list, p) || changed;
  // A project item that disappears keeps its key: it is a workspace file again when it is re-created.
  if (kind_ == Kind::Solution) changed = eraseSorted(loose_, p) || changed;
  if (changed) ++revision_;
  return changed;
}

bool Workspace::noteFileRenamed(const fs::path& from, const fs::path& to) {
  bool removed = noteFileRemoved(from);
  bool added = noteFileAdded(to);
  return removed || added;
}

std::vector<fs::path> Workspace::filesUnder(const fs::path& dir) const {
  std::vector<fs::path> out;
  for (auto* list : {&sources_, &headers_, &others_, &loose_})
    for (auto& f : *list) {
      auto [a, b] = std::mismatch(dir.begin(), dir.end(), f.begin(), f.end());
      if (a == dir.end()) out.push_back(f);
    }
  return out;
}

std::vector<fs::path> Workspace::codeFiles() const {
  std::vector<fs::path> out = sources_;
  out.insert(out.end(), headers_.begin(), headers_.end());
  return out;
}

std::vector<fs::path> Workspace::allFiles() const {
  std::vector<fs::path> out = sources_;
  out.insert(out.end(), headers_.begin(), headers_.end());
  out.insert(out.end(), others_.begin(), others_.end());
  return out;
}

std::vector<fs::path> Workspace::explorerFiles() const {
  std::vector<fs::path> out = allFiles();
  out.insert(out.end(), loose_.begin(), loose_.end());
  return out;
}

fs::path Workspace::chosenSolution() const {
  if (settings_.solution.empty()) return {};
  std::error_code ec;
  fs::path p = pathFromUtf8(settings_.solution);
  if (p.is_relative()) p = root_ / p;
  p = p.lexically_normal();
  return fs::is_regular_file(p, ec) && isSolutionFile(p) ? p : fs::path();
}

bool Workspace::setSolution(const fs::path& slnIn, std::string* error) {
  std::string value;
  if (!slnIn.empty()) {
    std::error_code ec;
    fs::path sln = slnIn.is_absolute() ? slnIn : root_ / slnIn;
    if (fs::path canon = fs::weakly_canonical(sln, ec); !ec) sln = canon;  // the root is canonical too
    sln = sln.lexically_normal();
    if (!isSolutionFile(sln) || !fs::is_regular_file(sln, ec)) {
      if (error) *error = pathUtf8(slnIn) + " is not an existing .sln / .slnx file";
      return false;
    }
    fs::path rel = sln.lexically_relative(root_);
    const bool inside = !rel.empty() && *rel.begin() != "..";
    value = inside ? pathKeyUtf8(rel) : pathUtf8(sln);
    // Windows paths compare case-insensitively: the same file spelled differently is not a change.
    if (!settings_.solution.empty() && chosenSolution() == sln) return true;
  } else if (settings_.solution.empty()) {
    return true;
  }
  settings_.solution = value;
  return saveSettings(error);
}

std::vector<fs::path> Workspace::solutionFiles() const {
  if (fs::path chosen = chosenSolution(); !chosen.empty()) return {chosen};
  std::vector<fs::path> out;
  std::error_code ec;
  fs::directory_iterator it(root_, fs::directory_options::skip_permission_denied, ec), end;
  for (; !ec && it != end; it.increment(ec)) {  // the range-for form throws when the directory vanishes mid-walk
    std::error_code e2;
    if (it->is_regular_file(e2) && compdb::isSolutionFile(it->path())) out.push_back(it->path());
  }
  std::sort(out.begin(), out.end());
  return out;
}

std::vector<compdb::SolutionConfiguration> Workspace::vsConfigurations() const {
  std::vector<compdb::SolutionConfiguration> out;
  for (auto& sln : solutionFiles()) {
    for (auto& c : compdb::solutionConfigurations(sln))
      if (std::find(out.begin(), out.end(), c) == out.end()) out.push_back(c);
  }
  return out;
}

compdb::SolutionConfiguration Workspace::activeVsConfiguration() const {
  return compdb::pickSolutionConfiguration(vsConfigurations(), settings_.vsConfiguration, settings_.vsPlatform);
}

bool Workspace::setVsConfiguration(const std::string& name, std::string* error) {
  compdb::SolutionConfiguration want = compdb::parseSolutionConfiguration(name);
  auto available = vsConfigurations();
  if (!available.empty()) {
    const compdb::SolutionConfiguration* found = nullptr;
    for (auto& c : available) {
      if (want.platform.empty() ? lower(c.configuration) == lower(want.configuration) : c == want) {
        found = &c;
        break;
      }
    }
    if (!found) {
      if (error) {
        *error = "unknown configuration '" + name + "'; available:";
        for (auto& c : available) *error += " " + c.name();
      }
      return false;
    }
    want = *found;
  } else if (want.platform.empty()) {
    want.platform = settings_.vsPlatform;
  }
  if (want.configuration.empty()) {
    if (error) *error = "empty configuration name";
    return false;
  }
  settings_.vsConfiguration = want.configuration;
  settings_.vsPlatform = want.platform;
  return saveSettings(error);
}

bool Workspace::hasCMake() const {
  std::error_code ec;
  return fs::is_regular_file(root_ / "CMakeLists.txt", ec);
}

std::vector<std::string> splitPathList(const std::string& pathList, char sep) {
  std::vector<std::string> out;
  size_t start = 0;
  while (start <= pathList.size()) {
    size_t e = pathList.find(sep, start);
    if (e == std::string::npos) e = pathList.size();
    if (e > start) out.push_back(pathList.substr(start, e - start));
    start = e + 1;
  }
  return out;
}

std::vector<std::string> executableCandidates(const std::string& name, bool windows,
                                              const std::vector<std::string>& exts) {
  if (!windows) return {name};
  std::string ln = lower(name);
  for (auto& e : exts)
    if (ln.size() >= e.size() && ln.compare(ln.size() - e.size(), e.size(), lower(e)) == 0) return {name};
  std::vector<std::string> out;
  for (auto& e : exts) out.push_back(name + e);
  return out;
}

fs::path canonicalExecutablePath(const fs::path& raw) {
  if (raw.empty()) return {};
  std::error_code ec;
  fs::path p = fs::weakly_canonical(raw, ec);
  return ec ? raw : p;
}

static fs::path selfExePath() {
  fs::path exe;
#ifdef _WIN32
  wchar_t buf[MAX_PATH * 4];
  DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(sizeof(buf) / sizeof(buf[0])));
  if (n > 0 && n < sizeof(buf) / sizeof(buf[0])) exe = fs::path(std::wstring(buf, n));
#elif defined(__APPLE__)
  // No /proc on macOS: ask dyld. The path may be relative or go through symlinks / "..", so canonicalise it.
  uint32_t size = 0;
  _NSGetExecutablePath(nullptr, &size);  // fails, reporting the required size
  std::string buf(size, '\0');
  if (size > 0 && _NSGetExecutablePath(buf.data(), &size) == 0) exe = canonicalExecutablePath(fs::path(buf.c_str()));
#else
  std::error_code ec;
  exe = fs::read_symlink("/proc/self/exe", ec);
  if (ec) exe.clear();
#endif
  return exe;
}

fs::path executableDir() {
  fs::path exe = selfExePath();
  return exe.empty() ? fs::path() : exe.parent_path();
}

fs::path bundledBinDir() {
  fs::path dir = executableDir();
  if (dir.empty()) return {};
  return dir / "bin";
}

std::vector<std::string> bundledSearchDirs(const fs::path& exeDir) {
  if (exeDir.empty()) return {};
  return {pathUtf8(exeDir), pathUtf8(exeDir / "bin")};
}

std::string findExecutableIn(const std::string& name, const std::vector<std::string>& dirs, bool windows) {
  for (auto& d : dirs) {
    for (auto& c : executableCandidates(name, windows)) {
      fs::path cand = fs::path(d) / c;
      std::error_code ec;
      if (fs::is_regular_file(cand, ec)) return pathUtf8(cand);
    }
  }
  return "";
}

std::string findExecutable(const std::string& name) { return findExecutable(name, false); }

std::string findExecutable(const std::string& name, bool preferBundled) {
#ifdef _WIN32
  const bool windows = true;
  const char sep = ';';
#else
  const bool windows = false;
  const char sep = ':';
#endif
  if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos) {
    std::error_code ec;
    for (auto& c : executableCandidates(name, windows))
      if (fs::is_regular_file(c, ec)) return c;
    return "";
  }
  std::vector<std::string> dirs;
  auto bundled = bundledSearchDirs(executableDir());
  if (preferBundled) dirs.insert(dirs.end(), bundled.begin(), bundled.end());
  if (const char* path = std::getenv("PATH")) {
    for (auto& d : splitPathList(path, sep)) dirs.push_back(d);
  }
  if (!preferBundled) dirs.insert(dirs.end(), bundled.begin(), bundled.end());
  return findExecutableIn(name, dirs, windows);
}

ClangdChoice resolveClangd(const std::string& setting, const std::vector<std::string>& bundledDirs,
                           const std::string& pathList, char sep, bool windows) {
  if (!setting.empty()) {
    if (setting.find('/') != std::string::npos || setting.find('\\') != std::string::npos) {
      std::error_code ec;
      for (auto& c : executableCandidates(setting, windows))
        if (fs::is_regular_file(c, ec)) return {c, "setting"};
      return {};
    }
    std::string r = findExecutableIn(setting, splitPathList(pathList, sep), windows);
    return r.empty() ? ClangdChoice{} : ClangdChoice{r, "setting"};
  }
  std::string r = findExecutableIn("clangd", bundledDirs, windows);
  if (!r.empty()) return {r, "bundled"};
  r = findExecutableIn("clangd", splitPathList(pathList, sep), windows);
  if (!r.empty()) return {r, "PATH"};
  return {};
}

ClangdChoice findClangd(const std::string& setting) {
#ifdef _WIN32
  const bool windows = true;
  const char sep = ';';
#else
  const bool windows = false;
  const char sep = ':';
#endif
  const char* path = std::getenv("PATH");
  return resolveClangd(setting, bundledSearchDirs(executableDir()), path ? path : "", sep, windows);
}

}  // namespace tf
