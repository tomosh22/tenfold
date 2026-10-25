#pragma once
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "core/Position.h"

namespace tf {

// Line bookmarks of the workspace, per file (B8). They follow edits exactly like NavigationHistory entries do
// (lines inserted / removed above shift them; a bookmark inside a replaced block moves to its first line) and are
// persisted as JSON (`.tenfold/bookmarks.json`: {"version":1,"bookmarks":{"<path relative to root>":[line,...]}},
// lines 0-based). Navigation order for next / prev is by path, then line, across all files.
class Bookmarks {
 public:
  struct Item {
    std::filesystem::path file;
    int line = 0;
  };

  // Toggles a bookmark; true when the line is bookmarked afterwards.
  bool toggle(const std::filesystem::path& file, int line);
  bool has(const std::filesystem::path& file, int line) const;
  std::vector<int> lines(const std::filesystem::path& file) const;  // ascending
  std::vector<Item> all() const;                                    // by path, then line
  bool empty() const { return files_.empty(); }
  size_t count() const;
  void clear();

  // The first bookmark after / before `from` in navigation order, wrapping around; nullopt without bookmarks.
  std::optional<Item> next(const std::filesystem::path& file, int line) const { return step(file, line, true); }
  std::optional<Item> prev(const std::filesystem::path& file, int line) const { return step(file, line, false); }

  // Same contract as NavigationHistory::onLinesReplaced: lines startLine..endLine (inclusive) became newLineCount.
  void onLinesReplaced(const std::filesystem::path& file, int startLine, int endLine, int newLineCount);
  void onFileRenamed(const std::filesystem::path& from, const std::filesystem::path& to);

  // Bumped by every change (views repaint their gutters when it moves).
  uint64_t generation() const { return gen_; }
  // Changed since the last save / load.
  bool dirty() const { return dirty_; }
  bool save(const std::filesystem::path& jsonFile, const std::filesystem::path& root, std::string* error = nullptr);
  // Replaces the content; false (nothing changed) when the file is missing or malformed.
  bool load(const std::filesystem::path& jsonFile, const std::filesystem::path& root, std::string* error = nullptr);

 private:
  struct Entry {
    std::filesystem::path file;
    std::set<int> lines;
  };
  std::optional<Item> step(const std::filesystem::path& file, int line, bool forward) const;
  std::map<std::string, Entry> files_;  // key: pathKey(file)
  bool dirty_ = false;
  uint64_t gen_ = 0;
};

}  // namespace tf
