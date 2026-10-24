#include "workspace/Workspace.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>

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
  if (!j.is_object()) { if (error) *error = "workspace.json is not an object"; return false; }
  auto getv = [&](const char* k, std::vector<std::string>& out) { if (j.contains(k) && j[k].is_array()) out = j[k].get<std::vector<std::string>>(); };
  getv("sourceExtensions", settings_.sourceExtensions);
  getv("headerExtensions", settings_.headerExtensions);
  getv("otherExtensions", settings_.otherExtensions);
  getv("ignoreDirPrefixes", settings_.ignoreDirPrefixes);
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
  settings_.followSymlinks = j.value("followSymlinks", settings_.followSymlinks);
  return true;
}

bool Workspace::saveSettings(std::string* error) const {
  Json j;
  j["sourceExtensions"] = settings_.sourceExtensions;
  j["headerExtensions"] = settings_.headerExtensions;
  j["otherExtensions"] = settings_.otherExtensions;
  j["ignoreDirs"] = std::vector<std::string>(settings_.ignoreDirs.begin(), settings_.ignoreDirs.end());
  j["ignoreDirPrefixes"] = settings_.ignoreDirPrefixes;
  j["cppStandard"] = settings_.cppStandard;
  j["clangdPath"] = settings_.clangdPath;
  j["clangdExtraArgs"] = settings_.clangdExtraArgs;
  j["vsConfiguration"] = settings_.vsConfiguration;
  j["vsPlatform"] = settings_.vsPlatform;
  j["runCMake"] = settings_.runCMake;
  j["followSymlinks"] = settings_.followSymlinks;
  std::error_code ec;
  fs::create_directories(cacheDir(), ec);
  std::ofstream out(cacheDir() / "workspace.json");
  if (!out) { if (error) *error = "cannot write workspace.json"; return false; }
  out << j.dump(2);
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
  for (auto& pre : settings_.ignoreDirPrefixes)
    if (ln.rfind(lower(pre), 0) == 0 && ln != "src") return true;
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

bool Workspace::hasCMake() const {
  std::error_code ec;
  return fs::is_regular_file(root_ / "CMakeLists.txt", ec);
}

std::string findExecutable(const std::string& name) {
  if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos) {
    std::error_code ec;
    return fs::is_regular_file(name, ec) ? name : "";
  }
  const char* path = std::getenv("PATH");
  if (!path) return "";
  std::string p(path);
  size_t start = 0;
  while (start <= p.size()) {
    size_t sep = p.find(':', start);
    if (sep == std::string::npos) sep = p.size();
    fs::path cand = fs::path(p.substr(start, sep - start)) / name;
    std::error_code ec;
    if (fs::is_regular_file(cand, ec)) return cand.string();
    start = sep + 1;
  }
  return "";
}

}  // namespace tf
