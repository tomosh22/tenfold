#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ui/widgets/Widget.h"

namespace tf::ui {

// One entry of a ContextMenu (W8f). A separator has no label; an item with `sub` opens a submenu instead of running
// `action`. The shortcut is display text only (10x notation, "Control Shift F").
struct MenuItem {
  std::string label;
  std::string shortcut;
  bool enabled = true;
  bool separator = false;
  std::vector<MenuItem> sub;
  std::function<void()> action;

  static MenuItem sep() {
    MenuItem m;
    m.separator = true;
    return m;
  }
};

// A context menu on the top layer. Shown with ContextMenu::open, which makes it the modal widget (full-window, owns
// the keys and the mouse, previous focus restored on close). Keys: Up / Down move over the enabled items (wrapping),
// Home / End jump to the first / last, Right opens a submenu, Left closes one, Enter / Space runs the item, Escape
// closes the innermost menu, a typed letter selects the next enabled item whose label starts with it. Mouse: hovering
// selects (and opens a submenu), a left press runs the item, a press outside every menu closes. The action runs after
// the menu has closed. Placement: below the anchor rectangle (the mouse point, the caret), above it when there is no
// room, then clamped to the window; a submenu opens to the right of its parent item or to the left when it does not
// fit.
class ContextMenu : public Widget {
 public:
  explicit ContextMenu(std::vector<MenuItem> items);

  // Shows the menu on `root`, `anchor` in window pixels (a zero-size rect is a point). `keyboard`: the first enabled
  // item starts selected. Returns the widget (owned by the tree) or null for an empty menu.
  static ContextMenu* open(RootWidget& root, std::unique_ptr<ContextMenu> m, const RectF& anchor, bool keyboard);
  void close();  // closes the modal and fires onClosed once; no-op when it is not shown
  void onClosed(std::function<void()> fn) { closed_ = std::move(fn); }

  const std::vector<MenuItem>& items() const { return items_; }
  size_t depth() const { return levels_.size(); }  // open menus: 1 = the top level only
  int selected(size_t level = 0) const { return level < levels_.size() ? levels_[level].sel : -1; }
  void setSelected(int i, size_t level = 0);
  // Geometry in window pixels (valid once the menu is shown and the root has a size).
  RectF menuRect(size_t level = 0) const;
  RectF itemRect(size_t level, int i) const;

  bool onEvent(const InputEvent& e) override;
  bool acceptsFocus() const override { return true; }

 protected:
  void paint(PaintContext& ctx) override;

 private:
  struct Level {
    const std::vector<MenuItem>* items;
    int sel = -1;
    RectF anchor;  // top level: the anchor passed to open(); a submenu: its parent item
  };
  struct Metrics {
    float rowH, sepH, pad, vpad, width;
  };
  Metrics metrics(const std::vector<MenuItem>& items) const;
  float heightOf(const std::vector<MenuItem>& items, const Metrics& m) const;
  RectF rectOf(size_t level) const;
  bool hit(float x, float y, size_t& level, int& item) const;
  int step(size_t level, int from, int dir) const;
  void openSub(size_t level);
  void truncate(size_t depth);
  void activate(size_t level, int i);
  const MenuItem* itemAt(size_t level, int i) const;

  std::vector<MenuItem> items_;
  std::vector<Level> levels_;
  RectF anchor_{0, 0, 0, 0};
  bool closedFired_ = false;
  std::function<void()> closed_;
};

}  // namespace tf::ui
