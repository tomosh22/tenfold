#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ui/widgets/TabStrip.h"
#include "ui/widgets/Widget.h"

namespace tf::ui {

// What a tree node looks like (icon) and means. The tree itself does not care.
enum class TreeKind : uint8_t { Root, Solution, Project, Folder, File };

struct TreeNode {
  std::string name;
  std::string path;  // file-system path (generic form) of a file / folder / project; empty for virtual nodes
  int32_t parent = -1;
  TreeKind kind = TreeKind::File;
  std::vector<int32_t> kids;
  bool expandable() const { return !kids.empty(); }
};

// A rooted forest-less tree stored flat. Nodes must be added parent first (a child's id is greater than its parent's);
// node 0 is the root. Pure data: unit-testable without a window.
class TreeModel {
 public:
  int add(int parent, std::string name, TreeKind kind, std::string path = {});
  // Orders the children of `id` (folders and projects first, then files; case-insensitive by name) unless `cmp` is
  // given.
  void sortKids(int id);
  void sortAll();  // sortKids for every node
  size_t size() const { return nodes_.size(); }
  bool empty() const { return nodes_.empty(); }
  const TreeNode& node(int id) const { return nodes_[static_cast<size_t>(id)]; }
  TreeNode& node(int id) { return nodes_[static_cast<size_t>(id)]; }
  // First node carrying `path` (compared as tf::pathKey, so case-insensitive on Windows; needs indexPaths()); -1 when
  // none.
  int findPath(const std::string& path) const;
  void indexPaths();
  int depthOf(int id) const;
  // "Source/dir/file.cpp"-style key made of the names from the root's child down; stable across rebuilds.
  std::string keyOf(int id) const;

 private:
  std::vector<TreeNode> nodes_;
  std::unordered_map<std::string, int> byPath_;
};

// A virtualised tree view (the workspace explorer's widget): only the rows inside the viewport are painted, rows are
// a flat list rebuilt on expand / collapse / filter (50k files in a few ms). Keyboard: Up/Down/PageUp/PageDown/Home/
// End select, Right expands (or enters the first child), Left collapses (or goes to the parent), Enter activates a
// file / toggles a folder, typing filters (fuzzy, every space-separated token must match a node name; matching nodes
// and their ancestors stay, expanded), Backspace edits the filter, Escape clears it or, when empty, calls onClose.
// Mouse: click selects (on the arrow it toggles), double click activates / toggles, wheel and the scrollbar scroll.
class TreeView : public Widget {
 public:
  using Mark = TabStrip::VcsMark;
  struct Row {
    int32_t node;
    int32_t depth;
  };

  TreeView() = default;

  // Replaces the content. Expansion and selection are carried over by node key; a first model gets its root expanded.
  void setModel(TreeModel&& m);
  const TreeModel& model() const { return model_; }

  const std::vector<Row>& rows() const { return rows_; }
  int selectedNode() const { return selected_; }
  // The node whose row contains the window point, -1 over no row.
  int nodeAt(float x, float y) const {
    const int r = rowAtY(y);
    return r >= 0 && x >= bounds().x && x < bounds().x + bounds().w ? rows_[static_cast<size_t>(r)].node : -1;
  }
  int selectedRow() const;  // -1 when the selection is not shown
  void select(int node);    // shows the node (clearing a filter that hides it) and scrolls to it
  bool isExpanded(int node) const {
    return node >= 0 && static_cast<size_t>(node) < expanded_.size() && expanded_[static_cast<size_t>(node)];
  }
  void setExpanded(int node, bool on);
  void toggle(int node) { setExpanded(node, !isExpanded(node)); }
  void expandAll(bool on);
  // Makes the node visible (expanding its ancestors, clearing the filter if it hides it), selects it and scrolls so it
  // is on screen (centred when it was not). Returns false for an invalid node.
  bool reveal(int node);
  // True when `node` is the selection and its row is inside the viewport.
  bool shown(int node) const;

  void setFilter(const std::string& f);
  const std::string& filter() const { return filter_; }

  // Per-node decoration; called for the visible rows only.
  std::function<Mark(const TreeNode&)> markOf;
  std::function<void(int node)> onActivate;  // Enter / double click on a node without children (a file)
  std::function<void(int node)> onSelected;  // the selection changed by the user
  std::function<void(int node)> onClicked;   // a single left click on a row (not on its expander arrow)
  std::function<void()> onClose;             // Escape with an empty filter

  // ---- geometry (window coordinates), for tests ----
  float rowHeight() const;
  RectF headerRect() const;
  RectF listRect() const;
  RectF rowRect(int row) const;  // may lie outside listRect when scrolled away
  RectF arrowRect(int row) const;
  float scrollY() const { return scrollY_; }
  void setScrollY(float y);
  int firstVisibleRow() const;
  int visibleRowCount() const;
  RectF thumbRect() const;  // empty when everything fits

  bool acceptsFocus() const override { return true; }
  bool onEvent(const InputEvent& e) override;
  void onHover(bool entered) override;
  void onFocusChanged(bool gained) override;

 protected:
  void paint(PaintContext& ctx) override;
  void onLayout() override;

 private:
  void rebuildRows();
  void clampScroll();
  void ensureVisible(int row, bool centre = false);
  int rowAtY(float y) const;
  void setSelectedRow(int row, bool user);
  void moveSelection(int delta);
  void collectExpandedKeys(std::unordered_set<std::string>& out) const;
  void applyExpandedKeys(const std::unordered_set<std::string>& keys);
  void activateOrToggle(int node);
  float contentHeight() const { return static_cast<float>(rows_.size()) * rowHeight(); }

  TreeModel model_;
  std::vector<uint8_t> expanded_;
  std::vector<Row> rows_;
  int selected_ = -1;
  std::string filter_;
  float scrollY_ = 0;
  int hoverRow_ = -1;
  bool dragThumb_ = false;
  float dragGrab_ = 0;
  bool focused_ = false;
  float metricLh_ = 16, metricCell_ = 8, metricPad_ = 4;  // cached from the last paint / layout
};

}  // namespace tf::ui
