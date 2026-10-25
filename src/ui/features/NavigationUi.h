#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "app/Editor.h"
#include "ui/shell/Workbench.h"

namespace tf::ui {

// B6: Back / Forward buttons in a thin bar above the Workbench, the history dropdown and the "← 3 | 1 →" status
// text. The buttons never call the Editor to navigate: they run the Nav.Back / Nav.Forward commands through
// `Hooks::run` (EditorApp's command path), so key bindings, thumb buttons and the buttons behave identically. Only
// the dropdown talks to the Editor (Editor::goToHistoryEntry), because "jump to entry N" is not a command.
//
// Input: left click = command; press and hold for kLongPressMs (driven by tick()) or right-click opens the dropdown,
// which lists NavigationHistory::entries() (Back: the current entry and everything older, newest first; Forward: the
// current entry and everything newer). The current entry is marked. Click = jump, Escape / click elsewhere = close.
// Time is owned by the caller: EditorApp passes its monotonic millisecond clock to setNow() / tick().
// NOTE: the dropdown is a private minimal list; replace it with B5a's ListPopup once that is on main.
class NavigationUi {
 public:
  enum class Which { Back, Forward };
  struct Hooks {
    std::function<bool(const std::string& commandId)> run;              // runs a command; false = failed / disabled
    std::function<std::string(const std::string& commandId)> shortcut;  // "Alt+Left"; empty = unbound
  };
  struct Row {
    std::string text;    // symbol label recorded by Editor::gotoLocation, else "file:line"
    std::string detail;  // "file:line" when text is a label, else empty
    size_t index = 0;    // into NavigationHistory::entries()
    bool current = false;
  };

  static constexpr int kLongPressMs = 400, kTooltipMs = 500, kMaxRows = 20;

  NavigationUi(Workbench& bench, Editor& editor, Hooks hooks);
  ~NavigationUi();
  NavigationUi(const NavigationUi&) = delete;
  NavigationUi& operator=(const NavigationUi&) = delete;

  void setNow(int64_t nowMs) { now_ = nowMs; }
  // Long-press / tooltip timers and repaint-on-state-change. Returns ms until the next tick is wanted (-1 = none).
  int tick(int64_t nowMs);

  bool enabled(Which w) const;
  std::string tooltip(Which w) const;                            // "Navigate Backward (Alt+Left)"
  const std::string& shownTooltip() const { return shownTip_; }  // what is on screen now (after the hover delay)
  RectF buttonRect(Which w) const;

  // "← 3 | 1 →": steps available in each direction.
  std::string countsText() const;
  void setShowCounts(bool on) { showCounts_ = on; }  // the nav.statusCounts setting (default off)
  bool showCounts() const { return showCounts_; }

  void openDropdown(Which w);  // no-op while that direction is empty
  void closeDropdown();
  bool dropdownOpen() const { return open_; }
  Which dropdownWhich() const { return which_; }
  const std::vector<Row>& rows() const { return rows_; }
  RectF rowRect(size_t i) const;
  void chooseRow(size_t i);  // jump to rows()[i]

 private:
  class Toolbar;
  class Dropdown;
  size_t currentIndex() const;  // entries() index representing the current position (history must be non-empty)
  void buildRows();

  Workbench& bench_;
  Editor& editor_;
  Hooks hooks_;
  Toolbar* bar_ = nullptr;
  Dropdown* drop_ = nullptr;
  float barH_ = 0;
  int64_t now_ = 0;
  bool showCounts_ = false;
  bool open_ = false;
  Which which_ = Which::Back;
  std::vector<Row> rows_;
  RectF listRect_{0, 0, 0, 0};
  float rowH_ = 0;
  std::string shownTip_;
  Widget* prevFocus_ = nullptr;
  bool lastBack_ = false, lastFwd_ = false;
};

}  // namespace tf::ui
