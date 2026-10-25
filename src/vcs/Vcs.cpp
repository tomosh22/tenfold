#include "vcs/Vcs.h"

namespace tf {

const char* vcsStatusName(VcsStatus s) {
  switch (s) {
    case VcsStatus::Unmodified: return "unmodified";
    case VcsStatus::Modified: return "modified";
    case VcsStatus::Added: return "added";
    case VcsStatus::Deleted: return "deleted";
    case VcsStatus::Renamed: return "renamed";
    case VcsStatus::Untracked: return "untracked";
    case VcsStatus::Ignored: return "ignored";
    case VcsStatus::CheckedOut: return "checked-out";
    case VcsStatus::ReadOnlyNotCheckedOut: return "read-only";
    case VcsStatus::Conflicted: return "conflicted";
    case VcsStatus::Unknown: return "unknown";
  }
  return "unknown";
}

}  // namespace tf
