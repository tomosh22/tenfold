#include "ui/widgets/StatusBar.h"

namespace tf::ui {

StatusBar::Segment* StatusBar::find(const std::string& key) {
  for (Segment& s : segs_)
    if (s.key == key) return &s;
  return nullptr;
}
const StatusBar::Segment* StatusBar::find(const std::string& key) const {
  return const_cast<StatusBar*>(this)->find(key);
}

void StatusBar::addSegment(const std::string& key, Align align, std::function<void(const std::string&)> onClick) {
  if (Segment* s = find(key)) {
    s->onClick = std::move(onClick);
    return;
  }
  hot_ = nullptr;  // the vector may reallocate
  segs_.push_back({key, {}, align, std::move(onClick)});
  markStale();
}

void StatusBar::setText(const std::string& key, std::string text) {
  Segment* s = find(key);
  if (!s) {
    addSegment(key, Align::Left);
    s = find(key);
  }
  if (s->text == text) return;
  s->text = std::move(text);
  markStale();
  markDirty();
}

const std::string& StatusBar::text(const std::string& key) const {
  static const std::string empty;
  const Segment* s = find(key);
  return s ? s->text : empty;
}

void StatusBar::setOnClick(const std::string& key, std::function<void(const std::string&)> onClick) {
  if (Segment* s = find(key)) s->onClick = std::move(onClick);
}

float StatusBar::preferredHeight() const {
  const RootWidget* r = root();
  return r ? r->text().lineHeight() + 2 * theme().verticalPad : 0;
}

void StatusBar::place() {
  stale_ = false;
  RootWidget* r = root();
  if (!r) return;
  const Theme& th = theme();
  const RectF& b = bounds();
  float left = b.x, right = b.x + b.w;
  for (Segment& s : segs_) {
    if (s.text.empty()) {
      s.rect = {0, 0, 0, 0};
      continue;
    }
    const float w = r->text().measure(s.text) + 2 * th.padding;
    if (s.align == Align::Left) {
      s.rect = {left, b.y, w, b.h};
      left += w;
    } else {
      right -= w;
      s.rect = {right, b.y, w, b.h};
    }
  }
}

RectF StatusBar::segmentRect(const std::string& key) {
  if (stale_) place();
  const Segment* s = find(key);
  return s ? s->rect : RectF{0, 0, 0, 0};
}

StatusBar::Segment* StatusBar::segmentAt(float x, float y) {
  if (stale_) place();
  for (Segment& s : segs_)
    if (s.rect.w > 0 && ui::contains(s.rect, x, y)) return &s;
  return nullptr;
}

void StatusBar::setDebugging(bool on) {
  if (debugging_ == on) return;
  debugging_ = on;
  markDirty();
}

void StatusBar::onHover(bool entered) {
  if (!entered && hot_) {
    hot_ = nullptr;
    markDirty();
  }
}

bool StatusBar::onEvent(const InputEvent& e) {
  if (e.type == InputType::MouseMove) {
    Segment* s = segmentAt(e.x, e.y);
    Segment* h = s && s->onClick ? s : nullptr;
    if (h != hot_) {
      hot_ = h;
      markDirty();
    }
    return false;
  }
  if (e.type == InputType::MouseDown && e.button == MouseButton::Left) {
    Segment* s = segmentAt(e.x, e.y);
    if (!s || !s->onClick) return false;
    const std::string key = s->key;
    auto cb = s->onClick;  // the callback may change segments
    cb(key);
    return true;
  }
  return false;
}

void StatusBar::paint(PaintContext& ctx) {
  if (stale_) place();
  const Theme& th = ctx.theme;
  const Color bg = debugging_ ? Theme::orElse(th.statusBackgroundDebugging, th.statusBackground) : th.statusBackground;
  const Color fg = debugging_ ? Theme::orElse(th.statusTextDebugging, th.statusText) : th.statusText;
  ctx.fill(bounds(), bg);
  const float y = bounds().y + (bounds().h - ctx.lineHeight()) / 2;
  for (const Segment& s : segs_) {
    if (s.rect.w <= 0) continue;
    if (&s == hot_) ctx.fill(s.rect, th.statusHover);
    ctx.text(s.rect.x + th.padding, y, s.text, fg, intersect(s.rect, bounds()));
  }
}

}  // namespace tf::ui
