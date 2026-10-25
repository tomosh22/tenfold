#pragma once
#include <filesystem>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include "semantic/CompileCommands.h"

namespace tf {

// A workspace is a root directory plus the set of files that belong to it.
// 10x-style: point it at a folder (or a .sln / CMakeLists.txt inside one) and
// it scans everything, honouring ignore rules. Settings live in
// <root>/.tenfold/workspace.json and are optional.
class Workspace {
 public:
  struct Settings {
    std::vector<std::string> sourceExtensions = {".cpp", ".cc", ".cxx", ".c", ".cu", ".mm", ".m"};
    std::vector<std::string> headerExtensions = {".h", ".hpp", ".hh", ".hxx", ".inl", ".ipp", ".tpp", ".cuh", ""};
    std::vector<std::string> otherExtensions = {".txt", ".md",     ".json", ".cmake", ".py",
                                                ".lua", ".natvis", ".def",  ".rc"};
    std::set<std::string> ignoreDirs = {".git",  ".svn",   ".hg",      ".vs",          ".vscode",
                                        ".idea", ".cache", ".tenfold", "node_modules", "__pycache__"};
    // Build-output directories. ignoreDirNames match the whole directory name (case-insensitive); ignoreDirPrefixes
    // are conventional patterns ("build-x64", "cmake-build-debug"). A folder such as "output" or "binding" is scanned.
    std::vector<std::string> ignoreDirNames = {"build", "out", "bin", "obj"};
    std::vector<std::string> ignoreDirPrefixes = {"build-", "cmake-build"};
    bool followSymlinks = false;
    std::string cppStandard = "c++20";
    std::string clangdPath;                    // empty: search PATH
    std::vector<std::string> clangdExtraArgs;  // appended to the default clangd command line
    // The active solution configuration|platform (workspace.json "vsConfiguration" / "vsPlatform"; the build uses
    // the same values). When the solution does not list it, the effective one falls back to Debug|x64 and then to
    // the first solution configuration (see Workspace::activeVsConfiguration).
    std::string vsConfiguration = "Debug";
    std::string vsPlatform = "x64";
    // Build integration (Editor::build). vsConfiguration / vsPlatform above are shared with the build so clangd and
    // the build agree. Empty buildCommand: pick by project type (msbuild / cmake --build / make). A non-empty one runs
    // through the shell (sh -c / cmd /c) in the workspace root. buildDir: CMake build directory (relative to the
    // root); empty uses the directory of the compile database, else .tenfold/cmake-build.
    std::string buildCommand;
    std::string buildDir;
    bool runCMake = true;  // configure CMake projects to obtain compile commands
    // Path of the MSVC cl.exe used for clangd's --query-driver (.vcxproj/.sln projects).
    // User-overridable; when empty it is discovered and then persisted here.
    std::string msvcToolchain;
    bool watchFiles = true;  // keep index / history / documents in sync with the file system
    // Editor::start() attaches to a Visual Studio that already has the solution open (never launches one), so
    // breakpoints toggled in tenfold reach it. JSON key "debug.attachAtStartup".
    bool debugAttachAtStartup = true;
    // Typing undo policy (OPEN_QUESTIONS #3): "word" joins consecutive typed characters into one undo step
    // (ended by whitespace, a pause, or a caret move); "char" makes every typed character its own step.
    std::string undoGrouping = "word";
    int undoGroupPauseMs = 1000;  // "word" mode: a longer pause between keystrokes starts a new step
  };

  explicit Workspace(std::filesystem::path root);

  const std::filesystem::path& root() const { return root_; }
  std::filesystem::path cacheDir() const { return root_ / ".tenfold"; }
  Settings& settings() { return settings_; }
  const Settings& settings() const { return settings_; }
  bool loadSettings(std::string* error = nullptr);
  bool saveSettings(std::string* error = nullptr) const;

  // Enumerate files. Blocking; cheap enough for ~100k files.
  void scan(std::function<void(size_t)> progress = {});
  const std::vector<std::filesystem::path>& sourceFiles() const { return sources_; }
  const std::vector<std::filesystem::path>& headerFiles() const { return headers_; }
  const std::vector<std::filesystem::path>& otherFiles() const { return others_; }
  std::vector<std::filesystem::path> codeFiles() const;  // sources + headers
  size_t fileCount() const { return sources_.size() + headers_.size() + others_.size(); }

  // Incremental maintenance of the file lists (used by the file watcher; call
  // from the editor thread). Paths that are not workspace files are ignored.
  // Each returns true if a list changed.
  bool noteFileAdded(const std::filesystem::path& file);
  bool noteFileRemoved(const std::filesystem::path& file);
  bool noteFileRenamed(const std::filesystem::path& from, const std::filesystem::path& to);
  // Every listed file at or below `dir`.
  std::vector<std::filesystem::path> filesUnder(const std::filesystem::path& dir) const;

  bool isSource(const std::filesystem::path& p) const;
  bool isHeader(const std::filesystem::path& p) const;
  bool isIgnoredDir(const std::filesystem::path& dir) const;

  // Build-system discovery.
  std::vector<std::filesystem::path> solutionFiles() const;
  // Visual Studio solution configurations of the workspace's .sln files (union, file order, no duplicates). Reads the
  // files on every call; empty without a solution.
  std::vector<compdb::SolutionConfiguration> vsConfigurations() const;
  // The configuration compile commands are generated for: settings().vsConfiguration|vsPlatform when a solution
  // lists it, else the closest match (compdb::pickSolutionConfiguration). Without a solution, the settings as is.
  compdb::SolutionConfiguration activeVsConfiguration() const;
  // Select a configuration by name ("Debug|x64"; a bare "Debug" picks that configuration's first platform). Fails
  // (error lists the available ones) when a solution exists and does not list it. On success updates settings and
  // saves workspace.json; the caller regenerates compile commands (Editor::selectVsConfiguration does).
  bool setVsConfiguration(const std::string& name, std::string* error = nullptr);
  bool hasCMake() const;

 private:
  std::filesystem::path root_;
  Settings settings_;
  std::vector<std::filesystem::path> sources_, headers_, others_;
};

std::string findExecutable(const std::string& name);
// preferBundled: look in the editor's own bin/ before PATH (used when the user
// has not configured an explicit executable path). Otherwise bin/ is a fallback.
std::string findExecutable(const std::string& name, bool preferBundled);

// Pure pieces of executable lookup, testable on any host.
// Split a PATH-style string on `sep`, dropping empty entries.
std::vector<std::string> splitPathList(const std::string& pathList, char sep);
// File names to try for `name`: on Windows the PATHEXT-style extensions are
// appended unless `name` already ends in one of them.
std::vector<std::string> executableCandidates(const std::string& name, bool windows,
                                              const std::vector<std::string>& exts = {".exe", ".cmd", ".bat"});
// The editor's bin/ directory next to the running executable ("" if unknown).
std::filesystem::path bundledBinDir();
// Search `dirs` in order for any candidate; returns the first existing file.
std::string findExecutableIn(const std::string& name, const std::vector<std::string>& dirs, bool windows);

// Directory holding the running executable ("" if unknown).
std::filesystem::path executableDir();
// Where a packaged build keeps helper executables, in search order: the executable's own directory (the packaged
// layout is <prefix>/bin/{tenfold-ui,tenfold-cli,clangd}) then <exe dir>/bin.
std::vector<std::string> bundledSearchDirs(const std::filesystem::path& exeDir);

// clangd discovery. Precedence: an explicit setting (path or name; no fallback when it does not resolve) > clangd
// bundled with the editor (`bundledDirs`) > PATH (`pathList`, `sep`-separated). Pure; findClangd() feeds it the real
// process environment.
struct ClangdChoice {
  std::string path;    // resolved executable, "" when none was found
  std::string source;  // "setting" | "bundled" | "PATH" | ""
};
ClangdChoice resolveClangd(const std::string& setting, const std::vector<std::string>& bundledDirs,
                           const std::string& pathList, char sep, bool windows);
ClangdChoice findClangd(const std::string& setting);

}  // namespace tf
