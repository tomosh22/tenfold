#pragma once
#include <filesystem>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "core/PathUtil.h"
#include "semantic/CompileCommands.h"

namespace tf {

// A workspace is a root directory plus the set of files that belong to it.
// 10x-style: point it at a folder (or a .sln / CMakeLists.txt inside one) and
// it scans everything, honouring ignore rules. Settings live in
// <root>/.tenfold/workspace.json and are optional.
//
// Two kinds (SLN-FILES, user decision of 2026-11-15; docs/ARCHITECTURE.md "Workspace kinds"):
//   * a folder workspace has every file under the root (the rules above);
//   * a solution workspace (a solution is chosen or found in the root, and no compile_commands.json / CMake project
//     takes precedence, as for the compile flags) has exactly the files of its projects wherever they are on disk
//     (every item of every project in all configurations, the project files, solution items, shared items) plus the
//     files of the folders listed in settings().additionalWorkspaceFolders, like 10x's Visual Studio sln workspace.
//     A loose file in the solution folder that no project lists is not a workspace file (it stays visible to the
//     explorer's folder view and "Show All Files": explorerFiles()).
class Workspace {
 public:
  enum class Kind { Folder, Solution };

  struct Settings {
    std::vector<std::string> sourceExtensions = {".cpp", ".cc", ".cxx", ".c", ".cu", ".mm", ".m"};
    std::vector<std::string> headerExtensions = {".h",   ".hpp", ".hh",  ".hxx", ".inl", ".ipp",
                                                 ".tpp", ".cuh", ".def", ".inc", ""};
    std::vector<std::string> otherExtensions = {".txt", ".md", ".json", ".cmake", ".py", ".lua", ".natvis", ".rc"};
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
    // RebuildActiveWorkspace for a custom build: a shell command run in the workspace root. Empty: the detected build
    // system's own rebuild (msbuild /t:Rebuild, cmake --build --clean-first, make clean && make); a custom
    // buildCommand without a rebuildCommand cannot be rebuilt.
    std::string rebuildCommand;
    std::string buildDir;
    bool runCMake = true;  // configure CMake projects to obtain compile commands
    // Path of the MSVC cl.exe used for clangd's --query-driver (.vcxproj/.sln projects).
    // User-overridable; when empty it is discovered and then persisted here.
    std::string msvcToolchain;
    bool watchFiles = true;  // keep index / history / documents in sync with the file system
    // Editor::start() attaches to a Visual Studio that already has the solution open (never launches one), so
    // breakpoints toggled in tenfold reach it. JSON key "debug.attachAtStartup".
    bool debugAttachAtStartup = true;
    // Mirrors 10x's VisualStudioSync: breakpoints and the active solution configuration are kept in sync with Visual
    // Studio. Off: no breakpoint sync, no configuration sync, no attach at startup (commands still work). JSON key
    // "debug.visualStudioSync".
    bool visualStudioSync = true;
    // Typing undo policy (OPEN_QUESTIONS #3): "word" joins consecutive typed characters into one undo step
    // (ended by whitespace, a pause, or a caret move); "char" makes every typed character its own step.
    std::string undoGrouping = "word";
    int undoGroupPauseMs = 1000;  // "word" mode: a longer pause between keystrokes starts a new step
    // The solution this workspace was opened with (JSON key "solution"; SLN-OPEN, docs/VS_BEHAVIOUR.md section 16): a
    // path relative to the workspace root (generic separators) or absolute, to a .sln or .slnx. When it names an
    // existing file it is THE solution: solutionFiles() returns only it, so compile flags, the Solution Explorer tree,
    // Build and the Visual Studio debugger all use it even when the folder holds several. Empty: every root-level
    // .sln feeds the flags and the first drives the rest (the behaviour before the key existed).
    std::string solution;
    // 10x's AdditionalWorkspaceFolders (workspace.json "additionalWorkspaceFolders", an array of strings or one comma
    // separated string, and the spelling "AdditionalWorkspaceFolders"): extra folders of a SOLUTION workspace, as
    // wildcard paths such as "MyFolder\\**\\*.*" (`**` = any depth, `*` / `?` within a name; relative paths are
    // relative to the root, absolute ones are kept). A folder written without a wildcard means everything below it.
    // Files take the folder rules of a folder workspace (ignored directories, the extension lists). Ignored by a
    // folder workspace. See parseFolderRule().
    std::vector<std::string> additionalWorkspaceFolders;
  };

  explicit Workspace(std::filesystem::path root);
  ~Workspace();
  Workspace(Workspace&&) noexcept;
  Workspace& operator=(Workspace&&) noexcept;

  const std::filesystem::path& root() const { return root_; }
  std::filesystem::path cacheDir() const { return root_ / ".tenfold"; }
  Settings& settings() { return settings_; }
  const Settings& settings() const { return settings_; }
  bool loadSettings(std::string* error = nullptr);
  bool saveSettings(std::string* error = nullptr) const;

  // Enumerate files. Blocking; cheap enough for ~100k files (a solution workspace also reads every project: about
  // 1 ms per project and configuration). Decides the kind.
  void scan(std::function<void(size_t)> progress = {});
  Kind kind() const { return kind_; }
  // Safe from any thread (the engine's workers ask); kind() is for the editor thread.
  bool isSolutionWorkspace() const;
  // A workspace with this one's root and settings and no files: `scan()` it on a worker thread, then hand it to
  // adoptScan() on the editor thread (the re-read after a project file changed must not block the UI).
  Workspace scanCopy() const;
  // Takes the file lists, kind and watch roots of `scanned` (a scanCopy() that was scanned); bumps revision() when
  // anything differs from the current lists. Returns the files that were listed before and are not now, and the
  // ones that are new.
  struct Changes {
    std::vector<std::filesystem::path> removed, added;
    bool any() const { return !removed.empty() || !added.empty(); }
  };
  Changes adoptScan(Workspace&& scanned);
  const std::vector<std::filesystem::path>& sourceFiles() const { return sources_; }
  const std::vector<std::filesystem::path>& headerFiles() const { return headers_; }
  const std::vector<std::filesystem::path>& otherFiles() const { return others_; }
  std::vector<std::filesystem::path> codeFiles() const;  // sources + headers
  // Every workspace file (sources, headers, others): in a solution workspace the files of the projects and additional
  // folders, wherever they are.
  std::vector<std::filesystem::path> allFiles() const;
  // What the explorer's folder view and Show All Files list: the workspace files plus, in a solution workspace, the
  // loose files below the root (found by the folder rules, listed by no project). Outside-root files may be in it.
  std::vector<std::filesystem::path> explorerFiles() const;
  // Safe from any thread. Whether the fast index should hold `file`: a folder workspace says yes (the caller filters by
  // extension); a solution workspace says yes for a file of a project (listed or not yet created) and for one an
  // additional folder rule covers.
  bool isMember(const std::filesystem::path& file) const;
  // Safe from any thread: path keys (pathKeyUtf8 of directories, no trailing slash) of the root and of every directory
  // that holds workspace files outside it. A file whose key starts with one of them plus '/' is "the workspace's own"
  // (symbol search lists those first).
  std::vector<std::string> memberRootKeys() const;
  // Editor thread. True when a change to `file` can change the file list: the solution, a project, its .filters, a
  // .props / .targets / .vcxitems one of them imports, Directory.Build.*. False for a folder workspace.
  bool isSolutionInput(const std::filesystem::path& file) const;
  // Directories the file watcher must watch besides the root: the directory of each project outside the root
  // (recursive), the other directories that hold workspace files or inputs outside the root, and the additional
  // folders. No directory is below another root or below the root (recursive ones cover them).
  struct WatchRoot {
    std::filesystem::path dir;
    bool recursive = true;
  };
  const std::vector<WatchRoot>& extraWatchRoots() const { return watchRoots_; }
  // Bumped by every scan() and by every incremental change that altered a list; the workspace explorer polls it.
  uint64_t revision() const { return revision_; }
  size_t fileCount() const { return sources_.size() + headers_.size() + others_.size(); }

  // Incremental maintenance of the file lists (used by the file watcher; call
  // from the editor thread). Paths that are not workspace files are ignored.
  // Each returns true if a list changed.
  bool noteFileAdded(const std::filesystem::path& file);
  // Lists `dir` from disk now and notes every file in it (returns the ones that were new). For the editor's own,
  // synchronous file operations (explorer move / copy of a folder). Not for file-watcher events: those are handled
  // late, and a late listing re-adds files that were already deleted again (the churn race fixed in CI-W4).
  std::vector<std::filesystem::path> noteTreeAdded(const std::filesystem::path& dir);
  bool noteFileRemoved(const std::filesystem::path& file);
  bool noteFileRenamed(const std::filesystem::path& from, const std::filesystem::path& to);
  // Every listed file at or below `dir`.
  std::vector<std::filesystem::path> filesUnder(const std::filesystem::path& dir) const;

  bool isSource(const std::filesystem::path& p) const;
  bool isHeader(const std::filesystem::path& p) const;
  bool isIgnoredDir(const std::filesystem::path& dir) const;

  // Build-system discovery. The chosen solution (settings().solution) alone when it exists, else every .sln in the
  // root.
  std::vector<std::filesystem::path> solutionFiles() const;
  // The absolute path of settings().solution when it is set and the file exists, else empty.
  std::filesystem::path chosenSolution() const;
  // Chooses `sln` (absolute, or relative to the root; must be an existing .sln / .slnx file): stores it in settings and
  // writes workspace.json, unless it is already the stored choice. An empty path forgets the choice. False (error
  // set) for a missing file or another extension, or when workspace.json cannot be written (the choice stays set in
  // memory then).
  bool setSolution(const std::filesystem::path& sln, std::string* error = nullptr);
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
  struct Shared;  // what other threads read: membership keys and folder rules
  void walkTree(const std::filesystem::path& start, const std::function<void(const std::filesystem::path&)>& onFile,
                size_t maxDepth = 256) const;
  void scanSolution(const std::function<void(size_t)>& progress);
  Kind detectKind() const;
  void listFile(const std::filesystem::path& p, bool anyExtension, bool* added = nullptr);
  void publishShared();
  std::filesystem::path root_;
  Settings settings_;
  Kind kind_ = Kind::Folder;
  std::vector<std::filesystem::path> sources_, headers_, others_;
  std::vector<std::filesystem::path> loose_;  // solution workspace: files below the root that no project lists
  std::vector<std::string> expectedKeys_;     // solution workspace: pathKeys of project items that do not exist (yet)
  std::set<std::string> inputKeys_;
  std::vector<WatchRoot> watchRoots_;
  std::vector<std::string> memberRootKeys_;
  std::unique_ptr<Shared> shared_;
  uint64_t revision_ = 0;
};

// One entry of AdditionalWorkspaceFolders (10x: "wildcard paths, for example MyFolder\\**\\*.*; 10x will look for
// MyFolder in the workspace root; \\**\\ means recursively add files"), parsed. Pure; `style` selects the case
// rule of the name matching (Windows: case-insensitive) and which absolute spellings count.
struct FolderRule {
  std::filesystem::path base;     // the directory the literal part names, absolute
  std::vector<std::string> rest;  // the pattern below it: "**", names with * and ?; the last is the file-name pattern
  bool recursive = false;         // contains "**": everything below `base` may match
  bool valid = false;
  // True when `file` (absolute, normal) is covered: below `base`, its name path matches `rest`, and no directory on
  // the way is one of the ignored names.
  bool matches(const std::filesystem::path& file, const std::function<bool(const std::string&)>& ignoredDirName,
               PathStyle style = nativePathStyle()) const;
};
FolderRule parseFolderRule(const std::string& entry, const std::filesystem::path& root,
                           PathStyle style = nativePathStyle());
// The entries of a comma separated AdditionalWorkspaceFolders value (blanks trimmed, empty ones dropped).
std::vector<std::string> splitFolderList(const std::string& commaSeparated);

// True for a Visual Studio solution file name: .sln or .slnx (case-insensitive).
bool isSolutionFile(const std::filesystem::path& p);

// What a command-line / open-dialog / drop argument means (SLN-OPEN): a directory is a folder workspace; a .sln /
// .slnx file is a workspace rooted at its folder with that solution chosen; any other existing file (a .vcxproj,
// CMakeLists.txt, ...) is a workspace rooted at its folder with no solution choice. `root` is absolute and lexically
// normal; `file` is the argument itself when it was a file (empty for a directory). A path that does not exist is
// treated as a directory.
struct WorkspaceTarget {
  std::filesystem::path root;
  std::filesystem::path solution;  // absolute; empty unless the argument was a .sln / .slnx file
  std::filesystem::path file;
};
WorkspaceTarget resolveWorkspaceTarget(const std::filesystem::path& arg);

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

// `raw` made absolute with symlinks and "." / ".." resolved (what macOS's _NSGetExecutablePath result needs before
// its parent directory is meaningful). Pure; usable on any platform.
std::filesystem::path canonicalExecutablePath(const std::filesystem::path& raw);
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
