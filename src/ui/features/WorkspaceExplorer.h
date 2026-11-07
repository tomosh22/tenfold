#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "app/Editor.h"
#include "ui/features/ExplorerPrompt.h"
#include "ui/shell/Workbench.h"
#include "ui/widgets/ContextMenu.h"
#include "ui/widgets/TreeView.h"

namespace tf::ui {

// What a project file lists (W8f): the files of one .vcxproj (or old-style project) and, when a `.vcxproj.filters`
// file sits next to it, the filter each belongs to.
struct ProjectFile {
  std::string path;    // absolute, generic '/' form, lexically normalised
  std::string filter;  // "Source Files\\Sub" ('\\' separated, as in the filters file); empty = at the project root
  // Configurations the item is excluded from the build in (`<ExcludedFromBuild Condition="...=='Debug|x64'">true`):
  // "Debug|x64", or "*" for an unconditional exclusion.
  std::vector<std::string> excluded;
};
struct ProjectContents {
  std::vector<ProjectFile> files;
  std::vector<std::string> filters;  // every Filter the filters file declares (empty ones included)
  bool hasFiltersFile = false;
  bool loaded = false;  // the project file could be read
};

// The workspace explorer (ToggleWorkspaceExplorer, ShowInWorkspaceTree[WithToggle]): a TreeView hosted by the
// Workbench's left side dock under the name "Explorer". A workspace with a .sln shows the solution -> solution
// folders -> projects -> the project's files (grouped by the .vcxproj.filters filters when the file exists, else by
// the folders on disk); any other workspace shows its folder tree from the workspace scan (so the scan's ignore rules
// apply). Enter / double click on a file opens it through Editor::gotoLocation (a normal tab); a single click opens it
// in the preview tab (Workbench::withPreview, VS-TABS T2) and leaves the keys in the tree. A file's version-control
// state is the tab marker's (vcsTabMark). The model is rebuilt (debounced) when the file lists change, when the
// solution or a project / filters file changes on disk and while the panel is open only; expansion and selection
// survive.
//
// VS-TREE (docs/VS_BEHAVIOUR.md section 11): typing jumps to the next name that starts with the typed letters (Ctrl+;
// filters, TreeView); "Switch Views" toggles Solution view (projects, filters) and Folder view (the folders on disk
// of the workspace scan); "Show All Files" adds, under each project, the files on disk that the project does not list
// (dimmed); the expanded folders, the selection and both toggles are saved in <workspace>/.tenfold/explorer.json and
// restored the first time the tree is built; a workspace with a solution opens the panel by default when no
// layout.json was restored; New File / New Folder / Rename (F2) / Delete (Delete key, "will be deleted permanently"
// confirmation) / drag and drop (move, Ctrl copies; confirmed) work on the disk and keep the `.vcxproj` and
// `.vcxproj.filters` Include attributes of moved, renamed and deleted files in step; Properties is a read-only page.
class WorkspaceExplorer {
 public:
  struct Hooks {
    std::function<void(const std::string&)> message;  // status-bar notice
    std::function<void()> focusEditor;                // give the keys back to the editor view
    PickerEnv* env = nullptr;                         // for the name / confirmation / properties dialogs; null = none
  };
  struct TreeOptions {
    bool folderView = false;  // the folders of the workspace scan instead of the solution
    bool showAll = false;     // Solution view: also the files under each project that it does not list (dimmed)
    std::vector<std::filesystem::path> extraDirs;  // empty folders to show (made from the tree this session)
  };
  static constexpr const char* kPanel = "Explorer";

  WorkspaceExplorer(Editor& editor, Workbench& bench, Hooks hooks);
  ~WorkspaceExplorer();

  // ---- model (pure; no window needed) ----
  // The tree of a workspace: the first root-level .sln (its projects) when it lists any project, else the folder tree.
  static TreeModel buildTree(const Workspace& ws);
  static TreeModel buildTree(const Workspace& ws, const TreeOptions& opts);
  static TreeModel buildFolderTree(const Workspace& ws);
  static TreeModel buildFolderTree(const std::filesystem::path& root, const std::vector<std::filesystem::path>& files,
                                   const std::vector<std::filesystem::path>& extraDirs = {});
  // `unlisted` (Show All Files): the workspace's files; those under a project's folder that the project does not
  // list are added to it, dimmed. `extraDirs` as in TreeOptions.
  static TreeModel buildSolutionTree(const std::filesystem::path& sln,
                                     const std::vector<std::filesystem::path>* unlisted = nullptr,
                                     const std::vector<std::filesystem::path>& extraDirs = {});
  static ProjectContents readProject(const std::filesystem::path& project);

  TreeView* view() const { return view_; }
  bool visible() const { return bench_.sideVisible() && bench_.sidePanelName() == kPanel; }
  bool solutionMode() const { return solutionMode_; }
  bool hasSolution() const;  // the workspace has a .sln that lists projects (Switch Views has something to switch)
  bool folderView() const { return folderView_; }
  bool showAllFiles() const { return showAll_; }
  static std::filesystem::path stateFile(const Workspace& ws);  // <workspace>/.tenfold/explorer.json

  // ---- settings ----
  void setOpenWithSolution(bool on) { openWithSolution_ = on; }  // explorer.openWithSolution
  void setDragDrop(bool on) { dragDrop_ = on; }                  // explorer.dragDrop (10x EnableWorkspaceTreeDragDrop)
  void setPromptBeforeMoving(bool on) { promptMove_ = on; }      // explorer.promptBeforeMoving

  // ---- commands ----
  void toggle();                 // ToggleWorkspaceExplorer
  void show(bool focus = true);  // opens the panel (building the tree), focuses it when asked
  void hide();                   // closes it; the keys go back to the editor when the tree had them
  // ShowInWorkspaceTree: opens the panel when closed, reveals and selects the current file and focuses the tree.
  // `toggle` (ShowInWorkspaceTreeWithToggle): when the tree already shows the file, closes it instead.
  bool showCurrentFile(bool toggle);
  bool reveal(const std::filesystem::path& file);    // false when the tree has no node for the file
  void setTrackActiveFile(bool on) { track_ = on; }  // 10x TrackActiveFileInWorkspaceTree
  void rebuild();                                    // now, keeping expansion and selection

  // ---- VS-TREE ----
  void setFolderView(bool on);  // Switch Views: Folder view (true) / Solution view
  void toggleFolderView() { setFolderView(!folderView_); }
  void setShowAllFiles(bool on);
  void toggleShowAllFiles() { setShowAllFiles(!showAll_); }
  // First start with no restored layout: opens the panel (without the keys) when the workspace has a solution and
  // explorer.openWithSolution is on. Returns whether it opened.
  bool openByDefault();
  bool saveState();  // now (also done debounced after a change, and when the explorer is destroyed)

  // File operations on a node (the dialogs ask first; the operations are not undoable). Each returns false, with a
  // status message, when the node does not allow it; true when the dialog opened or the operation ran.
  bool newFile(int node);
  bool newFolder(int node);
  bool rename(int node);
  bool remove(int node);
  bool properties(int node);
  // Moves (or copies) the file / folder `src` into the folder (or project, or root) `dst`; asks first unless copying
  // or explorer.promptBeforeMoving is off.
  bool move(int src, int dst, bool copy);
  bool canDrop(int src, int dst) const;
  // The tree's context-menu entries for `node`: `head` (New File, New Folder, Rename, Delete) and `tail` (Switch
  // Views, Show All Files, Properties).
  std::vector<MenuItem> operationItems(int node);
  std::vector<MenuItem> viewItems(int node);
  ExplorerPrompt* prompt() const { return prompt_.get(); }

  // Per event batch: notices file-list / solution / VCS changes (the rebuild is debounced to 250 ms) and the active
  // file for tracking. Returns the ms until it wants another call, -1 for none.
  int tick(int64_t nowMs);

  static constexpr int kDebounceMs = 250;
  static constexpr int kPollMs = 300;
  static constexpr int kStateSaveMs = 500;

 private:
  void opened(int node);
  void loadState();
  void markState();
  std::filesystem::path dirFor(int node) const;  // where New File / a drop on `node` puts things; empty = nowhere
  std::string nameProblem(const std::string& name, const std::filesystem::path& dir,
                          const std::filesystem::path& self) const;
  std::vector<std::filesystem::path> projectFiles() const;  // every project of the displayed solution
  bool relocate(const std::filesystem::path& from, const std::filesystem::path& to, bool isDir);
  void erase(const std::filesystem::path& path, bool isDir);
  void closeDocsUnder(const std::filesystem::path& path, bool isDir, bool discard,
                      std::filesystem::path* reopen = nullptr);
  bool dirtyUnder(const std::filesystem::path& path, bool isDir, std::string* which) const;
  void afterChange(const std::filesystem::path& select);
  void say(const std::string& m) const {
    if (hooks_.message) hooks_.message(m);
  }
  void previewed(int node);
  Position caretFor(const std::filesystem::path& file) const;
  uint64_t inputStamp() const;

  Editor& editor_;
  Workbench& bench_;
  Hooks hooks_;
  TreeView* view_ = nullptr;
  bool built_ = false;
  bool solutionMode_ = false;
  bool track_ = false;
  uint64_t revision_ = 0;
  uint64_t stamp_ = 0;
  uint64_t vcsGen_ = 0;
  bool pending_ = false;
  int64_t pendingSince_ = 0;
  int64_t lastPoll_ = -1000000;
  std::string trackedFile_;
  bool folderView_ = false, showAll_ = false;
  bool openWithSolution_ = true, dragDrop_ = true, promptMove_ = true;
  bool stateLoaded_ = false, stateDirty_ = false;
  int64_t stateSince_ = 0, lastTick_ = 0;
  std::vector<std::string> pendingKeys_;
  bool havePending_ = false;
  std::string pendingSel_;
  std::vector<std::filesystem::path> extraDirs_;
  std::unique_ptr<ExplorerPrompt> prompt_;
  std::vector<std::filesystem::path> inputs_;  // .sln / project / filters files watched for changes
};

}  // namespace tf::ui
