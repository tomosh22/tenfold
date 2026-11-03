#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "app/Editor.h"
#include "ui/shell/Workbench.h"
#include "ui/widgets/TreeView.h"

namespace tf::ui {

// What a project file lists (W8f): the files of one .vcxproj (or old-style project) and, when a `.vcxproj.filters`
// file sits next to it, the filter each belongs to.
struct ProjectFile {
  std::string path;    // absolute, generic '/' form, lexically normalised
  std::string filter;  // "Source Files\\Sub" ('\\' separated, as in the filters file); empty = at the project root
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
// apply). Enter / double click on a file opens it through Editor::gotoLocation. A file's version-control state is the
// tab marker's (vcsTabMark). The model is rebuilt (debounced) when the file lists change, when the solution or a
// project / filters file changes on disk and while the panel is open only; expansion and selection survive.
class WorkspaceExplorer {
 public:
  struct Hooks {
    std::function<void(const std::string&)> message;  // status-bar notice
    std::function<void()> focusEditor;                // give the keys back to the editor view
  };
  static constexpr const char* kPanel = "Explorer";

  WorkspaceExplorer(Editor& editor, Workbench& bench, Hooks hooks);
  ~WorkspaceExplorer();

  // ---- model (pure; no window needed) ----
  // The tree of a workspace: the first root-level .sln (its projects) when it lists any project, else the folder tree.
  static TreeModel buildTree(const Workspace& ws);
  static TreeModel buildFolderTree(const Workspace& ws);
  static TreeModel buildFolderTree(const std::filesystem::path& root, const std::vector<std::filesystem::path>& files);
  static TreeModel buildSolutionTree(const std::filesystem::path& sln);
  static ProjectContents readProject(const std::filesystem::path& project);

  TreeView* view() const { return view_; }
  bool visible() const { return bench_.sideVisible() && bench_.sidePanelName() == kPanel; }
  bool solutionMode() const { return solutionMode_; }

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

  // Per event batch: notices file-list / solution / VCS changes (the rebuild is debounced to 250 ms) and the active
  // file for tracking. Returns the ms until it wants another call, -1 for none.
  int tick(int64_t nowMs);

  static constexpr int kDebounceMs = 250;
  static constexpr int kPollMs = 300;

 private:
  void opened(int node);
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
  std::vector<std::filesystem::path> inputs_;  // .sln / project / filters files watched for changes
};

}  // namespace tf::ui
