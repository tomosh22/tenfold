#pragma once
#include <filesystem>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

namespace tf {

// What a version-control system says about one file. The Perforce-only values (CheckedOut,
// ReadOnlyNotCheckedOut) are part of the neutral vocabulary so the UI needs one mapping only.
enum class VcsStatus {
  Unmodified,
  Modified,
  Added,
  Deleted,
  Renamed,
  Untracked,
  Ignored,
  CheckedOut,             // Perforce: opened for edit
  ReadOnlyNotCheckedOut,  // Perforce: under the client but not opened (read-only on disk)
  Conflicted,
  Unknown,  // not under this provider (outside the repository, provider failed, no provider)
};

const char* vcsStatusName(VcsStatus s);
inline std::ostream& operator<<(std::ostream& os, VcsStatus s) { return os << vcsStatusName(s); }

struct VcsFileInfo {
  std::filesystem::path path;  // absolute
  VcsStatus status = VcsStatus::Unknown;
  std::filesystem::path renamedFrom;  // Renamed: the old path (absolute); empty otherwise
};

// A version-control back end (Git, Perforce, ...). EVERY method blocks (it runs a child process) and is called
// from the VcsManager's worker thread only, never from the UI thread; one provider instance is only ever used by
// one thread at a time, so implementations need no locking.
class VcsProvider {
 public:
  virtual ~VcsProvider() = default;

  virtual std::string name() const = 0;  // "git", "perforce"

  // Is `rootOrFile` (a directory, or a file whose directory is used) under this system? On success the provider
  // remembers its root and the other calls work; on failure it stays unusable and lastError() says why.
  virtual bool detect(const std::filesystem::path& rootOrFile) = 0;
  // The repository / client root found by detect() (absolute, empty before).
  virtual std::filesystem::path root() const = 0;

  // Status of `files` (absolute paths): one entry per input, same order. Files outside the repository are
  // Unknown. An EMPTY `files` asks for every file under root() that is not Unmodified (and not Ignored).
  virtual std::vector<VcsFileInfo> status(const std::vector<std::filesystem::path>& files) = 0;

  // The committed ("have") revision of `file`, bytes as stored (any line endings; compare with LineDiff's
  // splitLines, which ignores them). Nullopt with an empty *error: the file has no committed version (new file,
  // empty repository); nullopt with an error: the call failed.
  virtual std::optional<std::string> baseText(const std::filesystem::path& file, std::string* error = nullptr) = 0;

  // Discards local changes of `file` on disk (and in the index / open state) so it equals the committed version.
  // The caller reloads an open document afterwards.
  virtual bool revert(const std::filesystem::path& file, std::string* error = nullptr) = 0;

  // Called once, off the UI thread, before the first edit of a document is kept (Git: nothing; Perforce runs
  // `p4 edit` and makes the file writable). A failure is reported to the user, the edit is kept either way.
  virtual bool beforeFirstEdit(const std::filesystem::path& file, std::string* error = nullptr) {
    (void)file;
    (void)error;
    return true;
  }

  // The message of the last failed call (empty after a success).
  virtual std::string lastError() const { return {}; }
};

}  // namespace tf
