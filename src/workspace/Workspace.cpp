#include "workspace/Workspace.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>

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
  std::string e = lower(p.extension().string());
  return std::find(exts.begin(), exts.end(), e) != exts.end();
}
}  // namespace

Workspace::Workspace(fs::path root) {
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
  auto getv = [&](const char* k, std::vector<std::string>& out) {
    if (j.contains(k) && j[k].is_array()) out = j[k].get<std::vector<std::string>>();
  };
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
    for (auto& d : j["ignoreDirs"]) settings_.ignoreDirs.insert(d.get<std::string>());
  }
  settings_.cppStandard = j.value("cppStandard", settings_.cppStandard);
  settings_.clangdPath = j.value("clangdPath", settings_.clangdPath);
  settings_.vsConfiguration = j.value("vsConfiguration", settings_.vsConfiguration);
  settings_.vsPlatform = j.value("vsPlatform", settings_.vsPlatform);
  settings_.runCMake = j.value("runCMake", settings_.runCMake);
  settings_.buildCommand = j.value("buildCommand", settings_.buildCommand);
  settings_.buildDir = j.value("buildDir", settings_.buildDir);
  settings_.msvcToolchain = j.value("msvcToolchain", settings_.msvcToolchain);
  settings_.followSymlinks = j.value("followSymlinks", settings_.followSymlinks);
  settings_.watchFiles = j.value("watchFiles", settings_.watchFiles);
  settings_.debugAttachAtStartup = j.value("debug.attachAtStartup", settings_.debugAttachAtStartup);
  if (j.contains("undoGrouping") && j["undoGrouping"].is_string()) {
    std::string g = j["undoGrouping"].get<std::string>();
    if (g == "word" || g == "char") settings_.undoGrouping = g;
  }
  if (j.contains("undoGroupPauseMs") && j["undoGroupPauseMs"].is_number_integer())
    settings_.undoGroupPauseMs = std::max(0, j["undoGroupPauseMs"].get<int>());
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
  j["buildDir"] = settings_.buildDir;
  j["msvcToolchain"] = settings_.msvcToolchain;
  j["followSymlinks"] = settings_.followSymlinks;
  j["watchFiles"] = settings_.watchFiles;
  j["debug.attachAtStartup"] = settings_.debugAttachAtStartup;
  j["undoGrouping"] = settings_.undoGrouping;
  j["undoGroupPauseMs"] = settings_.undoGroupPauseMs;
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
    out << j.dump(2);
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
    std::string parent = lower(p.parent_path().filename().string());
    return parent == "include" || parent.find("inc") != std::string::npos;
  }
  return hasExt(p, settings_.headerExtensions);
}

bool Workspace::isIgnoredDir(const fs::path& dir) const {
  std::string name = dir.filename().string();
  if (settings_.ignoreDirs.count(name)) return true;
  std::string ln = lower(name);
  for (auto& n : settings_.ignoreDirNames)
    if (ln == lower(n)) return true;
  for (auto& pre : settings_.ignoreDirPrefixes)
    if (!pre.empty() && ln.rfind(lower(pre), 0) == 0) return true;
  return false;
}

void Workspace::scan(std::function<void(size_t)> progress) {
  sources_.clear();
  headers_.clear();
  others_.clear();
  std::error_code ec;
  auto opts = fs::directory_options::skip_permission_denied;
  if (settings_.followSymlinks) opts |= fs::directory_options::follow_directory_symlink;
  fs::recursive_directory_iterator it(root_, opts, ec), end;
  size_t n = 0;
  while (it != end) {
    const fs::directory_entry& e = *it;
    if (e.is_directory(ec)) {
      if (isIgnoredDir(e.path())) it.disable_recursion_pending();
    } else if (e.is_regular_file(ec)) {
      const fs::path& p = e.path();
      if (isSource(p)) sources_.push_back(p);
      else if (isHeader(p)) headers_.push_back(p);
      else if (hasExt(p, settings_.otherExtensions)) others_.push_back(p);
      if (progress && (++n % 1000) == 0) progress(n);
    }
    it.increment(ec);
    if (ec) { ec.clear(); }
  }
  std::sort(sources_.begin(), sources_.end());
  std::sort(headers_.begin(), headers_.end());
  std::sort(others_.begin(), others_.end());
}

bool Workspace::noteFileAdded(const fs::path& p) {
  std::vector<fs::path>* list = nullptr;
  if (isSource(p)) list = &sources_;
  else if (isHeader(p)) list = &headers_;
  else if (hasExt(p, settings_.otherExtensions)) list = &others_;
  if (!list) return false;
  auto it = std::lower_bound(list->begin(), list->end(), p);
  if (it != list->end() && *it == p) return false;
  list->insert(it, p);
  return true;
}

bool Workspace::noteFileRemoved(const fs::path& p) {
  bool changed = false;
  for (auto* list : {&sources_, &headers_, &others_}) {
    auto it = std::lower_bound(list->begin(), list->end(), p);
    if (it != list->end() && *it == p) {
      list->erase(it);
      changed = true;
    }
  }
  return changed;
}

bool Workspace::noteFileRenamed(const fs::path& from, const fs::path& to) {
  bool removed = noteFileRemoved(from);
  bool added = noteFileAdded(to);
  return removed || added;
}

std::vector<fs::path> Workspace::filesUnder(const fs::path& dir) const {
  std::vector<fs::path> out;
  for (auto* list : {&sources_, &headers_, &others_})
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

std::vector<fs::path> Workspace::solutionFiles() const {
  std::vector<fs::path> out;
  std::error_code ec;
  for (auto& e : fs::directory_iterator(root_, ec)) {
    if (e.is_regular_file(ec) && lower(e.path().extension().string()) == ".sln") out.push_back(e.path());
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
  return {exeDir.string(), (exeDir / "bin").string()};
}

std::string findExecutableIn(const std::string& name, const std::vector<std::string>& dirs, bool windows) {
  for (auto& d : dirs) {
    for (auto& c : executableCandidates(name, windows)) {
      fs::path cand = fs::path(d) / c;
      std::error_code ec;
      if (fs::is_regular_file(cand, ec)) return cand.string();
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
