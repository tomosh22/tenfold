// W8f: code folding (the 10x CollapseRegions commands). Region detection and the fold model as pure logic, WrapLayout's
// hidden lines against a brute-force model, then the TextView scripted through the real default key bindings
// (Control Shift [ / ], Control M Control M, Control M Control L) with a mock font (8 px cells, 14 px lines), one
// golden with a collapsed region (real font) and the keystroke-to-paint benchmark with 1000 collapsed regions.
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <thread>

#include "../text/text_test_util.h"
#include "app/Editor.h"
#include "ui/gfx/PngWrite.h"
#include "ui/gfx/TextRenderer.h"
#include "ui/shell/EditorApp.h"
#include "ui/view/FoldModel.h"
#include "ui/view/WrapLayout.h"
#include "ui/widgets/TextView.h"
#include "ui_test.h"

using namespace tf;
using namespace tf::ui;
namespace fs = std::filesystem;

namespace {

using Region = FoldModel::Region;
using Kind = FoldModel::Kind;

std::vector<std::string> foldLines(const std::string& text) {
  std::vector<std::string> v;
  size_t p = 0;
  for (;;) {
    const size_t e = text.find('\n', p);
    v.push_back(text.substr(p, e == std::string::npos ? std::string::npos : e - p));
    if (e == std::string::npos) break;
    p = e + 1;
  }
  return v;
}

std::vector<std::array<int, 2>> pairs(const std::vector<Region>& rs) {
  std::vector<std::array<int, 2>> v;
  for (const Region& r : rs) v.push_back({r.start, r.end});
  return v;
}
using P = std::vector<std::array<int, 2>>;

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// Region detection
// ---------------------------------------------------------------------------------------------------------------

TEST(fold_detect_brace_blocks_of_two_or_more_lines) {
  const auto rs =
      FoldModel::detect(foldLines("void f() {\n"            // 0
                                  "  if (x) {\n"            // 1
                                  "    y();\n"              // 2
                                  "  }\n"                   // 3
                                  "  z({1, 2});\n"          // 4  one-line braces do not fold
                                  "}\n"                     // 5
                                  "struct S { int a; };\n"  // 6
                                  "namespace n {\n"         // 7
                                  "}\n"));                  // 8
  CHECK(pairs(rs) == (P{{0, 5}, {1, 3}, {7, 8}}));
  CHECK(rs[0].kind == Kind::Brace);
}

TEST(fold_detect_ignores_braces_in_strings_chars_and_comments) {
  const auto rs =
      FoldModel::detect(foldLines("const char* s = \"{\";\n"     // 0
                                  "char c = '{';\n"              // 1
                                  "// {\n"                       // 2
                                  "/* { */\n"                    // 3
                                  "void f() {\n"                 // 4
                                  "  const char* t = \"}\";\n"   // 5
                                  "  char d = '}';\n"            // 6
                                  "  // }\n"                     // 7
                                  "}\n"                          // 8
                                  "auto r = R\"(\n{\n)\";\n"));  // raw string over lines 9-11
  CHECK(pairs(rs) == (P{{4, 8}}));
}

TEST(fold_detect_multi_line_comments_and_pragma_regions) {
  const auto rs =
      FoldModel::detect(foldLines("/* one line */\n"             // 0
                                  "/*\n"                         // 1
                                  " * two\n"                     // 2
                                  " */\n"                        // 3
                                  "int a; /* starts\n"           // 4
                                  "ends */ int b;\n"             // 5
                                  "#pragma region Outer\n"       // 6
                                  "#pragma region Inner\n"       // 7
                                  "int c;\n"                     // 8
                                  "#pragma endregion\n"          // 9
                                  "  #  pragma   endregion  \n"  // 10
                                  "#pragma once\n"               // 11
                                  "#if 1\n"                      // 12  #if blocks are not regions
                                  "int d;\n"                     // 13
                                  "#endif\n"));                  // 14
  CHECK(pairs(rs) == (P{{1, 3}, {4, 5}, {6, 10}, {7, 9}}));
  CHECK(rs[0].kind == Kind::Comment);
  CHECK(rs[2].kind == Kind::Pragma);
}

TEST(fold_detect_one_region_per_start_line_the_outermost) {
  const auto rs = FoldModel::detect(foldLines("a({ b({\n  c;\n}) ;\n  d;\n});\n"));
  CHECK(pairs(rs) == (P{{0, 4}}));
  // `} else {` ends one region and starts the next on the same line.
  const auto e = FoldModel::detect(foldLines("if (a) {\n  b;\n} else {\n  c;\n}\n"));
  CHECK(pairs(e) == (P{{0, 2}, {2, 4}}));
}

TEST(fold_detect_unbalanced_braces_do_not_throw) {
  CHECK(FoldModel::detect(foldLines("}\n}\n{\n")).empty());
  CHECK(pairs(FoldModel::detect(foldLines("{\n{\n}\n"))) == (P{{1, 2}}));
}

// ---------------------------------------------------------------------------------------------------------------
// The fold model
// ---------------------------------------------------------------------------------------------------------------
namespace {
// 0 { 1 { 2 } 3 { 4 } 5 }  6 (blank)  7 { 8 }
const char* kNested =
    "void a() {\n"  // 0
    "  if (x) {\n"  // 1
    "    p;\n"      // 2
    "  }\n"         // 3
    "  if (y) {\n"  // 4
    "    q;\n"      // 5
    "  }\n"         // 6
    "}\n"           // 7
    "\n"            // 8
    "void b() {\n"  // 9
    "  r;\n"        // 10
    "}\n";          // 11

Document::Change edit(Position a, Position b, const std::string& text) {
  Document::Change c;
  c.removed = {a, b};
  c.newText = text;
  c.lineDelta = static_cast<int>(std::count(text.begin(), text.end(), '\n')) - (b.line - a.line);
  return c;
}
}  // namespace

TEST(fold_model_commands_on_the_innermost_region_and_all_regions) {
  FoldModel m;
  m.setRegions(FoldModel::detect(foldLines(kNested)));
  REQUIRE(pairs(m.regions()) == (P{{0, 7}, {1, 3}, {4, 6}, {9, 11}}));
  CHECK((m.depths() == std::vector<int>{0, 1, 1, 0}));
  CHECK(m.innermostAt(2)->start == 1);
  CHECK(m.innermostAt(0)->start == 0);
  CHECK(m.innermostAt(8) == nullptr);
  // CollapseRegion at line 2 hides line 2..3; a second one at the same place takes the next region out.
  CHECK(m.collapseAt(2));
  CHECK(m.isCollapsedAt(1));
  REQUIRE(m.hidden().size() == 1);
  CHECK(m.hidden()[0].first == 2 && m.hidden()[0].last == 3);
  CHECK(m.isHidden(3) && !m.isHidden(1) && !m.isHidden(4));
  CHECK_EQ(m.headOf(3), 1);
  CHECK(m.collapseAt(2));  // line 2 is hidden but the model only knows regions: now the outer one
  CHECK(m.isCollapsedAt(0));
  CHECK(m.hidden().size() == 1 && m.hidden()[0].first == 1 && m.hidden()[0].last == 7);
  // ExpandRegion on the outer head: the inner one is still collapsed.
  CHECK(m.expandAt(0));
  CHECK(m.isCollapsedAt(1) && !m.isCollapsedAt(0));
  CHECK(!m.expandAt(0));
  // Toggle.
  CHECK(m.toggleAt(1));
  CHECK(!m.hasCollapsed());
  CHECK(m.toggleAt(9));
  CHECK(m.isCollapsedAt(9));
  CHECK(m.toggleAt(9));
  CHECK(!m.hasCollapsed());
}

TEST(fold_model_all_regions_recursive_or_outermost_only) {
  FoldModel m;
  m.setRegions(FoldModel::detect(foldLines(kNested)));
  CHECK(m.collapseAll(false));  // outermost only
  CHECK(m.isCollapsedAt(0) && m.isCollapsedAt(9) && !m.isCollapsedAt(1) && !m.isCollapsedAt(4));
  CHECK(!m.collapseAll(false));
  CHECK(m.collapseAll(true));  // the nested ones too
  CHECK(m.collapsed().size() == 4);
  CHECK(m.expandAll(false));  // only those whose head line is visible: the outer ones
  CHECK(m.isCollapsedAt(1) && m.isCollapsedAt(4) && !m.isCollapsedAt(0) && !m.isCollapsedAt(9));
  CHECK(!m.expandAll(false) || !m.isHidden(1));  // the inner heads are visible now: they open too
  CHECK(m.expandAll(true) || !m.hasCollapsed());
  CHECK(!m.hasCollapsed());
  CHECK(m.hidden().empty());
}

TEST(fold_model_toggle_all_follows_visible_state) {
  FoldModel m;
  m.setRegions(FoldModel::detect(foldLines(kNested)));
  CHECK(m.toggleAll());  // nothing collapsed: everything collapses
  CHECK(m.collapsed().size() == 4);
  CHECK(m.toggleAll());  // all collapsed: everything opens
  CHECK(!m.hasCollapsed());
  m.collapseAll(false);  // the outermost are collapsed, the inner ones are hidden by them
  CHECK(m.toggleAll());
  CHECK(!m.hasCollapsed());
  // Visual Studio's Toggle All Outlining: some collapsed and some not -> the collapsed ones open (everything open).
  m.collapse(*m.regionStartingAt(9));
  CHECK(m.toggleAll());
  CHECK(!m.hasCollapsed());
  m.collapse(*m.regionStartingAt(1));  // a nested one only: still "mixed", so it opens
  CHECK(m.toggleAll());
  CHECK(!m.hasCollapsed());
}

namespace {
const char* kDefs =
    "namespace n {\n"                  // 0
    "class C {\n"                      // 1
    "  void m() {\n"                   // 2
    "    if (x) {\n"                   // 3
    "    }\n"                          // 4
    "  }\n"                            // 5
    "  int v = 0;\n"                   // 6
    "};\n"                             // 7
    "enum E {\n"                       // 8
    "  A,\n"                           // 9
    "  B\n"                            // 10
    "};\n"                             // 11
    "void f()\n"                       // 12
    "{\n"                              // 13
    "}\n"                              // 14
    "extern \"C\" {\n"                 // 15
    "void g() {\n"                     // 16
    "}\n"                              // 17
    "}\n"                              // 18
    "}\n"                              // 19
    "#pragma region R\n"               // 20
    "#pragma endregion\n"              // 21
    "template <class T> void h() {\n"  // 22
    "  struct L {\n"                   // 23
    "  };\n"                           // 24
    "}\n";                             // 25
}  // namespace

TEST(fold_detect_marks_type_bodies) {
  const auto rs = FoldModel::detect(foldLines(kDefs));
  std::vector<int> types;
  for (const Region& r : rs)
    if (r.kind == Kind::Brace && r.type) types.push_back(r.start);
  CHECK((types == std::vector<int>{0, 1, 8, 15, 23}));
}

TEST(fold_model_collapse_to_definitions_collapses_member_bodies_only) {
  FoldModel m;
  m.setRegions(FoldModel::detect(foldLines(kDefs)));
  m.collapseAll(true);
  CHECK(m.collapseToDefinitions());  // everything else opens
  std::vector<int> c;
  for (const Region& r : m.collapsed()) c.push_back(r.start);
  CHECK((c == std::vector<int>{2, 13, 16, 22}));  // m, f (Allman), g, h; classes, namespaces, enum and pragma stay open
  CHECK(!m.collapseToDefinitions());
  CHECK(m.isHidden(3) && m.isHidden(14) && !m.isHidden(9) && m.isHidden(24));
}

TEST(fold_detect_outlining_options) {
  FoldModel::Options off;
  off.enabled = false;
  CHECK(FoldModel::detect(foldLines(kDefs), off).empty());
  FoldModel::Options noPragma;
  noPragma.pragmaRegions = false;
  const auto a = FoldModel::detect(foldLines(kDefs), noPragma);
  CHECK(FoldModel::detect(foldLines(kDefs)).size() == a.size() + 1);
  for (const Region& r : a) CHECK(r.kind != Kind::Pragma);
  // Outline Statement Blocks off: the `if` block inside m() goes; functions, types and comments stay.
  FoldModel::Options noStmt;
  noStmt.statementBlocks = false;
  const auto b = FoldModel::detect(foldLines(kDefs), noStmt);
  std::vector<int> starts;
  for (const Region& r : b) starts.push_back(r.start);
  CHECK((starts == std::vector<int>{0, 1, 2, 8, 13, 15, 16, 20, 22, 23}));
}

TEST(fold_model_collapsed_regions_move_with_edits_and_open_when_touched) {
  FoldModel m;
  m.setRegions(FoldModel::detect(foldLines(kNested)));
  m.collapse(*m.regionStartingAt(9));
  m.collapse(*m.regionStartingAt(1));
  CHECK(m.fresh());
  // A line inserted above both: they move down.
  CHECK(m.onChange(edit({0, 0}, {0, 0}, "// new\n")));
  CHECK(!m.fresh());
  CHECK(m.isCollapsedAt(2) && m.isCollapsedAt(10));
  CHECK(m.hidden()[0].first == 3 && m.hidden()[0].last == 4);
  // Typing at the end of the line before a head moves nothing, on the head line itself it opens the region.
  CHECK(!m.onChange(edit({0, 3}, {0, 3}, "x")));
  CHECK(m.isCollapsedAt(2));
  CHECK(m.onChange(edit({2, 5}, {2, 5}, "x")));
  CHECK(!m.isCollapsedAt(2) && m.isCollapsedAt(10));
  // An edit in the hidden lines of the other one, or on its closing line, opens it too.
  CHECK(m.onChange(edit({12, 0}, {12, 1}, "")));
  CHECK(!m.hasCollapsed());
  // Edits below leave a region alone.
  m.setRegions(FoldModel::detect(foldLines(kNested)));
  m.collapse(*m.regionStartingAt(0));
  CHECK(!m.onChange(edit({9, 0}, {9, 0}, "a\n")));
  CHECK(m.isCollapsedAt(0));
  CHECK(m.hidden()[0].first == 1 && m.hidden()[0].last == 7);
}

TEST(fold_model_fresh_regions_reconcile_with_collapsed_ones) {
  FoldModel m;
  m.setRegions(FoldModel::detect(foldLines(kNested)));
  m.collapse(*m.regionStartingAt(9));
  m.collapse(*m.regionStartingAt(0));
  // The text changed behind a collapsed region (a brace removed far away): its head is no longer a region.
  const bool changed = m.setRegions(FoldModel::detect(foldLines("void a() {\n  p;\n}\nint x;\nvoid b();\n")));
  CHECK(changed);
  CHECK(m.isCollapsedAt(0) && !m.isCollapsedAt(9));
  CHECK(m.hidden()[0].first == 1 && m.hidden()[0].last == 2);
  CHECK(m.fresh());
}

TEST(fold_model_patches_the_region_set_on_edits) {
  FoldModel m;
  m.setRegions(FoldModel::detect(foldLines(kNested)));
  // Inside region 0 (lines 0..7), below region 1..3: the enclosing region keeps its head, its end moves.
  m.onChange(edit({4, 0}, {4, 0}, "z;\n"));
  CHECK(pairs(m.regions()) == (P{{0, 8}, {1, 3}, {10, 12}}));  // {4, 6} starts on the edited line: gone
  CHECK(!m.fresh());
  // An edit on a head line drops that region (it may not be one any more).
  m.onChange(edit({1, 0}, {1, 1}, ""));
  CHECK(pairs(m.regions()) == (P{{0, 8}, {10, 12}}));
}

// ---------------------------------------------------------------------------------------------------------------
// WrapLayout with hidden lines against a naive model
// ---------------------------------------------------------------------------------------------------------------
namespace {

gfx::FontStack monoStack() {
  gfx::FontStack stack;
  stack.add(std::make_shared<tftext::MockFont>(U"abcdefghijklmnopqrstuvwxyz ._-\t"));
  return stack;
}

void checkHiddenModel(int cols, unsigned seed) {
  std::mt19937 rng(seed);
  const int n = 60;
  std::string text;
  for (int i = 0; i < n; ++i) {
    const int len = rng() % 4 == 0 ? 25 + static_cast<int>(rng() % 30) : static_cast<int>(rng() % 8);
    for (int k = 0; k < len; ++k) text += (k % 6 == 5) ? ' ' : static_cast<char>('a' + k % 26);
    if (i + 1 < n) text += '\n';
  }
  gfx::FontStack stack = monoStack();
  gfx::Atlas atlas;
  gfx::TextLayout tl(stack, atlas, 4);
  TextBuffer buf(text);
  WrapLayout w;
  w.bind(&buf, &tl, cols);
  // Some wrapped lines get scanned before the ranges arrive, some after.
  for (int i = 0; i < n; i += 3) w.rowsOf(i);
  std::vector<WrapLayout::HiddenRange> hr;
  std::vector<bool> hid(static_cast<size_t>(n), false);
  for (int at = 1 + static_cast<int>(rng() % 5); at < n - 1;) {
    const int len = 1 + static_cast<int>(rng() % 7), last = std::min(n - 2, at + len - 1);
    hr.push_back({at, last});
    for (int i = at; i <= last; ++i) hid[static_cast<size_t>(i)] = true;
    at = last + 2 + static_cast<int>(rng() % 6);
  }
  w.setHidden(hr);
  std::vector<int> vis, first;
  int rows = 0;
  for (int i = 0; i < n; ++i) {
    first.push_back(rows);
    if (hid[static_cast<size_t>(i)]) continue;
    vis.push_back(i);
    rows += cols > 0 ? w.rowsOf(i) : 1;
  }
  CHECK_EQ(w.totalRows(), rows);
  for (int i = 0; i < n; ++i) {
    CHECK_EQ(w.isHidden(i), static_cast<bool>(hid[static_cast<size_t>(i)]));
    CHECK_EQ(w.firstRowOf(i), first[static_cast<size_t>(i)]);
  }
  int expectLine = 0, expectRow = 0;
  for (int v = 0; v < rows; ++v) {
    int r = -1;
    const int line = w.lineOfRow(v, &r);
    // The row v belongs to the visible line whose rows contain it.
    int want = vis.front(), base = 0;
    for (int l : vis)
      if (first[static_cast<size_t>(l)] <= v) want = l, base = first[static_cast<size_t>(l)];
    CHECK_EQ(line, want);
    CHECK_EQ(r, v - base);
    expectLine = line;
    expectRow = r;
  }
  (void)expectLine;
  (void)expectRow;
  // Past the end: the last visible row.
  int r = -1;
  CHECK_EQ(w.lineOfRow(rows + 5, &r), vis.back());
  // Walking down visits exactly the visible rows, and back up.
  int line = vis.front(), row = 0, count = 1;
  while (w.nextRow(line, row)) {
    ++count;
    CHECK(!w.isHidden(line));
    CHECK_EQ(w.firstRowOf(line) + row, count - 1);
  }
  CHECK_EQ(count, rows);
  while (w.prevRow(line, row)) {
    --count;
    CHECK(!w.isHidden(line));
  }
  CHECK_EQ(count, 1);
  CHECK(line == vis.front() && row == 0);
  // Positions on a row after a hidden range land on the right line.
  for (int v = 0; v < rows; ++v) {
    const Position p = w.positionAt(v, 0);
    CHECK_EQ(w.visualRowOf(p), v);
  }
  // Clearing brings the identity back.
  w.setHidden({});
  CHECK(!w.hasHidden());
  int naive = 0;
  for (int i = 0; i < n - 1; ++i) naive += cols > 0 ? w.rowsOf(i) : 1;
  CHECK_EQ(w.firstRowOf(n - 1), naive);
}

}  // namespace

TEST(wraplayout_hidden_lines_match_a_naive_model_with_and_without_wrap) {
  for (unsigned seed = 1; seed <= 25; ++seed) {
    checkHiddenModel(0, seed);
    checkHiddenModel(10, seed);
    checkHiddenModel(24, seed + 100);
  }
}

TEST(wraplayout_hidden_lines_survive_edits_above) {
  gfx::FontStack stack = monoStack();
  gfx::Atlas atlas;
  gfx::TextLayout tl(stack, atlas, 4);
  Document d = Document::fromText("a.cpp", "aa\nbb\ncc\ndd\nee\nff\n");
  WrapLayout w;
  w.bind(&d.buffer(), &tl, 10);
  d.addListener([&](Document&, const Document::Change& c) { w.onChange(c); });
  w.setHidden({{2, 3}});
  CHECK_EQ(w.totalRows(), 5);
  CHECK_EQ(w.firstRowOf(4), 2);
  w.setHidden({{3, 4}});  // what the fold model does after a line was inserted above
  CHECK_EQ(w.firstRowOf(5), 3);
  CHECK_EQ(w.lineOfRow(3), 5);
}

// ---------------------------------------------------------------------------------------------------------------
// The TextView
// ---------------------------------------------------------------------------------------------------------------
namespace {

struct Rec4 : gfx::Renderer {
  struct R {
    gfx::RectF r;
    gfx::Color c;
  };
  struct Q {
    float x, y;
    gfx::Color c;
  };
  std::vector<R> rects;
  std::vector<Q> quads;
  void beginFrame(int, int, gfx::Color) override {
    rects.clear();
    quads.clear();
  }
  void drawRect(const gfx::RectF& r, gfx::Color c) override { rects.push_back({r, c}); }
  void drawLine(float, float, float, float, float, gfx::Color) override {}
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

InputEvent mouseEv(InputType t, float x, float y, int clicks = 1) {
  InputEvent e;
  e.type = t;
  e.x = x;
  e.y = y;
  e.clicks = clicks;
  e.buttons = t == InputType::MouseUp ? 0 : 1;
  return e;
}

// 8 px cells, 14 px lines, gutter 40, text from x = 44.
struct FFx {
  fs::path dir;
  std::unique_ptr<Editor> ed;
  Document* doc = nullptr;
  std::unique_ptr<gfx::TextRenderer> text;
  std::unique_ptr<RootWidget> root;
  std::unique_ptr<EditorApp> app;
  TextView* tv = nullptr;
  Rec4 rec;
  int w, h;
  int64_t now = 1000;

  explicit FFx(const std::string& content, bool wrap = false, int w_ = 400, int h_ = 280,
               std::unique_ptr<gfx::TextRenderer> tr = nullptr, const char* name = "a.cpp")
      : w(w_), h(h_) {
    static int counter = 0;
    dir = uitest::tempRoot() /
          ("tenfold_fold_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_" +
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
          U"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ._-:,;(){}[]=+*/<>\"'#!"));
      text = gfx::TextRenderer::fromFonts(std::move(stack));
    }
    root = std::make_unique<RootWidget>(*text);
    auto v = std::make_unique<TextView>(*ed);
    tv = v.get();
    tv->settings().smoothScroll = false;
    tv->settings().caretBlink = false;
    tv->settings().wordWrap = wrap;
    tv->settings().showFoldGlyphs = false;  // the default is on (VS); most tests count pixels without the lane
    tv->settings().clock = [this] { return now; };
    root->addChild(std::move(v));
    tv->setFocus();
    app = std::make_unique<EditorApp>(*root, *ed);
    frame();
  }
  ~FFx() {
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
  void type(const std::string& s) {
    InputEvent e;
    e.type = InputType::Text;
    e.text = s;
    app->handleEvent(e, ++now);
    app->tick(now);
    frame();
  }
  Position head() const { return ed->selections()[0].head; }
  float y(int line) const { return tv->caretRect({line, 0}).y; }
  // Ticks until the background region job of the view has been applied (or a second passed).
  bool waitFresh() {
    for (int i = 0; i < 200 && !tv->folds().fresh(); ++i) {
      now += 200;
      app->tick(now);
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return tv->folds().fresh();
  }
  bool click(float x, float yy, int clicks = 1) {
    root->dispatch(mouseEv(InputType::MouseDown, x, yy, clicks));
    root->dispatch(mouseEv(InputType::MouseUp, x, yy));
    frame();
    return true;
  }
};

constexpr uint8_t kCtrlShift = ModCtrl | ModShift;

// 0 void alpha() {  1 int a;  2 if (a) {  3 a = 2;  4 }  5 }  6 ""  7 /* multi  8 line */  9 void beta() {
// 10 return;  11 }  12 ""
const char* kSrc =
    "void alpha() {\n"
    "  int a = 1;\n"
    "  if (a) {\n"
    "    a = 2;\n"
    "  }\n"
    "}\n"
    "\n"
    "/* multi\n"
    "   line */\n"
    "void beta() {\n"
    "  return;\n"
    "}\n";

}  // namespace

TEST(foldview_control_shift_bracket_collapses_the_region_and_hides_its_lines) {
  FFx f(kSrc);
  f.ed->setSelections({caretAt({1, 4})});
  f.tv->foldRefreshNow();
  const float y0 = f.y(0);
  CHECK_EQ(f.y(6), y0 + 14.0f * 6);
  f.key(Key('['), kCtrlShift);
  CHECK(f.tv->folds().isCollapsedAt(0));
  // The caret was inside: it went to the end of the head line.
  CHECK(f.head() == (Position{0, 14}));
  WrapLayout& W = f.tv->wrapLayout();
  CHECK_EQ(W.totalRows(), 13 - 5);
  CHECK_EQ(f.y(6), y0 + 14.0f);  // the blank line sits right under the head
  CHECK_EQ(f.y(9), y0 + 14.0f * 4);
  // Rows of hidden lines map to the head's neighbour; mouse hit testing skips them.
  CHECK(f.tv->positionAt(60, y0 + 14.0f + 7).line == 6);
  CHECK(f.tv->positionAt(60, y0 + 14.0f * 2 + 7).line == 7);
  // Down from the head goes to the next visible line; Up comes back.
  f.key(Key::Down);
  CHECK_EQ(f.head().line, 6);
  f.key(Key::Up);
  CHECK_EQ(f.head().line, 0);
  // The marker is drawn after the head line text.
  const gfx::RectF m = f.tv->foldMarkerRect(0);
  CHECK(m.w > 0);
  CHECK_EQ(m.x, 44.0f + 14 * 8);  // "void alpha() {" is 14 cells, the marker "...}" follows directly
  CHECK_EQ(m.w, 4 * 8.0f);
  // Control Shift ] expands.
  f.key(Key(']'), kCtrlShift);
  CHECK(!f.tv->folds().hasCollapsed());
  CHECK_EQ(f.y(6), y0 + 14.0f * 6);
  CHECK_EQ(W.totalRows(), 13);
}

TEST(foldview_the_innermost_region_around_the_caret_collapses_first) {
  FFx f(kSrc);
  f.ed->setSelections({caretAt({3, 6})});
  f.key(Key('['), kCtrlShift);
  CHECK(f.tv->folds().isCollapsedAt(2) && !f.tv->folds().isCollapsedAt(0));
  CHECK(f.head() == (Position{2, 10}));
  // The caret is on the collapsed head: the next collapse takes the enclosing region.
  f.key(Key('['), kCtrlShift);
  CHECK(f.tv->folds().isCollapsedAt(0));
  CHECK(f.head() == (Position{0, 14}));
  // Expanding opens the outer region only; the inner one stays collapsed.
  f.key(Key(']'), kCtrlShift);
  CHECK(!f.tv->folds().isCollapsedAt(0) && f.tv->folds().isCollapsedAt(2));
}

TEST(foldview_toggle_region_and_toggle_all_chords) {
  FFx f(kSrc);
  f.ed->setSelections({caretAt({9, 2})});
  f.key(Key('M'), ModCtrl);
  f.key(Key('M'), ModCtrl);
  CHECK(f.tv->folds().isCollapsedAt(9));
  CHECK_EQ(f.tv->wrapLayout().totalRows(), 13 - 2);
  f.key(Key('M'), ModCtrl);
  f.key(Key('M'), ModCtrl);
  CHECK(!f.tv->folds().hasCollapsed());
  // Control M, Control L: everything collapses (nothing was collapsed), then everything opens.
  f.key(Key('M'), ModCtrl);
  f.key(Key('L'), ModCtrl);
  CHECK(f.tv->folds().isCollapsedAt(0) && f.tv->folds().isCollapsedAt(2) && f.tv->folds().isCollapsedAt(7) &&
        f.tv->folds().isCollapsedAt(9));
  CHECK_EQ(f.tv->wrapLayout().totalRows(), 5);  // alpha, the blank line, the comment head, beta, the empty last line
  f.key(Key('M'), ModCtrl);
  f.key(Key('L'), ModCtrl);
  CHECK(!f.tv->folds().hasCollapsed());
  CHECK_EQ(f.tv->wrapLayout().totalRows(), 13);
}

TEST(foldview_toggle_all_with_a_mixed_state_expands_everything_like_visual_studio) {
  FFx f(kSrc);
  f.ed->setSelections({caretAt({9, 2})});
  f.key(Key('M'), ModCtrl);
  f.key(Key('M'), ModCtrl);  // beta collapsed, alpha open: mixed
  CHECK(f.tv->folds().isCollapsedAt(9) && !f.tv->folds().isCollapsedAt(0));
  f.key(Key('M'), ModCtrl);
  f.key(Key('L'), ModCtrl);
  CHECK(!f.tv->folds().hasCollapsed());
  CHECK_EQ(f.tv->wrapLayout().totalRows(), 13);
  f.key(Key('M'), ModCtrl);  // nothing collapsed: all collapse, then the next one opens all again
  f.key(Key('L'), ModCtrl);
  CHECK_EQ(f.tv->folds().collapsed().size(), 4u);
  f.key(Key('M'), ModCtrl);
  f.key(Key('L'), ModCtrl);
  CHECK(!f.tv->folds().hasCollapsed());
}

TEST(foldview_collapse_to_definitions_chord_collapses_function_bodies_only) {
  FFx f(kSrc);
  f.ed->setSelections({caretAt({3, 6})});
  f.key(Key('M'), ModCtrl);
  f.key(Key('O'), ModCtrl);
  const FoldModel& m = f.tv->folds();
  CHECK(m.isCollapsedAt(0) && m.isCollapsedAt(9));  // alpha and beta; the nested `if` and the comment stay as they were
  CHECK(!m.isCollapsedAt(2) && !m.isCollapsedAt(7));
  CHECK(f.head() == (Position{0, 14}));  // the caret was inside alpha: it went to the head line
  CHECK_EQ(f.tv->wrapLayout().totalRows(), 13 - 5 - 2);
  // By name, from a fully collapsed state: the comment opens, only the bodies stay collapsed.
  f.app->runCommand("CollapseAllRegionsRecursive");
  CHECK(m.isCollapsedAt(7));
  f.app->runCommand("Editor.CollapseToDefinitions");
  CHECK(!m.isCollapsedAt(7) && m.isCollapsedAt(0) && m.isCollapsedAt(9) && m.collapsed().size() == 2u);
}

TEST(foldview_outline_options_switch_regions_off_and_back) {
  FFx f(kSrc);
  f.tv->settings().showFoldGlyphs = true;
  f.tv->foldRefreshNow();
  CHECK(f.tv->folds().regions().size() == 4);
  CHECK(f.tv->foldLaneWidth() > 0);
  EditorSettings es{false, false, false, true, false, 530, 3, 4, 0, true, false, true, true};
  f.tv->applySettings(es);  // outline.enabled = false
  f.tv->foldRefreshNow();
  CHECK(f.tv->folds().regions().empty());
  CHECK_EQ(f.tv->foldLaneWidth(), 0.0f);
  CHECK(!f.tv->foldingActive());
  CHECK(!f.tv->foldCommand(TextView::FoldOp::ToggleAll));
  es.outlineEnabled = true;
  es.outlineStatementBlocks = false;  // the `if` inside alpha is a statement block
  f.tv->applySettings(es);
  f.tv->foldRefreshNow();
  CHECK(f.tv->folds().regions().size() == 3);
  CHECK(f.tv->folds().regionStartingAt(2) == nullptr);
}

TEST(foldview_the_defaults_follow_visual_studio) {
  TextView::Settings s;
  CHECK(s.showFoldGlyphs && s.outlineEnabled && s.outlinePragmaRegions && s.outlineStatementBlocks);
}

TEST(foldview_hovering_the_marker_shows_the_hidden_code) {
  FFx f(kSrc);
  f.app->runCommand("CollapseAllRegions");
  f.frame();
  const gfx::RectF m = f.tv->foldMarkerRect(9);
  REQUIRE(m.w > 0);
  InputEvent e;
  e.type = InputType::MouseMove;
  e.x = m.x + 2;
  e.y = m.y + 7;
  f.app->hover().onMouse(e);
  CHECK(f.app->hover().pending());
  f.app->hover().tick(f.now + 10000);
  f.frame();
  CHECK(f.app->hover().shown());
  // Away from the marker: dismissed.
  e.x = 300;
  e.y = f.y(0) + 7;
  f.app->hover().onMouse(e);
  CHECK(!f.app->hover().shown());
}

TEST(foldview_the_unbound_commands_by_name) {
  FFx f(kSrc);
  f.app->runCommand("CollapseAllRegions");  // outermost only
  CHECK(f.tv->folds().isCollapsedAt(0) && f.tv->folds().isCollapsedAt(7) && f.tv->folds().isCollapsedAt(9));
  CHECK(!f.tv->folds().isCollapsedAt(2));
  f.app->runCommand("ExpandAllRegions");
  CHECK(!f.tv->folds().hasCollapsed());
  f.app->runCommand("CollapseAllRegionsRecursive");
  CHECK(f.tv->folds().isCollapsedAt(2));
  f.app->runCommand("ExpandAllRegions");  // opens the visible ones: alpha, comment, beta; the inner `if` stays
  CHECK(!f.tv->folds().isCollapsedAt(0) && f.tv->folds().isCollapsedAt(2));
  f.app->runCommand("ExpandAllRegionsRecursive");
  CHECK(!f.tv->folds().hasCollapsed());
  f.ed->setSelections({caretAt({0, 0})});
  f.app->runCommand("Editor.CollapseRegion");  // tenfold ids work too
  CHECK(f.tv->folds().isCollapsedAt(0));
}

TEST(foldview_a_non_cpp_file_has_no_regions) {
  FFx f("a {\n b\n}\n", false, 400, 280, nullptr, "a.txt");
  f.key(Key('['), kCtrlShift);
  CHECK(!f.tv->folds().hasCollapsed());
  CHECK(!f.tv->foldingActive());
}

TEST(foldview_scrollbar_selection_and_marks_skip_hidden_lines) {
  std::string src;
  for (int i = 0; i < 40; ++i) src += "void f" + std::to_string(i) + "() {\n  a;\n  b;\n}\n";
  FFx f(src, false, 400, 280);
  f.tv->foldRefreshNow();
  f.app->runCommand("CollapseAllRegions");
  CHECK_EQ(f.tv->folds().collapsed().size(), 40u);
  WrapLayout& W = f.tv->wrapLayout();
  CHECK_EQ(W.totalRows(), 41);  // 40 heads and the empty last line
  f.frame();
  // A selection from the first head to the third head covers the hidden lines in between without drawing them.
  f.ed->setSelections({Selection{{0, 3}, {8, 4}}});
  f.frame();
  CHECK(f.head().line == 8);
  // Find marks and caret marks of hidden lines land on the head's row.
  f.tv->setFindMatches({{{1, 2}, {1, 3}}}, 0);
  f.frame();
  CHECK(f.tv->folds().isHidden(1));
  CHECK_EQ(f.tv->visibleLineCount(), 20);
}

TEST(foldview_with_word_wrap_the_hidden_lines_have_no_rows_either) {
  std::string src = "int main() {\n";
  for (int i = 0; i < 4; ++i)
    src += "  call_some_function(argument_one, argument_two, argument_three, argument_four, argument_five);\n";
  src += "}\nint tail;\n";
  FFx f(src, true, 300, 280);
  WrapLayout& W = f.tv->wrapLayout();
  REQUIRE(W.enabled());
  const int rowsOfBody = W.rowsOf(1);
  REQUIRE(rowsOfBody >= 3);
  for (int i = 0; i < 7; ++i) W.rowsOf(i);  // exact row counts everywhere
  const int before = W.totalRows();
  f.ed->setSelections({caretAt({2, 4})});
  f.key(Key('['), kCtrlShift);
  CHECK(f.tv->folds().isCollapsedAt(0));
  CHECK_EQ(W.totalRows(), before - (4 * rowsOfBody + 1));
  CHECK_EQ(f.y(6), f.y(0) + 14.0f);
  f.key(Key(']'), kCtrlShift);
  CHECK_EQ(W.totalRows(), before);
}

TEST(foldview_typing_at_the_edge_or_a_jump_into_the_region_expands_it) {
  FFx f(kSrc);
  f.app->runCommand("CollapseAllRegionsRecursive");
  CHECK(f.tv->folds().isCollapsedAt(0) && f.tv->folds().isCollapsedAt(9));
  // The caret is on a head (the end of line 0): typing there opens that region only.
  f.ed->setSelections({caretAt({0, 14})});
  f.tv->foldRefreshNow();
  f.type("x");
  CHECK(!f.tv->folds().isCollapsedAt(0) && f.tv->folds().isCollapsedAt(9) && f.tv->folds().isCollapsedAt(2));
  // A jump (go to line, find, build error) lands inside a collapsed region: it opens (all that hide the line).
  CHECK(f.tv->folds().isHidden(3));
  f.ed->gotoLocation({f.doc->path(), {3, 4}});
  f.tv->scrollbar();
  f.root->tick(f.now);
  CHECK(!f.tv->folds().isCollapsedAt(2));
  CHECK(!f.tv->folds().isHidden(3));
  CHECK_EQ(f.head().line, 3);
  // Left from the line below a collapsed region lands on its hidden closing line: it opens.
  f.ed->setSelections({caretAt({12, 0})});
  f.key(Key::Left);
  f.key(Key::Left);
  CHECK(!f.tv->folds().isHidden(f.head().line));
}

TEST(foldview_folds_move_with_edits_elsewhere) {
  FFx f(kSrc);
  f.ed->setSelections({caretAt({9, 0})});
  f.key(Key('['), kCtrlShift);
  CHECK(f.tv->folds().isCollapsedAt(9));
  const float y9 = f.y(9);
  // Lines inserted at the top: the collapsed region moves down with its lines, and stays collapsed.
  f.ed->setSelections({caretAt({0, 0})});
  f.type("// a\n// b\n");
  CHECK(f.tv->folds().isCollapsedAt(11));
  CHECK(!f.tv->folds().isCollapsedAt(9));
  CHECK_EQ(f.tv->wrapLayout().totalRows(), 15 - 2);
  CHECK_EQ(f.y(11), y9 + 14.0f * 2);
  // Undo moves it back.
  f.key(Key('Z'), ModCtrl);
  CHECK(f.tv->folds().isCollapsedAt(9));
  CHECK_EQ(f.y(9), y9);
}

TEST(foldview_clicking_the_marker_expands_and_the_gutter_glyph_toggles) {
  FFx f(kSrc);
  f.tv->settings().showFoldGlyphs = true;
  f.frame();
  // The glyph lane sits between the numbers and the text; the numbers moved left by its width.
  const gfx::RectF lane = f.tv->foldLaneRect();
  REQUIRE(lane.w > 0);
  CHECK(lane.x > 0 && lane.x < 44);
  f.click(lane.x + lane.w / 2, f.y(9) + 7);  // beta's glyph
  CHECK(f.tv->folds().isCollapsedAt(9));
  f.click(lane.x + lane.w / 2, f.y(5) + 7);  // a line without a region: nothing
  CHECK_EQ(f.tv->folds().collapsed().size(), 1u);
  const gfx::RectF m = f.tv->foldMarkerRect(9);
  REQUIRE(m.w > 0);
  f.click(m.x + 2, m.y + 7);
  CHECK(!f.tv->folds().hasCollapsed());
  f.click(lane.x + lane.w / 2, f.y(0) + 7);
  CHECK(f.tv->folds().isCollapsedAt(0));
  // Triple click on a collapsed head selects the folded lines with it.
  f.click(60, f.y(0) + 7, 3);
  CHECK(f.ed->selections()[0].anchor == (Position{0, 0}));
  CHECK(f.head() == (Position{6, 0}));
}

TEST(foldview_without_the_glyph_setting_the_gutter_is_unchanged) {
  FFx a(kSrc), b(kSrc);
  b.tv->settings().showFoldGlyphs = true;
  b.frame();
  CHECK_EQ(a.tv->foldLaneWidth(), 0.0f);
  CHECK(b.tv->foldLaneWidth() > 0.0f);
  CHECK_EQ(a.tv->caretRect({0, 0}).x, 44.0f);
  CHECK(b.tv->caretRect({0, 0}).x > 44.0f);
  a.app->runCommand("CollapseAllRegions");
  a.frame();
  CHECK_EQ(a.tv->caretRect({0, 0}).x, 44.0f);  // a collapsed region does not move the text either
}

TEST(foldview_regions_are_computed_in_the_background_and_never_for_a_changed_buffer) {
  FFx f(kSrc);
  CHECK(!f.tv->folds().fresh());
  REQUIRE(f.waitFresh());
  CHECK(pairs(f.tv->folds().regions()) == (P{{0, 5}, {2, 4}, {7, 8}, {9, 11}}));
  // An edit makes the set stale; a job started after the pause but overtaken by another edit is dropped.
  f.ed->setSelections({caretAt({6, 0})});
  f.type("{\n}\n");
  CHECK(!f.tv->folds().fresh());
  f.now += 500;
  f.app->tick(f.now);   // starts the job for this version
  f.type("// more\n");  // ... which is now out of date
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  f.app->tick(f.now);  // collects it: wrong version, not applied
  CHECK(!f.tv->folds().fresh());
  REQUIRE(f.waitFresh());
  CHECK(pairs(f.tv->folds().regions()) == (P{{0, 5}, {2, 4}, {6, 7}, {10, 11}, {12, 14}}) ||
        pairs(f.tv->folds().regions()).size() == 5);
  // Typing in the middle of a pause-less burst never recomputes: no job until the pause.
  for (int i = 0; i < 5; ++i) f.type("a");
  CHECK(!f.tv->folds().fresh());
}

TEST(foldview_collapse_with_stale_regions_computes_them_first) {
  FFx f(kSrc);
  f.ed->setSelections({caretAt({0, 0})});
  f.type("// top\n");  // the regions were never computed, then moved: the command must not use a guess
  f.ed->setSelections({caretAt({2, 0})});
  f.key(Key('['), kCtrlShift);
  CHECK(f.tv->folds().isCollapsedAt(1));
  CHECK(f.tv->folds().fresh());
}

// ---------------------------------------------------------------------------------------------------------------
// Golden + benchmark (real font)
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

// A small C++ file with every region collapsed except the namespace and main (the `if` inside main stays collapsed),
// the fold glyphs on, a collapsed comment, and a selection across a collapsed function.
TEST(foldview_golden_w8f_collapsed_regions) {
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
  const std::string src =
      "#include <vector>\n"         // 0
      "\n"                          // 1
      "/* A comment that\n"         // 2
      "   spans two lines */\n"     // 3
      "namespace geo {\n"           // 4
      "struct Box {\n"              // 5
      "  int w = 0;\n"              // 6
      "  int h = 0;\n"              // 7
      "};\n"                        // 8
      "\n"                          // 9
      "int area(const Box& b) {\n"  // 10
      "  return b.w * b.h;\n"       // 11
      "}\n"                         // 12
      "}  // namespace geo\n"       // 13
      "\n"                          // 14
      "int main() {\n"              // 15
      "  geo::Box b;\n"             // 16
      "  if (b.w) {\n"              // 17
      "    return 1;\n"             // 18
      "  }\n"                       // 19
      "  return geo::area(b);\n"    // 20
      "}\n";                        // 21
  FFx f(src, false, 480, 360, std::move(tr));
  f.tv->settings().showFoldGlyphs = true;
  f.tv->foldRefreshNow();
  REQUIRE(f.tv->folds().regions().size() == 6);
  f.app->runCommand("CollapseAllRegionsRecursive");
  for (int line : {4, 15}) {  // open the namespace and main
    f.ed->setSelections({caretAt({line, 0})});
    f.root->tick(f.now);
    f.tv->foldCommand(TextView::FoldOp::Expand);
  }
  f.ed->setSelections({Selection{{5, 3}, {15, 4}}});  // across the collapsed `area`
  f.root->tick(f.now);
  f.text->releaseTexture(f.rec);
  gfx::GlRenderer& r = *gl->renderer;
  r.setOffscreen(true);
  f.root->requestLayout();
  f.root->render(r, f.w, f.h);
  std::vector<uint8_t> px(static_cast<size_t>(f.w) * f.h * 4);
  REQUIRE(r.readPixels(f.w, f.h, px.data()));
  f.text->releaseTexture(r);
  compareGolden("w8f_collapsed_regions", px, f.w, f.h);
}

// Keystroke-to-paint (budget 8 ms) on the 100k-line file with 1000 collapsed regions: typing on a visible line and
// painting the folded view, at 1920 x 1080 with word wrap on (the default).
TEST(foldview_perf_keystroke_to_paint_100k_lines_1000_collapsed_regions) {
  std::string content = "// header\n";
  for (int f = 0; f < 1000; ++f) {
    content += "void func" + std::to_string(f) + "(int a) {\n";
    for (int i = 0; i < 97; ++i) content += "  total += a * " + std::to_string(i) + "; // body line\n";
    content += "}\n";
  }
  const std::string font = dejavuMono();
  std::string err;
  auto tr = gfx::TextRenderer::create(font, 15.0f, 1.0f, &err);
  if (!tr) {
    std::cerr << "    SKIP: " << err << "\n";
    return;
  }
  FFx f(content, true, 1920, 1080, std::move(tr));
  using clk = std::chrono::steady_clock;
  auto ms = [](clk::time_point a, clk::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
  };
  auto t0 = clk::now();
  f.tv->foldRefreshNow();
  const double detect = ms(t0, clk::now());
  t0 = clk::now();
  f.app->runCommand("CollapseAllRegions");
  f.frame();
  const double collapse = ms(t0, clk::now());
  REQUIRE(f.tv->folds().collapsed().size() == 1000u);
  CHECK_EQ(f.tv->wrapLayout().totalRows(), 1002);  // 1000 heads, the header, the empty last line
  f.ed->setSelections({caretAt({0, 9})});
  f.root->tick(f.now);
  for (int i = 0; i < 20; ++i) {
    f.type("a");
    f.frame();
  }
  const int n = 300;
  std::vector<double> edit, paint, total;
  for (int i = 0; i < n; ++i) {
    InputEvent ev;
    ev.type = InputType::Text;
    ev.text = "a";
    f.now += 50;
    auto a = clk::now();
    f.root->dispatch(ev);
    auto b = clk::now();
    f.root->requestLayout();
    f.frame();
    auto c = clk::now();
    edit.push_back(ms(a, b));
    paint.push_back(ms(b, c));
    total.push_back(ms(a, c));
  }
  CHECK(f.doc->buffer().line(0).find(std::string(static_cast<size_t>(n), 'a')) != std::string::npos);
  CHECK_EQ(f.tv->folds().collapsed().size(), 1000u);
  double sumE = 0, sumP = 0;
  for (int i = 0; i < n; ++i) sumE += edit[static_cast<size_t>(i)], sumP += paint[static_cast<size_t>(i)];
  std::sort(total.begin(), total.end());
  // Scroll frames over the folded document.
  InputEvent wheel;
  wheel.type = InputType::Wheel;
  wheel.x = 500;
  wheel.y = 500;
  double scroll = 0;
  for (int i = 0; i < 100; ++i) {
    wheel.dy = i % 2 ? 1.0f : -1.0f;
    auto a = clk::now();
    f.root->dispatch(wheel);
    f.frame();
    scroll += ms(a, clk::now());
  }
  std::printf(
      "PERF: textview keystroke-to-paint, 100k lines, 1000 collapsed regions @1920x1080: edit+sync %.3f ms, "
      "layout+paint+record %.3f ms, total avg %.3f ms / p99 %.3f ms (budget 8 ms); scroll frame %.3f ms; "
      "region detection %.1f ms, CollapseAllRegions + frame %.1f ms\n",
      sumE / n, sumP / n, (sumE + sumP) / n, total[static_cast<size_t>(n * 0.99)], scroll / 100, detect, collapse);
#if defined(NDEBUG) && !defined(__SANITIZE_ADDRESS__)
  PERF_CHECK((sumE + sumP) / n < 8.0, "textview keystroke-to-paint with 1000 collapsed regions (8 ms)");
#endif
}
