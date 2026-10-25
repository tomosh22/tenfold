#include "ui/widgets/Widget.h"

#include <algorithm>

namespace tf::ui {

namespace {
bool within(const Widget* w, const Widget* ancestor) {
  for (; w; w = w->parent())
    if (w == ancestor) return true;
  return false;
}
}  // namespace

Widget::~Widget() {
  if (RootWidget* r = root()) r->forget(this);
  for (auto& c : children_) c->parent_ = nullptr;  // children must not walk a half-destroyed parent chain
}

RootWidget* Widget::root() const {
  const Widget* w = this;
  while (w->parent_) w = w->parent_;
  return dynamic_cast<RootWidget*>(const_cast<Widget*>(w));
}

const Theme& Widget::theme() const {
  static const Theme fallback;
  const RootWidget* r = root();
  return r ? r->rootTheme() : fallback;
}

Widget* Widget::addChild(std::unique_ptr<Widget> c) {
  Widget* raw = c.get();
  raw->parent_ = this;
  children_.push_back(std::move(c));
  if (RootWidget* r = root()) r->requestLayout();
  return raw;
}

std::unique_ptr<Widget> Widget::removeChild(Widget* c) {
  auto it = std::find_if(children_.begin(), children_.end(), [c](const auto& p) { return p.get() == c; });
  if (it == children_.end()) return nullptr;
  if (RootWidget* r = root()) {
    r->forget(c);
    r->requestLayout();
  }
  std::unique_ptr<Widget> out = std::move(*it);
  children_.erase(it);
  out->parent_ = nullptr;
  return out;
}

void Widget::layout(const RectF& r) {
  bounds_ = r;
  onLayout();
  markDirty();
}

void Widget::setVisible(bool v) {
  if (v == visible_) return;
  visible_ = v;
  if (RootWidget* r = root()) {
    if (!v) r->forget(this);
    r->requestLayout();
  }
}

void Widget::paintTree(PaintContext& ctx) {
  if (!visible_) return;
  paint(ctx);
  for (auto& c : children_) c->paintTree(ctx);
}

void Widget::markDirty() {
  if (RootWidget* r = root()) r->dirty_ = true;
}

Widget* Widget::hitTest(float x, float y) {
  if (!contains(x, y)) return nullptr;
  for (size_t i = children_.size(); i-- > 0;)
    if (Widget* w = children_[i]->hitTest(x, y)) return w;
  return this;
}

bool Widget::hasFocus() const {
  RootWidget* r = root();
  return r && r->focus_ == this;
}
void Widget::setFocus() {
  if (RootWidget* r = root()) r->setFocusTo(this);
}
void Widget::captureMouse() {
  if (RootWidget* r = root()) r->capture_ = this;
}
void Widget::releaseMouse() {
  RootWidget* r = root();
  if (r && r->capture_ == this) r->capture_ = nullptr;
}
bool Widget::hasCapture() const {
  RootWidget* r = root();
  return r && r->capture_ == this;
}
bool Widget::hovered() const {
  RootWidget* r = root();
  return r && r->hover_ == this;
}

RootWidget::~RootWidget() { focus_ = capture_ = hover_ = nullptr; }

void RootWidget::forget(Widget* s) {
  if (within(focus_, s)) focus_ = nullptr;
  if (within(capture_, s)) capture_ = nullptr;
  if (within(hover_, s)) hover_ = nullptr;
}

void RootWidget::setFocusTo(Widget* w) {
  if (w == focus_) return;
  Widget* old = focus_;
  focus_ = w;
  if (old) old->onFocusChanged(false);
  if (w) w->onFocusChanged(true);
  dirty_ = true;
}

bool RootWidget::dispatch(const InputEvent& e) {
  auto bubble = [&](Widget* w) {
    for (; w; w = w->parent())
      if (w->onEvent(e)) return true;
    return false;
  };
  switch (e.type) {
    case InputType::MouseDown:
    case InputType::MouseUp:
    case InputType::MouseMove:
    case InputType::Wheel: {
      Widget* target = capture_ ? capture_ : hitTest(e.x, e.y);
      if (e.type == InputType::MouseMove && target != hover_) {
        Widget* old = hover_;
        hover_ = target;
        if (old) old->onHover(false);
        if (target) target->onHover(true);
      }
      if (capture_) {
        Widget* c = capture_;
        const bool handled = c->onEvent(e);
        if (e.type == InputType::MouseUp && capture_ == c) capture_ = nullptr;
        return handled;
      }
      if (e.type == InputType::MouseDown) {
        Widget* f = target;
        while (f && !f->acceptsFocus()) f = f->parent();
        if (f) setFocusTo(f);
      }
      return bubble(target);
    }
    case InputType::KeyDown:
    case InputType::KeyUp:
    case InputType::Text:
    case InputType::TextEditing: return bubble(focus_);
    default: return false;
  }
}

void RootWidget::onLayout() {
  for (auto& c : children()) c->layout(bounds());
}

void RootWidget::render(gfx::Renderer& r, int w, int h) {
  if (layoutDirty_ || w != w_ || h != h_) {
    w_ = w;
    h_ = h;
    layoutDirty_ = false;
    layout({0, 0, static_cast<float>(w), static_cast<float>(h)});
  }
  PaintContext ctx{r, *text_, theme_};
  // A growing atlas invalidates UVs already recorded this frame; flushText then reports false and we repaint once.
  for (int attempt = 0; attempt < 2; ++attempt) {
    r.beginFrame(w, h, theme_.background);
    paintTree(ctx);
    if (text_->flushText(r)) break;
  }
  r.endFrame();
  dirty_ = false;
}

}  // namespace tf::ui
