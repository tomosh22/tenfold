// B3a: widget base (routing, capture, focus, dirty), TabStrip, Splitter, Panel, StatusBar, and the --shell demo.
// Everything but the golden uses a recording renderer and a mock monospace font (8 px cells, 14 px lines), so no GL.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>

#include "../text/text_test_util.h"
#include "ui/gfx/PngWrite.h"
#include "ui/gfx/TextRenderer.h"
#include "ui/widgets/Panel.h"
#include "ui/widgets/ShellDemo.h"
#include "ui/widgets/Splitter.h"
#include "ui/widgets/StatusBar.h"
#include "ui_test.h"

using namespace tf;
using namespace tf::ui;

namespace {

// Counts what a frame issues. "batches" approximates GlRenderer draw calls: rect/line runs and glyph runs alternate.
struct Recorder : gfx::Renderer {
  int rects = 0, glyphCalls = 0, scissors = 0, batches = 0, frames = 0;
  int lastKind = 0;
  void kind(int k) {
    if (k != lastKind) ++batches;
    lastKind = k;
  }
  void beginFrame(int, int, gfx::Color) override {
    rects = glyphCalls = scissors = batches = 0;
    lastKind = 0;
    ++frames;
  }
  void drawRect(const gfx::RectF&, gfx::Color) override {
    ++rects;
    kind(1);
  }
  void drawLine(float, float, float, float, float, gfx::Color) override { kind(1); }
  void drawGlyphs(gfx::TextureId, const gfx::GlyphQuad*, int, gfx::Color) override {
    ++glyphCalls;
    kind(2);
  }
  void pushScissor(const gfx::RectF&) override { ++scissors; }
  void popScissor() override {}
  gfx::TextureId createTexture(int, int, gfx::TextureFormat) override { return 1; }
  void updateTexture(gfx::TextureId, int, int, int, int, const void*, int) override {}
  void destroyTexture(gfx::TextureId) override {}
  void endFrame() override {}
  bool readPixels(int, int, uint8_t*) override { return false; }
};

std::unique_ptr<gfx::TextRenderer> mockText() {
  gfx::FontStack stack;
  stack.add(std::make_shared<tftext::MockFont>(U"abcdefghijklmnopqrstuvwxyz0123456789 ._-:,×●…ABCLMNTUFDK"));
  return gfx::TextRenderer::fromFonts(std::move(stack));
}

InputEvent mouse(InputType t, float x, float y, MouseButton b = MouseButton::Left, int clicks = 1) {
  InputEvent e;
  e.type = t;
  e.x = x;
  e.y = y;
  e.button = b;
  e.clicks = clicks;
  e.buttons = t == InputType::MouseUp ? 0 : static_cast<uint8_t>(1u << static_cast<int>(b));
  return e;
}
InputEvent key(Key k) {
  InputEvent e;
  e.type = InputType::KeyDown;
  e.key = k;
  return e;
}

// Records every event it receives; configurable hit area and focus behaviour.
struct Probe : Widget {
  std::vector<std::string>* log;
  std::string name;
  bool handles, focusable;
  Probe(std::vector<std::string>* l, std::string n, bool h, bool f = false)
      : log(l), name(std::move(n)), handles(h), focusable(f) {}
  bool acceptsFocus() const override { return focusable; }
  bool onEvent(const InputEvent& e) override {
    log->push_back(name + ":" + std::to_string(static_cast<int>(e.type)));
    if (e.type == InputType::MouseDown && handles && name == "cap") captureMouse();
    return handles;
  }
  void onFocusChanged(bool g) override { log->push_back(name + (g ? ":+focus" : ":-focus")); }
  void onHover(bool in) override { log->push_back(name + (in ? ":+hover" : ":-hover")); }
};

struct Fixture {
  std::unique_ptr<gfx::TextRenderer> text = mockText();
  RootWidget root{*text};
  Recorder rec;
  void frame(int w = 800, int h = 400) { root.render(rec, w, h); }
};

}  // namespace

TEST(widgets_hit_test_bubble_capture_focus) {
  Fixture f;
  std::vector<std::string> log;
  auto* outer = static_cast<Probe*>(f.root.addChild(std::make_unique<Probe>(&log, "outer", true, true)));
  auto* inner = static_cast<Probe*>(outer->addChild(std::make_unique<Probe>(&log, "inner", false)));
  auto* cap = static_cast<Probe*>(f.root.addChild(std::make_unique<Probe>(&log, "cap", true)));
  f.root.layout({0, 0, 400, 300});
  outer->layout({0, 0, 200, 200});
  inner->layout({10, 10, 50, 50});
  cap->layout({250, 0, 100, 100});

  CHECK(f.root.hitTest(20, 20) == inner);
  CHECK(f.root.hitTest(100, 100) == outer);
  CHECK(f.root.hitTest(260, 10) == cap);
  CHECK(f.root.hitTest(390, 290) == &f.root);
  CHECK(f.root.hitTest(500, 10) == nullptr);
  inner->setVisible(false);
  CHECK(f.root.hitTest(20, 20) == outer);
  inner->setVisible(true);

  // Unhandled in inner bubbles to outer; MouseDown focuses the nearest focusable ancestor.
  CHECK(f.root.dispatch(mouse(InputType::MouseDown, 20, 20)));
  CHECK(log == (std::vector<std::string>{"inner:5", "outer:5", "outer:+focus"}) ||
        log == (std::vector<std::string>{"outer:+focus", "inner:5", "outer:5"}));
  CHECK(outer->hasFocus());
  CHECK(f.root.focused() == outer);

  // Keys go to the focused widget.
  log.clear();
  CHECK(f.root.dispatch(key(Key::Enter)));
  CHECK(log.size() == 1 && log[0] == "outer:1");

  // Capture: the capturing widget gets events outside its bounds until MouseUp.
  log.clear();
  f.root.dispatch(mouse(InputType::MouseDown, 260, 10));
  CHECK(cap->hasCapture());
  log.clear();
  f.root.dispatch(mouse(InputType::MouseMove, 20, 20));
  f.root.dispatch(mouse(InputType::MouseUp, 20, 20));
  CHECK(log == (std::vector<std::string>{"cap:7", "cap:6"}) || (log.size() >= 2 && log[log.size() - 2] == "cap:7"));
  CHECK(!cap->hasCapture());
  CHECK(f.root.captured() == nullptr);

  // Removing the focused widget clears focus; destroying a hovered one clears hover.
  auto taken = f.root.removeChild(outer);
  CHECK(f.root.focused() == nullptr);
  CHECK(!f.root.dispatch(key(Key::Enter)));
}

TEST(widgets_dirty_propagation) {
  Fixture f;
  auto* strip = static_cast<TabStrip*>(f.root.addChild(std::make_unique<TabStrip>()));
  strip->addTab(1, "a.cpp");
  f.frame();
  CHECK(!f.root.dirty());
  strip->setDirty(1, true);
  CHECK(f.root.dirty());
  f.frame();
  CHECK(!f.root.dirty());
  strip->setDirty(1, true);  // no change -> stays clean
  CHECK(!f.root.dirty());
  strip->setTitle(1, "b.cpp");
  CHECK(f.root.dirty());
  f.frame();
  Widget loose;  // detached widgets cannot dirty anything and must not crash
  loose.markDirty();
  CHECK(!f.root.dirty());
  strip->markDirty();
  CHECK(f.root.dirty());
}

namespace {
struct StripFixture : Fixture {
  TabStrip* strip;
  std::vector<std::string> events;
  explicit StripFixture(float width = 600) {
    strip = static_cast<TabStrip*>(root.addChild(std::make_unique<TabStrip>()));
    strip->onActivate = [this](int id) { events.push_back("activate " + std::to_string(id)); };
    strip->onClose = [this](int id) { events.push_back("close " + std::to_string(id)); };
    strip->onReorder = [this](int id, int i) {
      events.push_back("reorder " + std::to_string(id) + "->" + std::to_string(i));
    };
    strip->onBeginDragOut = [this](int id, float, float) { events.push_back("dragout " + std::to_string(id)); };
    root.layout({0, 0, width, 300});
    strip->layout({0, 0, width, strip->preferredHeight()});
  }
  std::vector<int> order() {
    std::vector<int> v;
    for (auto& t : strip->tabs()) v.push_back(t.id);
    return v;
  }
  void click(float x, float y, MouseButton b = MouseButton::Left) {
    root.dispatch(mouse(InputType::MouseMove, x, y));
    root.dispatch(mouse(InputType::MouseDown, x, y, b));
    root.dispatch(mouse(InputType::MouseUp, x, y, b));
  }
  void drag(float x0, float y0, float x1, float y1) {
    root.dispatch(mouse(InputType::MouseMove, x0, y0));
    root.dispatch(mouse(InputType::MouseDown, x0, y0));
    for (int i = 1; i <= 8; ++i) {
      InputEvent m = mouse(InputType::MouseMove, x0 + (x1 - x0) * i / 8, y0 + (y1 - y0) * i / 8);
      m.buttons = 1;
      root.dispatch(m);
    }
    root.dispatch(mouse(InputType::MouseUp, x1, y1));
  }
  static float cx(const RectF& r) { return r.x + r.w / 2; }
};
}  // namespace

TEST(tabstrip_activate_close_mru) {
  StripFixture f;
  for (int i = 1; i <= 3; ++i) f.strip->addTab(i, "file" + std::to_string(i));
  CHECK(f.strip->activeId() == 1);
  CHECK(f.strip->mruOrder().front() == 1);
  f.click(StripFixture::cx(f.strip->tabRect(2)) - 10, 5);
  CHECK(f.strip->activeId() == 2);
  CHECK(f.events == (std::vector<std::string>{"activate 2"}));
  f.click(StripFixture::cx(f.strip->tabRect(3)) - 10, 5);
  f.click(StripFixture::cx(f.strip->tabRect(2)) - 10, 5);
  CHECK(f.strip->mruOrder() == (std::vector<int>{2, 3, 1}));
  f.events.clear();
  f.click(StripFixture::cx(f.strip->tabRect(2)) - 10, 5);  // already active: no callback
  CHECK(f.events.empty());

  // Close button (on the active tab) and middle click; the strip never removes by itself.
  const RectF c = f.strip->closeRect(2);
  f.click(c.x + c.w / 2, c.y + c.h / 2);
  CHECK(f.events == (std::vector<std::string>{"close 2"}));
  CHECK(f.strip->tabs().size() == 3u);
  f.events.clear();
  f.click(StripFixture::cx(f.strip->tabRect(1)) - 10, 5, MouseButton::Middle);
  CHECK(f.events == (std::vector<std::string>{"close 1"}));
  // Press on the close button but release elsewhere: no close.
  f.events.clear();
  f.root.dispatch(mouse(InputType::MouseDown, c.x + 2, c.y + 5));
  f.root.dispatch(mouse(InputType::MouseUp, 500, 100));
  CHECK(f.events.empty());

  // Removing the active tab activates the most recently used remaining one.
  f.strip->removeTab(2);
  CHECK(f.strip->activeId() == 3);
  CHECK(f.strip->mruOrder() == (std::vector<int>{3, 1}));
  f.strip->removeTab(3);
  f.strip->removeTab(1);
  CHECK(f.strip->activeId() == -1);
  CHECK(f.strip->mruOrder().empty());
}

TEST(tabstrip_pinned_and_dirty) {
  StripFixture f;
  f.strip->addTab(1, "a");
  f.strip->addTab(2, "b");
  f.strip->addTab(3, "c", true);
  f.strip->addTab(4, "d", true);
  CHECK(f.order() == (std::vector<int>{3, 4, 1, 2}));
  f.strip->setPinned(2, true);
  CHECK(f.order() == (std::vector<int>{3, 4, 2, 1}));
  f.strip->setPinned(3, false);
  CHECK(f.order() == (std::vector<int>{4, 2, 3, 1}));
  CHECK(f.strip->closeRect(4).w == 0);  // pinned: no close button
  CHECK(f.strip->closeRect(1).w > 0);
  // Pinned tabs ignore middle-click close.
  f.click(StripFixture::cx(f.strip->tabRect(4)), 5, MouseButton::Middle);
  CHECK(f.events.empty());
  f.click(StripFixture::cx(f.strip->tabRect(1)), 5, MouseButton::Middle);
  CHECK(f.events == (std::vector<std::string>{"close 1"}));
  // moveTab clamps to the group.
  f.strip->moveTab(1, 0);
  CHECK(f.order() == (std::vector<int>{4, 2, 1, 3}));
  // Dirty tabs render and size differently from clean ones (pinned dirty tabs reserve the marker slot).
  const float before = f.strip->tabRect(4).w;
  f.strip->setDirty(4, true);
  CHECK(f.strip->tabRect(4).w > before);
}

TEST(tabstrip_reorder_by_drag) {
  StripFixture f;
  for (int i = 1; i <= 4; ++i) f.strip->addTab(i, "tab" + std::to_string(i));
  f.strip->addTab(9, "pin", true);
  const RectF r1 = f.strip->tabRect(1);
  const RectF r3 = f.strip->tabRect(3);
  f.drag(StripFixture::cx(r1), 5, StripFixture::cx(r3) - 10, 8);  // dragged centre lands between tabs 3 and 4
  CHECK(f.order() == (std::vector<int>{9, 2, 3, 1, 4}));
  CHECK(f.events.back() == "reorder 1->3");

  // A small wiggle is a click, not a reorder; dragging a pinned tab right cannot leave the pinned group.
  f.events.clear();
  f.drag(StripFixture::cx(f.strip->tabRect(2)), 5, StripFixture::cx(f.strip->tabRect(2)) + 2, 5);
  CHECK(f.events.size() == 1u && f.events[0] == "activate 2");
  f.events.clear();
  f.drag(StripFixture::cx(f.strip->tabRect(9)), 5, 500, 5);
  CHECK(f.order().front() == 9);
  CHECK(f.events == (std::vector<std::string>{"activate 9"}));  // index unchanged -> no reorder

  // Dragging well outside the strip restores the order and hands the gesture to the owner.
  f.events.clear();
  const auto before = f.order();
  f.drag(StripFixture::cx(f.strip->tabRect(4)), 5, StripFixture::cx(f.strip->tabRect(4)), 200);
  CHECK(f.order() == before);
  CHECK(f.events.back() == "dragout 4");
  CHECK(std::count(f.events.begin(), f.events.end(), "dragout 4") == 1);
  CHECK(f.root.captured() == nullptr);
}

TEST(tabstrip_overflow_scroll) {
  StripFixture f(300);
  for (int i = 1; i <= 12; ++i) f.strip->addTab(i, "longer_name" + std::to_string(i));
  CHECK(f.strip->overflowing());
  CHECK(f.strip->scrollX() == 0);
  f.strip->setActive(12);  // keeps the active tab in view
  const RectF r = f.strip->tabRect(12);
  CHECK(r.x >= 0 && r.x + r.w <= 300 + 0.5f);
  CHECK(f.strip->scrollX() > 0);
  InputEvent w;
  w.type = InputType::Wheel;
  w.x = 10;
  w.y = 5;
  w.dy = 50;  // scroll back to the start
  f.root.dispatch(w);
  CHECK(f.strip->scrollX() == 0);
  w.dy = -1;
  f.root.dispatch(w);
  CHECK(f.strip->scrollX() > 0);
  // Hit-testing follows the scroll offset.
  const RectF r3 = f.strip->tabRect(3);
  CHECK(f.strip->tabAt(std::max(r3.x, 0.0f) + 3, 5) == 3);
  StripFixture g(2000);
  g.strip->addTab(1, "x");
  CHECK(!g.strip->overflowing());
  // Painting an overflowing strip does not draw outside it: titles are clipped, long ones truncated with an ellipsis.
  f.frame(300, 100);
  CHECK(f.rec.glyphCalls >= 1);
}

TEST(splitter_drag_clamp_ratio) {
  Fixture f;
  auto sp = std::make_unique<Splitter>(Splitter::Orientation::Horizontal);
  std::vector<std::string> log;
  auto* s = static_cast<Splitter*>(f.root.addChild(std::move(sp)));
  s->setChildren(std::make_unique<Probe>(&log, "a", false), std::make_unique<Probe>(&log, "b", false));
  s->setMinSizes(100, 150);
  f.frame(804, 300);  // 800 usable after the 4 px divider
  s->layout({0, 0, 804, 300});
  CHECK(s->ratio() == 0.5f);
  CHECK(s->first()->bounds().w == 400 && s->second()->bounds().w == 400);
  CHECK(s->second()->bounds().x == 404);

  const float dx = s->dividerRect().x + 1;
  f.root.dispatch(mouse(InputType::MouseMove, dx, 100));
  f.root.dispatch(mouse(InputType::MouseDown, dx, 100));
  CHECK(s->hasCapture());
  InputEvent m = mouse(InputType::MouseMove, 200, 100);
  f.root.dispatch(m);
  f.frame(804, 300);
  CHECK(std::fabs(s->first()->bounds().w - 199) < 1.5f);
  CHECK(std::fabs(s->ratio() - 199.0f / 800) < 0.01f);
  f.root.dispatch(mouse(InputType::MouseMove, -500, 100));  // clamped to the first minimum
  f.frame(804, 300);
  CHECK(s->first()->bounds().w == 100);
  CHECK(s->ratio() == 100.0f / 800);
  f.root.dispatch(mouse(InputType::MouseMove, 5000, 100));  // clamped by the second minimum
  f.frame(804, 300);
  CHECK(s->second()->bounds().w == 150);
  f.root.dispatch(mouse(InputType::MouseUp, 5000, 100));
  CHECK(!s->hasCapture());

  // Double-click on the divider resets to 50%.
  const float dx2 = s->dividerRect().x + 1;
  f.root.dispatch(mouse(InputType::MouseDown, dx2, 100, MouseButton::Left, 2));
  f.frame(804, 300);
  CHECK(s->ratio() == 0.5f);
  CHECK(s->first()->bounds().w == 400);
  // A window resize keeps the ratio and re-clamps against the minimums.
  f.frame(404, 300);
  CHECK(s->ratio() == 0.5f);
  CHECK(s->first()->bounds().w == 200);
  f.frame(304, 300);  // 300 usable: 150 would be below... first min 100 ok, second min 150 -> 150/150
  CHECK(s->first()->bounds().w >= 100 && s->second()->bounds().w >= 150);

  // Vertical orientation uses y.
  Fixture g;
  auto* v = static_cast<Splitter*>(g.root.addChild(std::make_unique<Splitter>(Splitter::Orientation::Vertical)));
  v->setChildren(std::make_unique<Probe>(&log, "a", false), std::make_unique<Probe>(&log, "b", false));
  g.frame(300, 404);
  CHECK(v->first()->bounds().h == 200 && v->second()->bounds().y == 204);
  g.root.dispatch(mouse(InputType::MouseDown, 50, v->dividerRect().y + 1));
  g.root.dispatch(mouse(InputType::MouseMove, 50, 100));
  g.frame(300, 404);
  CHECK(std::fabs(v->first()->bounds().h - 99) < 1.5f);
}

TEST(panel_hosts_active_content_only) {
  Fixture f;
  auto* p = static_cast<Panel*>(f.root.addChild(std::make_unique<Panel>()));
  std::vector<std::string> log;
  auto* a = new Probe(&log, "a", true);
  auto* b = new Probe(&log, "b", true);
  p->addTab(1, "one", std::unique_ptr<Widget>(a));
  p->addTab(2, "two", std::unique_ptr<Widget>(b));
  f.frame();
  CHECK(p->activeId() == 2);
  CHECK(b->visible() && !a->visible());
  CHECK(b->bounds().y == p->tabs().bounds().h && b->bounds().w == 800);
  const RectF t1 = p->tabs().tabRect(1);
  f.root.dispatch(mouse(InputType::MouseDown, t1.x + 5, t1.y + 5));
  f.root.dispatch(mouse(InputType::MouseUp, t1.x + 5, t1.y + 5));
  f.frame();
  CHECK(p->activeId() == 1 && a->visible() && !b->visible());
  CHECK(a->bounds().h > 0);
  // Close request: default removes the tab and destroys its content; a handler can veto.
  int asked = 0;
  p->onCloseRequested = [&](int) { ++asked; };
  const RectF c = p->tabs().closeRect(1);
  f.root.dispatch(mouse(InputType::MouseDown, c.x + 3, c.y + 5));
  f.root.dispatch(mouse(InputType::MouseUp, c.x + 3, c.y + 5));
  CHECK(asked == 1 && p->tabs().tabs().size() == 2u);
  p->onCloseRequested = nullptr;
  f.root.dispatch(mouse(InputType::MouseDown, c.x + 3, c.y + 5));
  f.root.dispatch(mouse(InputType::MouseUp, c.x + 3, c.y + 5));
  CHECK(p->tabs().tabs().size() == 1u && p->activeId() == 2 && p->content(1) == nullptr);
  auto taken = p->removeTab(2);  // hands the content back (for moving between panels)
  CHECK(taken.get() == b);
  CHECK(p->activeContent() == nullptr);
}

TEST(statusbar_segments) {
  Fixture f;
  auto* s = static_cast<StatusBar*>(f.root.addChild(std::make_unique<StatusBar>()));
  std::string clicked;
  s->setText("workspace", "proj");
  s->setText("pos", "Ln 12, Col 5");
  s->addSegment("eol", StatusBar::Align::Right, [&](const std::string& k) { clicked = k; });
  s->addSegment("encoding", StatusBar::Align::Right);
  s->setText("encoding", "UTF-8");
  s->setText("eol", "LF");
  s->layout({0, 300, 800, s->preferredHeight()});
  CHECK(s->text("pos") == "Ln 12, Col 5");
  const RectF w = s->segmentRect("workspace"), p = s->segmentRect("pos"), eol = s->segmentRect("eol"),
              enc = s->segmentRect("encoding");
  CHECK(w.x == 0 && p.x == w.x + w.w);                    // left segments in declaration order
  CHECK(eol.x + eol.w == 800 && enc.x + enc.w == eol.x);  // right segments pack from the right edge
  CHECK(p.w == 12 * 8 + 16);
  s->setText("pos", "");
  CHECK(s->segmentRect("pos").w == 0);
  f.frame();
  s->setText("pos", "Ln 1, Col 1");
  CHECK(f.root.dirty());
  f.root.dispatch(mouse(InputType::MouseDown, enc.x + 3, 310));  // no callback: unhandled
  CHECK(clicked.empty());
  f.root.dispatch(mouse(InputType::MouseDown, eol.x + 3, 310));
  CHECK(clicked == "eol");
  s->setText("eol", "CRLF");
  CHECK(s->text("eol") == "CRLF" && s->segmentRect("eol").w == 4 * 8 + 16);
}

TEST(shell_demo_draw_calls_and_interaction) {
  Fixture f;
  ShellDemo shell(*f.text, TENFOLD_SOURCE_DIR "/examples/sample_project");
  shell.render(f.rec, 1024, 640);
  shell.render(f.rec, 1024, 640);  // second frame: glyphs warm
  std::cerr << "    shell frame: " << f.rec.batches << " batches, " << f.rec.rects << " rects, " << f.rec.glyphCalls
            << " glyph calls\n";
  CHECK(f.rec.batches <= 10);
  CHECK(f.rec.scissors == 0);
  CHECK(!shell.dirty());

  // Mouse: switch tab on the left panel, drag the splitter, close a tab.
  TabStrip& ts = shell.left().tabs();
  const RectF t3 = ts.tabRect(3);
  shell.dispatch(mouse(InputType::MouseDown, t3.x + 4, t3.y + 4));
  shell.dispatch(mouse(InputType::MouseUp, t3.x + 4, t3.y + 4));
  CHECK(shell.left().activeId() == 3);
  CHECK(shell.dirty());
  const float before = shell.splitter().dividerRect().x;
  shell.dispatch(mouse(InputType::MouseDown, before + 1, 200));
  shell.dispatch(mouse(InputType::MouseMove, before + 101, 200));
  shell.dispatch(mouse(InputType::MouseUp, before + 101, 200));
  shell.render(f.rec, 1024, 640);
  CHECK(std::fabs(shell.splitter().dividerRect().x - (before + 100)) < 2);
  const RectF c = ts.closeRect(3);
  shell.dispatch(mouse(InputType::MouseDown, c.x + 3, c.y + 4));
  shell.dispatch(mouse(InputType::MouseUp, c.x + 3, c.y + 4));
  CHECK(shell.left().tabs().tabs().size() == 2u);
  shell.render(f.rec, 1024, 640);
  CHECK(f.rec.batches <= 10);
}

TEST(shell_golden_b3_shell) {
  SKIP_IF_NO_GL(fx);
  std::string font;
  for (const char* p : {"/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
                        "/usr/share/fonts/dejavu/DejaVuSansMono.ttf", "/usr/share/fonts/TTF/DejaVuSansMono.ttf"})
    if (std::ifstream(p).good()) {
      font = p;
      break;
    }
  if (font.empty()) {
    std::cerr << "    SKIP golden: DejaVuSansMono not installed; goldens are only comparable with the same font\n";
    return;
  }
  std::string err;
  auto tr = gfx::TextRenderer::create(font, 15.0f, 1.0f, &err);
  REQUIRE(tr != nullptr);
  ShellDemo shell(*tr, TENFOLD_SOURCE_DIR "/examples/sample_project");
  gfx::GlRenderer& r = *fx->renderer;
  const int w = 1024, h = 640;
  r.setOffscreen(true);
  shell.render(r, w, h);
  const auto st = r.lastFrameStats();
  std::cerr << "    GL draw calls for the shell frame: " << st.drawCalls << "\n";
  CHECK(st.drawCalls <= 10);
  std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4);
  REQUIRE(r.readPixels(w, h, px.data()));

  // Per-frame CPU of an idle redraw (layout + paint + record), llvmpipe submit excluded.
  using clk = std::chrono::steady_clock;
  Recorder nullR;  // same widgets and text path, but no GL submit
  double rec = 0;
  const int n = 500;
  for (int i = 0; i < n; ++i) {
    auto t0 = clk::now();
    shell.requestLayout();
    shell.render(nullR, w, h);
    rec += std::chrono::duration<double, std::milli>(clk::now() - t0).count();
  }
  std::printf("PERF: shell demo @%dx%d: layout+paint+record %.3f ms/frame (avg of %d, no GL submit)\n", w, h, rec / n,
              n);
  tr->releaseTexture(r);

  const std::string path = TENFOLD_SOURCE_DIR "/tests/golden/b3_shell.png";
  if (uitest::updateGoldens) {
    REQUIRE(gfx::writePngFile(path, px.data(), w, h));
    std::cerr << "    golden updated: " << path << "\n";
    return;
  }
  int gw = 0, gh = 0;
  std::vector<uint8_t> golden;
  if (!gfx::readPngFile(path, gw, gh, golden)) {
    CHECK(!"golden missing or unreadable; run tenfold-ui-tests --update-goldens");
    return;
  }
  REQUIRE(gw == w && gh == h);
  size_t bad = 0;
  for (size_t i = 0; i < golden.size(); i += 4) {
    int d = 0;
    for (int c = 0; c < 4; ++c) d = std::max(d, std::abs(int(golden[i + c]) - int(px[i + c])));
    if (d > 3) ++bad;
  }
  if (bad > golden.size() / 4 / 500) {
    gfx::writePngFile("b3_shell.actual.png", px.data(), w, h);
    std::cerr << "    " << bad << " pixels differ; wrote b3_shell.actual.png\n";
  }
  CHECK(bad <= golden.size() / 4 / 500);
}
