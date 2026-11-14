#pragma once
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace tf::ui {

// Visual Studio's File > Recent Projects and Solutions (SLN-OPEN, docs/VS_BEHAVIOUR.md section 16): the solutions,
// projects and folders opened most recently, newest first, at most `max` entries (Visual Studio: Tools > Options >
// Environment > General > "Items shown in recently used file lists", 1 to 24, default 10). The list belongs to the
// user, not to a workspace: it lives in a text file in the user config dir (`recent.txt`, one absolute path per line,
// UTF-8, newest first), shared by every tenfold window. Reading and writing never throw; a file that is missing or
// unreadable is an empty list, a failed write is reported by the return value and the in-memory list stays right.
//
// Entries whose file or folder has disappeared are KEPT (they are shown as missing, like Visual Studio's start page,
// and the picker offers to drop them): only drop() / pruneMissing() remove them.
class RecentSolutions {
 public:
  static constexpr size_t kDefaultMax = 10, kMaxMax = 24;
  explicit RecentSolutions(std::filesystem::path file, size_t max = kDefaultMax);

  const std::filesystem::path& file() const { return file_; }
  size_t max() const { return max_; }
  void setMax(size_t n);  // clamped to 1..kMaxMax; cuts the list

  // Re-reads the file (replacing the list); false when there is none yet.
  bool load();
  // Writes the file (temp file + rename). Creates its folder.
  bool save() const;

  // The entries, newest first.
  const std::vector<std::filesystem::path>& entries() const { return entries_; }
  // Moves `p` (made absolute and normal) to the front, dropping an older spelling of the same path (case-insensitive
  // on Windows), cutting the list at max(). Does not save.
  void add(const std::filesystem::path& p);
  // Removes `p`; true when it was listed. Does not save.
  bool drop(const std::filesystem::path& p);
  // Removes every entry that no longer exists; returns how many. Does not save.
  size_t pruneMissing();
  static bool exists(const std::filesystem::path& p);

 private:
  std::filesystem::path file_;
  size_t max_;
  std::vector<std::filesystem::path> entries_;
};

// "<user config dir>/recent.txt"
std::filesystem::path defaultRecentFile(const std::filesystem::path& userDir);

}  // namespace tf::ui
