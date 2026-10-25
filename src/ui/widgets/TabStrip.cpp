#include "ui/widgets/TabStrip.h"

#include <algorithm>
#include <cmath>

namespace tf::ui {

namespace {
constexpr const char* kClose = "\xC3\x97";      // U+00D7
constexpr const char* kDirty = "\xE2\x97\x8F";  // U+25CF
float markerSize(float lineHeight) { return std::max(4.0f, std::round(lineHeight * 0.32f)); }
}  // namespace

int TabStrip::indexOf(int id) const {
  for (size_t i = 0; i < tabs_.size(); ++i)
    if (tabs_[i].id == id) return static_cast<int>(i);
  return -1;
}

void TabStrip::addTab(int id, std::string title, bool pinned) {
  if (indexOf(id) >= 0) return;
  size_t at = tabs_.size();
  if (pinned) {
    at = 0;
    while (at < tabs_.size() && tabs_[at].pinned) ++at;
  }
  tabs_.insert(tabs_.begin() + static_cast<long>(at), Tab{id, std::move(title), false, pinned});
  mru_.push_back(id);
  if (active_ < 0) activate(id, false);
  invalidate();
}

void TabStrip::removeTab(int id) {
  const int i = indexOf(id);
  if (i < 0) return;
  tabs_.erase(tabs_.begin() + i);
  mru_.erase(std::find(mru_.begin(), mru_.end(), id));
  if (pressTab_ == id || pressMiddle_ == id) endPress();
  if (active_ == id) {
    active_ = mru_.empty() ? -1 : mru_.front();
    if (active_ >= 0) ensureVisible(active_);
  }
  invalidate();
}

bool TabStrip::setActive(int id) {
  if (indexOf(id) < 0) return false;
  activate(id, false);
  return true;
}

void TabStrip::activate(int id, bool notify) {
  mru_.erase(std::find(mru_.begin(), mru_.end(), id));
  mru_.insert(mru_.begin(), id);
  const bool changed = active_ != id;
  active_ = id;
  ensureVisible(id);
  markDirty();
  if (notify && changed && onActivate) onActivate(id);
}

void TabStrip::setTitle(int id, std::string title) {
  const int i = indexOf(id);
  if (i < 0 || tabs_[i].title == title) return;
  tabs_[i].title = std::move(title);
  invalidate();
}
void TabStrip::setDirty(int id, bool dirty) {
  const int i = indexOf(id);
  if (i < 0 || tabs_[i].dirty == dirty) return;
  tabs_[i].dirty = dirty;
  invalidate();
}
void TabStrip::setVcsMark(int id, VcsMark m) {
  const int i = indexOf(id);
  if (i < 0 || tabs_[i].vcs == m) return;
  tabs_[i].vcs = m;
  invalidate();
}
void TabStrip::setPinned(int id, bool pinned) {
  const int i = indexOf(id);
  if (i < 0 || tabs_[i].pinned == pinned) return;
  Tab t = std::move(tabs_[i]);
  tabs_.erase(tabs_.begin() + i);
  t.pinned = pinned;
  size_t at = 0;
  while (at < tabs_.size() && tabs_[at].pinned) ++at;  // end of pinned group == start of unpinned group
  tabs_.insert(tabs_.begin() + static_cast<long>(at), std::move(t));
  invalidate();
}
void TabStrip::moveTab(int id, int index) {
  const int i = indexOf(id);
  if (i < 0) return;
  int np = 0;
  for (const Tab& t : tabs_) np += t.pinned;
  const bool pin = tabs_[i].pinned;
  const int lo = pin ? 0 : np, hi = pin ? np - 1 : static_cast<int>(tabs_.size()) - 1;
  index = std::clamp(index, lo, hi);
  if (index == i) return;
  Tab t = std::move(tabs_[i]);
  tabs_.erase(tabs_.begin() + i);
  tabs_.insert(tabs_.begin() + index, std::move(t));
  invalidate();
}

float TabStrip::preferredHeight() const {
  const RootWidget* r = root();
  return r ? r->text().lineHeight() + 2 * theme().verticalPad : 0;
}

void TabStrip::ensureGeo() {
  if (geomValid_) return;
  geomValid_ = true;
  geo_.clear();
  RootWidget* r = root();
  if (!r) return;
  const gfx::TextRenderer& tr = r->text();
  const Theme& th = theme();
  const float slotW = tr.lineHeight(), gap = th.padding / 2;
  float x = 0;
  for (const Tab& t : tabs_) {
    Geo g;
    g.slot = !t.pinned || t.dirty;
    const float extra = g.slot ? gap + slotW : 0;
    g.markW = t.vcs == VcsMark::None ? 0 : markerSize(tr.lineHeight()) + gap;
    g.shown = tr.fit(t.title, th.tabMaxWidth - 2 * th.padding - extra - g.markW);
    g.titleW = tr.measure(g.shown);
    g.w = th.padding + g.markW + g.titleW + extra + (g.slot ? gap : th.padding);
    g.x = x;
    x += g.w;
    geo_.push_back(std::move(g));
  }
}

float TabStrip::totalWidth() {
  ensureGeo();
  return geo_.empty() ? 0 : geo_.back().x + geo_.back().w;
}
bool TabStrip::overflowing() { return totalWidth() > bounds().w; }

void TabStrip::onLayout() {
  geomValid_ = false;
  if (active_ >= 0) ensureVisible(active_);
  clampScroll();
}

void TabStrip::clampScroll() { scroll_ = std::clamp(scroll_, 0.0f, std::max(0.0f, totalWidth() - bounds().w)); }
void TabStrip::scrollBy(float dx) {
  const float old = scroll_;
  scroll_ += dx;
  clampScroll();
  if (scroll_ != old) markDirty();
}

void TabStrip::ensureVisible(int id) {
  ensureGeo();
  const int i = indexOf(id);
  if (i < 0 || i >= static_cast<int>(geo_.size()) || bounds().w <= 0) return;  // not laid out yet
  const Geo& g = geo_[i];
  if (g.x < scroll_) scroll_ = g.x;
  else if (g.x + g.w > scroll_ + bounds().w) scroll_ = g.x + g.w - bounds().w;
  clampScroll();
}

RectF TabStrip::tabRect(int id) {
  ensureGeo();
  const int i = indexOf(id);
  if (i < 0 || i >= static_cast<int>(geo_.size())) return {0, 0, 0, 0};
  return {bounds().x + geo_[i].x - scroll_, bounds().y, geo_[i].w, bounds().h};
}

RectF TabStrip::vcsMarkerRect(int id) {
  ensureGeo();
  const int i = indexOf(id);
  if (i < 0 || i >= static_cast<int>(geo_.size()) || geo_[i].markW <= 0 || !root()) return {0, 0, 0, 0};
  const float s = markerSize(root()->text().lineHeight());
  return {bounds().x + geo_[i].x - scroll_ + theme().padding, bounds().y + (bounds().h - s) / 2, s, s};
}

RectF TabStrip::closeRect(int id) {
  const int i = indexOf(id);
  RectF r = tabRect(id);
  if (i < 0 || !geo_[i].slot) return {0, 0, 0, 0};
  const float slotW = root()->text().lineHeight();
  return {r.x + r.w - theme().padding / 2 - slotW, r.y, slotW, r.h};
}

int TabStrip::tabAt(float x, float y) {
  ensureGeo();
  if (!contains(x, y)) return -1;
  for (size_t i = 0; i < tabs_.size() && i < geo_.size(); ++i) {
    const float x0 = bounds().x + geo_[i].x - scroll_;
    if (x >= x0 && x < x0 + geo_[i].w) return tabs_[i].id;
  }
  return -1;
}

void TabStrip::endPress() {
  pressTab_ = pressMiddle_ = -1;
  pressClose_ = dragging_ = false;
  releaseMouse();
}

void TabStrip::onHover(bool entered) {
  if (entered || hoverTab_ < 0) return;
  hoverTab_ = -1;
  hoverClose_ = false;
  markDirty();
}

void TabStrip::dragTo(float x, float y) {
  const Theme& th = theme();
  const RectF& b = bounds();
  const int id = pressTab_, i = indexOf(id);
  if (i < 0) return;
  if (y < b.y - th.dragOutDistance || y > b.y + b.h + th.dragOutDistance) {
    moveTab(id, startIndex_);
    endPress();
    if (onBeginDragOut) onBeginDragOut(id, x, y);
    return;
  }
  ensureGeo();
  const bool pin = tabs_[i].pinned;
  const float center = (x - b.x + scroll_) - grab_ + geo_[i].w / 2;
  float pos = 0;
  int count = 0, groupStart = 0;  // `count` = others whose midpoint is left of the dragged centre
  for (size_t j = 0; j < tabs_.size(); ++j) {
    if (static_cast<int>(j) == i) continue;
    const float mid = pos + geo_[j].w / 2;
    pos += geo_[j].w;
    if (tabs_[j].pinned != pin) {
      if (!pin) ++groupStart;  // pinned others precede the unpinned group
      continue;
    }
    if (mid < center) ++count;
  }
  moveTab(id, groupStart + count);
  ensureVisible(id);
}

bool TabStrip::onEvent(const InputEvent& e) {
  switch (e.type) {
    case InputType::MouseMove: {
      if (hasCapture() && pressTab_ >= 0 && !pressClose_ && (e.buttons & 1)) {
        if (!dragging_ && std::hypot(e.x - pressX_, e.y - pressY_) > theme().dragThreshold) dragging_ = true;
        if (dragging_) dragTo(e.x, e.y);
        return true;
      }
      const int t = tabAt(e.x, e.y);
      bool onClose = false;
      if (t >= 0) {
        const RectF c = closeRect(t);
        onClose = c.w > 0 && ui::contains(c, e.x, e.y) && closable(tabs_[indexOf(t)]);
      }
      if (t != hoverTab_ || onClose != hoverClose_) {
        hoverTab_ = t;
        hoverClose_ = onClose;
        markDirty();
      }
      return t >= 0;
    }
    case InputType::MouseDown: {
      const int t = tabAt(e.x, e.y);
      if (t < 0) return false;
      if (e.button == MouseButton::Middle) {
        pressMiddle_ = t;
        captureMouse();
        return true;
      }
      if (e.button != MouseButton::Left) return true;
      const RectF c = closeRect(t);
      pressTab_ = t;
      pressX_ = e.x;
      pressY_ = e.y;
      grab_ = e.x - tabRect(t).x;
      startIndex_ = indexOf(t);
      pressClose_ = c.w > 0 && ui::contains(c, e.x, e.y) && closable(tabs_[indexOf(t)]);
      if (!pressClose_) activate(t, true);
      captureMouse();
      return true;
    }
    case InputType::MouseUp: {
      const int t = tabAt(e.x, e.y);
      if (e.button == MouseButton::Middle && pressMiddle_ >= 0) {
        const int id = pressMiddle_;
        const int i = indexOf(id);
        endPress();
        if (t == id && i >= 0 && closable(tabs_[i]) && onClose) onClose(id);
        return true;
      }
      if (e.button != MouseButton::Left || pressTab_ < 0) return false;
      const int id = pressTab_;
      const bool wasClose = pressClose_, wasDrag = dragging_;
      endPress();
      if (wasClose) {
        const RectF c = closeRect(id);
        if (t == id && ui::contains(c, e.x, e.y) && onClose) onClose(id);
      } else if (wasDrag && indexOf(id) != startIndex_ && onReorder) {
        onReorder(id, indexOf(id));
      }
      return true;
    }
    case InputType::Wheel:
      if (!overflowing()) return false;
      scrollBy(-(e.dy != 0 ? e.dy : e.dx) * 3 * root()->text().cellWidth() * 4);
      return true;
    default: return false;
  }
}

void TabStrip::paint(PaintContext& ctx) {
  ensureGeo();
  const Theme& th = ctx.theme;
  const RectF& b = bounds();
  ctx.fill(b, th.tabStripBackground);
  const float lh = ctx.lineHeight(), textY = b.y + (b.h - lh) / 2, slotW = lh;
  for (size_t i = 0; i < tabs_.size() && i < geo_.size(); ++i) {
    const Tab& t = tabs_[i];
    const Geo& g = geo_[i];
    const RectF full{b.x + g.x - scroll_, b.y, g.w, b.h};
    const RectF r = intersect(full, b);
    if (r.w <= 0) continue;
    const bool active = t.id == active_, hot = t.id == hoverTab_;
    ctx.fill(r, active ? th.tabActive : (hot ? th.tabHover : th.tabInactive));
    if (active)
      ctx.fill(intersect({full.x, full.y + full.h - th.accentHeight, full.w, th.accentHeight}, b), th.tabAccent);
    ctx.fill(intersect({full.x + full.w - 1, full.y, 1, full.h}, b), th.tabStripBackground);  // 1px separator
    if (t.vcs != VcsMark::None) {
      const RectF m = intersect(vcsMarkerRect(t.id), b);
      const Color mc = t.vcs == VcsMark::Modified     ? Theme::orElse(th.tabVcsModified, th.dirtyMarker)
                       : t.vcs == VcsMark::Added      ? Theme::orElse(th.tabVcsAdded, th.vcsAdded())
                       : t.vcs == VcsMark::CheckedOut ? Theme::orElse(th.tabVcsCheckedOut, th.vcsDeleted())
                       : t.vcs == VcsMark::ReadOnly   ? Theme::orElse(th.tabVcsReadOnly, th.textDim)
                                                      : th.diagnosticError;
      if (t.vcs == VcsMark::ReadOnly && m.w >= 4 && m.h >= 4) {  // an outlined square
        ctx.fill({m.x, m.y, m.w, 1}, mc);
        ctx.fill({m.x, m.y + m.h - 1, m.w, 1}, mc);
        ctx.fill({m.x, m.y, 1, m.h}, mc);
        ctx.fill({m.x + m.w - 1, m.y, 1, m.h}, mc);
      } else {
        ctx.fill(m, mc);
      }
    }
    ctx.text(full.x + th.padding + g.markW, textY, g.shown,
             active ? Theme::orElse(th.tabActiveText, th.text)
             : hot  ? th.text
                    : Theme::orElse(th.tabInactiveText, th.textDim),
             r);
    if (!g.slot) continue;
    const RectF slot{full.x + full.w - th.padding / 2 - slotW, full.y, slotW, full.h};
    const bool closeHot = hot && hoverClose_ && !t.pinned;
    if (closeHot) ctx.fill(intersect({slot.x, slot.y + 2, slot.w, slot.h - 4}, b), th.closeHover);
    const char* glyph = nullptr;
    Color c = th.text;
    if (t.dirty && !closeHot) glyph = kDirty, c = th.dirtyMarker;
    else if (!t.pinned && (active || hot)) glyph = kClose;
    if (glyph) ctx.text(slot.x + (slot.w - ctx.measure(glyph)) / 2, textY, glyph, c, r);
  }
}

}  // namespace tf::ui
