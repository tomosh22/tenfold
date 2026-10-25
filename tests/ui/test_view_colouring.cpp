// B2b: syntax colouring, diagnostics squiggles, scrollbar with markers, column guide, whitespace, bracket match and
// the settings / theme wiring of TextView + EditorApp. Mock font (8x14 cells, text starts at x = 44) and a recording
// renderer that keeps glyph colours, so assertions are on what was drawn; two goldens (pinned DejaVuSansMono) and the
// PERF tests need the real font / GL and skip without.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>

#include "../text/text_test_util.h"
#include "app/Editor.h"
#include "ui/gfx/GlLoader.h"
#include "ui/gfx/PngWrite.h"
#include "ui/gfx/TextRenderer.h"
#include "ui/settings/Settings.h"
#include "ui/settings/Theme.h"
#include "ui/shell/EditorApp.h"
#include "ui/widgets/TextView.h"
#include "ui_test.h"

using namespace tf;
using namespace tf::ui;
namespace fs = std::filesystem;

namespace {

struct Rec2 : gfx::Renderer {
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

InputEvent mouse(InputType t, float x, float y) {
  InputEvent e;
  e.type = t;
  e.x = x;
  e.y = y;
  e.clicks = 1;
  e.buttons = t == InputType::MouseUp ? 0 : 1;
  return e;
}

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

Theme darkTheme() {
  std::vector<SettingsDiag> d;
  return loadTheme("dark", {}, {}, d);
}

// A real Editor over a temp file. Mock cells 8 x 14; the gutter is 40 wide, text starts at x = 44 (fewer than 1000
// lines). `render` = false leaves the first frame to the test (cold-cache measurements).
struct Fx2 {
  fs::path dir;
  std::unique_ptr<Editor> ed;
  Document* doc = nullptr;
  std::unique_ptr<gfx::TextRenderer> text;
  std::unique_ptr<RootWidget> root;
  TextView* tv = nullptr;
  Rec2 rec;
  Theme th = darkTheme();
  int w, h;

  static std::unique_ptr<gfx::TextRenderer> mockText() {
    std::u32string cps;
    for (char32_t c = 32; c < 127; ++c) cps += c;
    cps += U"漢字";
    gfx::FontStack stack;
    stack.add(std::make_shared<tftext::MockFont>(cps));
    return gfx::TextRenderer::fromFonts(std::move(stack));
  }

  explicit Fx2(const std::string& content, const char* name = "a.cpp", int w_ = 640, int h_ = 280,
               std::unique_ptr<gfx::TextRenderer> tr = nullptr, bool render = true, bool wrap = false)
      : w(w_), h(h_) {
    static int counter = 0;
    dir = uitest::tempRoot() /
          ("tenfold_col_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_" +
           std::to_string(counter++));
    fs::create_directories(dir);
    {
      std::ofstream out(dir / name, std::ios::binary);
      out << content;
    }
    ed = std::make_unique<Editor>(dir);
    doc = ed->open(name);
    ed->gotoLocation({doc->path(), {0, 0}});
    text = tr ? std::move(tr) : mockText();
    root = std::make_unique<RootWidget>(*text, th);
    auto v = std::make_unique<TextView>(*ed);
    tv = v.get();
    tv->settings().smoothScroll = false;
    tv->settings().wordWrap = wrap;  // the colour scenarios are written for unwrapped lines
    tv->settings().caretBlink = false;
    root->addChild(std::move(v));
    tv->setFocus();
    if (render) frame();
  }
  ~Fx2() {
    root.reset();
    ed.reset();
    std::error_code ec;
    fs::remove_all(dir, ec);
  }
  void frame() { root->render(rec, w, h); }
  void tick() { root->tick(0); }
  // The colour of the glyph drawn in cell (line, col) at the current scroll, or null when none was drawn.
  const gfx::Color* colorAt(int line, int col, float scrollY = 0) const {
    const float x = 44.0f + static_cast<float>(col) * 8, y = static_cast<float>(line) * 14 - scrollY;
    for (const Rec2::Q& q : rec.quads)
      if (q.x >= x && q.x < x + 8 && q.y >= y && q.y < y + 14) return &q.c;
    return nullptr;
  }
  bool is(int line, int col, gfx::Color c, float scrollY = 0) const {
    const gfx::Color* g = colorAt(line, col, scrollY);
    return g && same(*g, c);
  }
};

}  // namespace

TEST(colouring_lexical_keywords_strings_comments_and_numbers) {
  Fx2 f("int main() {\n  const char* s = \"hi\"; // tail\n  return 42;\n}\n");
  const Theme& t = f.th;
  CHECK(f.is(0, 0, t.tokenColor("keyword")) && f.is(0, 2, t.tokenColor("keyword")));
  CHECK(f.is(0, 4, t.tokenColor("identifier")));
  CHECK(f.is(0, 8, t.tokenColor("punctuation")));
  CHECK(f.is(1, 2, t.tokenColor("keyword")) && f.is(1, 14, t.tokenColor("identifier")));  // const, s
  CHECK(f.is(1, 18, t.tokenColor("string")) && f.is(1, 20, t.tokenColor("string")));      // "hi"
  CHECK(f.is(1, 24, t.tokenColor("comment")) && f.is(1, 30, t.tokenColor("comment")));
  CHECK(f.is(2, 2, t.tokenColor("keyword")) && f.is(2, 9, t.tokenColor("number")));
  CHECK(!same(t.tokenColor("keyword"), t.tokenColor("string")));
  // Plain text files are not coloured.
  Fx2 p("int main() {\n", "a.txt");
  CHECK(p.is(0, 0, p.th.text) && p.is(0, 8, p.th.text));
}

TEST(colouring_block_comment_opened_above_the_viewport) {
  std::string src = "/* start\n";
  for (int i = 1; i < 60; ++i) src += "int x" + std::to_string(i) + ";\n";
  src += "*/ int y; \"s\"\nint z;\n";
  Fx2 f(src);
  const float sy = 50 * 14;
  f.tv->viewport().jumpTo(0, sy);  // lines 50.. visible; the comment opened on line 0
  f.frame();
  const Theme& t = f.th;
  CHECK(f.is(55, 0, t.tokenColor("comment"), sy));  // "int" inside the comment is not a keyword
  CHECK(!f.is(55, 0, t.tokenColor("keyword"), sy));
  CHECK(f.is(60, 0, t.tokenColor("comment"), sy) && f.is(60, 1, t.tokenColor("comment"), sy));
  CHECK(f.is(60, 3, t.tokenColor("keyword"), sy));  // after the closing */
  CHECK(f.is(61, 0, t.tokenColor("keyword"), sy));
}

TEST(colouring_lexer_raw_strings_directives_and_edits_relex_forward) {
  std::vector<LexSpan> sp;
  LexState st = lexLine("auto s = R\"x(a // b", {}, sp);
  CHECK(st.mode == LexState::Raw && sp.back().kind == Lex::String);
  sp.clear();
  st = lexLine("c)x\" + 1; // c", st, sp);
  CHECK(st.mode == LexState::None && sp[0].kind == Lex::String && sp.back().kind == Lex::Comment);
  sp.clear();
  st = lexLine("#define A(x) \\", {}, sp);
  CHECK(st.mode == LexState::Directive);
  sp.clear();
  CHECK(lexLine("  x + 1", st, sp).mode == LexState::None && sp.size() == 1 && sp[0].kind == Lex::Preprocessor);

  // Opening a block comment on line 0 recolours the lines below; closing it again restores them (the cache walks
  // forward until the state matches what each cached line was lexed with).
  Fx2 f("int a;\nint b;\nint c;\n");
  const Theme& t = f.th;
  CHECK(f.is(2, 0, t.tokenColor("keyword")));
  f.doc->insert({0, 0}, "/*");
  f.frame();
  CHECK(f.is(0, 0, t.tokenColor("comment")) && f.is(2, 0, t.tokenColor("comment")));
  f.doc->erase({{0, 0}, {0, 2}});
  f.frame();
  CHECK(f.is(0, 0, t.tokenColor("keyword")) && f.is(2, 0, t.tokenColor("keyword")));
}

TEST(colouring_semantic_tokens_override_identifiers_only) {
  Fx2 f("int foo = bar(1); // foo\n");
  const Theme& t = f.th;
  CHECK(f.is(0, 4, t.tokenColor("identifier")));
  const gfx::Color kw = *f.colorAt(0, 0), cm = *f.colorAt(0, 18);
  LspSemanticTokensLegend legend;
  legend.types = {"variable", "function", "type"};
  legend.modifiers = {"declaration", "readonly"};
  // foo (variable), bar (function), a bogus span over the keyword `int` and one over the comment's `foo`.
  f.tv->setSemanticTokens({{0, 4, 3, 0, 1}, {0, 10, 3, 1, 0}, {0, 0, 3, 2, 0}, {0, 21, 3, 0, 0}}, legend);
  f.frame();
  CHECK(f.is(0, 4, t.tokenColor("variable", {"declaration"})) && f.is(0, 6, t.tokenColor("variable", {"declaration"})));
  CHECK(f.is(0, 10, t.tokenColor("function")) && f.is(0, 12, t.tokenColor("function")));
  CHECK(!same(t.tokenColor("function"), t.tokenColor("identifier")));
  CHECK(same(*f.colorAt(0, 0), kw) && same(*f.colorAt(0, 22), cm));  // untouched
  // Unknown legend index: no override, no crash.
  f.tv->setSemanticTokens({{0, 4, 3, 9, 0}}, legend);
  f.frame();
  CHECK(f.is(0, 4, t.tokenColor("identifier")));
}

TEST(colouring_semantic_spans_remap_through_edits) {
  Fx2 f("int a;\nint foo;\nint bar;\n");
  const Theme& t = f.th;
  LspSemanticTokensLegend legend;
  legend.types = {"variable", "function"};
  f.tv->setSemanticTokens({{1, 4, 3, 0, 0}, {2, 4, 3, 1, 0}}, legend);
  f.frame();
  CHECK(f.is(1, 4, t.tokenColor("variable")) && f.is(2, 4, t.tokenColor("function")));
  f.doc->insert({0, 0}, "int z;\n");  // a line inserted above: spans shift down
  f.frame();
  CHECK(f.tv->highlighter().semanticCount() == 2);
  CHECK(f.is(2, 4, t.tokenColor("variable")) && f.is(3, 4, t.tokenColor("function")));
  CHECK(f.is(1, 4, t.tokenColor("identifier")));  // the line now at 1 never had a span
  f.doc->insert({2, 3}, "   ");                   // before foo on its own line: the span moves right
  f.frame();
  CHECK(f.is(2, 7, t.tokenColor("variable")));
  f.doc->insert({3, 5}, "x");  // inside bar: the span is dropped, bar falls back to the lexical colour
  f.frame();
  CHECK(f.is(3, 4, t.tokenColor("identifier")) && f.is(2, 7, t.tokenColor("variable")));
  f.doc->erase({{1, 0}, {3, 0}});  // two lines removed: spans of the removed lines go, the rest shift up
  f.frame();
  CHECK(f.tv->highlighter().semanticCount() <= 2);
  CHECK(!f.is(1, 7, t.tokenColor("variable")));
}

TEST(squiggle_is_drawn_under_a_diagnostic_range) {
  Fx2 f("int a;\nint foo = 1;\nint c;\nint d;\n");
  Diagnostic err, warn;
  err.file = warn.file = f.doc->path();
  err.range = {{1, 4}, {1, 7}};
  err.severity = 1;
  warn.range = {{3, 0}, {3, 3}};
  warn.severity = 2;
  f.ed->setDiagnostics(f.doc->path(), {err, warn});
  f.tick();
  f.frame();
  int nErr = 0, nWarn = 0;
  for (const Rec2::Ln& l : f.rec.lines) {
    if (same(l.c, f.th.diagnosticError)) {
      ++nErr;
      CHECK(l.x0 >= 44 + 4 * 8 - 0.01f && l.x1 <= 44 + 7 * 8 + 0.01f);  // only under the three columns
      CHECK(l.y0 >= 14 && l.y1 < 28 && l.y0 >= 14 + 14 - 4);            // line 1, near its bottom
    } else if (same(l.c, f.th.diagnosticWarning)) {
      ++nWarn;
      CHECK(l.y0 >= 42 && l.y1 < 56);
    }
  }
  CHECK(nErr >= 5 && nWarn >= 5);
  f.ed->setDiagnostics(f.doc->path(), {});
  f.tick();
  f.frame();
  for (const Rec2::Ln& l : f.rec.lines) CHECK(!same(l.c, f.th.diagnosticError));
}

TEST(scrollbar_marker_lanes_for_diagnostics_search_and_carets) {
  std::string src;
  for (int i = 0; i < 100; ++i) src += "x;\n";
  Fx2 f(src);
  Diagnostic e, wn;
  e.range = {{10, 0}, {10, 1}};
  e.severity = 1;
  wn.range = {{50, 0}, {50, 1}};
  wn.severity = 2;
  f.ed->setDiagnostics(f.doc->path(), {e, wn});
  f.tv->setSearchMarkers({{{20, 0}, {20, 1}}});
  f.tick();
  f.frame();
  const Scrollbar& sb = f.tv->scrollbar();
  const gfx::RectF b = sb.bounds();
  CHECK(b.w == 12 && b.x == 640 - 12 && b.h == 280);
  auto y = [&](int line) { return std::floor(b.y + (static_cast<float>(line) + 0.5f) / 101.0f * b.h); };
  auto find = [&](gfx::Color c, float x) {
    for (const Rec2::R& r : f.rec.rects)
      if (same(r.c, c) && r.r.x == x && r.r.w == 4 && r.r.h == 2) return r.r.y;
    return -1.0f;
  };
  CHECK(find(f.th.diagnosticError, b.x + 8) == y(10));
  CHECK(find(f.th.diagnosticWarning, b.x + 8) == y(50));
  CHECK(find(f.th.matchMarker, b.x) == y(20));
  CHECK(find(f.th.textDim, b.x + 4) == y(0));  // the caret lane
  f.ed->setSelections({caretAt({70, 0})});
  f.tick();
  f.frame();
  CHECK(find(f.th.textDim, b.x + 4) == y(70));
}

TEST(scrollbar_thumb_drag_and_click_to_page) {
  std::string src;
  for (int i = 0; i < 300; ++i) src += "x;\n";
  Fx2 f(src);
  const Scrollbar& sb = f.tv->scrollbar();
  const gfx::RectF t = sb.thumbRect();
  CHECK(t.w == 12 && t.h >= 24 && t.y == 0);
  const float x = t.x + 6, grab = t.y + 5;
  const float range = 301 * 14 - 140;  // maxScrollY: the last half page may scroll past the end
  f.root->dispatch(mouse(InputType::MouseDown, x, grab));
  CHECK(sb.dragging());
  f.root->dispatch(mouse(InputType::MouseMove, x, grab + 100));
  const float free = 280 - t.h;
  CHECK(std::abs(f.tv->viewport().y() - 100.0f / free * range) <= 1.0f);
  f.root->dispatch(mouse(InputType::MouseMove, x, grab + 5000));  // clamps at the end
  CHECK(std::abs(f.tv->viewport().y() - range) <= 1.0f);
  f.root->dispatch(mouse(InputType::MouseUp, x, grab + 5000));
  CHECK(!sb.dragging());
  f.frame();
  const gfx::RectF t2 = sb.thumbRect();
  CHECK(t2.y + t2.h <= 280.01f && t2.y > 200);
  // Click above the thumb pages up by 90% of the view.
  const float before = f.tv->viewport().targetY();
  f.root->dispatch(mouse(InputType::MouseDown, x, 2));
  f.root->dispatch(mouse(InputType::MouseUp, x, 2));
  CHECK(std::abs(f.tv->viewport().targetY() - (before - 252)) <= 1.0f);
  // The wheel over the bar still scrolls the view.
  InputEvent wheel;
  wheel.type = InputType::Wheel;
  wheel.x = x;
  wheel.y = 100;
  wheel.dy = -1;
  const float y0 = f.tv->viewport().targetY();
  f.root->dispatch(wheel);
  CHECK(f.tv->viewport().targetY() > y0);
}

TEST(guide_whitespace_and_bracket_match_drawing) {
  Fx2 f("a b\tc\nf(a[1], {b});\n");
  f.tv->settings().columnGuide = 10;
  f.tv->settings().showWhitespace = true;
  f.frame();
  bool guide = false;
  for (const Rec2::R& r : f.rec.rects)
    if (same(r.c, f.th.columnGuide) && r.r.w == 1 && r.r.h == 280 && r.r.x == 44 + 10 * 8) guide = true;
  CHECK(guide);
  int dots = 0;
  for (const Rec2::R& r : f.rec.rects)
    if (same(r.c, f.th.whitespace) && r.r.w == 1 && r.r.h == 1 && r.r.y == 6) ++dots;
  CHECK(dots >= 1);  // the space in "a b" (col 1) and in "f(a[1], {b})" is on line 1 (y 20)
  bool dotCol1 = false, arrow = false;
  for (const Rec2::R& r : f.rec.rects) dotCol1 |= same(r.c, f.th.whitespace) && r.r.x == 55 && r.r.y == 6;
  for (const Rec2::Ln& l : f.rec.lines)
    arrow |= same(l.c, f.th.whitespace) && l.x0 == 44 + 3 * 8 + 2 && l.x1 == 44 + 4 * 8 - 2;
  CHECK(dotCol1 && arrow);
  f.tv->settings().showWhitespace = false;
  f.frame();
  for (const Rec2::R& r : f.rec.rects) CHECK(!(same(r.c, f.th.whitespace)));

  // Bracket match: caret before '(' pairs with the ')' skipping the nested ones.
  f.ed->setSelections({caretAt({1, 1})});
  f.tick();
  f.frame();
  const auto m = f.tv->bracketMatch();
  CHECK(m && m->first == (Position{1, 1}) && m->second == (Position{1, 11}));
  int boxes = 0;
  for (const Rec2::R& r : f.rec.rects)
    if (same(r.c, f.th.bracketMatch)) {
      ++boxes;
      CHECK(r.r.x == 44 + 1 * 8 || r.r.x == 44 + 11 * 8);
    }
  CHECK(boxes == 2);
  f.ed->setSelections({caretAt({1, 12})});  // caret after ')': the char before counts
  CHECK(f.tv->bracketMatch() && f.tv->bracketMatch()->second == (Position{1, 1}));
}

TEST(bracket_match_ignores_comments_strings_and_angle_brackets) {
  Fx2 f("x = \"(\" + (y); // )\n{\n  // }\n  s = \"}\";\n}\na < b\n");
  auto at = [&](Position p) {
    f.ed->setSelections({caretAt(p)});
    f.tick();
    return f.tv->bracketMatch();
  };
  CHECK(!at({0, 5}));  // the '(' inside the string
  const auto m = at({0, 10});
  CHECK(m && m->second == (Position{0, 12}));  // the real ')', not the one in the comment
  const auto b = at({1, 0});
  CHECK(b && b->second == (Position{4, 0}));  // braces in the comment and the string are skipped
  CHECK(!at({5, 2}));                         // '<' is never matched
  CHECK(!at({0, 0}));
}

TEST(settings_change_repaints_with_new_tab_width_and_theme) {
  Fx2 f("\tx\n");
  const fs::path user = f.dir / "user";
  SettingsService::Options o;
  o.workspaceRoot = f.dir;
  o.userDir = user;
  SettingsService svc(o);
  EditorApp app(*f.root, *f.ed);  // after svc: it unregisters from it on destruction
  app.attachSettings(svc, 1.0f);
  f.tick();
  f.frame();
  auto xOfX = [&] {
    for (const Rec2::Q& q : f.rec.quads)
      if (q.y < 14 && q.x >= 44) return q.x;
    return -1.0f;
  };
  CHECK(xOfX() == 44 + 4 * 8 + 1);
  fs::create_directories(user);
  {
    std::ofstream out(user / "settings.txt", std::ios::binary);
    out << "editor.tabWidth = 2\neditor.columnGuide = 20\neditor.showWhitespace = true\nui.theme = light\n"
           "editor.bogus = 1\n";
  }
  svc.notifyFileChanged(user / "settings.txt");
  CHECK(svc.poll());
  f.tick();
  f.frame();
  CHECK(xOfX() == 44 + 2 * 8 + 1);
  CHECK(f.tv->settings().columnGuide == 20 && f.tv->settings().showWhitespace && f.tv->settings().tabWidth == 2);
  std::vector<SettingsDiag> d;
  const Theme light = loadTheme("light", {}, {}, d);
  CHECK(same(f.root->rootTheme().background, light.background));
  CHECK(app.message().find("bogus") != std::string::npos);  // settings diagnostics reach the status message
  Fx2* none = nullptr;
  (void)none;
  // The new theme's colours are used for the text (keyword in light differs from dark).
  f.doc->insert({0, 1}, "int ");
  f.frame();
  const gfx::Color* kw = f.colorAt(0, 2);  // after the tab (2 columns now)
  CHECK(kw && same(*kw, light.tokenColor("keyword")) &&
        !same(light.tokenColor("keyword"), darkTheme().tokenColor("keyword")));
  // A font.* change calls the rebuild hook once per change.
  int rebuilt = 0;
  app.attachSettings(svc, 1.0f, [&] { return ++rebuilt > 0; });
  {
    std::ofstream out(user / "settings.txt", std::ios::binary);
    out << "font.size = 14\n";
  }
  svc.notifyFileChanged(user / "settings.txt");
  svc.poll();
  CHECK(rebuilt == 1);
}

// ---- goldens (real font + GL) ----

namespace {

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

// The sample file coloured lexically; `rich` adds semantic tokens (made by a naive rule), two squiggles, a column
// guide at 60 and visible whitespace.
void goldenCase(const char* name, bool rich) {
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
  Fx2 f(readFile(TENFOLD_SOURCE_DIR "/examples/sample_project/src/main.cpp"), "a.cpp", 800, 480, std::move(tr));
  if (rich) {
    LspSemanticTokensLegend legend;
    legend.types = {"type", "function", "variable"};
    std::vector<TokenSpan> spans;
    const TextBuffer& b = f.doc->buffer();
    for (int l = 0; l < b.lineCount(); ++l) {
      const std::string& s = b.line(l);
      for (size_t i = 0; i < s.size();) {
        if (!(std::isalpha(static_cast<unsigned char>(s[i])) || s[i] == '_')) {
          ++i;
          continue;
        }
        size_t j = i;
        while (j < s.size() && (std::isalnum(static_cast<unsigned char>(s[j])) || s[j] == '_')) ++j;
        const uint16_t type = j < s.size() && s[j] == '(' ? 1 : std::isupper(static_cast<unsigned char>(s[i])) ? 0 : 2;
        spans.push_back({l, static_cast<int>(i), static_cast<int>(j - i), type, 0});
        i = j;
      }
    }
    f.tv->setSemanticTokens(std::move(spans), legend);
    Diagnostic e, w;
    e.range = {{8, 4}, {8, 12}};
    e.severity = 1;
    w.range = {{11, 2}, {11, 20}};
    w.severity = 2;
    f.ed->setDiagnostics(f.doc->path(), {e, w});
    f.tv->settings().columnGuide = 60;
    f.tv->settings().showWhitespace = true;
  }
  f.tick();
  f.text->releaseTexture(f.rec);  // the warm-up frame went to the recording renderer: re-create the atlas texture on GL
  gfx::GlRenderer& r = *gl->renderer;
  r.setOffscreen(true);
  f.root->requestLayout();
  f.root->render(r, f.w, f.h);
  std::vector<uint8_t> px(static_cast<size_t>(f.w) * f.h * 4);
  REQUIRE(r.readPixels(f.w, f.h, px.data()));
  f.text->releaseTexture(r);
  compareGolden(name, px, f.w, f.h);
}

}  // namespace

TEST(golden_b2b_lexical_colours) { goldenCase("b2b_lexical", false); }
TEST(golden_b2b_semantic_squiggles_guide_whitespace) { goldenCase("b2b_semantic", true); }

// B7b: a representative C++ file in the vs-dark theme: lexical kinds, semantic types and modifiers (made from a
// word list), current line outline, selection, find matches, bracket match and the three squiggle severities.
TEST(golden_vs_dark_editor) {
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
      "#include <string>\n"
      "#define MAX_ITEMS 16\n"
      "\n"
      "namespace shapes {\n"
      "enum class Kind { Circle, Square };\n"
      "\n"
      "// Base class of all shapes.\n"
      "class Shape {\n"
      " public:\n"
      "  static int count;\n"
      "  virtual double area(double scale) const = 0;\n"
      "  int sides = 4;\n"
      "};\n"
      "\n"
      "template <typename T> T clamp(T value, T hi) {\n"
      "  const char* label = \"area\\n\";\n"
      "  for (int i = 0; i < MAX_ITEMS; ++i) {\n"
      "    if (value > hi) return hi * 0.5f + 'x';\n"
      "  }\n"
      "  Kind k = Kind::Circle;\n"
      "  return value;\n"
      "}\n"
      "}  // namespace shapes\n";
  Fx2 f(src, "vs.cpp", 800, 480, std::move(tr));
  std::vector<SettingsDiag> d;
  f.root->setTheme(loadTheme("vs-dark", {}, {}, d));
  CHECK(d.empty());
  LspSemanticTokensLegend legend;
  legend.types = {"namespace", "class",    "enum",      "enumMember", "function",     "method",
                  "property",  "variable", "parameter", "macro",      "typeParameter"};
  legend.modifiers = {"static", "declaration"};
  struct W {
    const char* word;
    uint16_t type;
    uint32_t mods;
  };
  const W words[] = {{"shapes", 0, 0},    {"Shape", 1, 0}, {"Kind", 2, 0},  {"Circle", 3, 0}, {"Square", 3, 0},
                     {"count", 6, 1},     {"area", 5, 0},  {"scale", 8, 0}, {"sides", 6, 0},  {"clamp", 4, 0},
                     {"T", 10, 0},        {"value", 8, 0}, {"hi", 8, 0},    {"label", 7, 0},  {"i", 7, 0},
                     {"MAX_ITEMS", 9, 0}, {"k", 7, 0}};
  std::vector<TokenSpan> spans;
  const TextBuffer& b = f.doc->buffer();
  for (int l = 0; l < b.lineCount(); ++l) {
    const std::string& s = b.line(l);
    if (!s.empty() && s[0] == '#') continue;
    for (size_t i = 0; i < s.size();) {
      if (!(std::isalpha(static_cast<unsigned char>(s[i])) || s[i] == '_')) {
        ++i;
        continue;
      }
      size_t j = i;
      while (j < s.size() && (std::isalnum(static_cast<unsigned char>(s[j])) || s[j] == '_')) ++j;
      const std::string w = s.substr(i, j - i);
      const bool call = j < s.size() && s[j] == '(';
      for (const W& e : words)
        if (w == e.word && !(s.compare(0, 2, "//") == 0))
          spans.push_back(
              {l, static_cast<int>(i), static_cast<int>(j - i), call && e.type == 5 ? uint16_t(5) : e.type, e.mods});
      i = j;
    }
  }
  f.tv->setSemanticTokens(std::move(spans), legend);
  Diagnostic e, w, n;
  e.range = {{19, 9}, {19, 13}};
  e.severity = 1;
  w.range = {{15, 20}, {15, 26}};
  w.severity = 2;
  n.range = {{10, 9}, {10, 13}};
  n.severity = 3;
  f.ed->setDiagnostics(f.doc->path(), {e, w, n});
  f.tv->setFindMatches({{{16, 6}, {16, 9}}, {{16, 24}, {16, 27}}}, 0);
  f.ed->moveCursor({17, 8});
  f.ed->setSelections({Selection{{11, 2}, {11, 9}}});  // selection head = current line (12)
  f.tick();
  f.text->releaseTexture(f.rec);
  gfx::GlRenderer& r = *gl->renderer;
  r.setOffscreen(true);
  f.root->requestLayout();
  f.root->render(r, f.w, f.h);
  std::vector<uint8_t> px(static_cast<size_t>(f.w) * f.h * 4);
  REQUIRE(r.readPixels(f.w, f.h, px.data()));
  f.text->releaseTexture(r);
  compareGolden("vs_dark_editor", px, f.w, f.h);
}

// ---- PERF ----

namespace {
std::string bigCpp(bool blockComment) {
  std::string s;
  s.reserve(100000 * 70);
  for (int i = 0; i < 100000; ++i) {
    if (blockComment && i == 100) s += "/* a long comment opened here\n";
    else if (blockComment && i == 99900) s += "end of it */ int closed = 0;\n";
    else if (i % 50 == 0)
      s += "// section " + std::to_string(i) + ": \"quoted\" text and 0x" + std::to_string(i) + "\n";
    else
      s += "    for (int k" + std::to_string(i) + " = 0; k < n_" + std::to_string(i % 97) +
           "; ++k) { total += static_cast<double>(k) * 1.5;" + std::string(blockComment ? " }\n" : " /* c */ }\n");
  }
  return s;
}
}  // namespace

TEST(perf_colouring_keystroke_to_paint_and_cold_first_paint_100k_lines) {
  const std::string font = dejavuMono();
  std::string err;
  auto tr0 = gfx::TextRenderer::create(font, 15.0f, 1.0f, &err);
  if (!tr0) {
    std::cerr << "    SKIP: " << err << "\n";
    return;
  }
  using clk = std::chrono::steady_clock;
  auto ms = [](clk::time_point a, clk::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
  };
  const std::string plain = bigCpp(false);
  // Keystroke-to-paint: same loop as the B2a PERF test, plain text vs colouring.
  double avg[2] = {0, 0}, p99[2] = {0, 0};
  for (int colour = 0; colour < 2; ++colour) {
    Fx2 f(plain, colour ? "a.cpp" : "a.txt", 1920, 1080, gfx::TextRenderer::create(font, 15.0f, 1.0f, &err), true,
          true);  // word wrap on, the default
    InputEvent ev;
    ev.type = InputType::Text;
    ev.text = "a";
    for (int i = 0; i < 20; ++i) {
      f.root->dispatch(ev);
      f.frame();
    }
    std::vector<double> total;
    for (int i = 0; i < 400; ++i) {
      auto t0 = clk::now();
      f.root->dispatch(ev);
      f.root->requestLayout();
      f.frame();
      total.push_back(ms(t0, clk::now()));
    }
    std::sort(total.begin(), total.end());
    for (double v : total) avg[colour] += v / static_cast<double>(total.size());
    p99[colour] = total[static_cast<size_t>(total.size() * 0.99)];
  }
  std::printf(
      "PERF: colouring keystroke-to-paint, 100k lines @1920x1080: plain avg %.3f ms / p99 %.3f ms, coloured avg %.3f "
      "ms / p99 %.3f ms (budget 8 ms)\n",
      avg[0], p99[0], avg[1], p99[1]);
#if defined(NDEBUG) && !defined(__SANITIZE_ADDRESS__)
  CHECK(avg[1] < 8.0);
#endif

  // Cold first paint: nothing lexed yet. At the top only the visible lines are lexed; far down the exact pass is
  // too far behind, so the visible lines are lexed provisionally and finished in the background.
  const std::string commented = bigCpp(true);
  Fx2 f(commented, "a.cpp", 1920, 1080, std::move(tr0), false, true);
  auto t0 = clk::now();
  f.frame();
  const double top = ms(t0, clk::now());
  CHECK(f.tv->highlighter().exactLines() < 200);  // never lexed the file for the first paint
  f.tick();                                       // the first sync with the editor (resets the scroll position)
  const float lh = f.text->lineHeight(), cw = f.text->cellWidth(), far0 = 90000 * lh;
  f.tv->viewport().jumpTo(0, far0);
  f.root->requestLayout();
  t0 = clk::now();
  f.frame();
  const double far = ms(t0, clk::now());
  CHECK(f.tv->highlighter().pending());
  // Colour of the first glyph after the 4-space indent of a line (gutter: 6 digits + 2 cells, text +half a cell).
  auto colourOfLine = [&](int line) {
    const float y = static_cast<float>(line) * lh - far0, x0 = 8.5f * cw + 4 * cw - 1;
    const Rec2::Q* best = nullptr;
    for (const Rec2::Q& q : f.rec.quads)
      if (q.y >= y && q.y < y + lh && q.x >= x0 && (!best || q.x < best->x)) best = &q;
    return best ? best->c : gfx::Color{0, 0, 0, 0};
  };
  // Provisional: the block comment opened on line 100 is not known yet, so `for` shows as a keyword.
  CHECK(same(colourOfLine(90010), f.th.tokenColor("keyword")));
  int ticks = 0;
  t0 = clk::now();
  while (f.tv->highlighter().pending() && ticks < 1000) {
    f.tick();
    ++ticks;
  }
  const double finish = ms(t0, clk::now());
  f.frame();
  CHECK(!f.tv->highlighter().pending());
  CHECK(f.tv->highlighter().exactLines() > 90000);
  CHECK(same(colourOfLine(90010), f.th.tokenColor("comment")));  // inside the comment from line 100: now exact
  std::printf(
      "PERF: cold first paint of a 100k-line C++ file @1920x1080 (glyph atlas cold too): top %.3f ms; scrolled to line "
      "90000 (provisional) %.3f ms; exact pass finished in the background in %d ticks, %.1f ms CPU total\n",
      top, far, ticks, finish);
#if defined(NDEBUG) && !defined(__SANITIZE_ADDRESS__)
  CHECK(top < 50.0 && far < 50.0);
#endif
}
