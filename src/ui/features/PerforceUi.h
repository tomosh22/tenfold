#pragma once
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ui/features/OsLauncher.h"
#include "ui/features/PickerUtil.h"
#include "vcs/PerforceProvider.h"

namespace tf::ui {

class Settings;

// The two Perforce commands that need UI (W8f), on top of VcsManager's Perforce helpers (which run `p4` off the UI
// thread). Both say in the status bar when the workspace is not under Perforce.
//
//  * PerforceHistory (Control P, Control H): `p4 filelog` of the current file in a modal ListPopup, newest first. A row
//    shows "#rev  change N  date  user  action" and the first line of the description; the input filters by any of
//    that text. Enter opens that revision (`p4 print`) in a new tab, read-only: the text is written to a temp file
//    `<temp>/tenfold-p4/<id>/<name>#<rev><ext>` which is made read-only on disk (the editor refuses to save it). There
//    is no "diff against the current file" action; the working copy's diff against #have is Vcs.DiffFileExternal. Dates
//    are UTC (`p4 filelog` reports epoch seconds).
//  * PerforceShowInP4v (Control P, Control V): starts `p4v -p <port> -u <user> -c <client> -s <depot path>` through the
//    OsLauncher (setting vcs.p4vPath names another program; empty = `p4v` from PATH). Port, user and client come from
//    `p4 info`, so P4CONFIG / `p4 set` are honoured; any that p4 does not report are left out and p4v uses its own.
class PerforceUi {
 public:
  struct Hooks {
    std::function<void(const std::string& text, int ttlMs)> message;
    std::function<const OsLauncher&()> launcher;
  };
  PerforceUi(PickerEnv& env, Hooks hooks);
  ~PerforceUi();
  PerforceUi(const PerforceUi&) = delete;
  PerforceUi& operator=(const PerforceUi&) = delete;

  void applySettings(const Settings& s);  // vcs.p4vPath
  void setTempRoot(std::filesystem::path p) { tempRoot_ = std::move(p); }

  void history();
  void showInP4v();
  void close();
  bool isOpen() const { return popup_ != nullptr; }
  ListPopup* popup() const { return popup_; }

  // "2023-11-14 22:13" (UTC).
  static std::string formatTime(long long epochSeconds);
  // The p4v arguments for a target (empty fields are left out).
  static std::vector<std::string> p4vArgs(const PerforceProvider::P4vTarget& t);

 private:
  void say(const std::string& text, int ttlMs = 6000) const;
  void showRevisions(const std::filesystem::path& file, std::vector<PerforceProvider::Revision> revs);
  void rebuild();
  void openRevision(const std::filesystem::path& file, const PerforceProvider::Revision& rev);
  std::string relName(const std::filesystem::path& p) const;

  PickerEnv& env_;
  Hooks hooks_;
  std::string p4vPath_;
  std::filesystem::path tempRoot_;
  ListPopup* popup_ = nullptr;
  std::filesystem::path file_;
  std::vector<PerforceProvider::Revision> revs_, shown_;
  std::shared_ptr<int> life_ = std::make_shared<int>(0);
};

}  // namespace tf::ui
