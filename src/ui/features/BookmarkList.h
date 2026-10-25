#pragma once
#include <vector>

#include "ui/features/PickerUtil.h"

namespace tf::ui {

// Bookmark list (Ctrl+Shift+F2, B8): a modal ListPopup of every bookmark of the workspace (path, line and the line's
// text); the input filters by text or path. Enter / click -> Editor::gotoBookmarkItem (navigation history).
class BookmarkList {
 public:
  explicit BookmarkList(PickerEnv& env) : env_(env) {}
  void open();
  void close();
  bool isOpen() const { return popup_ != nullptr; }
  ListPopup* popup() const { return popup_; }
  const std::vector<Bookmarks::Item>& shown() const { return shown_; }

 private:
  void rebuild();

  PickerEnv& env_;
  ListPopup* popup_ = nullptr;
  std::vector<Bookmarks::Item> shown_;  // aligned with the popup rows
};

}  // namespace tf::ui
