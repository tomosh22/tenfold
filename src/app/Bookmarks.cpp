#include "app/Bookmarks.h"

#include <fstream>

#include <nlohmann/json.hpp>

#include "core/PathUtil.h"

namespace tf {

namespace fs = std::filesystem;

bool Bookmarks::toggle(const fs::path& file, int line) {
  Entry& e = files_[pathKey(file)];
  e.file = file;
  dirty_ = true;
  ++gen_;
  if (e.lines.erase(line)) {
    if (e.lines.empty()) files_.erase(pathKey(file));
    return false;
  }
  e.lines.insert(line);
  return true;
}

bool Bookmarks::has(const fs::path& file, int line) const {
  auto it = files_.find(pathKey(file));
  return it != files_.end() && it->second.lines.count(line) > 0;
}

std::vector<int> Bookmarks::lines(const fs::path& file) const {
  auto it = files_.find(pathKey(file));
  if (it == files_.end()) return {};
  return {it->second.lines.begin(), it->second.lines.end()};
}

std::vector<Bookmarks::Item> Bookmarks::all() const {
  std::vector<Item> out;
  for (const auto& [key, e] : files_)
    for (int l : e.lines) out.push_back({e.file, l});
  return out;
}

size_t Bookmarks::count() const {
  size_t n = 0;
  for (const auto& [key, e] : files_) n += e.lines.size();
  return n;
}

void Bookmarks::clear() {
  if (!files_.empty()) dirty_ = true, ++gen_;
  files_.clear();
}

std::optional<Bookmarks::Item> Bookmarks::step(const fs::path& file, int line, bool forward) const {
  const std::vector<Item> v = all();
  if (v.empty()) return std::nullopt;
  const std::string key = pathKey(file);
  // Items are in (path key, line) order; compare an item with the origin in that order.
  auto cmp = [&](const Item& it) {
    const std::string k = pathKey(it.file);
    if (k != key) return k < key ? -1 : 1;
    return it.line < line ? -1 : it.line > line ? 1 : 0;
  };
  if (forward) {
    for (const Item& it : v)
      if (cmp(it) > 0) return it;
    return v.front();
  }
  for (size_t i = v.size(); i-- > 0;)
    if (cmp(v[i]) < 0) return v[i];
  return v.back();
}

void Bookmarks::onLinesReplaced(const fs::path& file, int startLine, int endLine, int newLineCount) {
  auto it = files_.find(pathKey(file));
  if (it == files_.end()) return;
  const int delta = newLineCount - (endLine - startLine + 1);
  if (delta == 0 && startLine == endLine) return;  // a single-line edit moves nothing
  std::set<int> next;
  for (int ln : it->second.lines) {
    if (ln > endLine) ln += delta;
    else if (ln > startLine) ln = startLine;
    next.insert(ln);
  }
  if (next != it->second.lines) {
    it->second.lines = std::move(next);
    dirty_ = true;
    ++gen_;
  }
}

void Bookmarks::onFileRenamed(const fs::path& from, const fs::path& to) {
  auto it = files_.find(pathKey(from));
  if (it == files_.end()) return;
  Entry e = std::move(it->second);
  files_.erase(it);
  Entry& dst = files_[pathKey(to)];
  dst.file = to;
  dst.lines.insert(e.lines.begin(), e.lines.end());
  dirty_ = true;
  ++gen_;
}

bool Bookmarks::save(const fs::path& jsonFile, const fs::path& root, std::string* error) {
  nlohmann::json files = nlohmann::json::object();
  for (const auto& [key, e] : files_) {
    const fs::path rel = e.file.lexically_relative(root);
    const bool inside = !rel.empty() && *rel.begin() != "..";
    files[(inside ? rel : e.file).generic_string()] = std::vector<int>(e.lines.begin(), e.lines.end());
  }
  nlohmann::json j;
  j["version"] = 1;
  j["bookmarks"] = std::move(files);
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

bool Bookmarks::load(const fs::path& jsonFile, const fs::path& root, std::string* error) {
  std::ifstream in(jsonFile, std::ios::binary);
  if (!in) {
    if (error) *error = "cannot open " + jsonFile.string();
    return false;
  }
  nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
  if (j.is_discarded() || !j.is_object() || !j.contains("bookmarks") || !j["bookmarks"].is_object()) {
    if (error) *error = jsonFile.string() + " is not a bookmarks file";
    return false;
  }
  std::map<std::string, Entry> next;
  for (const auto& [name, arr] : j["bookmarks"].items()) {
    if (!arr.is_array()) continue;
    fs::path p(name);
    if (p.is_relative()) p = root / p;
    Entry e;
    e.file = p;
    for (const auto& v : arr)
      if (v.is_number_integer() && v.get<int>() >= 0) e.lines.insert(v.get<int>());
    if (!e.lines.empty()) next[pathKey(p)] = std::move(e);
  }
  files_ = std::move(next);
  dirty_ = false;
  ++gen_;
  return true;
}

}  // namespace tf
