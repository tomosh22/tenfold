#pragma once
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include "ui/gfx/Renderer.h"
#include "ui/gfx/TextRenderer.h"
#include "ui/platform/Input.h"
#include "ui/widgets/Theme.h"

// Retained widget layer. All coordinates are absolute window pixels (a child's bounds are not relative to its
// parent), which keeps hit-testing and events trivial. No GL or SDL in here: widgets draw through gfx::Renderer and
// receive platform-neutral InputEvents, so everything is testable with a recording renderer.
//
// Frame model (event-driven): the app feeds events to RootWidget::dispatch, and when RootWidget::dirty() is true it
// calls RootWidget::render() (layout if needed, paint, flush) and then swaps. Widgets call markDirty() when their
// visual state changes; it propagates to the root. Setters only mark dirty on a real change.
//
// Paint order: PaintContext::fill() draws rects/lines immediately; PaintContext::text() is deferred and emitted after
// the whole tree has painted. All text therefore draws on top of all rects (do not put a filled rect over text that
// belongs to a widget painted earlier). A shell frame is thus one rect batch plus one glyph batch per text colour.
// Overlay layer: Widget::paintOverlay() runs for the whole tree after the glyphs were emitted, so rects drawn there
// (text carets) land on top of all text. Use it sparingly: it costs one extra rect batch per frame.
// Top layer (popups): a widget with setTopLayer(true) is skipped by the base pass and painted afterwards, together
// with its subtree, as a second frame layer: rects, then its own glyphs (flushText again), then its overlay rects. A
// popup therefore covers the editor's glyphs for the price of one more rect batch and one glyph batch per colour.
// Modal: RootWidget::showModal(w) makes `w` (full-window bounds, top layer) the keyboard and mouse target until
// closeModal(); see RootWidget.
// Timers: the app calls RootWidget::tick(nowMs) after handling events; widgets that animate (caret blink, smooth
// scroll) return how many ms until they want the next tick from onTick() and mark themselves dirty when it changed.
namespace tf::ui {

using gfx::Color;
using gfx::RectF;

inline bool contains(const RectF& r, float x, float y) {
  return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}
inline RectF intersect(const RectF& a, const RectF& b) {
  const float x0 = a.x > b.x ? a.x : b.x, y0 = a.y > b.y ? a.y : b.y;
  const float x1 = a.x + a.w < b.x + b.w ? a.x + a.w : b.x + b.w, y1 = a.y + a.h < b.y + b.h ? a.y + a.h : b.y + b.h;
  return {x0, y0, x1 > x0 ? x1 - x0 : 0, y1 > y0 ? y1 - y0 : 0};
}

struct PaintContext {
  gfx::Renderer& renderer;
  gfx::TextRenderer& textRenderer;
  const Theme& theme;
  void fill(const RectF& r, Color c) { renderer.drawRect(r, c); }
  void line(float x0, float y0, float x1, float y1, float thickness, Color c) {
    renderer.drawLine(x0, y0, x1, y1, thickness, c);
  }
  // Deferred single-line label; `y` = top of the line box; clipped to `clip`.
  void text(float x, float y, std::string_view s, Color c, const RectF& clip) {
    textRenderer.drawText(x, y, s, c, clip);
  }
  float measure(std::string_view s) const { return textRenderer.measure(s); }
  float lineHeight() const { return textRenderer.lineHeight(); }
};

class RootWidget;

class Widget {
 public:
  Widget() = default;
  virtual ~Widget();
  Widget(const Widget&) = delete;
  Widget& operator=(const Widget&) = delete;

  // ---- tree ----
  Widget* parent() const { return parent_; }
  const std::vector<std::unique_ptr<Widget>>& children() const { return children_; }
  Widget* addChild(std::unique_ptr<Widget> c);     // returns the raw pointer; requests layout
  std::unique_ptr<Widget> removeChild(Widget* c);  // null if not a child; drops focus/capture inside it
  RootWidget* root() const;                        // null while detached

  // ---- geometry / visibility ----
  const RectF& bounds() const { return bounds_; }
  void layout(const RectF& r);  // stores bounds, calls onLayout (which lays out children)
  bool visible() const { return visible_; }
  void setVisible(bool v);
  bool contains(float x, float y) const { return visible_ && ui::contains(bounds_, x, y); }

  // ---- painting ----
  void paintTree(PaintContext& ctx, bool inTop = false);  // paint(), then children in order (top-layer ones skipped
                                                          // unless inTop), skipped when invisible
  void paintOverlayTree(PaintContext& ctx, bool inTop = false);  // paintOverlay() of visible widgets, parents first
  void paintTopTree(PaintContext& ctx, bool overlay);  // the top-layer subtrees (paint, or paintOverlay if `overlay`)
  bool hasTopLayer() const;                            // any visible top-layer widget below this one
  bool topLayer() const { return topLayer_; }
  void setTopLayer(bool t) { topLayer_ = t; }
  void markDirty();             // request a redraw (propagates to the root)
  int tickTree(int64_t nowMs);  // onTick() of visible widgets; min wait in ms, -1 = nothing pending

  // ---- input ----
  // Deepest visible descendant (or this) containing the point; children later in the list are on top. Null if none.
  Widget* hitTest(float x, float y);
  virtual bool acceptsFocus() const { return false; }  // clicking inside gives focus to the nearest such ancestor
  bool hasFocus() const;
  void setFocus();
  void captureMouse();  // all mouse events go here until the button is released (or releaseMouse())
  void releaseMouse();
  bool hasCapture() const;
  bool hovered() const;

  // Return true if handled; unhandled events bubble to the parent. Mouse events carry absolute coordinates.
  virtual bool onEvent(const InputEvent&) { return false; }
  virtual void onHover(bool /*entered*/) {}
  virtual void onFocusChanged(bool /*gained*/) {}

 protected:
  virtual void paint(PaintContext&) {}
  virtual void paintOverlay(PaintContext&) {}
  virtual int onTick(int64_t /*nowMs*/) { return -1; }  // ms until the next wanted tick, -1 = none
  virtual void onLayout() {}
  const Theme& theme() const;  // the root's theme (a default one while detached)

 private:
  friend class RootWidget;
  Widget* parent_ = nullptr;
  std::vector<std::unique_ptr<Widget>> children_;
  RectF bounds_{0, 0, 0, 0};
  bool visible_ = true;
  bool topLayer_ = false;
};

// Top of a widget tree. Owns the theme, holds the text renderer, routes input, tracks dirtiness.
class RootWidget : public Widget {
 public:
  explicit RootWidget(gfx::TextRenderer& text, Theme theme = Theme::darkDefault()) : text_(&text), theme_(theme) {}
  ~RootWidget() override;

  gfx::TextRenderer& text() const { return *text_; }
  const Theme& rootTheme() const { return theme_; }
  void setTheme(const Theme& t) {
    theme_ = t;
    requestLayout();
  }

  // Routing: with a capture, mouse events go to the capturing widget only (and MouseUp then ends the capture).
  // Otherwise mouse events go to the deepest widget under the pointer and bubble up; MouseDown first moves focus to
  // the nearest focusable ancestor-or-self. Key/Text events go to the focused widget and bubble up. Mouse events also
  // maintain hover (onHover). Returns handled.
  bool dispatch(const InputEvent& e);

  // Advances animations; returns the ms until the next tick is needed (-1 = none). Call after dispatching events.
  int tick(int64_t nowMs) { return tickTree(nowMs); }

  bool dirty() const { return dirty_; }
  void clearDirty() { dirty_ = false; }
  void requestLayout() {
    layoutDirty_ = true;
    dirty_ = true;
  }

  // Full frame into `r` (beginFrame ... endFrame): lays the tree out to (w, h) if the size or layout changed, paints
  // rects, flushes text, endFrame. Clears the dirty flag. Does not swap.
  void render(gfx::Renderer& r, int w, int h);

  // ---- modal popups ----
  // Shows `w` as the (single) modal widget: it becomes a top-layer child with the root's full bounds, takes the focus
  // (`focus` or itself) and receives every mouse event; key events go to the focused widget inside it (or to `w`).
  // The previous focus returns on closeModal(). Showing a second modal closes the first. Closing is safe from inside
  // the modal's own event handler: the widget is destroyed on the next dispatch/render.
  void showModal(std::unique_ptr<Widget> w, Widget* focus = nullptr);
  void closeModal();
  Widget* modal() const { return modal_; }

  Widget* focused() const { return focus_; }
  Widget* captured() const { return capture_; }
  Widget* hoveredWidget() const { return hover_; }

 protected:
  void onLayout() override;  // default: every child fills the root; subclasses override for real layouts

 private:
  friend class Widget;
  void forget(Widget* subtree);  // drop focus/capture/hover pointing into `subtree`
  void setFocusTo(Widget* w);

  gfx::TextRenderer* text_;
  Theme theme_;
  Widget *focus_ = nullptr, *capture_ = nullptr, *hover_ = nullptr;
  Widget *modal_ = nullptr, *beforeModal_ = nullptr;
  std::vector<std::unique_ptr<Widget>> retired_;  // closed modals, destroyed at the next dispatch/render
  bool dirty_ = true, layoutDirty_ = true;
  int w_ = 0, h_ = 0;
};

}  // namespace tf::ui
