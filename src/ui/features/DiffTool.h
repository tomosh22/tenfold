#pragma once
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "lsp/Process.h"

namespace tf::ui {

// External diff tool (C3c, Vcs.DiffFileExternal): writes the base (committed) text and the current buffer to temp
// files that keep the original file name and extension (so the tool can colour them), starts the configured program on
// them and leaves it running on its own. Settings: `vcs.diffTool` (empty = auto: p4merge) and `vcs.diffToolArgs`.
//
// Argument template: split like a command line (double quotes group, no escapes), then every token has its
// placeholders replaced: {base} {current} (the temp files), {file} (the real path), {baseTitle} {currentTitle} (labels
// such as "src/a.cpp#have" / "src/a.cpp (unsaved)"). Substituting after the split keeps paths with spaces in one
// argument. When the template is the default one and the tool is p4merge, `-nl <baseTitle> -nr <currentTitle>` is put
// in front, which p4merge shows as the names of the two sides.
//
// The program runs through tf::Process; the object is kept (never killed on exit) so the program outlives its
// launcher where the platform lets it. tick() collects finished programs, reports a program that failed to start
// (exit code 127 / 126) and removes the temp files of programs that ran for a while; files of programs that returned at
// once (a launcher that hands over to a running instance) stay until the next sweep (older than a day) so the running
// instance can still read them.
class DiffTool {
 public:
  struct Request {
    std::filesystem::path file;  // the real file (absolute)
    std::string title;           // workspace-relative name used in the titles
    std::string baseText;        // committed text, line endings as they should appear in the temp file
    std::string currentText;     // the buffer
    std::string baseLabel = "#have";
    std::string currentLabel = " (unsaved)";
  };
  struct Config {
    std::string tool;  // vcs.diffTool; empty = auto
    std::string args;  // vcs.diffToolArgs; empty = the default
  };
  struct Launched {
    bool ok = false;
    std::string error;  // why not (no tool found, temp files, start failure)
    std::string exe;    // resolved program
    std::vector<std::string> args;
    std::filesystem::path baseFile, currentFile;
  };

  DiffTool();
  ~DiffTool();
  DiffTool(const DiffTool&) = delete;
  DiffTool& operator=(const DiffTool&) = delete;

  Launched launch(const Request& r, const Config& c);
  // Collects finished programs; `onError` (if set) gets "diff tool exited with code N" for a failed start.
  void tick();
  size_t running() const { return running_.size(); }
  std::function<void(const std::string&)> onError;

  // Where temp files go (default: <system temp>/tenfold-diff) and how long a program must have run before its files
  // are removed on exit (default 3000 ms; tests use 0).
  void setTempRoot(std::filesystem::path p) { root_ = std::move(p); }
  void setKeepMs(int ms) { keepMs_ = ms; }
  // Search this PATH instead of the environment's (tests: no tool installed).
  void setPathOverride(std::string path) {
    pathOverride_ = std::move(path);
    usePathOverride_ = true;
  }

  // ---- pieces, public for tests ----
  // The program to start. `configured` empty = auto-detect: on Windows p4merge.exe in `pathVar`, then
  // "<programFiles>\Perforce\p4merge.exe"; elsewhere p4merge in `pathVar`. A configured name without a directory is
  // searched in `pathVar`, one with a directory is used as it is when the file exists. Empty result: `error` says why.
  static std::string resolveTool(const std::string& configured, const std::string& pathVar,
                                 const std::string& programFiles, std::string* error);
  static std::string resolveTool(const std::string& configured, std::string* error);  // PATH / ProgramFiles from env
  // Command-line splitting (see above) and the repair of a template that lost its outer quotes in the settings file.
  static std::vector<std::string> splitArgs(const std::string& s);
  static std::string defaultArgs() { return "\"{base}\" \"{current}\""; }
  static bool isP4Merge(const std::string& exe);

 private:
  struct Running {
    std::unique_ptr<Process> proc;
    std::filesystem::path dir;
    std::chrono::steady_clock::time_point started;
  };
  void sweepOld();

  std::filesystem::path root_;
  int keepMs_ = 3000;
  std::string pathOverride_;
  bool usePathOverride_ = false;
  std::vector<Running> running_;
  bool swept_ = false;
};

}  // namespace tf::ui
