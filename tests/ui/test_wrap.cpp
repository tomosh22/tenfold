// B2c: soft word wrap. WrapLayout against a mock-font TextLayout (8 px cells; the cells in a test are `cols` wide),
// then the TextView scripted through the real key map: caret Up / Down / Home / End by visual row, mouse hits on
// continuation rows, selection and scrollbar in rows, the toggle command. One golden with wrap on (real font), the
// keystroke-to-paint benchmarks with wrap on (100k lines and one 50 MB line).
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
#include "ui/view/WrapLayout.h"
#include "ui/widgets/TextView.h"
#include "ui_test.h"

using namespace tf;
using namespace tf::ui;
namespace fs = std::filesystem;

namespace {

// ---------------------------------------------------------------------------------------------------------------
// WrapLayout on its own
// ---------------------------------------------------------------------------------------------------------------

gfx::FontStack monoStack() {
  gfx::FontStack stack;
  stack.add(std::make_shared<tftext::MockFont>(U"abcdefghijklmnopqrstuvwxyz ._-\t"));
  return stack;
}

struct WL {
  gfx::FontStack stack{monoStack()};
  gfx::Atlas atlas;
  gfx::TextLayout tl;
  TextBuffer buf;
  WrapLayout w;
  WL(const std::string& text, int cols, int tab = 4) : tl(stack, atlas, tab), buf(text) { w.bind(&buf, &tl, cols); }
  std::vector<int> starts(int line) {
    std::vector<int> v;
    const int n = w.rowsOf(line);
    for (int r = 0; r < n; ++r) v.push_back(w.rowStart(line, r));
    return v;
  }
};

using V = std::vector<int>;

}  // namespace

TEST(wraplayout_breaks_after_the_last_space_that_fits) {
  WL a("hello world foo", 10);
  CHECK(a.starts(0) == (V{0, 6}));
  CHECK_EQ(a.w.rowEnd(0, 0), 6);
  CHECK_EQ(a.w.rowEnd(0, 1), 15);
  // One space may hang past the edge (the view reserves a cell for it).
  WL b("abcdefghij klm", 10);
  CHECK(b.starts(0) == (V{0, 11}));
  CHECK_EQ(b.w.rowLastCaret(0, 0), 10);  // the caret stays before the hanging space
  WL c("foo    bar", 6);
  CHECK(c.starts(0) == (V{0, 7}));
  // A line that fits stays one row, also with its trailing hanging space.
  WL d("exactly ten ", 11);
  CHECK(d.starts(0) == (V{0}));
  CHECK_EQ(d.w.rowsOf(0), 1);
}

TEST(wraplayout_cuts_a_word_only_when_it_does_not_fit_a_row) {
  WL a("abcdefghijklmnopqrstuvwxyz", 10);
  CHECK(a.starts(0) == (V{0, 10, 20}));
  CHECK_EQ(a.w.rowEnd(0, 2), 26);
  // The long word moves to its own row first, then is cut.
  WL b("ab cdefghijklmnopqrstuvwxyz", 8);
  CHECK(b.starts(0) == (V{0, 3, 11, 19}));
  // A word that fits on a row of its own is not cut.
  WL c("aaaaa bbbbbbbbb", 10);
  CHECK(c.starts(0) == (V{0, 6}));
}

TEST(wraplayout_counts_tab_stops_from_the_start_of_a_row) {
  WL a("\tab\tcd efgh", 8);  // tab = 4 cells here: cols 0-3, a, b, tab to 8, then "cd"
  CHECK(a.starts(0) == (V{0, 4, 7}));
  CHECK_EQ(a.w.rowIndent(0, 0), 0);
  CHECK_EQ(a.w.rowIndent(0, 1), 4);  // the leading tab is the indent of the continuation rows
  WL b("xxxxxxxx\tyy", 10);          // the tab would end at column 12: it starts the next row
  CHECK(b.starts(0) == (V{0, 8}));
  // On the continuation row the tab is measured from the row start: it is 4 cells wide, "yy" follow.
  CHECK_EQ(b.w.xOf(0, 1, 9), 4 * 8.0f);
  CHECK_EQ(b.w.xOf(0, 1, 11), 6 * 8.0f);
  WL c("a\tb", 20, 8);  // another tab width
  CHECK_EQ(c.w.xOf(0, 0, 2), 8 * 8.0f);
}

TEST(wraplayout_wide_characters_take_two_cells) {
  const std::string han = "\xE6\xBC\xA2\xE5\xAD\x97";  // two characters, 3 bytes and 2 cells each
  WL a(han + han + han, 5);
  CHECK(a.starts(0) == (V{0, 6, 12}));
  WL b(han + han + han, 1);  // not even one cell fits: a character is taken anyway, never an empty row
  CHECK(b.starts(0) == (V{0, 3, 6, 9, 12, 15}));
  WL c("ab" + han, 3);
  CHECK(c.starts(0) == (V{0, 2, 5}));
  CHECK_EQ(c.w.xOf(0, 1, 5), 2 * 8.0f);  // the row "<han1>" is 2 cells wide
}

TEST(wraplayout_continuation_rows_take_the_indent_of_the_first_row) {
  WL a("    foo bar baz qux", 10);
  CHECK(a.starts(0) == (V{0, 8, 12, 16}));
  CHECK_EQ(a.w.rowIndent(0, 0), 0);
  for (int r = 1; r < 4; ++r) CHECK_EQ(a.w.rowIndent(0, r), 4);
  CHECK_EQ(a.w.xOf(0, 1, 8), 4 * 8.0f);   // row 1 starts after the indent
  CHECK_EQ(a.w.xOf(0, 1, 12), 8 * 8.0f);  // "bar " is 4 cells
  // Capped at half the width.
  WL b(std::string(20, ' ') + "abc def ghi", 10);
  CHECK_EQ(b.w.rowIndent(0, 1), 5);
  CHECK(b.w.rowsOf(0) >= 3);
  // An edit inside the indent re-reads it: the indent shrinks with the whitespace.
  WL c("      long words go on and on", 12);
  CHECK_EQ(c.w.rowIndent(0, 1), 6);
}

TEST(wraplayout_empty_lines_and_wrap_off) {
  WL a("\n\nx", 10);
  CHECK_EQ(a.w.rowsOf(0), 1);
  CHECK_EQ(a.w.rowStart(0, 0), 0);
  CHECK_EQ(a.w.rowEnd(0, 0), 0);
  CHECK_EQ(a.w.rowLastCaret(0, 0), 0);
  CHECK_EQ(a.w.visualRowOf({1, 0}), 1);
  CHECK(a.w.positionAt(0, 50.0f) == (Position{0, 0}));
  CHECK_EQ(a.w.totalRows(), 3);
  WL off("a very long line that would wrap", 0);
  CHECK(!off.w.enabled());
  CHECK_EQ(off.w.rowsOf(0), 1);
  CHECK_EQ(off.w.rowEnd(0, 0), 32);
  CHECK_EQ(off.w.firstRowOf(0), 0);
  CHECK_EQ(off.w.lineOfRow(0), 0);
  CHECK(off.w.positionAt(0, 8 * 5.0f) == (Position{0, 5}));
}

TEST(wraplayout_rebinding_to_another_width_reflows) {
  WL a("one two three four five six seven", 10);
  const int rows10 = a.w.rowsOf(0);
  const uint32_t g = a.w.generation();
  a.w.bind(&a.buf, &a.tl, 10);
  CHECK_EQ(a.w.generation(), g);  // same width: nothing is dropped
  a.w.bind(&a.buf, &a.tl, 20);
  CHECK(a.w.generation() != g);
  const int rows20 = a.w.rowsOf(0);
  CHECK(rows20 < rows10 && rows20 >= 2);
  a.w.bind(&a.buf, &a.tl, 100);
  CHECK_EQ(a.w.rowsOf(0), 1);
  a.tl.setTabWidth(8);  // a tab width change re-reads everything on the next bind
  a.w.bind(&a.buf, &a.tl, 10);
  CHECK_EQ(a.w.rowsOf(0), rows10);
}

TEST(wraplayout_maps_lines_to_visual_rows_with_an_estimate_for_unseen_lines) {
  std::string text = "short\n";
  for (int i = 0; i < 3; ++i) text += "aaa bbb ccc ddd eee fff ggg hhh iii jjj\nx\n";
  WL a(text, 10);
  const int lines = a.buf.lineCount();
  CHECK_EQ(a.w.totalRows(), lines);  // nothing looked at yet: one row per line
  CHECK_EQ(a.w.firstRowOf(5), 5);
  const int r2 = a.w.rowsOf(1);  // looking at a line teaches the map
  CHECK(r2 >= 4);
  CHECK_EQ(a.w.totalRows(), lines + r2 - 1);
  CHECK_EQ(a.w.firstRowOf(2), 1 + r2);
  CHECK_EQ(a.w.firstRowOf(5), 5 + r2 - 1);
  // Everything is consistent once all lines were looked at.
  for (int l = 0; l < lines; ++l) a.w.rowsOf(l);
  int total = 0;
  for (int l = 0; l < lines; ++l) {
    CHECK_EQ(a.w.firstRowOf(l), total);
    total += a.w.rowsOf(l);
  }
  CHECK_EQ(a.w.totalRows(), total);
  for (int v = 0; v < total; ++v) {
    int r = -1;
    const int l = a.w.lineOfRow(v, &r);
    CHECK_EQ(a.w.firstRowOf(l) + r, v);
    CHECK(r >= 0 && r < a.w.rowsOf(l));
    CHECK_EQ(a.w.visualRowOf({l, a.w.rowStart(l, r)}), v);
  }
  CHECK_EQ(a.w.lineOfRow(total + 50), lines - 1);  // clamped
  CHECK_EQ(a.w.lineOfRow(-3), 0);
}

TEST(wraplayout_position_at_never_lands_on_the_next_row) {
  WL a("abcdefghij klmnopqrstuvwxyz", 10);  // rows "abcdefghij " and "klmnopqrst" and "uvwxyz"
  CHECK(a.starts(0) == (V{0, 11, 21}));
  CHECK(a.w.positionAt(0, 3 * 8.0f + 2) == (Position{0, 3}));
  CHECK(a.w.positionAt(0, 3 * 8.0f + 5) == (Position{0, 4}));  // nearest boundary
  CHECK(a.w.positionAt(0, 500.0f) == (Position{0, 10}));       // past the end of row 0: before the hanging space
  CHECK(a.w.positionAt(1, 500.0f) == (Position{0, 20}));       // before the last cell of row 1, not row 2's start
  CHECK(a.w.positionAt(2, 500.0f) == (Position{0, 27}));       // the line end
  CHECK(a.w.positionAt(1, -4.0f) == (Position{0, 11}));
  CHECK(a.w.positionAt(99, 0.0f) == (Position{0, 21}));  // clamped to the last row
  CHECK_EQ(a.w.rowOfByte(0, 10), 0);
  CHECK_EQ(a.w.rowOfByte(0, 11), 1);  // a boundary belongs to the later row
  CHECK_EQ(a.w.rowOfByte(0, 27), 2);
  CHECK_EQ(a.w.rowLastCaret(0, 1), 20);
  CHECK_EQ(a.w.rowLastCaret(0, 2), 27);
}

namespace {
struct Rng {
  uint32_t s = 12345;
  int next(int n) {
    s = s * 1664525u + 1013904223u;
    return static_cast<int>((s >> 8) % static_cast<uint32_t>(n));
  }
};
}  // namespace

// Edits keep the rows the edit cannot have changed and drop the rest: after any sequence of edits (and partial
// lookups in between) every line must have exactly the rows a fresh layout of the same text has.
TEST(wraplayout_edits_leave_the_same_rows_as_a_fresh_layout) {
  gfx::FontStack stack = monoStack();
  gfx::Atlas atlas;
  gfx::TextLayout tl(stack, atlas, 4);
  const std::string pieces[] = {"ab",   " ",   "  ", "\t", "word", "\n", "\xE6\xBC\xA2", "x", "longerwordhere",
                                " yz ", "\n\n"};
  Rng rng;
  std::string lastEdit;
  for (int cols : {5, 9, 14}) {
    Document doc =
        Document::fromText("w.txt", "alpha beta gamma delta epsilon\n  indented line of several words\n\nx\n");
    WrapLayout w;
    w.bind(&doc.buffer(), &tl, cols);
    doc.addListener([&](Document&, const Document::Change& c) { w.onChange(c); });
    for (int step = 0; step < 400; ++step) {
      const TextBuffer& b = doc.buffer();
      const int line = rng.next(b.lineCount());
      const int len = static_cast<int>(b.line(line).size());
      int col = rng.next(len + 1);
      while (col > 0 && col < len &&
             (static_cast<unsigned char>(b.line(line)[static_cast<size_t>(col)]) & 0xC0) == 0x80)
        --col;  // stay on a character boundary
      Range r{{line, col}, {line, col}};
      if (rng.next(3) == 0 && col < len) {  // erase up to a few characters
        int e = std::min(len, col + 1 + rng.next(4));
        while (e < len && (static_cast<unsigned char>(b.line(line)[static_cast<size_t>(e)]) & 0xC0) == 0x80) ++e;
        r.end.column = e;
      }
      if (rng.next(8) == 0 && line + 1 < b.lineCount()) r.end = {line + 1, 0};  // a line break goes
      const std::string ins = rng.next(4) == 0 ? "" : pieces[rng.next(11)];
      lastEdit = "[" + std::to_string(r.start.line) + ":" + std::to_string(r.start.column) + "-" +
                 std::to_string(r.end.line) + ":" + std::to_string(r.end.column) + "] <- '" + ins + "'";
      doc.replace(r, ins);
      // Partial lookups leave entries half scanned.
      for (int k = 0; k < 3; ++k) {
        const int l = rng.next(doc.buffer().lineCount());
        switch (rng.next(4)) {
          case 0: w.rowsEstimate(l); break;
          case 1: w.rowOfByte(l, rng.next(static_cast<int>(doc.buffer().line(l).size()) + 1)); break;
          case 2: w.rowStart(l, rng.next(3)); break;
          default: w.firstRowOf(l); break;
        }
      }
      if (step % 7 == 0 || step == 399) {
        WrapLayout fresh;
        fresh.bind(&doc.buffer(), &tl, cols);
        for (int l = 0; l < doc.buffer().lineCount(); ++l) {
          const int n = fresh.rowsOf(l);
          if (w.rowsOf(l) != n) {
            for (int q = 0; q < n; ++q) std::cerr << "fresh " << fresh.rowStart(l, q) << " ";
            for (int q = 0; q < w.rowsOf(l); ++q) std::cerr << "mine " << w.rowStart(l, q) << " ";
            std::cerr << "last edit " << lastEdit << "\n";
          }
          if (w.rowsOf(l) != n)
            std::cerr << "    cols " << cols << " step " << step << " line " << l << " rows " << w.rowsOf(l)
                      << " vs fresh " << n << " text '" << doc.buffer().line(l) << "'\n";
          REQUIRE(w.rowsOf(l) == n);
          for (int row = 0; row < n; ++row) {
            REQUIRE(w.rowStart(l, row) == fresh.rowStart(l, row));
            REQUIRE(w.rowIndent(l, row) == fresh.rowIndent(l, row));
          }
        }
        REQUIRE(w.totalRows() == fresh.totalRows());
      }
    }
  }
}

// A 3 MB single line: rows are scanned front to back only as far as asked, the count is an estimate until then, and
// typing at the end re-scans nothing before the edit.
TEST(wraplayout_a_huge_line_is_scanned_lazily) {
  std::string line;
  for (int i = 0; line.size() < 3000000; ++i) line += "word" + std::to_string(i % 10) + " ";
  Document doc = Document::fromText("huge.txt", line);
  gfx::FontStack stack = monoStack();
  gfx::Atlas atlas;
  gfx::TextLayout tl(stack, atlas, 4);
  WrapLayout w;
  w.bind(&doc.buffer(), &tl, 80);
  doc.addListener([&](Document&, const Document::Change& c) { w.onChange(c); });
  using clk = std::chrono::steady_clock;
  auto ms = [](clk::time_point a) { return std::chrono::duration<double, std::milli>(clk::now() - a).count(); };
  auto t0 = clk::now();
  const int est = w.rowsEstimate(0);
  w.rowStart(0, 40);
  const double first = ms(t0);
  CHECK(est > 3000000 / 90 && est < 3000000 / 60);  // about one row per 80 bytes before any scan
  t0 = clk::now();
  const int rows = w.rowsOf(0);
  const double full = ms(t0);
  CHECK(rows > 3000000 / 90 && rows < 3000000 / 60);
  CHECK_EQ(w.rowEnd(0, rows - 1), static_cast<int>(line.size()));
  // Typing at the end: only the last rows are scanned again.
  const int end = static_cast<int>(doc.buffer().line(0).size());
  doc.replace({{0, end}, {0, end}}, "x");
  t0 = clk::now();
  const int vr = w.visualRowOf({0, end + 1});
  const double edit = ms(t0);
  CHECK_EQ(vr, w.rowsOf(0) - 1);
  CHECK(w.rowsOf(0) >= rows);
  std::printf(
      "PERF: wrap of a 3 MB single line: first rows %.3f ms, whole line %.1f ms (%d rows), re-scan after typing "
      "at the end %.3f ms\n",
      first, full, rows, edit);
#if defined(NDEBUG) && !defined(__SANITIZE_ADDRESS__)
  CHECK(first < 1.0 && edit < 2.0);
#endif
}

// ---------------------------------------------------------------------------------------------------------------
// The TextView with wrap on
// ---------------------------------------------------------------------------------------------------------------
namespace {

struct Rec3 : gfx::Renderer {
  struct R {
    gfx::RectF r;
    gfx::Color c;
  };
  struct Ln {
    float x0, y0, x1, y1;
    gfx::Color c;
  };
  struct Q {
    float x, y;
    gfx::Color c;
  };
  std::vector<R> rects;
  std::vector<Ln> lines;
  std::vector<Q> quads;
  void beginFrame(int, int, gfx::Color) override {
    rects.clear();
    lines.clear();
    quads.clear();
  }
  void drawRect(const gfx::RectF& r, gfx::Color c) override { rects.push_back({r, c}); }
  void drawLine(float x0, float y0, float x1, float y1, float, gfx::Color c) override {
    lines.push_back({x0, y0, x1, y1, c});
  }
  void drawGlyphs(gfx::TextureId, const gfx::GlyphQuad* q, int n, gfx::Color c) override {
    for (int i = 0; i < n; ++i) quads.push_back({q[i].x0, q[i].y0, c});
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

// 8 px cells, 14 px lines, gutter 40, text from x = 44, scrollbar 12 px: a 300 px wide view wraps at 29 cells.
struct WFx {
  fs::path dir;
  std::unique_ptr<Editor> ed;
  Document* doc = nullptr;
  std::unique_ptr<gfx::TextRenderer> text;
  std::unique_ptr<RootWidget> root;
  std::unique_ptr<EditorApp> app;
  TextView* tv = nullptr;
  Rec3 rec;
  int w, h;

  explicit WFx(const std::string& content, int w_ = 300, int h_ = 280, bool wrap = true,
               std::unique_ptr<gfx::TextRenderer> tr = nullptr, const char* name = "a.txt")
      : w(w_), h(h_) {
    static int counter = 0;
    dir = uitest::tempRoot() /
          ("tenfold_wrap_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_" +
           std::to_string(counter++));
    fs::create_directories(dir);
    {
      std::ofstream out(dir / name, std::ios::binary);
      out << content;
    }
    ed = std::make_unique<Editor>(dir);
    doc = ed->open(name);
    ed->gotoLocation({doc->path(), {0, 0}});
    if (tr) {
      text = std::move(tr);
    } else {
      gfx::FontStack stack;
      stack.add(std::make_shared<tftext::MockFont>(
          U"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ._-:,;(){}[]=+*/<>\"'"));
      text = gfx::TextRenderer::fromFonts(std::move(stack));
    }
    root = std::make_unique<RootWidget>(*text);
    auto v = std::make_unique<TextView>(*ed);
    tv = v.get();
    tv->settings().smoothScroll = false;
    tv->settings().caretBlink = false;
    tv->settings().wordWrap = wrap;
    root->addChild(std::move(v));
    tv->setFocus();
    app = std::make_unique<EditorApp>(*root, *ed);
    frame();
  }
  ~WFx() {
    app.reset();
    root.reset();
    ed.reset();
    std::error_code ec;
    fs::remove_all(dir, ec);
  }
  void frame() { root->render(rec, w, h); }
  void key(Key k, uint8_t mods = 0) {
    InputEvent e;
    e.type = InputType::KeyDown;
    e.key = k;
    e.mods = mods;
    app->handleEvent(e, ++now);
    app->tick(now);
    frame();
  }
  Position head() const { return ed->selections()[0].head; }
  WrapLayout& wl() const { return tv->wrapLayout(); }
  // Pixel of a caret position, mid-line.
  std::pair<float, float> at(Position p) const {
    const gfx::RectF r = tv->caretRect(p);
    return {r.x, r.y + r.h / 2};
  }
  void click(Position p, uint8_t mods = 0) {
    auto [x, y] = at(p);
    root->dispatch(mouse(InputType::MouseDown, x, y, mods));
    root->dispatch(mouse(InputType::MouseUp, x, y));
  }
  std::string sel() const {
    std::string s;
    for (const Selection& x : ed->selections())
      s += "(" + std::to_string(x.anchor.line) + ":" + std::to_string(x.anchor.column) + "-" +
           std::to_string(x.head.line) + ":" + std::to_string(x.head.column) + ")";
    return s;
  }
  int64_t now = 1000;
};

// 87 characters: three rows at 29 cells ("one two ... " breaks after the last space that fits).
const char* kLong = "alpha beta gamma delta epsilon zeta eta theta iota kappa lambda mu nu xi omicron pi rho sigma";

}  // namespace

TEST(wrapview_continuation_rows_sit_below_the_first_one_and_the_gutter_numbers_the_line_once) {
  WFx f(std::string(kLong) + "\nshort\n");
  WrapLayout& W = f.wl();
  REQUIRE(f.tv->wordWrapActive());
  CHECK_EQ(W.cols(), 29);
  const int rows = W.rowsOf(0);
  REQUIRE(rows >= 3);
  const gfx::RectF r0 = f.tv->caretRect({0, 0});
  for (int r = 1; r < rows; ++r) {
    const gfx::RectF c = f.tv->caretRect({0, W.rowStart(0, r)});
    CHECK_EQ(c.y, r0.y + 14.0f * static_cast<float>(r));
    CHECK_EQ(c.x, 44.0f);  // no indent on this line
  }
  CHECK_EQ(f.tv->caretRect({1, 0}).y, r0.y + 14.0f * static_cast<float>(rows));
  CHECK_EQ(f.tv->visibleLineCount(), 20);
  // Gutter digits: "1" at the line's first row, "2" below all its rows, nothing on the continuation rows.
  f.frame();
  int gutterGlyphs = 0;
  for (const Rec3::Q& q : f.rec.quads)
    if (q.x < 40) ++gutterGlyphs;
  CHECK_EQ(gutterGlyphs, 3);  // "1", "2", "3": one per line, none on the continuation rows
}

TEST(wrapview_caret_up_and_down_move_by_visual_row_and_keep_the_x) {
  const std::string l2 = "second line is long enough to wrap around the view width as well";
  WFx f(std::string(kLong) + "\nxy\n" + l2 + "\n");
  WrapLayout& W = f.wl();
  const int rows0 = W.rowsOf(0);
  REQUIRE(rows0 >= 3 && W.rowsOf(2) >= 2);
  // A caret on the last row of line 0, 5 cells in.
  const int start = W.rowStart(0, rows0 - 1);
  f.ed->setSelections({caretAt({0, start + 5})});
  f.root->tick(f.now);
  const float x0 = f.tv->caretRect({0, start + 5}).x;
  f.key(Key::Up);
  CHECK(f.head() == (Position{0, W.rowStart(0, rows0 - 2) + 5}));
  CHECK_EQ(f.tv->caretRect(f.head()).x, x0);
  f.key(Key::Down);
  CHECK(f.head() == (Position{0, start + 5}));
  f.key(Key::Down);  // the short line: the caret is clamped to its end ...
  CHECK(f.head() == (Position{1, 2}));
  f.key(Key::Down);  // ... and the x of the first move comes back on the next row
  CHECK(f.head() == (Position{2, 5}));
  f.key(Key::Down);
  CHECK(f.head() == (Position{2, W.rowStart(2, 1) + 5}));
  f.key(Key::Up);
  f.key(Key::Up);
  CHECK(f.head() == (Position{1, 2}));
  f.key(Key::Up);
  CHECK(f.head() == (Position{0, start + 5}));
  // A horizontal move forgets the remembered x.
  f.key(Key::Right);
  f.key(Key::Up);
  CHECK(f.head() == (Position{0, W.rowStart(0, rows0 - 2) + 6}));
  // At the top of the document Up goes to the very start, at the bottom Down to the very end.
  f.ed->setSelections({caretAt({0, 3})});
  f.key(Key::Up);
  CHECK(f.head() == (Position{0, 0}));
  const int last = f.doc->buffer().lineCount() - 1;
  f.ed->setSelections({caretAt({last - 1, W.rowStart(last - 1, W.rowsOf(last - 1) - 1)})});
  f.key(Key::Down);
  CHECK_EQ(f.head().line, last);
  f.key(Key::Down);
  CHECK(f.head() == (Position{last, 0}));  // the empty last line
  // Shift extends from the anchor, by row.
  f.ed->setSelections({caretAt({0, 4})});
  f.key(Key::Down, ModShift);
  CHECK(f.ed->selections()[0].anchor == (Position{0, 4}));
  CHECK(f.head() == (Position{0, W.rowStart(0, 1) + 4}));
  // PageDown / PageUp move by a screenful of rows (here the document is shorter than that: its end and its start).
  f.ed->setSelections({caretAt({0, 0})});
  f.key(Key::PageDown);
  CHECK(f.head() == (Position{last, 0}));
  f.key(Key::PageUp);
  CHECK(f.head() == (Position{0, 0}));
}

TEST(wrapview_page_down_counts_rows_not_lines) {
  std::string text;
  for (int i = 0; i < 30; ++i) text += std::string(kLong) + " " + std::to_string(i) + "\n";
  WFx f(text);
  WrapLayout& W = f.wl();
  const int rows = W.rowsOf(0);
  REQUIRE(rows >= 3);
  f.key(Key::PageDown);  // 20 rows on screen
  int vrow = W.visualRowOf(f.head());
  CHECK_EQ(vrow, 20);
  CHECK(f.head().line < 20);  // far fewer lines than rows
  f.key(Key::PageDown);
  CHECK_EQ(W.visualRowOf(f.head()), 40);
  f.key(Key::PageUp);
  CHECK_EQ(W.visualRowOf(f.head()), 20);
  const gfx::RectF c = f.tv->caretRect(f.head());
  CHECK(c.y >= 0 && c.y + c.h <= 280);
}

TEST(wrapview_home_and_end_go_to_the_row_edge_first_and_the_line_edge_second) {
  WFx f(std::string(kLong) + "\n");
  WrapLayout& W = f.wl();
  const int rows = W.rowsOf(0);
  REQUIRE(rows >= 3);
  const int size = static_cast<int>(f.doc->buffer().line(0).size());
  f.ed->setSelections({caretAt({0, W.rowStart(0, 1) + 7})});
  f.key(Key::Home);
  CHECK(f.head() == (Position{0, W.rowStart(0, 1)}));
  f.key(Key::Home);
  CHECK(f.head() == (Position{0, 0}));
  f.key(Key::Home);
  CHECK(f.head() == (Position{0, 0}));
  f.key(Key::End);
  CHECK(f.head() == (Position{0, W.rowLastCaret(0, 0)}));
  CHECK(f.head().column < W.rowStart(0, 1));  // still on row 0
  f.key(Key::End);
  CHECK(f.head() == (Position{0, size}));
  f.key(Key::End);
  CHECK(f.head() == (Position{0, size}));
  f.key(Key::Home);  // on the last row: its start, then the line's
  CHECK(f.head() == (Position{0, W.rowStart(0, rows - 1)}));
  // Shift+Home / Shift+End select to the row edge.
  f.ed->setSelections({caretAt({0, W.rowStart(0, 1) + 3})});
  f.key(Key::End, ModShift);
  CHECK(f.ed->selections()[0].anchor == (Position{0, W.rowStart(0, 1) + 3}));
  CHECK(f.head() == (Position{0, W.rowLastCaret(0, 1)}));
  // Ctrl+Home / Ctrl+End are the document's, not the row's.
  f.key(Key::End, ModCtrl);
  CHECK(f.head() == (Position{1, 0}));
  f.key(Key::Home, ModCtrl);
  CHECK(f.head() == (Position{0, 0}));
}

TEST(wrapview_mouse_hits_continuation_rows) {
  WFx f(std::string(kLong) + "\nnext\n");
  WrapLayout& W = f.wl();
  const int rows = W.rowsOf(0);
  REQUIRE(rows >= 3);
  const float y1 = f.tv->caretRect({0, 0}).y + 14 + 7;  // the middle of row 1
  const int s1 = W.rowStart(0, 1);
  f.root->dispatch(mouse(InputType::MouseDown, 44 + 4 * 8, y1));
  f.root->dispatch(mouse(InputType::MouseUp, 44 + 4 * 8, y1));
  CHECK_EQ(f.sel(), "(0:" + std::to_string(s1 + 4) + "-0:" + std::to_string(s1 + 4) + ")");
  CHECK(f.tv->positionAt(44 + 4 * 8 + 2, y1) == (Position{0, s1 + 4}));
  // Left of the text on row 2 is the row's start.
  const float y2 = f.tv->caretRect({0, 0}).y + 28 + 7;
  CHECK(f.tv->positionAt(2, y2) == (Position{0, W.rowStart(0, 2)}));
  // Far right of row 0 is the end of row 0, not the start of row 1.
  const float y0 = f.tv->caretRect({0, 0}).y + 7;
  CHECK(f.tv->positionAt(290, y0) == (Position{0, W.rowLastCaret(0, 0)}));
  // Past the end of the last row: the end of the line; below everything: the end of the document.
  const float yl = f.tv->caretRect({0, 0}).y + 14.0f * static_cast<float>(rows - 1) + 7;
  CHECK(f.tv->positionAt(290, yl) == (Position{0, static_cast<int>(f.doc->buffer().line(0).size())}));
  CHECK_EQ(f.tv->positionAt(100, 279).line, 2);
  // Drag from row 0 to row 2 selects across the break of rows.
  auto [ax, ay] = f.at({0, 3});
  auto [bx, by] = f.at({0, W.rowStart(0, 2) + 5});
  f.root->dispatch(mouse(InputType::MouseDown, ax, ay));
  f.root->dispatch(mouse(InputType::MouseMove, bx, by));
  f.root->dispatch(mouse(InputType::MouseUp, bx, by));
  CHECK_EQ(f.sel(), "(0:3-0:" + std::to_string(W.rowStart(0, 2) + 5) + ")");
  // Double click on a word of a continuation row selects that word.
  const int w1 = s1;  // the first word of row 1
  auto [wx, wy] = f.at({0, w1 + 1});
  f.root->dispatch(mouse(InputType::MouseDown, wx, wy, 0, 2));
  f.root->dispatch(mouse(InputType::MouseUp, wx, wy));
  CHECK_EQ(f.ed->selections()[0].anchor.column, w1);
}

TEST(wrapview_selection_is_painted_row_by_row) {
  WFx f(std::string(kLong) + "\nnext\n");
  WrapLayout& W = f.wl();
  const int rows = W.rowsOf(0);
  REQUIRE(rows >= 3);
  gfx::Color sel = Theme::darkDefault().tabAccent;
  sel.a = 0.38f;
  f.ed->setSelections({Selection{{0, 5}, {0, W.rowStart(0, 2) + 3}}});
  f.root->tick(f.now);
  f.frame();
  std::vector<gfx::RectF> bars;
  for (const Rec3::R& r : f.rec.rects)
    if (same(r.c, sel)) bars.push_back(r.r);
  REQUIRE(bars.size() == 3u);
  std::sort(bars.begin(), bars.end(), [](const gfx::RectF& a, const gfx::RectF& b) { return a.y < b.y; });
  const float top = f.tv->caretRect({0, 0}).y;
  for (int i = 0; i < 3; ++i) {
    CHECK_EQ(bars[static_cast<size_t>(i)].y, top + 14.0f * static_cast<float>(i));
    CHECK_EQ(bars[static_cast<size_t>(i)].h, 14.0f);
  }
  CHECK_EQ(bars[0].x, 44.0f + 5 * 8);
  CHECK_EQ(bars[1].x, 44.0f);
  CHECK_EQ(bars[2].x, 44.0f);
  CHECK_EQ(bars[2].w, 3 * 8.0f);
  // A selection that continues to the next line paints the line break after the last row.
  f.ed->setSelections({Selection{{0, 5}, {1, 2}}});
  f.root->tick(f.now);
  f.frame();
  bars.clear();
  for (const Rec3::R& r : f.rec.rects)
    if (same(r.c, sel)) bars.push_back(r.r);
  CHECK_EQ(bars.size(), static_cast<size_t>(rows + 1));
}

TEST(wrapview_squiggles_and_find_marks_follow_the_rows) {
  WFx f(std::string(kLong) + "\nx\n", 300, 280, true, nullptr, "a.cpp");
  WrapLayout& W = f.wl();
  REQUIRE(W.rowsOf(0) >= 3);
  const Theme th = Theme::darkDefault();
  // A diagnostic from row 0 into row 1 is underlined on both rows, each under its own cells.
  Diagnostic d;
  d.file = f.doc->path();
  d.range = {{0, W.rowLastCaret(0, 0) - 3}, {0, W.rowStart(0, 1) + 4}};
  d.severity = 1;
  f.ed->setDiagnostics(f.doc->path(), {d});
  // Find matches: one entirely on row 1, one running over the break between rows 1 and 2.
  const int s1 = W.rowStart(0, 1), s2 = W.rowStart(0, 2);
  f.tv->setFindMatches({{{0, s1 + 1}, {0, s1 + 3}}, {{0, s2 - 3}, {0, s2 + 2}}}, 1);
  f.root->tick(f.now);
  f.frame();
  float lo0 = 1e9f, hi0 = -1e9f, lo1 = 1e9f, hi1 = -1e9f;
  int nRow0 = 0, nRow1 = 0;
  for (const Rec3::Ln& l : f.rec.lines) {
    if (!same(l.c, th.diagnosticError)) continue;
    if (l.y0 < 14) {
      ++nRow0;
      lo0 = std::min(lo0, l.x0);
      hi0 = std::max(hi0, l.x1);
    } else if (l.y0 < 28) {
      ++nRow1;
      lo1 = std::min(lo1, l.x0);
      hi1 = std::max(hi1, l.x1);
    }
  }
  CHECK(nRow0 >= 3 && nRow1 >= 3);
  CHECK(std::fabs(lo0 - (44 + static_cast<float>(W.rowLastCaret(0, 0) - 3) * 8)) < 0.01f);
  CHECK(lo1 == 44.0f && std::fabs(hi1 - (44 + 4 * 8)) < 0.01f);
  // Find marks: 0.30 alpha for the match on row 1, 0.65 for the current one split over rows 1 and 2.
  int weak = 0, strong = 0;
  for (const Rec3::R& r : f.rec.rects) {
    if (r.c.r != th.dirtyMarker.r || r.c.g != th.dirtyMarker.g) continue;
    if (r.c.a == 0.30f) {
      ++weak;
      CHECK(r.r.y == 14.0f && r.r.x == 44.0f + 8);
    } else if (r.c.a == 0.65f) {
      ++strong;
    }
  }
  CHECK_EQ(weak, 1);
  CHECK_EQ(strong, 2);
}

TEST(wrapview_scrollbar_markers_are_placed_by_visual_row) {
  std::string text;
  for (int i = 0; i < 40; ++i) text += std::string(kLong) + " " + std::to_string(i) + "\n";
  WFx f(text);
  WrapLayout& W = f.wl();
  for (int l = 0; l < 41; ++l) W.rowsOf(l);
  Diagnostic e;
  e.range = {{20, 0}, {20, 1}};
  e.severity = 1;
  f.ed->setDiagnostics(f.doc->path(), {e});
  f.root->tick(f.now);
  f.frame();
  f.frame();
  const Scrollbar& sb = f.tv->scrollbar();
  const gfx::RectF b = sb.bounds();
  const float expect =
      std::floor(b.y + (static_cast<float>(W.firstRowOf(20)) + 0.5f) / static_cast<float>(W.totalRows()) * b.h);
  float got = -1;
  for (const Rec3::R& r : f.rec.rects)
    if (same(r.c, Theme::darkDefault().diagnosticError) && r.r.x == b.x + 8 && r.r.w == 4 && r.r.h == 2) got = r.r.y;
  CHECK_EQ(got, expect);
  CHECK(W.firstRowOf(20) > 60);  // in rows, not in lines
}

TEST(wrapview_scrolling_and_the_scrollbar_work_in_rows) {
  std::string text;
  for (int i = 0; i < 40; ++i) text += std::string(kLong) + " " + std::to_string(i) + "\n";
  WFx f(text);
  f.key(Key::End, ModCtrl);  // looks at the last line
  WrapLayout& W = f.wl();
  const int rows0 = W.rowsOf(0);
  REQUIRE(rows0 >= 3);
  // The caret is on screen and the view stopped at (about) the end of the document in rows.
  const gfx::RectF c = f.tv->caretRect(f.head());
  CHECK(c.y >= 0 && c.y + c.h <= 280);
  for (int l = 0; l < 40; ++l) W.rowsOf(l);
  CHECK(W.totalRows() >= 40 * 3);
  f.frame();
  const float thumbWrapped = f.tv->scrollbar().thumbRect().h;
  f.tv->setWordWrap(false);
  f.frame();
  CHECK(f.tv->scrollbar().thumbRect().h > thumbWrapped);  // fewer rows to scroll: a bigger thumb
  f.tv->setWordWrap(true);
  f.frame();
  for (int l = 0; l < 40; ++l) W.rowsOf(l);  // the row counts went with the old width: look again
  f.frame();
  CHECK_EQ(f.tv->scrollbar().thumbRect().h, thumbWrapped);
  // The caret marker is at the row of the caret's line.
  f.key(Key::Home, ModCtrl);
  f.frame();
  const Scrollbar& sb = f.tv->scrollbar();
  bool found = false;
  for (const Rec3::R& r : f.rec.rects)
    if (same(r.c, Theme::darkDefault().textDim) && r.r.x == sb.bounds().x + 4 && r.r.h == 2)
      found = found || r.r.y == std::floor(sb.bounds().y + 0.5f / static_cast<float>(W.totalRows()) * sb.bounds().h);
  CHECK(found);
  // Wheel scrolling is in rows; horizontal scrolling is off.
  InputEvent wheel;
  wheel.type = InputType::Wheel;
  wheel.x = 100;
  wheel.y = 100;
  wheel.dy = -1;
  f.root->dispatch(wheel);
  CHECK_EQ(f.tv->viewport().y(), 3 * 14.0f);
  wheel.dy = 0;
  wheel.dx = -5;
  f.root->dispatch(wheel);
  CHECK_EQ(f.tv->viewport().x(), 0.0f);
  f.key(Key::End);
  CHECK_EQ(f.tv->viewport().x(), 0.0f);
}

TEST(wrapview_toggle_command_and_alt_z) {
  std::string text;
  for (int i = 0; i < 30; ++i) text += std::string(kLong) + " " + std::to_string(i) + "\n";
  WFx f(text);
  CHECK(f.tv->wordWrapActive());
  f.key(static_cast<Key>('Z'), ModAlt);
  CHECK(!f.tv->wordWrapActive());
  CHECK_EQ(f.app->message(), std::string("Word wrap off"));
  CHECK_EQ(f.tv->caretRect({1, 0}).y, 14.0f);  // one row per line again
  f.key(static_cast<Key>('Z'), ModAlt);
  CHECK(f.tv->wordWrapActive());
  CHECK(f.tv->caretRect({1, 0}).y > 14.0f);
  // Toggling keeps the first visible line where it is.
  f.root->dispatch([] {
    InputEvent e;
    e.type = InputType::Wheel;
    e.x = 100;
    e.y = 100;
    e.dy = -1;
    return e;
  }());
  f.root->dispatch([] {
    InputEvent e;
    e.type = InputType::Wheel;
    e.x = 100;
    e.y = 100;
    e.dy = -1;
    return e;
  }());
  f.frame();
  // The line the first visible row belongs to (it may start above the view).
  auto topLine = [&] {
    int top = 0;
    for (int l = 0; l < 30; ++l)
      if (f.tv->caretRect({l, 0}).y <= 0.5f) top = l;
    return top;
  };
  const int before = topLine();
  CHECK(before >= 1);
  f.key(static_cast<Key>('Z'), ModAlt);
  f.frame();
  CHECK_EQ(topLine(), before);
  CHECK(f.app->commands().find("View.ToggleWordWrap") != nullptr);
}

TEST(wrapview_resizing_reflows_and_keeps_the_caret_on_its_text) {
  WFx f(std::string(kLong) + "\n");
  const int size = static_cast<int>(f.doc->buffer().line(0).size());
  f.ed->setSelections({caretAt({0, size})});
  f.root->tick(f.now);
  const int rows300 = f.wl().rowsOf(0);
  f.w = 560;
  f.root->requestLayout();
  f.frame();
  const int rows560 = f.wl().rowsOf(0);
  CHECK(rows560 < rows300 && rows560 >= 2);
  CHECK_EQ(f.wl().cols(), (560 - 12 - 44) / 8 - 1);
  const gfx::RectF c = f.tv->caretRect({0, size});
  CHECK_EQ(c.y, 14.0f * static_cast<float>(rows560 - 1));
  // The caret at the end of a full last row still fits inside the text area.
  CHECK(c.x + c.w <= 560 - 12);
}

TEST(wrapview_editing_in_a_wrapped_line_keeps_it_consistent) {
  WFx f(std::string(kLong) + "\nnext\n");
  const int rows = f.wl().rowsOf(0);
  f.ed->setSelections({caretAt({0, 0})});
  for (int i = 0; i < 40; ++i)
    f.root->dispatch([] {
      InputEvent e;
      e.type = InputType::Text;
      e.text = "w";
      return e;
    }());
  f.frame();
  CHECK(f.wl().rowsOf(0) > rows);
  // The caret stays visible and on its row after typing at the start of a long line.
  const gfx::RectF c = f.tv->caretRect(f.head());
  CHECK(c.y >= 0 && c.y + c.h <= 280);
  // Enter splits the line; both halves get their own rows.
  f.key(Key::Enter);
  CHECK_EQ(f.doc->buffer().lineCount(), 4);
  CHECK(f.wl().rowsOf(1) >= 3);
  // Undo restores the layout (compare with a fresh map).
  f.key(static_cast<Key>('Z'), ModCtrl);
  f.key(static_cast<Key>('Z'), ModCtrl);
  WrapLayout fresh;
  fresh.bind(&f.doc->buffer(), &f.text->textLayout(), f.wl().cols());
  for (int l = 0; l < f.doc->buffer().lineCount(); ++l) CHECK_EQ(f.wl().rowsOf(l), fresh.rowsOf(l));
}

// ---------------------------------------------------------------------------------------------------------------
// Golden + benchmarks (real font)
// ---------------------------------------------------------------------------------------------------------------
namespace {

std::string dejavuMono() {
  for (const char* p : {"/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
                        "/usr/share/fonts/dejavu/DejaVuSansMono.ttf", "/usr/share/fonts/TTF/DejaVuSansMono.ttf"})
    if (std::ifstream(p).good()) return p;
  return {};
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

}  // namespace

// Wrapped C++ at 560 px: the long lines of the sample break at spaces with the code's indentation on the continuation
// rows, one multi-row selection, the caret on a continuation row, find matches on a broken line, a squiggle across a
// break, the current-line highlight on the caret's row, and the gutter numbering the first rows only.
TEST(wrapview_golden_b2c_word_wrap) {
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
  std::ifstream in(TENFOLD_SOURCE_DIR "/examples/sample_project/src/main.cpp", std::ios::binary);
  const std::string src((std::istreambuf_iterator<char>(in)), {});
  WFx f(src, 560, 480, true, std::move(tr), "a.cpp");
  f.tv->settings().highlightCurrentLine = true;
  const int n = f.doc->buffer().lineCount();
  REQUIRE(n > 24);
  f.ed->setSelections({Selection{{22, 10}, {23, 40}}, caretAt({10, 20})});
  Diagnostic d;
  d.file = f.doc->path();
  d.range = {{22, 40}, {23, 12}};
  d.severity = 1;
  f.ed->setDiagnostics(f.doc->path(), {d});
  f.tv->setFindMatches({{{22, 4}, {22, 10}}, {{9, 4}, {9, 7}}}, 0);
  f.root->tick(f.now);
  f.text->releaseTexture(f.rec);
  gfx::GlRenderer& r = *gl->renderer;
  r.setOffscreen(true);
  f.root->requestLayout();
  f.root->render(r, f.w, f.h);
  std::vector<uint8_t> px(static_cast<size_t>(f.w) * f.h * 4);
  REQUIRE(r.readPixels(f.w, f.h, px.data()));
  f.text->releaseTexture(r);
  compareGolden("b2c_word_wrap", px, f.w, f.h);
}

// Keystroke-to-paint with wrap on, 1920 x 1080 (budget 8 ms): the 100k-line file with every line fitting one row and
// the same file with a third of the lines wrapping, then a single 50 MB line with the caret in the middle and at the
// end. The GL submit is not part of the number.
namespace {
double perfKeystroke(WFx& f, int n, const char* what) {
  using clk = std::chrono::steady_clock;
  auto ms = [](clk::time_point a, clk::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
  };
  InputEvent ev;
  ev.type = InputType::Text;
  ev.text = "a";
  for (int i = 0; i < 20; ++i) {
    f.root->dispatch(ev);
    f.frame();
  }
  std::vector<double> total;
  double edit = 0, paint = 0;
  for (int i = 0; i < n; ++i) {
    auto t0 = clk::now();
    f.root->dispatch(ev);
    auto t1 = clk::now();
    f.root->requestLayout();
    f.frame();
    auto t2 = clk::now();
    edit += ms(t0, t1);
    paint += ms(t1, t2);
    total.push_back(ms(t0, t2));
  }
  std::sort(total.begin(), total.end());
  const double avg = (edit + paint) / n;
  std::printf(
      "PERF: wrap %s keystroke-to-paint, %s @1920x1080: edit+sync %.3f ms + layout+paint %.3f ms = avg %.3f ms / p99 "
      "%.3f ms / max %.3f ms (budget 8 ms)\n",
      f.tv->wordWrapActive() ? "ON" : "off", what, edit / n, paint / n, avg,
      total[static_cast<size_t>(static_cast<double>(n) * 0.99)], total.back());
#if defined(NDEBUG) && !defined(__SANITIZE_ADDRESS__)
  CHECK(avg < 8.0);
#endif
  return avg;
}
}  // namespace

TEST(wrapview_perf_keystroke_to_paint_100k_lines_with_wrap_on) {
  const std::string font = dejavuMono();
  std::string err;
  if (font.empty() || !gfx::TextRenderer::create(font, 15.0f, 1.0f, &err)) {
    std::cerr << "    SKIP: no font (" << err << ")\n";
    return;
  }
  std::string fits, wraps;
  for (int i = 0; i < 100000; ++i) {
    fits += "    for (int k" + std::to_string(i) + " = 0; k < n_" + std::to_string(i % 97) + "; ++k) { total += k; }\n";
    wraps += (i % 3 ? "    for (int k" + std::to_string(i) + " = 0; k < n_; ++k) { total += k; }\n"
                    : "    // " + std::string(i % 7 + 4, 'x') +
                          " a comment that is long enough to wrap around a 1920 pixel wide view when it is set in a "
                          "15 pixel font, with words, words and more words to break the rows at: " +
                          std::to_string(i) +
                          " lorem ipsum dolor sit amet consectetur adipiscing elit sed do eiusmod "
                          "tempor incididunt ut labore et dolore magna aliqua\n");
  }
  {
    WFx f(fits, 1920, 1080, true, gfx::TextRenderer::create(font, 15.0f, 1.0f, &err));
    perfKeystroke(f, 400, "100k lines, all rows single");
    CHECK(f.tv->wordWrapActive());
  }
  {
    WFx f(wraps, 1920, 1080, true, gfx::TextRenderer::create(font, 15.0f, 1.0f, &err));
    REQUIRE(f.wl().rowsOf(0) >= 2);
    perfKeystroke(f, 400, "100k lines, a third of them wrapping");
    // A caret far into the file (the rows above are only estimates, the ones around the caret are real).
    f.ed->setSelections({caretAt({77777, 10})});
    f.root->tick(f.now);
    f.frame();
    perfKeystroke(f, 200, "100k lines, wrapping, caret at line 77777");
  }
}

TEST(wrapview_perf_keystroke_to_paint_a_50_mb_single_line_with_wrap_on) {
  const std::string font = dejavuMono();
  std::string err;
  if (font.empty() || !gfx::TextRenderer::create(font, 15.0f, 1.0f, &err)) {
    std::cerr << "    SKIP: no font (" << err << ")\n";
    return;
  }
  std::string line;
  line.reserve(50u << 20);
  for (int i = 0; line.size() < (50u << 20); ++i) line += "token" + std::to_string(i % 1000) + " ";
  WFx f(line, 1920, 1080, true, gfx::TextRenderer::create(font, 15.0f, 1.0f, &err));
  using clk = std::chrono::steady_clock;
  auto ms = [](clk::time_point a) { return std::chrono::duration<double, std::milli>(clk::now() - a).count(); };
  CHECK(f.tv->wordWrapActive());
  perfKeystroke(f, 200, "50 MB single line, caret at the start");
  {  // the same without wrap, for reference (the engine's own cost of inserting into a 50 MB string is in "edit")
    WFx g(line, 1920, 1080, false, gfx::TextRenderer::create(font, 15.0f, 1.0f, &err));
    perfKeystroke(g, 200, "50 MB single line, caret at the start");
  }
  const int mid = static_cast<int>(f.doc->buffer().line(0).size() / 2);
  auto t0 = clk::now();
  f.ed->setSelections({caretAt({0, mid})});
  f.root->tick(f.now);
  f.frame();
  std::printf("PERF: wrap ON: first paint with the caret in the middle of a 50 MB line (rows scanned once): %.1f ms\n",
              ms(t0));
  perfKeystroke(f, 200, "50 MB single line, caret in the middle");
  const int end = static_cast<int>(f.doc->buffer().line(0).size());
  t0 = clk::now();
  f.ed->setSelections({caretAt({0, end})});
  f.root->tick(f.now);
  f.frame();
  std::printf(
      "PERF: wrap ON: first paint with the caret at the end of a 50 MB line (rest of the rows scanned once): "
      "%.1f ms\n",
      ms(t0));
  perfKeystroke(f, 200, "50 MB single line, caret at the end");
  // Caret movement by row at the end of that line is cheap too.
  t0 = clk::now();
  for (int i = 0; i < 50; ++i) f.key(Key::Up);
  std::printf("PERF: wrap ON: 50 x Up on a 50 MB line incl. paint: %.3f ms each\n", ms(t0) / 50);
}
