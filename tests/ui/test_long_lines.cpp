// W11 perf: very long lines. The column anchors (LineAnchors), the windowed lexing of the Highlighter (resume points,
// re-joining after an edit) and the TextView paths that use them must give exactly the answers a full scan gives:
// caret positions, hit testing, horizontal scroll, colours and the bracket match, after edits at the start, in the
// middle and at the end of a line of hundreds of KB, with wide characters, combining marks, tabs, control characters
// and invalid UTF-8. The costs are measured by test_pathological_ui.cpp.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>

#include "../text/text_test_util.h"
#include "app/Editor.h"
#include "ui/gfx/TextLayout.h"
#include "ui/gfx/TextRenderer.h"
#include "ui/shell/EditorApp.h"
#include "ui/view/Highlighter.h"
#include "ui/view/LineAnchors.h"
#include "ui/widgets/TextView.h"
#include "ui_test.h"

using namespace tf;
using namespace tf::ui;
namespace fs = std::filesystem;

namespace {

// A deterministic line of about `n` bytes mixing every kind of cell: words, spaces, tabs, CJK (3 bytes, 2 cells), an
// emoji (4 bytes, 2 cells), combining marks (0 cells), control characters (2 cells) and invalid UTF-8.
std::string mixedLine(size_t n, unsigned seed = 7) {
  std::mt19937 rng(seed);
  static const char* kPieces[] = {
      "word", " ", "  ", "\t",   "\xe4\xb8\xad", "\xe6\x96\x87", "e\xcc\x81",         "\xf0\x9f\x98\x80",
      "x",    "(", ")",  "\x01", "\xff",         "\xe4\xb8",     "a\xcc\x81\xcc\x82", "12"};
  std::string s;
  while (s.size() < n) s += kPieces[rng() % std::size(kPieces)];
  return s;
}

struct TlFix {
  gfx::FontStack stack;
  gfx::Atlas atlas;
  std::unique_ptr<gfx::TextLayout> tl;
  TlFix() {
    stack.add(std::make_shared<tftext::MockFont>(U"abcdefghijklmnopqrstuvwxyz0123456789 ._-:,;(){}[]=+*/<>\"'"));
    tl = std::make_unique<gfx::TextLayout>(stack, atlas, 4);
  }
};

bool sameColor(gfx::Color a, gfx::Color b) { return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a; }

}  // namespace

TEST(long_lines_text_layout_anchors_give_the_full_scan_answers) {
  TlFix f;
  gfx::TextLayout& tl = *f.tl;
  const std::string line = mixedLine(120000);
  std::vector<gfx::Anchor> anchors{{}};
  tl.collectAnchors(line, {}, line.size(), 1000, anchors);
  CHECK(anchors.size() > 100);
  for (size_t i = 1; i < anchors.size(); ++i) {
    CHECK(anchors[i].byte >= anchors[i - 1].byte + 1000);
    CHECK_EQ(anchors[i].col, tl.columnForByte(line, anchors[i].byte));  // an anchor is a cell boundary at its column
    CHECK_EQ(tl.byteForColumn(line, anchors[i].col), anchors[i].byte);
  }
  std::mt19937 rng(1);
  auto below = [&](size_t byte) {  // the largest anchor at or before a byte
    gfx::Anchor a;
    for (const gfx::Anchor& x : anchors)
      if (x.byte <= byte) a = x;
    return a;
  };
  const int total = tl.columnCount(line);
  for (int k = 0; k < 300; ++k) {
    const size_t b = rng() % (line.size() + 1);
    CHECK_EQ(tl.columnForByte(line, b, below(b)), tl.columnForByte(line, b));
    CHECK_EQ(tl.xForByte(line, b, below(b)), tl.xForByte(line, b));
    const int col = static_cast<int>(rng() % static_cast<unsigned>(total + 3));
    gfx::Anchor a;
    for (const gfx::Anchor& x : anchors)
      if (x.col <= col) a = x;
    CHECK_EQ(tl.byteForColumn(line, col, a), tl.byteForColumn(line, col));
    const float x = (static_cast<float>(col) + static_cast<float>(rng() % 100) / 100.0f) * tl.cellWidth();
    CHECK_EQ(tl.byteForX(line, x, a), tl.byteForX(line, x));
  }
  CHECK_EQ(tl.columnCount(line, anchors.back()), total);
  // An anchor past the answer is ignored.
  CHECK_EQ(tl.columnForByte(line, 500, anchors.back()), tl.columnForByte(line, 500));
  // Drawing from an anchor emits the same glyphs as drawing from the start.
  const float vis = 80 * tl.cellWidth();
  for (size_t ai : {size_t{3}, anchors.size() / 2, anchors.size() - 2}) {
    const gfx::Anchor a = anchors[ai];
    const float scroll = static_cast<float>(a.col) * tl.cellWidth() + 3;
    gfx::LineLayout full, part;
    tl.layoutLine(line, 10, 30, scroll, vis, {}, 0, full);
    tl.layoutLine(line, 10, 30, scroll, vis, {}, 0, part, a);
    REQUIRE(full.quads.size() == part.quads.size());
    CHECK(!full.quads.empty());
    for (size_t i = 0; i < full.quads.size(); ++i) CHECK(full.quads[i].x0 == part.quads[i].x0);
  }
}

namespace {
// A document whose changes feed a LineAnchors like TextView feeds its WrapLayout.
struct AnchorFix {
  TlFix fx;
  Document doc;
  LineAnchors la;
  explicit AnchorFix(std::string text) : doc(Document::fromText("a.txt", std::move(text))) {
    la.bind(&doc.buffer(), fx.tl.get());
    doc.addListener([this](Document&, const Document::Change& c) { la.onChange(c); });
  }
  // Every cached answer of every line equals the full scan.
  void verify(int samples = 40) {
    const TextBuffer& b = doc.buffer();
    std::mt19937 rng(static_cast<unsigned>(b.lineCount()) * 31u + 5u);
    for (int l = 0; l < b.lineCount(); ++l) {
      const std::string& s = b.line(l);
      for (int k = 0; k < samples; ++k) {
        const size_t byte = rng() % (s.size() + 1);
        const gfx::Anchor a = la.atByte(l, byte);
        CHECK(a.byte <= byte);
        CHECK_EQ(fx.tl->columnForByte(s, a.byte), a.col);
        CHECK_EQ(fx.tl->columnForByte(s, byte, a), fx.tl->columnForByte(s, byte));
        const int col = static_cast<int>(rng() % (static_cast<unsigned>(fx.tl->columnCount(s)) + 2));
        const gfx::Anchor c = la.atColumn(l, col);
        CHECK(c.col <= col || col < 0);
        CHECK_EQ(fx.tl->byteForColumn(s, col, c), fx.tl->byteForColumn(s, col));
      }
      CHECK_EQ(la.columnCount(l), fx.tl->columnCount(s));
    }
  }
};
}  // namespace

TEST(long_lines_anchors_stay_exact_through_edits) {
  const std::string a = mixedLine(150000, 1), c = mixedLine(60000, 3);
  AnchorFix f("short line\twith tab\n" + a + "\n" + "tail \xe4\xb8\xad\n" + c);
  f.verify();
  CHECK(f.la.cachedLines() >= 2);
  const int big = 1;
  auto len = [&](int l) { return static_cast<int>(f.doc.buffer().line(l).size()); };
  // At the end, the start and in the middle; a combining mark glued right behind a base character; a tab; a wide
  // character; deleting across anchors; replacing across anchors.
  f.doc.insert({big, len(big)}, "tail");
  f.verify();
  f.doc.insert({big, 0}, "\t\xe4\xb8\xad");
  f.verify();
  f.doc.insert({big, 70000}, "x");
  f.verify();
  f.doc.insert({big, 70001}, "\xcc\x81\xcc\x82");  // marks join the character before them
  f.verify();
  for (int at : {1000, 1001, 2047, 2048, 5000, 99999}) {  // around anchor positions
    f.doc.insert({big, at}, "\t");
    f.verify(8);
    f.doc.insert({big, at}, "\xe6\x96\x87");
    f.verify(8);
  }
  f.doc.erase({{big, 3000}, {big, 90000}});
  f.verify();
  f.doc.replace({{big, 100}, {big, 40000}}, "\xe4\xb8\xad\t\t\xe4\xb8\xad");
  f.verify();
  // Splitting the line, joining lines, a multi-line paste and its removal.
  f.doc.insert({big, 20000}, "\n");
  f.verify();
  CHECK(f.doc.buffer().lineCount() == 5);
  f.doc.erase({{big, len(big)}, {big + 1, 0}});  // join again
  f.verify();
  f.doc.insert({0, 3}, "one\ntwo\n" + mixedLine(5000, 9) + "\nthree");
  f.verify();
  f.doc.erase({{0, 0}, {f.doc.buffer().lineCount() - 1, 2}});
  f.verify();
  // An edit that makes a long line short forgets it; one that makes a short line long starts a cache.
  AnchorFix g("abc\n" + mixedLine(3000, 2) + "\nzz");
  g.verify();
  g.doc.erase({{1, 10}, {1, static_cast<int>(g.doc.buffer().line(1).size())}});
  g.verify();
  g.doc.insert({0, 3}, mixedLine(5000, 4));
  g.verify();
  CHECK(g.la.anchorsOf(0) != nullptr);
}

TEST(long_lines_anchors_are_kept_for_edits_after_them) {
  AnchorFix f(mixedLine(200000, 11));
  const int cols = f.la.columnCount(0);
  CHECK(cols > 0);
  const size_t before = f.la.anchorsOf(0)->size();
  CHECK(before > 100);
  f.doc.insert({0, static_cast<int>(f.doc.buffer().line(0).size())}, "z");  // typing at the end
  CHECK(f.la.anchorsOf(0)->size() + 1 >= before);                           // nothing but the last one is gone
  f.doc.insert({0, 10}, "z");                                               // typing at the start
  CHECK(f.la.anchorsOf(0)->size() == 1);
  f.verify();
}

namespace {

// Per-byte colours of [b, e) of `line` as the highlighter reports them, against a full lexLine of the line.
struct HlFix {
  Document doc;
  Highlighter hl;
  Theme theme;
  explicit HlFix(std::string text) : doc(Document::fromText("a.cpp", std::move(text))) {
    hl.attach(&doc.buffer());
    hl.setTheme(theme);
    doc.addListener([this](Document&, const Document::Change& c) { hl.onChange(c); });
    hl.prepare(0, doc.buffer().lineCount());
  }
  gfx::Color kindColor(Lex k) const {
    static const char* names[10] = {"keyword", "identifier",   "number",   "string",      "char",
                                    "comment", "preprocessor", "operator", "punctuation", "controlKeyword"};
    return theme.tokenColor(names[static_cast<int>(k)]);
  }
  // Compares every line's colours in the windows with a from-scratch lex of the whole document.
  void verify(const std::vector<std::pair<double, double>>& windows = {{0, 1}, {0.4, 0.45}, {0.9, 1}}) {
    hl.prepare(0, doc.buffer().lineCount());
    LexState st;
    for (int l = 0; l < doc.buffer().lineCount(); ++l) {
      const std::string& s = doc.buffer().line(l);
      std::vector<LexSpan> full;
      st = lexLine(s, st, full);
      std::vector<gfx::Color> want(s.size(), theme.text);
      for (const LexSpan& x : full)
        for (uint32_t i = x.begin; i < x.end; ++i) want[i] = kindColor(x.kind);
      for (const auto& w : windows) {
        const size_t b = static_cast<size_t>(w.first * static_cast<double>(s.size())),
                     e = static_cast<size_t>(w.second * static_cast<double>(s.size()));
        const auto& spans = hl.colorSpans(l, b, e);
        std::vector<gfx::Color> got(s.size(), hl.palette()[hl.defaultColor()]);
        for (const gfx::ColorSpan& x : spans)
          for (uint32_t i = x.begin; i < x.end && i < s.size(); ++i) got[i] = hl.palette()[x.color];
        for (size_t i = b; i < e; ++i)
          if (!sameColor(want[i], got[i])) {
            CHECK(sameColor(want[i], got[i]));
            std::printf("    line %d byte %zu of %zu (window %zu-%zu): colours differ\n", l, i, s.size(), b, e);
            return;
          }
      }
    }
  }
};

std::string expr(size_t terms) {
  std::string s = "int e = ";
  for (size_t i = 0; i < terms; ++i) s += "f(a" + std::to_string(i) + ", \"s\") + 0x1F + 'c' /*c*/ + ";
  return s + "1;";
}

}  // namespace

TEST(long_lines_highlighter_windows_match_a_full_lex_through_edits) {
  const std::string big = expr(6000);  // about 190 KB
  HlFix f("#define X 1\n" + big + "\nint after = 2; /* c */ int y;\n");
  CHECK(f.hl.isLong(1));
  CHECK(!f.hl.isLong(0));
  f.verify();
  const int len = static_cast<int>(f.doc.buffer().line(1).size());
  // Typing at the end, in the middle and at the start, before the cached tokens and inside them.
  const uint64_t lexed0 = f.hl.longLineBytesLexed();
  f.doc.insert({1, len}, " + z");
  f.hl.prepare(0, 3);
  CHECK(f.hl.longLineBytesLexed() - lexed0 < 4 * Highlighter::kResumeStride);  // only the end of the line was lexed
  f.verify();
  const uint64_t lexed1 = f.hl.longLineBytesLexed();
  f.doc.insert({1, len / 2}, "abc");
  f.hl.prepare(0, 3);
  CHECK(f.hl.longLineBytesLexed() - lexed1 < 4 * Highlighter::kResumeStride);  // re-joined at the next resume point
  f.verify();
  f.doc.insert({1, 0}, "  ");
  f.verify();
  f.doc.insert({1, 20}, "9");
  f.verify();
  f.doc.erase({{1, 100}, {1, 5000}});
  f.verify();
  // Constructs that change everything behind them, then undo that.
  f.doc.insert({1, 3000}, "\"");  // an unterminated string: the rest of the line is its text
  f.verify();
  f.doc.insert({1, 90000}, "\"");  // closes it again
  f.verify();
  f.doc.insert({1, 40000}, "/*");  // an unterminated comment runs into the next lines
  f.verify();
  f.doc.insert({2, 5}, "*/");
  f.verify();
  f.doc.erase({{1, 40000}, {1, 40002}});
  f.verify();
  f.doc.insert({1, 0}, "//");  // everything is a comment (and a trailing backslash continues it)
  f.verify();
  f.doc.insert({1, static_cast<int>(f.doc.buffer().line(1).size())}, "\\");
  f.verify();
  f.doc.erase({{1, 0}, {1, 2}});
  f.doc.erase(
      {{1, static_cast<int>(f.doc.buffer().line(1).size()) - 1}, {1, static_cast<int>(f.doc.buffer().line(1).size())}});
  f.verify();
  f.doc.insert({1, 0}, "R\"x(");  // a raw string opened at the start
  f.verify();
  f.doc.insert({2, 0}, ")x\" ");
  f.verify();
  // Splitting and joining a long line.
  f.doc.insert({1, 50000}, "\n");
  f.verify();
  f.doc.erase({{1, static_cast<int>(f.doc.buffer().line(1).size())}, {2, 0}});
  f.verify();
  f.doc.replace({{0, 0}, {3, 0}}, big + "\n" + big + "\n");
  f.verify();
}

TEST(long_lines_highlighter_resumes_inside_long_string_literals) {
  std::string s = "const char* s = \"";
  for (int i = 0; i < 40000; ++i) s += (i % 7 == 3) ? "a\\\"b" : (i % 11 == 5 ? "\\\\" : "xy");
  s += "\"; int after = 1;";
  HlFix f("int a;\n" + s + "\nint b;\n");
  CHECK(f.hl.isLong(1));
  size_t inLiterals = 0;
  CHECK(f.hl.resumePoints(1, &inLiterals) > 20);
  CHECK(inLiterals > 20);  // the literal is resumable from the inside
  f.verify({{0, 1}, {0.3, 0.31}, {0.5, 0.5001}, {0.99, 1}});
  const uint64_t lexed = f.hl.longLineBytesLexed();
  f.doc.insert({1, static_cast<int>(f.doc.buffer().line(1).size()) - 3}, " ");  // after the literal
  f.hl.prepare(0, 3);
  CHECK(f.hl.longLineBytesLexed() - lexed < 8 * Highlighter::kResumeStride);
  f.doc.insert({1, 50000}, "q");  // inside the literal
  f.verify({{0.3, 0.31}, {0.5, 0.5001}, {0.99, 1}});
  f.doc.insert({1, 50000}, "\\");  // an escape that swallows the next character
  f.verify({{0.3, 0.31}, {0.5, 0.5001}, {0.99, 1}});
  f.doc.insert({1, 60000}, "\"");  // ends the literal early; the text after it is code
  f.verify({{0.3, 0.31}, {0.5, 0.5001}, {0.99, 1}});
}

TEST(long_lines_highlighter_bracket_search_in_windows) {
  HlFix f("{" + std::string(30000, ' ') + "(" + std::string(20000, 'a') + ")" + std::string(30000, 'b') + "}\n");
  std::vector<LexSpan> out;
  CHECK(f.hl.punctuation(0, 0, 100000, out, true));
  CHECK_EQ(out.size(), 4u);
  out.clear();
  CHECK(f.hl.punctuation(0, 30001, 30002, out, true));
  CHECK_EQ(out.size(), 1u);
  CHECK_EQ(out[0].begin, 30001u);
  out.clear();
  CHECK(f.hl.punctuation(0, 2, 30000, out, true));
  CHECK(out.empty());
}

// ---- TextView on long lines ----

namespace {

struct CountingRenderer : gfx::Renderer {
  size_t quads = 0;
  void beginFrame(int, int, gfx::Color) override { quads = 0; }
  void drawRect(const gfx::RectF&, gfx::Color) override {}
  void drawLine(float, float, float, float, float, gfx::Color) override {}
  void drawGlyphs(gfx::TextureId, const gfx::GlyphQuad*, int n, gfx::Color) override {
    quads += static_cast<size_t>(n);
  }
  void pushScissor(const gfx::RectF&) override {}
  void popScissor() override {}
  gfx::TextureId createTexture(int, int, gfx::TextureFormat) override { return 1; }
  void updateTexture(gfx::TextureId, int, int, int, int, const void*, int) override {}
  void destroyTexture(gfx::TextureId) override {}
  void endFrame() override {}
  bool readPixels(int, int, uint8_t*) override { return false; }
};

InputEvent mouseAt(InputType t, float x, float y) {
  InputEvent e;
  e.type = t;
  e.x = x;
  e.y = y;
  e.clicks = 1;
  e.buttons = t == InputType::MouseUp ? 0 : 1;
  return e;
}

struct LFx {
  fs::path dir;
  std::unique_ptr<Editor> ed;
  Document* doc = nullptr;
  std::unique_ptr<gfx::TextRenderer> text;
  std::unique_ptr<RootWidget> root;
  std::unique_ptr<EditorApp> app;
  TextView* tv = nullptr;
  CountingRenderer rec;
  int w = 640, h = 280;
  int64_t now = 1000;

  LFx(const std::string& content, bool wrap, const char* name = "long.cpp") {
    static int counter = 0;
    dir = uitest::tempRoot() /
          ("tenfold_long_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_" +
           std::to_string(counter++));
    fs::create_directories(dir);
    std::ofstream(dir / name, std::ios::binary) << content;
    ed = std::make_unique<Editor>(dir);
    doc = ed->open(name);
    ed->gotoLocation({doc->path(), {0, 0}});
    gfx::FontStack stack;
    stack.add(std::make_shared<tftext::MockFont>(
        U"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ._-:,;(){}[]=+*/<>\"'\\#"));
    text = gfx::TextRenderer::fromFonts(std::move(stack));
    root = std::make_unique<RootWidget>(*text);
    auto v = std::make_unique<TextView>(*ed);
    tv = v.get();
    tv->settings().smoothScroll = false;
    tv->settings().caretBlink = false;
    tv->settings().showFoldGlyphs = false;
    tv->settings().wordWrap = wrap;
    root->addChild(std::move(v));
    tv->setFocus();
    app = std::make_unique<EditorApp>(*root, *ed);
    frame();
  }
  ~LFx() {
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
  void type(const char* s) {
    InputEvent e;
    e.type = InputType::Text;
    e.text = s;
    root->dispatch(e);
    root->requestLayout();
    frame();
  }
  Position head() const { return ed->selections()[0].head; }
  const gfx::TextLayout& tl() const { return text->textLayout(); }
};

// The view's answers for a caret position against a plain scan of the text (wrap off).
void checkGeometry(LFx& f, Position p) {
  const std::string& s = f.doc->buffer().line(p.line);
  const gfx::RectF r = f.tv->caretRect(p);
  const float cw = f.tl().cellWidth();
  const float origin = f.tv->textArea().x + cw / 2;  // the text starts half a cell right of the gutter
  const float want =
      origin - f.tv->viewport().x() + static_cast<float>(f.tl().columnForByte(s, static_cast<size_t>(p.column))) * cw;
  CHECK(std::fabs(r.x - want) < 0.01f);
  if (r.x >= f.tv->textArea().x && r.x + cw < f.tv->textArea().x + f.tv->textArea().w) {
    // Hit testing the middle of the caret's cell comes back to the caret position (cells start at their caret).
    const Position q = f.tv->positionAt(r.x + 1, r.y + 2);
    CHECK(q.line == p.line);
    const float lx = r.x + 1 - origin + f.tv->viewport().x();
    CHECK_EQ(static_cast<size_t>(q.column), f.tl().byteForX(s, lx));
  }
}

}  // namespace

TEST(long_lines_view_geometry_matches_a_plain_scan_wrap_off) {
  const std::string text = "head\n" + mixedLine(150000, 21) + "\ntail\n";
  LFx f(text, false);
  CHECK(!f.tv->wordWrapActive());
  f.key(Key::Down);
  f.key(Key::End);
  CHECK_EQ(f.head().line, 1);
  CHECK_EQ(static_cast<size_t>(f.head().column), f.doc->buffer().line(1).size());
  CHECK(f.tv->viewport().x() > 1000);  // scrolled to the caret
  const gfx::RectF cr = f.tv->caretRect(f.head());
  CHECK(cr.x >= f.tv->textArea().x && cr.x < f.tv->textArea().x + f.tv->textArea().w);  // and it is visible
  checkGeometry(f, f.head());
  // Walk back with word jumps and arrow keys, over wide characters, marks and tabs; the view follows.
  for (int i = 0; i < 40; ++i) {
    f.key(Key::Left, i % 3 == 0 ? ModCtrl : 0);
    checkGeometry(f, f.head());
  }
  // Typing at the caret, in the middle of the line, at its start; undo.
  f.type("q\t\xe4\xb8\xad");
  checkGeometry(f, f.head());
  f.key(Key::Home);
  CHECK_EQ(f.head().column, 0);
  CHECK(f.tv->viewport().x() == 0);
  f.type("\xcc\x81 Z");
  checkGeometry(f, f.head());
  for (int i = 0; i < 20; ++i) f.key(Key::Right);
  checkGeometry(f, f.head());
  f.key(Key('Z'), ModCtrl);
  f.key(Key('Z'), ModCtrl);
  checkGeometry(f, f.head());
  // Mouse: click at several x positions, the caret lands where a plain scan says.
  f.key(Key::Home);
  for (float x : {60.0f, 200.0f, 333.0f, 480.0f}) {
    const gfx::RectF a = f.tv->textArea();
    const float y = a.y + f.tv->caretRect({1, 0}).h * 1.5f;
    f.root->dispatch(mouseAt(InputType::MouseDown, a.x + x, y));
    f.root->dispatch(mouseAt(InputType::MouseUp, a.x + x, y));
    const std::string& s = f.doc->buffer().line(f.head().line);
    if (f.head().line == 1)
      CHECK_EQ(static_cast<size_t>(f.head().column),
               f.tl().byteForX(s, x - f.tl().cellWidth() / 2 + f.tv->viewport().x()));
  }
  // Selecting to the end of the line (Shift+End) paints and keeps the caret exact.
  f.key(Key::End, ModShift);
  checkGeometry(f, f.head());
  CHECK(f.rec.quads > 0);
  // Ctrl+End, then a wide character typed at the very end of the document's last line.
  f.key(Key::End, ModCtrl);
  f.type("\xe4\xb8\xad");
  CHECK(f.rec.quads > 0);
}

TEST(long_lines_view_geometry_matches_a_plain_scan_wrap_on) {
  const std::string text = mixedLine(60000, 5) + "\nsecond line\n";
  LFx f(text, true);
  CHECK(f.tv->wordWrapActive());
  f.key(Key::End, ModCtrl);
  f.key(Key::Up, ModCtrl);  // scroll up; the caret stays
  f.key(Key::Home, ModCtrl);
  f.key(Key::End);
  WrapLayout& W = f.tv->wrapLayout();
  // Row geometry from the wrap map equals a plain scan of the row's text.
  for (int row = 0; row < 40; ++row) {
    const int a = W.rowStart(0, row), z = W.rowEnd(0, row);
    const std::string_view v(f.doc->buffer().line(0).data() + a, static_cast<size_t>(z - a));
    for (int off : {0, static_cast<int>(v.size()) / 2, static_cast<int>(v.size())})
      CHECK_EQ(W.xOf(0, row, a + off), static_cast<float>(W.rowIndent(0, row)) * f.tl().cellWidth() +
                                           f.tl().xForByte(v, static_cast<size_t>(off)));
  }
  f.type("\xe4\xb8\xad\t");
  f.key(Key::Home, ModCtrl);
  f.type("\xcc\x81x");
  CHECK(f.rec.quads > 0);
  for (int row = 0; row < 10; ++row) {
    const Position p = W.positionIn(0, row, 5 * f.tl().cellWidth());
    CHECK(p.line == 0 && p.column >= W.rowStart(0, row) && p.column <= W.rowEnd(0, row));
  }
}

TEST(long_lines_view_bracket_match_on_a_long_line) {
  // Partner 30 KB away: found; 200 KB away: not searched (the search of a long line is capped at 64 KB).
  const std::string near = "int f() { " + std::string(30000, ' ') + "return 0; }\n";
  LFx f(near, false);
  f.key(Key::End);
  f.key(Key::Left);  // the caret before the closing brace
  auto m = f.tv->bracketMatch();
  REQUIRE(m.has_value());
  CHECK_EQ(m->first.column, static_cast<int>(near.size()) - 2);
  CHECK_EQ(m->second.column, 8);
  const std::string far = "int f() { " + std::string(200000, ' ') + "return 0; }\n";
  LFx g(far, false);
  g.key(Key::End);
  g.key(Key::Left);
  CHECK(!g.tv->bracketMatch().has_value());
  // In a string it is no bracket at all.
  LFx h("const char* s = \"" + std::string(3000, 'a') + "(" + std::string(3000, 'b') + ")\";\n", false);
  h.key(Key::End);
  for (int i = 0; i < 5; ++i) h.key(Key::Left);
  CHECK(!h.tv->bracketMatch().has_value());
}
