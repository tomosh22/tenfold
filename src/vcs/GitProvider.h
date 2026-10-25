#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "vcs/Vcs.h"
#include "vcs/VcsExec.h"

namespace tf {

// Git through the `git` command line (no libgit2). Paths are passed as separate arguments (no shell), pathspecs are
// taken literally (GIT_LITERAL_PATHSPECS), so names with spaces, glob characters or non-ASCII text work.
class GitProvider : public VcsProvider {
 public:
  explicit GitProvider(std::string gitExe = "git") : exe_(std::move(gitExe)) {}

  std::string name() const override { return "git"; }
  bool detect(const std::filesystem::path& rootOrFile) override;
  std::filesystem::path root() const override { return root_; }
  std::vector<VcsFileInfo> status(const std::vector<std::filesystem::path>& files) override;
  std::optional<std::string> baseText(const std::filesystem::path& file, std::string* error = nullptr) override;
  bool revert(const std::filesystem::path& file, std::string* error = nullptr) override;
  std::string lastError() const override { return lastError_; }

  // One record of `git status --porcelain=v2 -z`; paths are relative to the repository root, '/' separated.
  struct Entry {
    std::string path;
    std::string origPath;  // renames / copies: the source
    VcsStatus status = VcsStatus::Unknown;
  };
  static std::vector<Entry> parseStatusV2(const std::string& output);
  // The VcsStatus of an XY pair of a "1" (ordinary) entry; exposed for tests.
  static VcsStatus statusOfXY(char x, char y);

 private:
  ExecResult git(const std::vector<std::string>& args) const;
  // `file` relative to the root with '/' separators; nullopt when it is outside the repository.
  std::optional<std::string> relative(const std::filesystem::path& file) const;
  bool fail(std::string* error, const std::string& msg);

  std::string exe_;
  std::filesystem::path root_;
  std::string lastError_;
};

}  // namespace tf
