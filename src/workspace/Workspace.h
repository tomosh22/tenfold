#pragma once
#include <filesystem>
#include <functional>
#include <set>
#include <string>
#include <vector>

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
    std::vector<std::string> ignoreDirPrefixes = {"build", "out", "cmake-build", "bin", "obj"};
    bool followSymlinks = false;
    std::string cppStandard = "c++20";
    std::string clangdPath;                    // empty: search PATH
    std::vector<std::string> clangdExtraArgs;  // appended to the default clangd command line
    std::string vsConfiguration = "Debug";
    std::string vsPlatform = "x64";
    bool runCMake = true;  // configure CMake projects to obtain compile commands
    // Path of the MSVC cl.exe used for clangd's --query-driver (.vcxproj/.sln projects).
    // User-overridable; when empty it is discovered and then persisted here.
    std::string msvcToolchain;
    bool watchFiles = true;  // keep index / history / documents in sync with the file system
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

}  // namespace tf
