#include "ui/widgets/Panel.h"

#include <algorithm>

namespace tf::ui {

Panel::Panel() {
  auto s = std::make_unique<TabStrip>();
  strip_ = s.get();
  addChild(std::move(s));
  strip_->onActivate = [this](int id) {
    syncVisibility();
    if (onActivated) onActivated(id);
  };
  strip_->onClose = [this](int id) {
    if (onCloseRequested) onCloseRequested(id);
    else removeTab(id);
  };
}

void Panel::addTab(int id, std::string title, std::unique_ptr<Widget> content, bool pinned) {
  Widget* w = addChild(std::move(content));
  pages_.push_back({id, w});
  strip_->addTab(id, std::move(title), pinned);
  strip_->setActive(id);
  syncVisibility();
}

std::unique_ptr<Widget> Panel::removeTab(int id) {
  auto it = std::find_if(pages_.begin(), pages_.end(), [id](const Page& p) { return p.id == id; });
  if (it == pages_.end()) return nullptr;
  Widget* w = it->w;
  pages_.erase(it);
  strip_->removeTab(id);
  std::unique_ptr<Widget> out = removeChild(w);
  syncVisibility();
  return out;
}

void Panel::setActive(int id) {
  if (strip_->setActive(id)) syncVisibility();
}

Widget* Panel::content(int id) const {
  for (const Page& p : pages_)
    if (p.id == id) return p.w;
  return nullptr;
}

void Panel::syncVisibility() {
  const int a = strip_->activeId();
  for (const Page& p : pages_) p.w->setVisible(p.id == a);
  if (RootWidget* r = root()) r->requestLayout();
}

void Panel::onLayout() {
  const RectF& b = bounds();
  const float th = std::min(strip_->preferredHeight(), b.h);
  strip_->layout({b.x, b.y, b.w, th});
  content_ = {b.x, b.y + th, b.w, b.h - th};
  for (const Page& p : pages_)
    if (p.w->visible()) p.w->layout(content_);
}

void Panel::paint(PaintContext& ctx) {
  if (!activeContent()) ctx.fill(content_, ctx.theme.panelBackground);
}

}  // namespace tf::ui
