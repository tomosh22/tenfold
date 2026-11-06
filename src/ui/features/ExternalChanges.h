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
// Settings (VS-TABS T9, Visual Studio's Tools > Options > Environment > Documents):
//   editor.detectExternalChanges (true)   notice changes at all; off: nothing happens and nothing is said
//   editor.reloadUnlessUnsaved (false)    a clean document is reloaded without asking (Editor::reloadFromDisk: only the
//                                         changed lines are replaced, so carets, scroll and navigation history entries
//                                         keep their place); off: it asks too
// The question is Visual Studio's: "The file X has been modified outside of the source editor. Do you want to reload
// it?" with Yes / Yes to All / No / No to All. A document with unsaved changes always asks (the default choice is No,
// Yes says the changes are lost). Yes to All also reloads the other changed files that wait and have no unsaved
// changes, No to All keeps all of them. The older editor.reloadExternalChanges (auto | prompt | never) still wins
// when it is set: auto = detect + reloadUnlessUnsaved, prompt = detect only, never = do not detect (and say so in the
// status bar); resolvePolicy combines the three settings.
// No leaves the buffer as it is; the question comes back with the next change on disk. Prompts are modal and shown one
// at a time, when no other popup is open (queued meanwhile). A prompt that another popup closed is asked again later;
// Escape counts as No.
//
// Deleted files: a document whose file vanished keeps its buffer. The tab gets "(deleted)" appended, the status bar
// shows "deleted on disk" while it is current, and one message says so. Saving recreates the file. Found by polling the
// open documents about once a second.
class ExternalChanges {
 public:
  struct Policy {
    bool detect = true;
    bool reloadUnlessUnsaved = false;
    bool announce = false;  // legacy "never": say in the status bar that a change was not reloaded
  };
  // `legacy` is the text of editor.reloadExternalChanges: auto / prompt / never override the two flags, anything else
  // (including the empty default) leaves them.
  static Policy resolvePolicy(bool detect, bool reloadUnlessUnsaved, const std::string& legacy);

  ExternalChanges(PickerEnv& env, Workbench* bench);
  ~ExternalChanges();
  ExternalChanges(const ExternalChanges&) = delete;
  ExternalChanges& operator=(const ExternalChanges&) = delete;

  void setPolicy(Policy p) { policy_ = p; }
  const Policy& policy() const { return policy_; }
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
  Policy policy_;
  std::deque<std::filesystem::path> queue_;
  std::set<std::string> missing_;
  ListPopup* popup_ = nullptr;
  std::filesystem::path asking_;
  bool forced_ = false;
  int64_t nextPoll_ = 0;
  std::shared_ptr<int> life_ = std::make_shared<int>(0);
};

}  // namespace tf::ui
