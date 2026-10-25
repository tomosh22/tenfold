#pragma once
#include <filesystem>
#include <string>

#include "ui/widgets/TabStrip.h"
#include "vcs/VcsManager.h"

// How the version-control state of a file is shown (C3c): the status-bar segment text and the tab marker. Pure
// mappings from VcsStatus, shared by the Workbench (tabs, status bar) and the tests.
namespace tf::ui {

// "Git" / "P4" for the providers that ship, the provider's own name otherwise.
inline std::string vcsProviderLabel(const std::string& provider) {
  if (provider == "git") return "Git";
  if (provider == "perforce" || provider == "p4") return "P4";
  return provider;
}

// Status-bar text of `file`: "Git: modified", "P4: checked out", "P4: read-only \xE2\x80\x94 will check out on edit".
// Empty for a clean, ignored or unknown file and when no provider accepted the workspace: the segment is only there
// when there is something to say.
inline std::string vcsStatusText(const VcsManager& vcs, const std::filesystem::path& file) {
  if (!vcs.active()) return {};
  const char* what = nullptr;
  switch (vcs.fileStatus(file)) {
    case VcsStatus::Modified: what = "modified"; break;
    case VcsStatus::Added: what = "added"; break;
    case VcsStatus::Deleted: what = "deleted"; break;
    case VcsStatus::Renamed: what = "renamed"; break;
    case VcsStatus::Untracked: what = "untracked"; break;
    case VcsStatus::Conflicted: what = "conflict"; break;
    case VcsStatus::CheckedOut: what = "checked out"; break;
    case VcsStatus::ReadOnlyNotCheckedOut: what = "read-only \xE2\x80\x94 will check out on edit"; break;
    case VcsStatus::Unmodified:
    case VcsStatus::Ignored:
    case VcsStatus::Unknown: break;
  }
  return what ? vcsProviderLabel(vcs.providerName()) + ": " + what : std::string();
}

inline TabStrip::VcsMark vcsTabMark(VcsStatus s) {
  switch (s) {
    case VcsStatus::Modified:
    case VcsStatus::Renamed:
    case VcsStatus::Deleted: return TabStrip::VcsMark::Modified;
    case VcsStatus::Added:
    case VcsStatus::Untracked: return TabStrip::VcsMark::Added;
    case VcsStatus::CheckedOut: return TabStrip::VcsMark::CheckedOut;
    case VcsStatus::ReadOnlyNotCheckedOut: return TabStrip::VcsMark::ReadOnly;
    case VcsStatus::Conflicted: return TabStrip::VcsMark::Conflict;
    default: return TabStrip::VcsMark::None;
  }
}

}  // namespace tf::ui
