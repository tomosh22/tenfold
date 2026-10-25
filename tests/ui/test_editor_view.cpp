// B2a: the TextView widget against a real Editor on a temp file. Input is synthetic InputEvents dispatched through a
// RootWidget; assertions are on Editor state and on what a recording renderer receives (mock 8x14 cells, no GL).
// Two goldens (pinned DejaVuSansMono) and the keystroke-to-paint benchmark need the real font / GL and skip without.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>

#include "../text/text_test_util.h"
#include "app/Editor.h"
#include "ui/gfx/PngWrite.h"
#include "ui/gfx/TextRenderer.h"
#include "ui/shell/EditorApp.h"
#include "ui/widgets/TextView.h"
#include "ui_test.h"

using namespace tf;
using namespace tf::ui;
namespace fs = std::filesystem;

namespace {

// Records rects (with order relative to glyph draws) and counts batches like GlRenderer would split them.
struct Rec : gfx::Renderer {
  struct R {
    gfx::RectF r;
    gfx::Color c;
    int seq;
  };
  std::vector<R> rects;
  int seq = 0, glyphCalls = 0, lastGlyphSeq = -1, batches = 0, lastKind = 0;
  void kind(int k) {
    if (k != lastKind) ++batches;
    lastKind = k;
  }
  void beginFrame(int, int, gfx::Color) override {
    rects.clear();
    seq = glyphCalls = batches = lastKind = 0;
    lastGlyphSeq = -1;
  }
  void drawRect(const gfx::RectF& r, gfx::Color c) override {
    rects.push_back({r, c, seq++});
    kind(1);
  }
  void drawLine(float, float, float, float, float, gfx::Color) override { kind(1); }
  void drawGlyphs(gfx::TextureId, const gfx::GlyphQuad*, int, gfx::Color) override {
    ++glyphCalls;
    lastGlyphSeq = seq++;
    kind(2);
  }
  void pushScissor(const gfx::RectF&) override {}
  void popScissor() override {}
  gfx::TextureId createTexture(int, int, gfx::TextureFormat) override { return 1; }
  void updateTexture(gfx::TextureId, int, int, int, int, const void*, int) override {}
  void destroyTexture(gfx::TextureId) override {}
  void endFrame() override {}
  bool readPixels(int, int, uint8_t*) override { return false; }
};

bool same(gfx::Color a, gfx::Color b) { return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a; }

std::string str(const std::vector<Selection>& v) {
  std::string s;
  for (const Selection& x : v)
    s += "(" + std::to_string(x.anchor.line) + ":" + std::to_string(x.anchor.column) + "-" +
         std::to_string(x.head.line) + ":" + std::to_string(x.head.column) + ")";
  return s;
}

InputEvent mouse(InputType t, float x, float y, uint8_t mods = 0, int clicks = 1) {
  InputEvent e;
  e.type = t;
  e.x = x;
  e.y = y;
  e.mods = mods;
  e.clicks = clicks;
  e.buttons = t == InputType::MouseUp ? 0 : 1;
  return e;
}
InputEvent keyEv(Key k, uint8_t mods = 0) {
  InputEvent e;
  e.type = InputType::KeyDown;
  e.key = k;
  e.mods = mods;
  return e;
}
InputEvent textEv(const std::string& s) {
  InputEvent e;
  e.type = InputType::Text;
  e.text = s;
  return e;
}

// 8 px cells, 14 px lines, gutter 40 + half cell: text starts at x = 44. A real Editor over a temp file.
struct Fx {
  fs::path dir;
  std::shared_ptr<int64_t> clock = std::make_shared<int64_t>(0);
  std::unique_ptr<Editor> ed;
  Document* doc = nullptr;
  std::unique_ptr<gfx::TextRenderer> text;
  std::unique_ptr<RootWidget> root;
  std::unique_ptr<EditorApp> app;  // keys go through the default bindings and the command registry
  TextView* tv = nullptr;
  Rec rec;
  int w, h;

  static std::unique_ptr<gfx::TextRenderer> mockText() {
    gfx::FontStack stack;
    stack.add(std::make_shared<tftext::MockFont>(
        U"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ._-:,;(){}[]=+*/<>\"'漢字"));
    return gfx::TextRenderer::fromFonts(std::move(stack));
  }

  explicit Fx(const std::string& content, int w_ = 640, int h_ = 280, std::unique_ptr<gfx::TextRenderer> tr = nullptr)
      : w(w_), h(h_) {
    static int counter = 0;
    dir = uitest::tempRoot() /
          ("tenfold_tv_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_" +
           std::to_string(counter++));
    fs::create_directories(dir);
    {
      std::ofstream out(dir / "a.txt", std::ios::binary);
      out << content;
    }
    Editor::Options o;
    auto c = clock;
    o.clock = [c] { return *c; };
    ed = std::make_unique<Editor>(dir, o);
    doc = ed->open("a.txt");
    ed->gotoLocation({doc->path(), {0, 0}});
    text = tr ? std::move(tr) : mockText();
    root = std::make_unique<RootWidget>(*text);
    auto v = std::make_unique<TextView>(*ed);
    tv = v.get();
    tv->settings().smoothScroll = false;
    tv->settings().caretBlink = false;
    tv->settings().clock = [c] { return *c; };
    root->addChild(std::move(v));
    tv->setFocus();
    app = std::make_unique<EditorApp>(*root, *ed);
    frame();
  }
  ~Fx() {
    app.reset();
    root.reset();
    ed.reset();
    std::error_code ec;
    fs::remove_all(dir, ec);
  }
  void frame() { root->render(rec, w, h); }
  void tick() { root->tick(*clock); }
  void key(Key k, uint8_t mods = 0) {
    app->handleEvent(keyEv(k, mods), *clock);
    app->tick(*clock);  // the app loop ticks after every event batch
  }
  void type(const std::string& s) {
    for (char ch : s) {
      *clock += 50;
      root->dispatch(textEv(std::string(1, ch)));
    }
  }
  // Pixel of the caret position p (x on the boundary, y mid-line), in the view's current scroll state.
  std::pair<float, float> at(Position p) const {
    const gfx::RectF r = tv->caretRect(p);
    return {r.x, r.y + r.h / 2};
  }
  void down(Position p, uint8_t mods = 0, int clicks = 1) {
    auto [x, y] = at(p);
    root->dispatch(mouse(InputType::MouseDown, x, y, mods, clicks));
  }
  void move(Position p, uint8_t mods = 0) {
    auto [x, y] = at(p);
    root->dispatch(mouse(InputType::MouseMove, x, y, mods));
  }
  void up(Position p) {
    auto [x, y] = at(p);
    root->dispatch(mouse(InputType::MouseUp, x, y));
  }
  void click(Position p, uint8_t mods = 0, int clicks = 1) {
    down(p, mods, clicks);
    up(p);
  }
  std::string sel() const { return str(ed->selections()); }
  std::string txt() const { return doc->buffer().text(); }
};

std::string numbered(int n) {
  std::string s;
  for (int i = 0; i < n; ++i) s += "line " + std::to_string(i) + "\n";
  return s;
}

}  // namespace

TEST(textview_click_and_hit_testing_with_tabs_and_wide_chars) {
  Fx f("hello world\n\tabc\n\xE6\xBC\xA2\xE5\xAD\x97x\n");
  f.click({0, 5});
  CHECK_EQ(f.sel(), std::string("(0:5-0:5)"));
  // The tab occupies columns 0..3 (32 px): its far boundary is byte 1, its near boundary byte 0.
  f.click({1, 1});
  CHECK_EQ(f.sel(), std::string("(1:1-1:1)"));
  const float x1 = f.tv->caretRect({1, 1}).x, y1 = f.tv->caretRect({1, 1}).y + 7;
  f.root->dispatch(mouse(InputType::MouseDown, x1 - 25, y1));  // 7 px into the tab: nearest boundary is before it
  f.root->dispatch(mouse(InputType::MouseUp, x1 - 25, y1));
  CHECK_EQ(f.sel(), std::string("(1:0-1:0)"));
  // Wide characters: 3 bytes each, 2 cells (16 px). Boundaries at bytes 0, 3, 6, 7.
  f.click({2, 3});
  CHECK_EQ(f.sel(), std::string("(2:3-2:3)"));
  CHECK(f.tv->caretRect({2, 3}).x - f.tv->caretRect({2, 0}).x == 16);
  f.click({2, 6});
  CHECK_EQ(f.sel(), std::string("(2:6-2:6)"));
  const gfx::RectF r6 = f.tv->caretRect({2, 6});
  f.root->dispatch(mouse(InputType::MouseDown, r6.x - 5, r6.y + 7));  // closer to byte 6 than 3
  f.root->dispatch(mouse(InputType::MouseUp, r6.x - 5, r6.y + 7));
  CHECK_EQ(f.sel(), std::string("(2:6-2:6)"));
  // Click far right of a short line and below the last line: clamps.
  f.root->dispatch(mouse(InputType::MouseDown, 600, f.tv->caretRect({0, 0}).y + 7));
  f.root->dispatch(mouse(InputType::MouseUp, 600, 7));
  CHECK_EQ(f.sel(), std::string("(0:11-0:11)"));
  f.root->dispatch(mouse(InputType::MouseDown, 100, 270));
  f.root->dispatch(mouse(InputType::MouseUp, 100, 270));
  CHECK_EQ(f.sel().substr(0, 3), std::string("(3:"));  // the empty last line
  // Status text: visual column after a tab.
  f.click({1, 1});
  CHECK_EQ(f.tv->positionText(), std::string("Ln 2, Col 5"));
}

TEST(textview_shift_double_triple_click_and_drag) {
  Fx f("hello world\nsecond line\nthird");
  f.click({0, 2});
  f.click({0, 8}, ModShift);
  CHECK_EQ(f.sel(), std::string("(0:2-0:8)"));
  f.click({0, 0}, ModShift);  // extends from the same anchor, backwards
  CHECK_EQ(f.sel(), std::string("(0:2-0:0)"));
  f.click({0, 7}, 0, 2);  // double click inside "world"
  CHECK_EQ(f.sel(), std::string("(0:6-0:11)"));
  f.up({0, 7});
  f.click({1, 3}, 0, 3);  // triple click: the line and its break
  CHECK_EQ(f.sel(), std::string("(1:0-2:0)"));
  f.click({2, 2}, 0, 3);  // the last line has no break to include
  CHECK_EQ(f.sel(), std::string("(2:0-2:5)"));
  // Plain drag.
  f.down({0, 3});
  f.move({1, 4});
  CHECK_EQ(f.sel(), std::string("(0:3-1:4)"));
  f.move({0, 1});
  CHECK_EQ(f.sel(), std::string("(0:3-0:1)"));
  f.up({0, 1});
  f.move({2, 2});  // after release the move is ignored
  CHECK_EQ(f.sel(), std::string("(0:3-0:1)"));
  // Double-click-and-drag grows by whole... from the word: anchored at the word start / end.
  f.down({0, 7}, 0, 2);
  f.move({1, 3});
  CHECK_EQ(f.sel(), std::string("(0:6-1:3)"));
  f.move({0, 1});
  CHECK_EQ(f.sel(), std::string("(0:11-0:1)"));
  f.up({0, 1});
}

TEST(textview_alt_drag_column_and_ctrl_click_carets) {
  Fx f("abcdefgh\nab\nabcdefgh\nabcdefgh\n");
  f.down({0, 2}, ModAlt);
  CHECK_EQ(f.ed->selections().size(), 2u);  // alt-click alone adds a caret like ctrl-click
  f.move({2, 5}, ModAlt);
  const auto& s = f.ed->selections();
  CHECK_EQ(s.size(), 3u);
  CHECK_EQ(s[0].head.line, 2);  // primary on the line of the drag end
  std::vector<std::string> got;
  for (const Selection& x : s) got.push_back(str({x}));
  std::sort(got.begin(), got.end());
  CHECK_EQ(got[0], std::string("(0:2-0:5)"));
  CHECK_EQ(got[1], std::string("(1:2-1:2)"));  // short line: clamped to its end
  CHECK_EQ(got[2], std::string("(2:2-2:5)"));
  f.up({2, 5});

  f.click({0, 1});
  f.click({2, 3}, ModCtrl);
  CHECK_EQ(f.sel(), std::string("(2:3-2:3)(0:1-0:1)"));  // new caret is primary
  f.click({3, 4}, ModCtrl);
  CHECK_EQ(f.ed->selections().size(), 3u);
  f.down({1, 0}, ModCtrl);  // ctrl-drag grows the added selection
  f.move({1, 2}, ModCtrl);
  f.up({1, 2});
  CHECK_EQ(f.ed->selections().size(), 4u);
  CHECK_EQ(str({f.ed->selections()[0]}), std::string("(1:0-1:2)"));
  f.click({0, 0});  // plain click collapses everything
  CHECK_EQ(f.sel(), std::string("(0:0-0:0)"));
}

TEST(textview_keyboard_edits_and_moves) {
  Fx f("one two\nthree\n");
  f.type("ab");
  CHECK_EQ(f.txt(), std::string("abone two\nthree\n"));
  f.key(Key::Backspace);
  CHECK_EQ(f.txt(), std::string("aone two\nthree\n"));
  f.key(Key::Right, ModCtrl);                   // word move
  CHECK_EQ(f.sel(), std::string("(0:5-0:5)"));  // Editor's word motion lands on the start of the next word
  f.key(Key::Left, ModShift);
  f.key(Key::Left, ModShift);
  CHECK_EQ(f.sel(), std::string("(0:5-0:3)"));
  f.key(Key::Delete);
  CHECK_EQ(f.txt(), std::string("aontwo\nthree\n"));
  f.key(Key::End);
  CHECK_EQ(f.sel(), std::string("(0:6-0:6)"));
  f.key(Key::Enter);
  f.key(Key::Tab);
  CHECK_EQ(f.txt(), std::string("aontwo\n    \nthree\n"));
  f.key(Key::Home, ModCtrl);
  CHECK_EQ(f.sel(), std::string("(0:0-0:0)"));
  f.key(Key::End, ModCtrl | ModShift);
  CHECK_EQ(f.sel(), std::string("(0:0-3:0)"));
  f.key(Key::Left);  // collapses to the selection's edge
  CHECK_EQ(f.sel(), std::string("(0:0-0:0)"));
  f.key(Key::Down);
  CHECK_EQ(f.sel(), std::string("(1:0-1:0)"));
  f.key(Key::Backspace, ModCtrl);  // delete previous word: here the line break
  CHECK_EQ(f.txt(), std::string("aontwo    \nthree\n"));
  f.key(Key::Unknown);
  f.key(Key('A'), ModCtrl);
  CHECK_EQ(f.sel(), std::string("(0:0-2:0)"));
  f.key(Key('Z'), ModCtrl);
  f.key(Key('Y'), ModCtrl);
  f.key(Key('Z'), ModCtrl | ModShift);  // ctrl+shift+z is redo too: nothing left to redo
  CHECK_EQ(f.txt(), std::string("aontwo    \nthree\n"));
  // Ctrl+Alt+Down adds a caret below; Escape collapses.
  f.click({0, 1});
  f.key(Key::Down, ModCtrl | ModAlt);
  CHECK_EQ(f.ed->selections().size(), 2u);
  f.key(Key::Escape);
  CHECK_EQ(f.ed->selections().size(), 1u);
}

TEST(textview_scripted_type_select_delete_undo) {
  Fx f("");
  f.type("hello");
  for (int i = 0; i < 3; ++i) f.key(Key::Left, ModShift);
  CHECK_EQ(f.sel(), std::string("(0:5-0:2)"));
  f.key(Key::Delete);
  CHECK_EQ(f.txt(), std::string("he"));
  CHECK_EQ(f.sel(), std::string("(0:2-0:2)"));
  f.key(Key('Z'), ModCtrl);  // undo restores the text AND the selection
  CHECK_EQ(f.txt(), std::string("hello"));
  CHECK_EQ(f.sel(), std::string("(0:5-0:2)"));
  // ... visually: one selection rect over columns 2..5 on line 0 and the caret at its head.
  f.frame();
  bool rect = false;
  for (const auto& r : f.rec.rects)
    if (r.c.a == 0.38f && r.r.x == 44 + 16 && r.r.w == 24 && r.r.y == 0 && r.r.h == 14) rect = true;
  CHECK(rect);
  f.key(Key('Z'), ModCtrl);
  CHECK_EQ(f.txt(), std::string(""));
  f.key(Key('Y'), ModCtrl);
  f.key(Key('Y'), ModCtrl);
  CHECK_EQ(f.txt(), std::string("he"));
  CHECK_EQ(f.sel(), std::string("(0:2-0:2)"));
}

TEST(textview_scrolling_keep_caret_visible_and_page_size) {
  Fx f(numbered(200) + std::string(300, 'x'));  // 280 px = 20 lines
  CHECK_EQ(f.tv->visibleLineCount(), 20);
  f.key(Key::PageDown);  // Editor::setPageLines came from the viewport height
  CHECK_EQ(f.ed->selections()[0].head.line, 20);
  f.key(Key::Home, ModCtrl);
  CHECK_EQ(f.tv->viewport().y(), 0.0f);
  f.key(Key::End, ModCtrl);
  const int last = f.doc->buffer().lineCount() - 1;
  gfx::RectF c = f.tv->caretRect({last, 0});
  CHECK(c.y >= 0 && c.y + c.h <= 280 && f.tv->viewport().y() > 0);
  f.key(Key::Up);
  c = f.tv->caretRect({last - 1, 0});
  CHECK(c.y >= 0 && c.y + c.h <= 280);
  // Typing far down keeps the view on the caret; so does an undo that jumps back up.
  f.key(Key::Home, ModCtrl);
  f.key(Key::Down);
  for (int i = 0; i < 40; ++i) f.key(Key::Down);
  c = f.tv->caretRect(f.ed->selections()[0].head);
  CHECK(c.y >= 0 && c.y + c.h <= 280);
  // Horizontal: End on the 300 character line scrolls right so the caret is inside the text area.
  f.key(Key::End, ModCtrl);
  CHECK(f.tv->viewport().x() > 0);
  c = f.tv->caretRect(f.ed->selections()[0].head);
  CHECK(c.x >= 44 && c.x + c.w <= 640);
  f.key(Key::Home);
  CHECK_EQ(f.tv->viewport().x(), 0.0f);
  // Wheel scrolls without touching the carets.
  const std::string before = f.sel();
  InputEvent wheel;
  wheel.type = InputType::Wheel;
  wheel.x = 100;
  wheel.y = 100;
  wheel.dy = -1;
  const float y0 = f.tv->viewport().y();
  f.root->dispatch(wheel);
  CHECK_EQ(f.tv->viewport().y(), y0 + 3 * 14);
  wheel.dy = 1000;
  f.root->dispatch(wheel);
  CHECK_EQ(f.tv->viewport().y(), 0.0f);
  CHECK_EQ(f.sel(), before);
}

TEST(textview_smooth_scroll_blink_and_dirty_tracking) {
  Fx f(numbered(300));
  f.tv->settings().smoothScroll = true;
  f.tv->settings().caretBlink = true;
  f.root->tick(0);
  f.frame();
  CHECK(!f.root->dirty());
  CHECK(f.root->tick(0) >= 0);  // blink wants a wake-up
  // Idle ticks neither dirty the tree nor change anything.
  f.root->tick(100);
  CHECK(!f.root->dirty());
  // Blink: off after caretBlinkMs, on again after two periods.
  f.root->tick(540);
  CHECK(f.root->dirty());
  f.frame();
  bool caretDrawn = false;
  for (const auto& r : f.rec.rects)
    if (same(r.c, Theme().text) && r.seq > f.rec.lastGlyphSeq) caretDrawn = true;
  CHECK(!caretDrawn);
  f.root->tick(1070);
  f.frame();
  for (const auto& r : f.rec.rects)
    if (same(r.c, Theme().text) && r.seq > f.rec.lastGlyphSeq && r.r.w <= 2) caretDrawn = true;
  CHECK(caretDrawn);
  // Smooth wheel: the target moves at once, the view follows over several ticks.
  InputEvent wheel;
  wheel.type = InputType::Wheel;
  wheel.x = 100;
  wheel.y = 100;
  wheel.dy = -5;
  f.root->dispatch(wheel);
  CHECK_EQ(f.tv->viewport().targetY(), 5.0f * 3 * 14);
  CHECK_EQ(f.tv->viewport().y(), 0.0f);
  f.frame();
  int64_t t = 2000;
  int ticks = 0;
  float prev = 0;
  while (f.root->tick(t) >= 0 && f.tv->viewport().animating() && ticks < 200) {
    CHECK(f.root->dirty());
    CHECK(f.tv->viewport().y() >= prev);
    prev = f.tv->viewport().y();
    f.frame();
    t += 16;
    ++ticks;
  }
  CHECK_EQ(f.tv->viewport().y(), 5.0f * 3 * 14);
  CHECK(ticks > 3 && ticks < 100);
  // A change made behind the view's back (another view / LSP edit) is noticed by the next tick.
  f.frame();
  CHECK(!f.root->dirty());
  f.ed->insertText("zz");
  f.root->tick(t + 5000);
  CHECK(f.root->dirty());
}

TEST(textview_paint_layers_selections_carets_and_draw_calls) {
  Fx f("alpha beta\n\tgamma\ndelta\n");
  f.ed->setSelections({{{0, 0}, {0, 5}}, caretAt({1, 3}), caretAt({2, 5})});
  f.tick();
  f.frame();
  // Draw calls stay a handful: rect batch, glyph batch(es) and the overlay rect batch.
  CHECK(f.rec.batches <= 5);
  int carets = 0;
  for (const auto& r : f.rec.rects)
    if (same(r.c, Theme().text)) {
      ++carets;
      CHECK(r.seq > f.rec.lastGlyphSeq);  // overlay: after all glyphs
      CHECK(r.r.h == 14);
    }
  CHECK_EQ(carets, 3);
  int selRects = 0;
  for (const auto& r : f.rec.rects)
    if (r.c.a == 0.38f) {
      ++selRects;
      CHECK(r.seq < f.rec.lastGlyphSeq);  // selection: behind the text
      CHECK(r.r.x == 44 && r.r.w == 40);
    }
  CHECK_EQ(selRects, 1);
  // Caret on the tab line: after one tab (4 columns) and 2 chars -> column 6.
  bool found = false;
  for (const auto& r : f.rec.rects)
    if (same(r.c, Theme().text) && r.r.y == 14) found = std::fabs(r.r.x - (44 + 6 * 8)) < 1e-3;
  CHECK(found);
}

TEST(textview_pinned_view_activates_its_document) {
  Fx f("first\n");
  {
    std::ofstream out(f.dir / "b.txt", std::ios::binary);
    out << "second\nline\n";
  }
  Document* b = f.ed->open("b.txt");
  REQUIRE(b != nullptr);
  auto v = std::make_unique<TextView>(*f.ed, b);
  TextView* second = v.get();
  second->settings().smoothScroll = false;
  f.root->addChild(std::move(v));
  f.frame();
  CHECK(!second->isCurrent() && f.tv->isCurrent());
  f.root->dispatch(mouse(InputType::MouseDown, 100, 7));  // both fill the root: the later child is on top
  CHECK(second->isCurrent());
  CHECK(f.ed->currentDocument() == b);
  f.root->dispatch(mouse(InputType::MouseUp, 100, 7));
  f.root->dispatch(textEv("x"));
  CHECK_EQ(b->buffer().line(0), std::string("xsecond").substr(0, 0) + b->buffer().line(0));
  CHECK(b->buffer().line(0).find('x') != std::string::npos);
  CHECK_EQ(f.doc->buffer().line(0), std::string("first"));
}

// ---------------------------------------------------------------------------------------------------------------
// Goldens + benchmark (real font)
// ---------------------------------------------------------------------------------------------------------------
namespace {

std::string dejavuMono() {
  for (const char* p : {"/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
                        "/usr/share/fonts/dejavu/DejaVuSansMono.ttf", "/usr/share/fonts/TTF/DejaVuSansMono.ttf"})
    if (std::ifstream(p).good()) return p;
  return {};
}

std::string readFile(const std::string& p) {
  std::ifstream in(p, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), {});
}

void compareGolden(const char* name, const std::vector<uint8_t>& px, int w, int h) {
  const std::string path = std::string(TENFOLD_SOURCE_DIR "/tests/golden/") + name + ".png";
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
    gfx::writePngFile(std::string(name) + ".actual.png", px.data(), w, h);
    std::cerr << "    " << bad << " pixels differ; wrote " << name << ".actual.png\n";
  }
  CHECK(bad <= golden.size() / 4 / 500);
}

void goldenCase(const char* name, bool multi) {
  auto* gl = uitest::glFixture();
  if (!gl) {
    std::cerr << "    SKIP: no display/OpenGL 3.3 (run under xvfb-run -a)\n";
    return;
  }
  const std::string font = dejavuMono();
  if (font.empty()) {
    std::cerr << "    SKIP golden: DejaVuSansMono not installed; goldens are only comparable with the same font\n";
    return;
  }
  std::string err;
  auto tr = gfx::TextRenderer::create(font, 15.0f, 1.0f, &err);
  REQUIRE(tr != nullptr);
  Fx f(readFile(TENFOLD_SOURCE_DIR "/examples/sample_project/src/main.cpp"), 800, 480, std::move(tr));
  if (multi) {
    const int n = f.doc->buffer().lineCount();
    f.ed->setSelections({{{4, 4}, {6, 12}}, caretAt({10, 8}), caretAt({12, 0}), caretAt({std::min(15, n - 1), 6})});
  }
  f.tick();
  f.text->releaseTexture(f.rec);  // the warm-up frame went to the recording renderer: re-create the atlas texture on GL
  gfx::GlRenderer& r = *gl->renderer;
  r.setOffscreen(true);
  f.root->requestLayout();
  f.root->render(r, f.w, f.h);
  const auto st = r.lastFrameStats();
  std::cerr << "    GL draw calls: " << st.drawCalls << "\n";
  CHECK(st.drawCalls <= 8);
  std::vector<uint8_t> px(static_cast<size_t>(f.w) * f.h * 4);
  REQUIRE(r.readPixels(f.w, f.h, px.data()));
  f.text->releaseTexture(r);
  compareGolden(name, px, f.w, f.h);
}

}  // namespace

TEST(textview_golden_b2_plain) { goldenCase("b2_plain", false); }
TEST(textview_golden_b2_selection_carets) { goldenCase("b2_selection_carets", true); }

// Keystroke-to-paint (§1.7 budget 8 ms): synthetic key event -> Editor edit -> view sync -> layout/paint recording on a
// 100k-line file. The GL submit (llvmpipe here) is excluded from the CPU number and reported separately.
TEST(textview_perf_keystroke_to_paint_100k_lines) {
  std::string content;
  content.reserve(100000 * 60);
  for (int i = 0; i < 100000; ++i)
    content +=
        "    for (int k" + std::to_string(i) + " = 0; k < n_" + std::to_string(i % 97) + "; ++k) { total += k; }\n";
  const std::string font = dejavuMono();
  std::string err;
  auto tr = gfx::TextRenderer::create(font, 15.0f, 1.0f, &err);
  if (!tr) {
    std::cerr << "    SKIP: " << err << "\n";
    return;
  }
  Fx f(content, 1920, 1080, std::move(tr));
  using clk = std::chrono::steady_clock;
  auto ms = [](clk::time_point a, clk::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
  };
  auto* gl = uitest::glFixture();
  for (int carets : {1, 1000}) {
    if (carets > 1) {
      std::vector<Selection> v;
      for (int i = 0; i < carets; ++i) v.push_back(caretAt({i, 4}));
      f.ed->setSelections(std::move(v));
    }
    f.tick();
    for (int i = 0; i < 20; ++i) {  // warm the glyph atlas and caches
      f.root->dispatch(textEv("a"));
      f.frame();
    }
    const int n = carets > 1 ? 100 : 400;
    std::vector<double> edit, paint;
    for (int i = 0; i < n; ++i) {
      *f.clock += 50;
      auto t0 = clk::now();
      f.root->dispatch(textEv("a"));  // key -> Editor::insertText -> view sync (keep-visible, dirty)
      auto t1 = clk::now();
      f.root->requestLayout();
      f.frame();  // layout + paint + glyph/rect recording
      auto t2 = clk::now();
      edit.push_back(ms(t0, t1));
      paint.push_back(ms(t1, t2));
    }
    CHECK(f.doc->buffer().line(0).find(std::string(static_cast<size_t>(n), 'a')) != std::string::npos);
    std::vector<double> total(n);
    for (int i = 0; i < n; ++i) total[i] = edit[i] + paint[i];
    std::sort(total.begin(), total.end());
    double sumE = 0, sumP = 0;
    for (int i = 0; i < n; ++i) sumE += edit[i], sumP += paint[i];
    // Scroll frames: wheel notch + frame, no edit.
    InputEvent wheel;
    wheel.type = InputType::Wheel;
    wheel.x = 500;
    wheel.y = 500;
    double scroll = 0;
    const int ns = 200;
    for (int i = 0; i < ns; ++i) {
      wheel.dy = i % 2 ? 1.0f : -1.0f;
      auto t0 = clk::now();
      f.root->dispatch(wheel);
      f.frame();
      scroll += ms(t0, clk::now());
    }
    double submit = -1;
    if (gl) {  // full frame on llvmpipe minus the same frame recorded into the null renderer
      gfx::GlRenderer& r = *gl->renderer;
      r.setOffscreen(true);
      f.text->releaseTexture(f.rec);  // the atlas texture id came from the recording renderer; never hand it to GL
      double full = 0, rec = 0;
      for (int i = 0; i < 30; ++i) {
        f.root->requestLayout();
        auto t0 = clk::now();
        f.root->render(r, 1920, 1080);
        auto t1 = clk::now();
        f.root->requestLayout();
        f.frame();
        auto t2 = clk::now();
        full += ms(t0, t1);
        rec += ms(t1, t2);
      }
      submit = (full - rec) / 30;
      f.text->releaseTexture(r);
    }
    std::printf(
        "PERF: textview keystroke-to-paint, 100k lines, %d caret(s) @1920x1080: edit+sync %.3f ms, layout+paint+record "
        "%.3f ms, total avg %.3f ms / p99 %.3f ms (budget 8 ms); scroll frame %.3f ms; llvmpipe submit %s (avg of "
        "%d)\n",
        carets, sumE / n, sumP / n, (sumE + sumP) / n, total[static_cast<size_t>(n * 0.99)], scroll / ns,
        submit < 0 ? "n/a" : (std::to_string(submit).substr(0, 5) + " ms").c_str(), n);
#if defined(NDEBUG) && !defined(__SANITIZE_ADDRESS__)
    CHECK((sumE + sumP) / n < 8.0);
#endif
  }
}
