#pragma once
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <set>
#include <string>

#include "ui/features/PickerUtil.h"

namespace tf::ui {

class Workbench;

// Files that change on disk behind the editor's back. Wires Editor::onExternalChange (the engine fires it, on the pump
// thread, when an open file's content differs from what was last loaded / saved / reloaded) and polls for deleted
// files.
//
// Setting editor.reloadExternalChanges (the default mirrors Visual Studio's "reload modified files unless there are
// unsaved changes"):
//   auto    a clean document is reloaded (Editor::reloadFromDisk: only the changed lines are replaced, so carets,
//   scroll
//           and navigation history entries keep their place); a dirty one asks "<file> changed on disk": Reload (lose
//           my changes) / Keep my version (the default choice).
//   prompt  asks for every change, also for a clean document (Reload / Keep my version).
//   never   leaves the buffer alone and only says so in the status bar.
// Keep my version leaves the buffer as it is; the question comes back with the next change on disk. Prompts are modal
// and shown one at a time, when no other popup is open (queued meanwhile). A prompt that another popup closed is asked
// again later; Escape counts as "Keep my version".
//
// Deleted files: a document whose file vanished keeps its buffer. The tab gets "(deleted)" appended, the status bar
// shows "deleted on disk" while it is current, and one message says so. Saving recreates the file. Found by polling the
// open documents about once a second.
class ExternalChanges {
 public:
  enum class Mode { Auto, Prompt, Never };
  static Mode parseMode(const std::string& text);  // unknown text: Auto

  ExternalChanges(PickerEnv& env, Workbench* bench);
  ~ExternalChanges();
  ExternalChanges(const ExternalChanges&) = delete;
  ExternalChanges& operator=(const ExternalChanges&) = delete;

  void setMode(Mode m) { mode_ = m; }
  Mode mode() const { return mode_; }
  // Opens the next queued prompt (when no popup is up) and polls for deleted files. Returns ms until the next poll.
  int tick(int64_t nowMs);
  void close();  // closes an open prompt; it is asked again later
  bool isOpen() const { return popup_ != nullptr; }
  ListPopup* popup() const { return popup_; }
  size_t pending() const { return queue_.size(); }
  bool isMissing(const std::filesystem::path& file) const { return missing_.count(file.generic_string()) != 0; }

 private:
  void onChanged(Document& d);
  void reload(const std::filesystem::path& file);
  void openPrompt(const std::filesystem::path& file);
  void pollDeleted();
  std::string name(const std::filesystem::path& file) const;

  PickerEnv& env_;
  Workbench* bench_;
  Mode mode_ = Mode::Auto;
  std::deque<std::filesystem::path> queue_;
  std::set<std::string> missing_;
  ListPopup* popup_ = nullptr;
  std::filesystem::path asking_;
  bool forced_ = false;
  int64_t nextPoll_ = 0;
  std::shared_ptr<int> life_ = std::make_shared<int>(0);
};

}  // namespace tf::ui
