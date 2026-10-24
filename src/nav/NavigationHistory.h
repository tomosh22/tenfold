#pragma once
#include <chrono>
#include <deque>
#include <filesystem>
#include <optional>
#include <string>

#include "core/Position.h"

namespace tf {

// Back / Forward navigation history, modelled on Visual Studio's
// "Navigate Backward/Forward" and VS Code's Alt+Left/Right:
//
//  * Every "jump" (go to definition, symbol search result, find-in-files
//    result, go to line, opening a file, ...) records the position you jumped
//    FROM and the position you arrived AT.
//  * Ordinary caret movement that travels more than `coalesceLines` lines in
//    one file also records a point (so scrolling far away and coming back works).
//  * Nearby entries in the same file are coalesced so the stack isn't flooded.
//  * Going back from the "live" position first records it, so Forward returns
//    to exactly where you were.
//  * Recording a new point while in the middle of the history truncates the
//    forward entries (browser semantics).
//  * Entries track edits: lines inserted/removed above an entry shift it.
class NavigationHistory {
 public:
  enum class Kind { Jump, Caret, Edit };

  struct Entry {
    Location loc;
    Kind kind = Kind::Jump;
    std::string label;  // optional, e.g. the symbol navigated to
    std::chrono::steady_clock::time_point time{};
  };

  struct Options {
    size_t maxEntries = 200;
    int coalesceLines = 10;  // entries closer than this in the same file merge
  };

  NavigationHistory() = default;
  explicit NavigationHistory(Options opt) : opt_(opt) {}

  // Record an explicit navigation point (before and/or after a jump).
  void record(const Location& loc, Kind kind = Kind::Jump, std::string label = {});

  // Call on every caret move with the *previous* caret location and the new one.
  // Records `from` as a point if the move is a "far" move (file change or
  // more than coalesceLines lines away).
  void onCaretMoved(const Location& from, const Location& to);

  // Navigate. `current` is where the caret is right now. Returns the
  // location to go to, or nullopt if there is nothing in that direction.
  std::optional<Location> back(const Location& current);
  std::optional<Location> forward(const Location& current);

  bool canGoBack() const;
  bool canGoForward() const;

  // Keep entries valid while editing. `startLine..endLine` (inclusive,
  // 0-based) were replaced by `newLineCount` lines.
  void onLinesReplaced(const std::filesystem::path& file, int startLine, int endLine, int newLineCount);
  void onFileRenamed(const std::filesystem::path& from, const std::filesystem::path& to);
  void onFileRemoved(const std::filesystem::path& file);

  void clear();
  const std::deque<Entry>& entries() const { return entries_; }
  // Index of the entry we are currently "at", or entries().size() if at the live position.
  size_t cursor() const { return cursor_; }

 private:
  bool near(const Location& a, const Location& b) const;
  void push(Entry e);

  Options opt_;
  std::deque<Entry> entries_;
  size_t cursor_ = 0;  // == entries_.size() means "live"
};

}  // namespace tf
