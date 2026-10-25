#pragma once
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "vcs/Vcs.h"
#include "vcs/VcsExec.h"

namespace tf {

// Perforce through the `p4` command line (always `-ztag`, run with the workspace directory as cwd so p4 resolves
// P4CONFIG / P4CLIENT / P4PORT / P4USER itself). Local paths are passed as separate arguments (no shell) with the
// Perforce wildcard characters escaped (%40 for '@', %23 '#', %2A '*', %25 '%').
//
// Auto-checkout: beforeFirstEdit() runs `p4 edit` for a file that is in the depot and not opened, and verifies that
// the file is opened and writable afterwards. Files that are not in the depot are left alone: nothing runs
// `p4 add` automatically (use add() explicitly).
//
// Binary file types (binary, ubinary, utf16, apple, resource, symlink and friends) have no textual base: baseText()
// returns nullopt with an empty error for them.
class PerforceProvider : public VcsProvider {
 public:
  explicit PerforceProvider(std::string p4Exe = "p4", int detectTimeoutMs = 3000, int timeoutMs = 30000)
      : exe_(p4Exe.empty() ? "p4" : std::move(p4Exe)), detectTimeoutMs_(detectTimeoutMs), timeoutMs_(timeoutMs) {}

  std::string name() const override { return "perforce"; }
  bool detect(const std::filesystem::path& rootOrFile) override;
  std::filesystem::path root() const override { return root_; }
  std::vector<VcsFileInfo> status(const std::vector<std::filesystem::path>& files) override;
  std::optional<std::string> baseText(const std::filesystem::path& file, std::string* error = nullptr) override;
  bool revert(const std::filesystem::path& file, std::string* error = nullptr) override;
  bool beforeFirstEdit(const std::filesystem::path& file, std::string* error = nullptr) override;
  std::string lastError() const override { return lastError_; }

  // `p4 add` for a file that is not in the depot (not called automatically).
  bool add(const std::filesystem::path& file, std::string* error = nullptr);

  // Forgets the cached negative detection results (they live a few seconds so that a missing server does not
  // cost its timeout again for every probe).
  static void clearDetectCache();

  // One tagged record: field name -> value ("... depotFile //depot/a.txt").
  using Record = std::map<std::string, std::string>;
  static std::vector<Record> parseTagged(const std::string& output);
  // The status of an fstat record; `exists` / `writable` describe the local file. Exposed for tests.
  static VcsStatus classify(const Record& r, bool exists, bool writable);
  static std::string escapePath(const std::string& utf8Path);
  static bool isBinaryType(const std::string& type);

 private:
  ExecResult p4(const std::vector<std::string>& args, int timeoutMs = 0) const;
  static bool succeeded(const ExecResult& r);
  static std::string messageOf(const ExecResult& r);
  bool fail(std::string* error, const std::string& msg);
  bool underRoot(const std::filesystem::path& file) const;
  std::string arg(const std::filesystem::path& file) const;  // escaped local path argument
  // fstat of local files; records keyed by the normalised local path. `err` gets the combined stderr.
  bool fstat(const std::vector<std::filesystem::path>& files, std::map<std::string, Record>& out, std::string& err);
  std::map<std::string, std::filesystem::path> localOfDepot(const std::vector<std::string>& depotPaths);
  VcsFileInfo infoOf(const std::filesystem::path& file, const Record* rec, const std::string& err,
                     const std::map<std::string, std::filesystem::path>& moved) const;

  std::string exe_;
  int detectTimeoutMs_;
  int timeoutMs_;
  std::filesystem::path root_;     // the client root (or the workspace directory when it is mapped but outside it)
  std::filesystem::path workDir_;  // cwd of every p4 call
  std::string rootKey_;
  std::string lastError_;
};

}  // namespace tf
