#include "nav/NavigationHistory.h"

#include <algorithm>
#include <cstdlib>

namespace tf {

bool NavigationHistory::near(const Location& a, const Location& b) const {
  return a.file == b.file && std::abs(a.pos.line - b.pos.line) < opt_.coalesceLines;
}

void NavigationHistory::push(Entry e) {
  e.time = std::chrono::steady_clock::now();
  // Truncate anything "forward" of the cursor (browser semantics).
  if (cursor_ < entries_.size()) entries_.erase(entries_.begin() + static_cast<long>(cursor_) + 1, entries_.end());
  if (!entries_.empty() && near(entries_.back().loc, e.loc)) {
    entries_.back() = std::move(e);  // coalesce: keep the most recent position
  } else {
    entries_.push_back(std::move(e));
    while (entries_.size() > opt_.maxEntries) entries_.pop_front();
  }
  cursor_ = entries_.size();
}

void NavigationHistory::record(const Location& loc, Kind kind, std::string label) {
  push(Entry{loc, kind, std::move(label)});
}

void NavigationHistory::onCaretMoved(const Location& from, const Location& to) {
  if (from.file.empty()) return;
  if (near(from, to)) return;  // small move: nothing to remember
  // Record where we were. If we're browsing history, the move invalidates the forward stack.
  push(Entry{from, Kind::Caret, {}});
}

std::optional<Location> NavigationHistory::back(const Location& current) {
  if (entries_.empty()) return std::nullopt;
  if (cursor_ >= entries_.size()) {
    // Leaving the live position: remember it so Forward can return here.
    if (near(entries_.back().loc, current)) {
      entries_.back().loc = current;
      cursor_ = entries_.size() - 1;
    } else {
      push(Entry{current, Kind::Caret, {}});
      cursor_ = entries_.size() - 1;
    }
  }
  if (cursor_ == 0) return std::nullopt;
  --cursor_;
  return entries_[cursor_].loc;
}

std::optional<Location> NavigationHistory::forward(const Location& current) {
  if (cursor_ + 1 >= entries_.size()) return std::nullopt;
  ++cursor_;
  Location target = entries_[cursor_].loc;
  if (cursor_ + 1 == entries_.size()) cursor_ = entries_.size();  // back at the live position
  return target;
}

bool NavigationHistory::canGoBack() const {
  if (entries_.empty()) return false;
  if (cursor_ >= entries_.size()) return true;
  return cursor_ > 0;
}

bool NavigationHistory::canGoForward() const { return cursor_ + 1 < entries_.size(); }

void NavigationHistory::onLinesReplaced(const std::filesystem::path& file, int startLine, int endLine,
                                        int newLineCount) {
  const int delta = newLineCount - (endLine - startLine + 1);
  for (auto& e : entries_) {
    if (e.loc.file != file) continue;
    int& ln = e.loc.pos.line;
    if (ln > endLine) {
      ln += delta;
    } else if (ln > startLine) {
      // Inside the replaced block: clamp to the start of the replacement.
      ln = startLine;
      e.loc.pos.column = 0;
    }
  }
}

void NavigationHistory::onFileRenamed(const std::filesystem::path& from, const std::filesystem::path& to) {
  for (auto& e : entries_)
    if (e.loc.file == from) e.loc.file = to;
}

void NavigationHistory::onFileRemoved(const std::filesystem::path& file) {
  size_t orig = 0;
  size_t newCursor = cursor_;
  for (auto it = entries_.begin(); it != entries_.end(); ++orig) {
    if (it->loc.file == file) {
      if (orig < cursor_) --newCursor;
      it = entries_.erase(it);
    } else {
      ++it;
    }
  }
  cursor_ = std::min(newCursor, entries_.size());
}

void NavigationHistory::clear() {
  entries_.clear();
  cursor_ = 0;
}

}  // namespace tf
