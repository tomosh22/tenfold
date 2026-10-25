#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "ui/features/PickerUtil.h"

namespace tf::ui {

// Find in files (Ctrl+Shift+F): a large modal ListPopup (decision: a modal list, not a docked bottom panel, so the
// Workbench layout stays untouched; the results survive the popup for F8 / Shift+F8). Typing searches live
// (debounced) through Editor::findInWorkspace; batches stream in via post() and rows are rebuilt at most every
// kRebuildMs. Rows are grouped by file (a header row with the match count, then one row per match with the
// context line and the match highlighted). Option toggles: Alt+C case sensitive, Alt+W whole word, Alt+R regex.
// Globs: type "query :: *.cpp, !third_party" (include globs, '!' excludes). Enter / click -> Editor::gotoSearchMatch
// (navigation history, constraint 1.3). A new query or closing the popup cancels the running search.
class FindInFiles {
 public:
  using SearchFn =
      std::function<SearchHandle(const std::string& query, SearchOptions, Editor::SearchBatch, Editor::SearchDone)>;
  // Per file: the match ranges (empty = clear) for TextView::setSearchMarkers.
  using MarkersFn = std::function<void(const std::filesystem::path&, const std::vector<Range>&)>;

  explicit FindInFiles(PickerEnv& env) : env_(env) {}
  ~FindInFiles();
  FindInFiles(const FindInFiles&) = delete;
  FindInFiles& operator=(const FindInFiles&) = delete;

  void setSearchFn(SearchFn fn) { search_ = std::move(fn); }  // default: Editor::findInWorkspace
  void setMarkersFn(MarkersFn fn) { markers_ = std::move(fn); }
  void setDebounceMs(int ms) { debounceMs_ = ms; }
  void open(const std::string& initialQuery = {});
  void close();
  bool isOpen() const { return popup_ != nullptr; }
  ListPopup* popup() const { return popup_; }
  // Fires a due debounced search and a due row rebuild; returns ms until the next one (-1 = none).
  int tick(int64_t nowMs);

  // F8 / Shift+F8: the next / previous match of the last search (wraps), jumping through gotoSearchMatch.
  bool step(bool forward);
  const std::vector<SearchMatch>& matches() const { return all_; }
  bool running() const { return running_; }
  const SearchSummary& summary() const { return summary_; }
  int current() const { return cur_; }  // index into the grouped order, -1 = none
  // Parses "query :: globs" and the option flags into SearchOptions (exposed for tests).
  static SearchOptions parse(const std::string& text, bool cs, bool word, bool regex);

 private:
  class Popup;
  void queryChanged();
  void start();
  void cancel();
  void onBatch(uint64_t seq, const std::vector<SearchMatch>& b);
  void onDone(uint64_t seq, const SearchSummary& s);
  void rebuild();
  void setMarkers();
  std::string statusText() const;
  void accept(int row);

  PickerEnv& env_;
  SearchFn search_;
  MarkersFn markers_;
  ListPopup* popup_ = nullptr;
  SearchHandle handle_;
  std::vector<SearchMatch> all_;  // arrival order
  std::vector<size_t> order_;     // indexes into all_, grouped by file (sorted by path, line, column)
  std::vector<int> rowMatch_;     // popup row -> index into order_ (headers: their first match)
  std::set<std::filesystem::path> marked_;
  SearchSummary summary_;
  uint64_t seq_ = 0;
  int cur_ = -1, debounceMs_ = 150, rebuildMs_ = 60;
  bool running_ = false, pending_ = false, dirty_ = false, cs_ = false, word_ = false, regex_ = false;
  int64_t lastEdit_ = 0, lastRebuild_ = -1000000;
  std::shared_ptr<int> alive_ = std::make_shared<int>(0);
};

}  // namespace tf::ui
