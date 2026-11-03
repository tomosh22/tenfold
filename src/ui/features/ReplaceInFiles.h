#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ui/features/FindInFiles.h"
#include "ui/features/PickerUtil.h"
#include "ui/widgets/InputLine.h"

namespace tf::ui {

// Replace in files (ReplaceInFiles, Ctrl+Shift+H): a modal panel with a find field, a replace field and the matches of
// the search as a checklist grouped by file. Each match row shows its line with the old text struck and the
// replacement after it, so the list is the preview. Typing in the find field searches the workspace live (the same
// query syntax as find in files: `query :: *.cpp, !third_party`, Alt+C / Alt+W / Alt+R toggles); typing in the
// replace field updates the preview. Every match starts checked.
//
// Keys: Tab switches between the two fields, Up/Down/PageUp/PageDown move the row, Insert (or a click on the box)
// checks / unchecks the row (a file row: all its matches), Alt+T checks all / none, Enter replaces the checked matches,
// Escape closes. Replacing works through Editor::replaceInFiles: open documents are edited in place (one undo step per
// document, left unsaved), closed files are rewritten on disk. When closed files are involved Enter first asks for
// one confirmation (the status line says how many files are written; Enter again confirms, Escape goes back). The
// replacement is literal text (regex group references are not expanded).
class ReplaceInFiles {
 public:
  using SearchFn = FindInFiles::SearchFn;
  struct Entry {
    SearchMatch match;
    bool checked = true;
  };

  explicit ReplaceInFiles(PickerEnv& env) : env_(env) {}
  ~ReplaceInFiles();
  ReplaceInFiles(const ReplaceInFiles&) = delete;
  ReplaceInFiles& operator=(const ReplaceInFiles&) = delete;

  void setSearchFn(SearchFn fn) { search_ = std::move(fn); }  // default: Editor::findInWorkspace
  void setDebounceMs(int ms) { debounceMs_ = ms; }
  // Opens the panel; `initialQuery` (a single-line selection) fills the find field and searches at once.
  void open(const std::string& initialQuery = {});
  void close();
  bool isOpen() const;
  int tick(int64_t nowMs);  // fires a due debounced search and row rebuild; ms until the next one (-1 = none)

  // The matches in display order (relative path, line, column) and whether each is checked.
  const std::vector<Entry>& entries() const { return entries_; }
  size_t checkedCount() const;
  bool running() const { return running_; }
  bool confirming() const { return confirming_; }
  std::string statusText() const;
  InputLine* findInput() const;
  InputLine* replaceInput() const;
  const std::string& error() const { return summary_.error; }

  void setChecked(size_t entry, bool on);
  void setAllChecked(bool on);
  void setOptions(bool caseSensitive, bool wholeWord, bool regex);
  // Enter: replaces the checked matches. With checked matches in closed files the first call only asks (confirming()
  // becomes true) and returns false; the next call writes. True once the replacement ran.
  bool apply();
  // Result of the last apply(), for the status bar / tests.
  const Editor::ReplaceInFilesResult& lastResult() const { return last_; }

 private:
  class Panel;
  friend class Panel;
  struct Row {
    bool header = false;
    std::string text;    // header: relative path
    size_t first = 0;    // header: first entry of the file; match row: its entry
    size_t count = 0;    // header: matches in the file
    size_t checked = 0;  // header: checked matches
  };
  void queryChanged();
  void start();
  void cancel();
  void rebuild();
  void toggleRow(int row);
  void resetConfirm();

  PickerEnv& env_;
  SearchFn search_;
  Panel* panel_ = nullptr;
  SearchHandle handle_;
  std::vector<Entry> entries_;  // sorted by (relative path, line, column) at every rebuild
  std::vector<Row> rows_;
  SearchSummary summary_;
  Editor::ReplaceInFilesResult last_;
  uint64_t seq_ = 0;
  int debounceMs_ = 150, rebuildMs_ = 60;
  bool running_ = false, pending_ = false, dirty_ = false, confirming_ = false;
  bool cs_ = false, word_ = false, regex_ = false;
  int64_t lastEdit_ = 0, lastRebuild_ = -1000000;
  std::shared_ptr<int> alive_ = std::make_shared<int>(0);
};

}  // namespace tf::ui
