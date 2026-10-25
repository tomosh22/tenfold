#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace tf {

// Identity of a file for breakpoints, the same canonical key the Editor uses for documents (symlinks resolved, so
// macOS /var vs /private/var, Windows 8.3 names; on Windows also case and separators ignored). Thread-safe.
std::string breakpointFileKey(const std::filesystem::path& file);

// One line breakpoint. Lines are 0-based like every Position in the engine; backends convert.
struct Breakpoint {
  std::filesystem::path file;
  int line = 0;
  bool enabled = true;
  std::string condition;  // empty = unconditional
};

// Line breakpoints of the workspace, one per (file, line). They follow edits exactly like Bookmarks do (same
// onLinesReplaced / onFileRenamed contract: lines inserted / removed above shift them, a breakpoint inside a replaced
// block moves to its first line, and breakpoints that collapse onto one line merge into the first) and are persisted
// as JSON (`.tenfold/breakpoints.json`:
//   {"version":1,"breakpoints":{"<path relative to root>":[{"line":3,"enabled":true,"condition":"i > 2"},...]}}).
// Main thread only. A change handler (the DebugSession) is told after every change so it can push it to a backend.
class Breakpoints {
 public:
  // Toggles an unconditional enabled breakpoint at the line; true when a breakpoint exists afterwards.
  bool toggle(const std::filesystem::path& file, int line);
  // Adds the breakpoint, or replaces enabled / condition of the one at the same line.
  void add(const Breakpoint& bp);
  bool remove(const std::filesystem::path& file, int line);
  bool setEnabled(const std::filesystem::path& file, int line, bool enabled);
  bool setCondition(const std::filesystem::path& file, int line, const std::string& condition);
  bool has(const std::filesystem::path& file, int line) const { return get(file, line).has_value(); }
  std::optional<Breakpoint> get(const std::filesystem::path& file, int line) const;
  std::vector<int> lines(const std::filesystem::path& file) const;  // ascending
  std::vector<Breakpoint> all() const;                              // by path, then line
  bool empty() const { return files_.empty(); }
  size_t count() const;
  void clear();

  // Same contract as NavigationHistory::onLinesReplaced: lines startLine..endLine (inclusive) became newLineCount.
  void onLinesReplaced(const std::filesystem::path& file, int startLine, int endLine, int newLineCount);
  void onFileRenamed(const std::filesystem::path& from, const std::filesystem::path& to);

  // breakpointFileKey with a cache (main thread only): the key every lookup here and in DebugSession uses.
  const std::string& fileKey(const std::filesystem::path& file) const;

  // Bumped by every change (views repaint their gutters when it moves).
  uint64_t generation() const { return gen_; }
  // Called (synchronously, after the change) by every mutation that changed something. One handler.
  void setChangeHandler(std::function<void()> h) { handler_ = std::move(h); }
  // Changed since the last save / load.
  bool dirty() const { return dirty_; }
  bool save(const std::filesystem::path& jsonFile, const std::filesystem::path& root, std::string* error = nullptr);
  // Replaces the content; false (nothing changed) when the file is missing, malformed or of a newer version.
  bool load(const std::filesystem::path& jsonFile, const std::filesystem::path& root, std::string* error = nullptr);

 private:
  struct Entry {
    std::filesystem::path file;
    std::map<int, Breakpoint> bps;  // by line
  };
  void changed();
  mutable std::unordered_map<std::string, std::string> keyCache_;  // spelling -> key
  std::map<std::string, Entry> files_;                             // key: pathKey(file)
  bool dirty_ = false;
  uint64_t gen_ = 0;
  std::function<void()> handler_;
};

}  // namespace tf
