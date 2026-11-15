#pragma once
#include <functional>
#include <string>
#include <vector>

#include "ui/widgets/ContextMenu.h"

namespace tf::ui {

// The menu bar across the top of the window (MENUBAR task; docs/VS_BEHAVIOUR.md section 17): a row of titles ("File",
// "Edit", "View"). Clicking a title opens its drop-down (a ContextMenu built when it opens, so labels, key texts and
// disabled states are current); with a menu open, moving across the titles switches menus, Left / Right at the edge of
// a drop-down do too, Escape or a click elsewhere closes it, a click on the open title closes it. Alt alone (the
// application calls focus()) highlights the first title and shows the access keys underlined; Left / Right move,
// Enter / Down open, Escape or any other key leaves, and an access key opens its menu. The bar draws nothing and takes
// no room while it has no menus. `hidden` (10x's HideMainMenu): no room either, the bar is only drawn, over the top of
// the window, while it is revealed (pointer at the top edge, Alt, an open menu).
class MenuBar : public Widget {
 public:
  struct Menu {
    std::string title;                             // "File"
    char access = 0;                               // the underlined letter of the title ('F')
    std::function<std::vector<MenuItem>()> build;  // called each time the menu opens
  };
  void setMenus(std::vector<Menu> menus);
  const std::vector<Menu>& menus() const { return menus_; }
  bool hasMenus() const { return !menus_.empty(); }

  void setHidden(bool hidden);
  bool hidden() const { return hidden_; }
  // The height the layout reserves: 0 without menus or while hidden.
  float reservedHeight() const;
  float barHeight() const;  // the height the bar has when drawn
  // Reveal / hide the bar of a hidden bar (the pointer at the top edge); ignored while the bar is in use.
  void reveal(bool on);
  bool revealed() const { return !hidden_ || revealed_ || active(); }

  RectF titleRect(int i) const;  // window coordinates
  int titleAt(float x, float y) const;

  // State: -1 when none. `open` is the menu whose drop-down is shown, `focus` the title highlighted for the keyboard.
  int openIndex() const;
  int focusIndex() const { return open_ >= 0 ? openIndex() : focus_; }
  bool active() const { return focus_ >= 0 || openIndex() >= 0; }
  bool showAccess() const { return active(); }
  ContextMenu* dropdown() const;  // the open drop-down, null when none

  void open(int i, bool keyboard);
  void openByAccess(char letter);  // 'f' / 'F': false-y no-op when no menu has that access key
  bool hasAccess(char letter) const;
  void focus(int i = 0);  // keyboard focus on a title without a drop-down
  void leave();           // closes the drop-down and gives up the keyboard focus
  // Keys while the bar has the focus without a drop-down; true when consumed. Any key it does not know leaves the bar.
  bool handleKey(const InputEvent& e);

  std::function<void()> onActiveChanged;  // the active state changed (the application redraws, restores focus)

  bool onEvent(const InputEvent& e) override;
  void onHover(bool entered) override;

 protected:
  void paint(PaintContext& ctx) override;

 private:
  bool outside(const InputEvent& e);
  void setActive();

  std::vector<Menu> menus_;
  bool hidden_ = false, revealed_ = false;
  int focus_ = -1, open_ = -1, hot_ = -1;
  ContextMenu* menu_ = nullptr;
  bool wasActive_ = false;
};

}  // namespace tf::ui
