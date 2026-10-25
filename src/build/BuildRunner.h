#pragma once
#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "lsp/Process.h"
#include "workspace/Workspace.h"

namespace tf {

// What to execute. Either exe + args (run directly), or `shell` (a command line for sh -c / cmd /c).
struct BuildCommand {
  std::string exe;
  std::vector<std::string> args;
  std::string shell;
  std::filesystem::path cwd;
  std::string display;  // human readable command line
};

struct BuildOverrides {
  std::string command;             // custom shell command; wins over everything
  std::string configuration;       // empty: Workspace::Settings::vsConfiguration
  std::string platform;            // empty: Workspace::Settings::vsPlatform
  std::filesystem::path buildDir;  // CMake build dir; empty: settings, else the compile database's, else .tenfold
};

// Picks the build command for a workspace: custom command (override or settings) > .sln (msbuild) > CMake
// (cmake --build) > Makefile (make -jN). False with `error` when nothing applies.
bool detectBuildCommand(const Workspace& ws, const BuildOverrides& o, BuildCommand* out, std::string* error);

struct BuildResult {
  int exitCode = -1;
  bool cancelled = false;
  bool startFailed = false;
  std::string error;  // start failure
  long elapsedMs = 0;
  size_t errors = 0, warnings = 0;  // filled by Editor::build
  bool ok() const { return !cancelled && !startFailed && exitCode == 0; }
};

// Runs one build at a time, streaming merged stdout+stderr. Output is read on a background thread; `post` hands
// callbacks to the owner's main thread (Editor::post). Starting a build cancels the previous one (its done callback
// still fires, with cancelled = true, and it delivers no further lines).
class BuildRunner {
 public:
  using Post = std::function<void(std::function<void()>)>;
  using Lines = std::function<void(std::vector<std::string>)>;  // complete lines, \r\n stripped
  using Done = std::function<void(const BuildResult&)>;

  explicit BuildRunner(Post post);
  ~BuildRunner();  // cancels and joins; may block until the killed process tree closes its pipe
  BuildRunner(const BuildRunner&) = delete;
  BuildRunner& operator=(const BuildRunner&) = delete;

  bool start(const BuildCommand& cmd, Lines onLines, Done onDone, std::string* error = nullptr);
  void cancel();  // non-blocking: kills the process tree
  bool running() const;

 private:
  struct Job;
  void reap(bool all);
  Post post_;
  mutable std::mutex mu_;
  std::shared_ptr<Job> current_;
  std::vector<std::shared_ptr<Job>> retired_;
};

}  // namespace tf
