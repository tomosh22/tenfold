#pragma once
#include <functional>
#include <string>

#include "ui/widgets/TabStrip.h"

namespace tf::ui {

// A TabStrip on top and a content area below hosting one child widget per tab. Only the active tab's content is
// visible (hence laid out and painted); the others are kept alive but hidden. Tab ids are caller-chosen and unique
// within the panel. B3b moves tabs between panels with removeTab() (which hands the content back) + addTab().
class Panel : public Widget {
 public:
  Panel();

  TabStrip& tabs() { return *strip_; }
  // Adds a tab (activating it) with `content` as its page.
  void addTab(int id, std::string title, std::unique_ptr<Widget> content, bool pinned = false);
  // Removes the tab and returns its content (null for an unknown id). The strip picks the MRU tab as the new active.
  std::unique_ptr<Widget> removeTab(int id);
  void setActive(int id);
  int activeId() const { return strip_->activeId(); }
  Widget* content(int id) const;
  Widget* activeContent() const { return content(activeId()); }
  const RectF& contentRect() const { return content_; }

  // Called when the user asks to close a tab ("x" or middle click). Default (empty): the tab is removed and its
  // content destroyed. Set it to veto or confirm (e.g. unsaved changes); call removeTab yourself to close.
  std::function<void(int id)> onCloseRequested;
  std::function<void(int id)> onActivated;  // user activated a tab

 protected:
  void paint(PaintContext& ctx) override;
  void onLayout() override;

 private:
  void syncVisibility();
  TabStrip* strip_;
  struct Page {
    int id;
    Widget* w;
  };
  std::vector<Page> pages_;
  RectF content_{0, 0, 0, 0};
};

}  // namespace tf::ui
