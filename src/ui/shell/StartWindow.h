#pragma once
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "ui/settings/RecentSolutions.h"
#include "ui/widgets/InputLine.h"
#include "ui/widgets/Widget.h"

namespace tf::ui {

// The client area of the empty window (START-WINDOW, docs/VS_BEHAVIOUR.md section 20), modelled on Visual Studio 2022's
// Start window but only with what tenfold can do:
//   * left column "Open recent": a "Search recent" box over the recent list (RecentSolutions, newest first). Each row
//     shows the file or folder name and below it the full path; a missing file or folder is still listed, dimmed, with
//     a "missing" badge. The filter is a case-insensitive subsequence match on the name (the pickers' matcher) or a
//     substring of the full path (a subsequence over a long path would match nearly everything). Up / Down / PageUp /
//     PageDown move the selection (the search box keeps the keyboard focus, so typing always filters),
//     Enter or a click opens the row, Delete (with the caret at the end of the search text) or the context menu's
//     "Remove from list" drops it from the list, Escape clears the search.
//   * right column "Get started": "Open a project or solution" and "Open a local folder", the two commands tenfold has.
// The widget only reads and edits the RecentSolutions it is given and calls the hooks; opening is the program's flow
// (WorkspaceOpener) so a click here and File > Recent Projects and Solutions do the same thing.
class StartWindow : public Widget {
 public:
  struct Hooks {
    RecentSolutions* recent = nullptr;
    std::function<void(const std::filesystem::path&)> openRecent;  // a row chosen (existing or missing)
    std::function<void()> openProject;                             // "Open a project or solution"
    std::function<void()> openFolder;                              // "Open a local folder"
  };
  struct Row {
    std::filesystem::path path;
    std::string name, where;  // file / folder name, full path
    std::vector<int> match;   // highlighted bytes of `name`
    bool missing = false, folder = false;
  };
  explicit StartWindow(Hooks h);

  InputLine* search() const { return search_; }
  const std::vector<Row>& rows() const { return rows_; }
  int selected() const { return selected_; }
  void setSelected(int i);
  // Re-reads the recent list and the filter (after an entry was added, dropped or the search text changed).
  void refresh(bool redraw = true);
  // Opens the selected row; false when there is none.
  bool openSelected();
  // Removes the row from the recent list (saved at once); the selection stays at the same position.
  bool removeRow(int i);
  // Delete key before the search box sees it: removes the selected row when the caret is at the end of the text.
  bool preKey(const InputEvent& e);
  // The message for an empty list ("No recent ..."), or for a filter without a match.
  std::string emptyText() const;

  RectF rowRect(int i) const;
  RectF actionRect(int i) const;  // 0 = open project / solution, 1 = open folder
  static constexpr int kActions = 2;

  bool onEvent(const InputEvent& e) override;
  void onHover(bool entered) override;

 protected:
  void onLayout() override;
  void paint(PaintContext& ctx) override;

 private:
  struct Geo {
    float margin, leftX, leftW, rightX, rightW, titleY, searchY, listY, listH, rowH;
  };
  Geo geo() const;
  int rowAt(float x, float y) const;
  int actionAt(float x, float y) const;
  int visibleRows() const;
  void ensureVisible();
  void showMenu(int row, float x, float y);
  void runAction(int i);
  bool sameAsShown() const;

  Hooks hooks_;
  InputLine* search_ = nullptr;
  std::vector<Row> rows_;
  std::vector<std::filesystem::path> snapshot_;
  std::string shownFilter_;
  int selected_ = -1, first_ = 0, hoverRow_ = -1, hoverAction_ = -1;
};

}  // namespace tf::ui
