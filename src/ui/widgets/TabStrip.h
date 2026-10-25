#pragma once
#include <functional>
#include <string>
#include <vector>

#include "ui/widgets/Widget.h"

namespace tf::ui {

// A horizontal strip of tabs. Tabs are identified by caller-chosen int ids.
//
// Behaviour (OPEN_QUESTIONS #5 defaults):
//  * Pinned tabs always come first, have no close button and are never closed by middle-click.
//  * Left press activates (activate callback), press+release on the "x" or middle-click closes (close callback; the
//    strip never removes a tab by itself, the owner calls removeTab). A dirty tab shows "●" in the close slot until the
//    pointer is over the slot.
//  * Drag a tab horizontally to reorder it within its group (pinned / unpinned); the order changes live and the reorder
//    callback fires on release with the final index. Dragging more than Theme::dragOutDistance above or below the
//    strip restores the original order and fires beginDragOut(id, screenX, screenY) once; the mouse capture is released
//    so the owner (B3b) can take over the gesture.
//  * When the tabs are wider than the strip, the strip scrolls (mouse wheel; the active tab is kept visible).
//  * mruOrder() lists tab ids, most recently activated first; the active tab is first. Ctrl+Tab UI is B4/B3b.
// Programmatic calls (addTab, setActive, ...) never fire callbacks; callbacks mean "the user asked for this".
class TabStrip : public Widget {
 public:
  // Version-control state of a tab (C3c): a small marker left of the title. Modified / Added / CheckedOut are filled
  // squares in the theme's tabVcs* colours, ReadOnly (Perforce, not checked out) an outlined one.
  enum class VcsMark { None, Modified, Added, CheckedOut, ReadOnly, Conflict };
  struct Tab {
    int id;
    std::string title;
    bool dirty = false;
    bool pinned = false;
    VcsMark vcs = VcsMark::None;
  };

  std::function<void(int id)> onActivate, onClose;
  std::function<void(int id, int newIndex)> onReorder;
  std::function<void(int id, float x, float y)> onBeginDragOut;

  // Pinned tabs are inserted after the last pinned tab, others are appended. The first tab becomes active.
  void addTab(int id, std::string title, bool pinned = false);
  // Removes the tab; if it was active the most recently used remaining tab becomes active.
  void removeTab(int id);
  bool setActive(int id);  // false for an unknown id
  void setTitle(int id, std::string title);
  void setDirty(int id, bool dirty);
  void setVcsMark(int id, VcsMark m);
  RectF vcsMarkerRect(int id);          // window coordinates; empty without a marker
  void setPinned(int id, bool pinned);  // moves the tab to the end of the pinned group / start of the unpinned group
  // Moves a tab to `index` (clamped to its group).
  void moveTab(int id, int index);

  const std::vector<Tab>& tabs() const { return tabs_; }
  int indexOf(int id) const;
  int activeId() const { return active_; }  // -1 when empty
  const std::vector<int>& mruOrder() const { return mru_; }

  float preferredHeight() const;  // line height + vertical padding; 0 without a root
  float scrollX() const { return scroll_; }
  bool overflowing();
  void scrollBy(float dx);
  // Geometry in window coordinates (after scrolling); empty rect if the id is unknown.
  RectF tabRect(int id);
  RectF closeRect(int id);
  int tabAt(float x, float y);  // id or -1

  bool onEvent(const InputEvent& e) override;
  void onHover(bool entered) override;

 protected:
  void paint(PaintContext& ctx) override;
  void onLayout() override;

 private:
  struct Geo {
    float x, w;         // content coordinates (before scrolling)
    float titleW;       // reserved for the title
    bool slot;          // has a close/dirty slot at the right
    float markW;        // width reserved left of the title for the VCS marker (0 = none)
    std::string shown;  // truncated title
  };
  void ensureGeo();
  float totalWidth();
  void ensureVisible(int id);
  void clampScroll();
  void activate(int id, bool notify);
  void dragTo(float x, float y);
  void endPress();
  bool closable(const Tab& t) const { return !t.pinned; }
  void invalidate() {
    geomValid_ = false;
    markDirty();
  }

  std::vector<Tab> tabs_;
  std::vector<int> mru_;
  std::vector<Geo> geo_;
  bool geomValid_ = false;
  int active_ = -1;
  float scroll_ = 0;
  // interaction
  int hoverTab_ = -1;
  bool hoverClose_ = false;
  int pressTab_ = -1, pressMiddle_ = -1, startIndex_ = 0;
  bool pressClose_ = false, dragging_ = false;
  float pressX_ = 0, pressY_ = 0, grab_ = 0;
};

}  // namespace tf::ui
