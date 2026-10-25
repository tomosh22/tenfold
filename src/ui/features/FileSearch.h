#pragma once
#include <filesystem>
#include <string>
#include <vector>

#include "ui/features/PickerUtil.h"
#include "ui/shell/Workbench.h"

namespace tf::ui {

// Open-file picker (Ctrl+P / Ctrl+O): fuzzy search over the workspace file list with FastIndex::fuzzyScore. The
// filename weighs four times as much as the whole relative path (an exact filename beats any path fragment), files
// opened recently (Workbench::recentFiles) get a bonus that decays with their rank, and with an empty query the recent
// files come first. Enter opens the file with Workbench::openFile: a tab switch, not a navigation jump.
class FileSearch {
 public:
  FileSearch(PickerEnv& env, Workbench& bench);

  void open();
  void close();
  bool isOpen() const { return popup_ != nullptr; }
  ListPopup* popup() const { return popup_; }

  // The ranking, independent of any widget (tests, benchmark). `files` are workspace-relative generic paths and
  // `recent` the same form, most recent first. Returns indices into `files`, best first, at most `limit`.
  static std::vector<size_t> rank(const std::vector<std::string>& files, const std::vector<std::string>& recent,
                                  const std::string& query, size_t limit = 200);
  // Workspace-relative generic paths of every workspace file (sources, headers, others).
  static std::vector<std::string> collect(const Workspace& ws);

 private:
  void refresh();

  PickerEnv& env_;
  Workbench& bench_;
  ListPopup* popup_ = nullptr;
  std::vector<std::string> files_, recent_;
  std::vector<size_t> shown_;
};

}  // namespace tf::ui
