#include "debug/Breakpoints.h"

#include <fstream>

#include <nlohmann/json.hpp>

#include "core/PathUtil.h"

namespace tf {

namespace fs = std::filesystem;

std::string breakpointFileKey(const fs::path& file) {
  std::error_code ec;
  return pathKey(fs::weakly_canonical(file, ec));
}

const std::string& Breakpoints::fileKey(const fs::path& file) const {
  if (keyCache_.size() > 4096) keyCache_.clear();
  std::string spelling = file.generic_string();
  auto it = keyCache_.find(spelling);
  if (it == keyCache_.end()) it = keyCache_.emplace(std::move(spelling), breakpointFileKey(file)).first;
  return it->second;
}

void Breakpoints::changed() {
  dirty_ = true;
  ++gen_;
  if (handler_) handler_();
}

bool Breakpoints::toggle(const fs::path& file, int line) {
  if (remove(file, line)) return false;
  Breakpoint bp;
  bp.file = file;
  bp.line = line;
  add(bp);
  return true;
}

void Breakpoints::add(const Breakpoint& bp) {
  Entry& e = files_[fileKey(bp.file)];
  if (e.bps.empty()) e.file = bp.file;
  auto it = e.bps.find(bp.line);
  if (it != e.bps.end() && it->second.enabled == bp.enabled && it->second.condition == bp.condition) return;
  Breakpoint stored = bp;
  stored.file = e.file;
  e.bps[bp.line] = std::move(stored);
  changed();
}

bool Breakpoints::remove(const fs::path& file, int line) {
  auto it = files_.find(fileKey(file));
  if (it == files_.end() || it->second.bps.erase(line) == 0) return false;
  if (it->second.bps.empty()) files_.erase(it);
  changed();
  return true;
}

bool Breakpoints::setEnabled(const fs::path& file, int line, bool enabled) {
  auto it = files_.find(fileKey(file));
  if (it == files_.end()) return false;
  auto b = it->second.bps.find(line);
  if (b == it->second.bps.end()) return false;
  if (b->second.enabled != enabled) {
    b->second.enabled = enabled;
    changed();
  }
  return true;
}

bool Breakpoints::setCondition(const fs::path& file, int line, const std::string& condition) {
  auto it = files_.find(fileKey(file));
  if (it == files_.end()) return false;
  auto b = it->second.bps.find(line);
  if (b == it->second.bps.end()) return false;
  if (b->second.condition != condition) {
    b->second.condition = condition;
    changed();
  }
  return true;
}

std::optional<Breakpoint> Breakpoints::get(const fs::path& file, int line) const {
  auto it = files_.find(fileKey(file));
  if (it == files_.end()) return std::nullopt;
  auto b = it->second.bps.find(line);
  if (b == it->second.bps.end()) return std::nullopt;
  return b->second;
}

std::vector<int> Breakpoints::lines(const fs::path& file) const {
  std::vector<int> out;
  auto it = files_.find(fileKey(file));
  if (it != files_.end())
    for (const auto& [ln, bp] : it->second.bps) out.push_back(ln);
  return out;
}

std::vector<Breakpoint> Breakpoints::all() const {
  std::vector<Breakpoint> out;
  for (const auto& [key, e] : files_)
    for (const auto& [ln, bp] : e.bps) out.push_back(bp);
  return out;
}

size_t Breakpoints::count() const {
  size_t n = 0;
  for (const auto& [key, e] : files_) n += e.bps.size();
  return n;
}

void Breakpoints::clear() {
  if (files_.empty()) return;
  files_.clear();
  changed();
}

void Breakpoints::onLinesReplaced(const fs::path& file, int startLine, int endLine, int newLineCount) {
  if (files_.empty()) return;  // the common case: no canonicalisation on every keystroke
  auto it = files_.find(fileKey(file));
  if (it == files_.end()) return;
  const int delta = newLineCount - (endLine - startLine + 1);
  if (delta == 0 && startLine == endLine) return;  // a single-line edit moves nothing
  // Same shifting rule as Bookmarks / NavigationHistory; breakpoints that land on one line keep the first.
  std::map<int, Breakpoint> next;
  bool moved = false;
  for (auto& [ln0, bp] : it->second.bps) {
    int ln = ln0;
    if (ln > endLine) ln += delta;
    else if (ln > startLine) ln = startLine;
    if (ln != ln0) moved = true;
    bp.line = ln;
    next.emplace(ln, std::move(bp));
  }
  if (!moved) return;
  it->second.bps = std::move(next);
  changed();
}

void Breakpoints::onFileRenamed(const fs::path& from, const fs::path& to) {
  auto it = files_.find(fileKey(from));
  if (it == files_.end()) return;
  Entry e = std::move(it->second);
  files_.erase(it);
  Entry& dst = files_[fileKey(to)];
  dst.file = to;
  for (auto& [ln, bp] : e.bps) {
    bp.file = to;
    dst.bps.emplace(ln, std::move(bp));
  }
  changed();
}

bool Breakpoints::save(const fs::path& jsonFile, const fs::path& root, std::string* error) {
  nlohmann::json files = nlohmann::json::object();
  std::error_code cec;
  const fs::path canonRoot = fs::weakly_canonical(root, cec);
  for (const auto& [key, e] : files_) {
    // Canonical on both sides, so a spelling that differs from the workspace root's still ends up relative.
    fs::path rel = fs::weakly_canonical(e.file, cec).lexically_relative(canonRoot);
    if (rel.empty() || *rel.begin() == "..") rel = e.file.lexically_relative(root);
    const bool inside = !rel.empty() && *rel.begin() != "..";
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& [ln, bp] : e.bps) {
      nlohmann::json o;
      o["line"] = bp.line;
      o["enabled"] = bp.enabled;
      o["condition"] = bp.condition;
      arr.push_back(std::move(o));
    }
    files[(inside ? rel : e.file).generic_string()] = std::move(arr);
  }
  nlohmann::json j;
  j["version"] = 1;
  j["breakpoints"] = std::move(files);
  std::error_code ec;
  fs::create_directories(jsonFile.parent_path(), ec);
  std::ofstream out(jsonFile, std::ios::binary | std::ios::trunc);
  if (!out) {
    if (error) *error = "cannot write " + jsonFile.string();
    return false;
  }
  out << j.dump(2);
  dirty_ = false;
  return true;
}

bool Breakpoints::load(const fs::path& jsonFile, const fs::path& root, std::string* error) {
  std::ifstream in(jsonFile, std::ios::binary);
  if (!in) {
    if (error) *error = "cannot open " + jsonFile.string();
    return false;
  }
  nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
  if (j.is_discarded() || !j.is_object() || !j.contains("breakpoints") || !j["breakpoints"].is_object()) {
    if (error) *error = jsonFile.string() + " is not a breakpoints file";
    return false;
  }
  if (j.contains("version") && j["version"].is_number_integer() && j["version"].get<int>() > 1) {
    if (error) *error = jsonFile.string() + " has a newer format version";
    return false;
  }
  std::map<std::string, Entry> next;
  for (const auto& [name, arr] : j["breakpoints"].items()) {
    if (!arr.is_array()) continue;
    fs::path p(name);
    if (p.is_relative()) p = root / p;
    Entry e;
    e.file = p;
    for (const auto& v : arr) {
      if (!v.is_object() || !v.contains("line") || !v["line"].is_number_integer() || v["line"].get<int>() < 0) continue;
      Breakpoint bp;
      bp.file = p;
      bp.line = v["line"].get<int>();
      if (v.contains("enabled") && v["enabled"].is_boolean()) bp.enabled = v["enabled"].get<bool>();
      if (v.contains("condition") && v["condition"].is_string()) bp.condition = v["condition"].get<std::string>();
      e.bps.emplace(bp.line, std::move(bp));
    }
    if (!e.bps.empty()) next[fileKey(p)] = std::move(e);
  }
  files_ = std::move(next);
  dirty_ = false;
  ++gen_;
  if (handler_) handler_();
  return true;
}

}  // namespace tf
