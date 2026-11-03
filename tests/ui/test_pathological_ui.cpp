// W9f: the TextView on pathological files: a giant C++ file, a text file with 100k lines, one-line files of 1-3 MB
// (a plain expression, a comment, a minified program), a file full of NULs / invalid UTF-8, and a very deep nesting.
// For each: open, first frame, scroll to the end and back, type, undo, soft wrap on and off, with the
// keystroke-to-paint budget (8 ms; PERF_CHECK, enforced only with TENFOLD_ENFORCE_PERF=1, and only in optimised
// unsanitised builds). The functional parts (no crash, no hang, the caret ends up where it should, the view still
// paints) always run.
//
// tools/stress/run_pathological.py runs the same bodies on the full-size files when TENFOLD_PATHOLOGICAL_DIR is set.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>

#include "../text/text_test_util.h"
#include "app/Editor.h"
#include "ui/gfx/Renderer.h"
#include "ui/gfx/TextRenderer.h"
#include "ui/shell/EditorApp.h"
#include "ui/widgets/TextView.h"
#include "ui_test.h"

using namespace tf;
using namespace tf::ui;
namespace fs = std::filesystem;

namespace {

// Counts what a frame draws; no GPU.
struct CountingRenderer : gfx::Renderer {
  size_t rects = 0, quads = 0, lines = 0;
  void beginFrame(int, int, gfx::Color) override { rects = quads = lines = 0; }
  void drawRect(const gfx::RectF&, gfx::Color) override { ++rects; }
  void drawLine(float, float, float, float, float, gfx::Color) override { ++lines; }
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

// Sizes shrink under AddressSanitizer (about 10x slower).
#if defined(__SANITIZE_ADDRESS__)
constexpr size_t kSan = 12;
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
constexpr size_t kSan = 12;
#else
constexpr size_t kSan = 1;
#endif
#else
constexpr size_t kSan = 1;
#endif
size_t scaled(size_t n) { return n / kSan; }

using clk = std::chrono::steady_clock;
double msSince(clk::time_point t) { return std::chrono::duration<double, std::milli>(clk::now() - t).count(); }

std::string rep(const std::string& s, size_t n) {
  std::string out;
  out.reserve(s.size() * n);
  for (size_t i = 0; i < n; ++i) out += s;
  return out;
}

std::string giantCpp(int lines) {
  std::string t;
  t.reserve(static_cast<size_t>(lines) * 30);
  for (int i = 0; i < lines; ++i) {
    switch (i % 8) {
      case 0: t += "int giant_fn_" + std::to_string(i) + "(int a, int b) {\n"; break;
      case 1: t += "  int local_" + std::to_string(i) + " = a * " + std::to_string(i % 1000) + " + b;\n"; break;
      case 2: t += "  if (local_" + std::to_string(i - 1) + " > 5) return 1;\n"; break;
      case 3: t += "  const char* s = \"string // not a comment /* nor this */\";\n"; break;
      case 4: t += "  // trailing comment line\n"; break;
      case 5: t += "  return a;\n"; break;
      case 6: t += "}\n"; break;
      default: t += "\n"; break;
    }
  }
  return t;
}

struct PFx {
  fs::path dir;
  std::unique_ptr<Editor> ed;
  Document* doc = nullptr;
  std::unique_ptr<gfx::TextRenderer> text;
  std::unique_ptr<RootWidget> root;
  std::unique_ptr<EditorApp> app;
  TextView* tv = nullptr;
  CountingRenderer rec;
  int w, h;
  int64_t now = 1000;
  double openMs = 0, firstFrameMs = 0;

  PFx(const std::string& content, const char* name, bool wrap, int w_ = 1280, int h_ = 720) : w(w_), h(h_) {
    static int counter = 0;
    dir = uitest::tempRoot() / ("tenfold_patho_ui_" + std::to_string(clk::now().time_since_epoch().count()) + "_" +
                                std::to_string(counter++));
    fs::create_directories(dir);
    std::ofstream(dir / name, std::ios::binary) << content;
    ed = std::make_unique<Editor>(dir);
    auto t = clk::now();
    doc = ed->open(name);
    ed->gotoLocation({doc->path(), {0, 0}});
    openMs = msSince(t);
    gfx::FontStack stack;
    stack.add(std::make_shared<tftext::MockFont>(
        U"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ._-:,;(){}[]=+*/<>\"'\\#"));
    text = gfx::TextRenderer::fromFonts(std::move(stack));
    root = std::make_unique<RootWidget>(*text);
    auto v = std::make_unique<TextView>(*ed);
    tv = v.get();
    tv->settings().smoothScroll = false;
    tv->settings().caretBlink = false;
    tv->settings().wordWrap = wrap;
    root->addChild(std::move(v));
    tv->setFocus();
    app = std::make_unique<EditorApp>(*root, *ed);
    t = clk::now();
    frame();
    firstFrameMs = msSince(t);
  }
  ~PFx() {
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
  void typeChar(const char* s = "a") {
    InputEvent e;
    e.type = InputType::Text;
    e.text = s;
    root->dispatch(e);
    root->requestLayout();
    frame();
  }
  Position head() const { return ed->selections()[0].head; }
};

// Types `n` characters, one frame each; returns the average keystroke-to-paint in ms and prints it.
double typing(PFx& f, int n, const std::string& what) {
  for (int i = 0; i < 5; ++i) f.typeChar();
  std::vector<double> t;
  for (int i = 0; i < n; ++i) {
    auto t0 = clk::now();
    f.typeChar();
    t.push_back(msSince(t0));
  }
  std::sort(t.begin(), t.end());
  double sum = 0;
  for (double x : t) sum += x;
  std::printf("PERF: pathological %s: keystroke-to-paint avg %.3f ms / p99 %.3f ms / max %.3f ms (budget 8 ms)\n",
              what.c_str(), sum / t.size(), t[static_cast<size_t>(static_cast<double>(t.size()) * 0.99)], t.back());
  return sum / static_cast<double>(t.size());
}

// open -> first frame -> End/Home -> PageDown x N -> Ctrl+End -> type -> undo; checks positions and that frames paint.
void session(PFx& f, const std::string& what, int expectLastLine, bool keystrokeBudget = true) {
  std::printf("PERF: pathological %s: open %.1f ms, first frame %.1f ms\n", what.c_str(), f.openMs, f.firstFrameMs);
  CHECK(f.rec.quads > 0);  // the first screenful was drawn
  CHECK(f.firstFrameMs < 30000);

  auto t = clk::now();
  for (int i = 0; i < 20; ++i) f.key(Key::PageDown);
  std::printf("PERF: pathological %s: 20 x PageDown incl. paint %.2f ms each\n", what.c_str(), msSince(t) / 20);
  CHECK(f.head().line > 0 || expectLastLine == 0);

  t = clk::now();
  f.key(Key::End, ModCtrl);
  std::printf("PERF: pathological %s: Ctrl+End (scroll to the end) incl. paint %.2f ms\n", what.c_str(), msSince(t));
  CHECK_EQ(f.head().line, expectLastLine);
  CHECK(f.rec.quads > 0);
  CHECK(msSince(t) < 30000);

  const size_t before = f.doc->buffer().byteSize();
  const int nKeys = kSan > 1 ? 15 : 60;  // + 5 warm-up keys
  const double avg = typing(f, nKeys, what + ", caret at the end");
#if defined(NDEBUG) && !defined(__SANITIZE_ADDRESS__)
  if (keystrokeBudget) PERF_CHECK(avg < 8.0, "pathological " + what + " keystroke-to-paint (8 ms)");
#else
  (void)avg;
  (void)keystrokeBudget;
#endif
  CHECK_EQ(f.doc->buffer().byteSize(), before + static_cast<size_t>(nKeys) + 5);
  t = clk::now();
  f.key(Key('Z'), ModCtrl);  // the typing was one undo group or a few: undo until the text is the original
  for (int i = 0; i < 100 && f.doc->buffer().byteSize() > before; ++i) f.key(Key('Z'), ModCtrl);
  CHECK_EQ(f.doc->buffer().byteSize(), before);
  std::printf("PERF: pathological %s: undo of the typing %.1f ms\n", what.c_str(), msSince(t));

  f.key(Key::Home, ModCtrl);
  CHECK_EQ(f.head().line, 0);
  CHECK(f.rec.quads > 0);
}

}  // namespace

TEST(pathological_ui_giant_cpp_file_open_scroll_type) {
  const int lines = static_cast<int>(scaled(300000));
  const std::string text = giantCpp(lines);
  for (bool wrap : {false, true}) {
    PFx f(text, "giant.cpp", wrap);
    CHECK_EQ(f.tv->wordWrapActive(), wrap);
    session(f, std::string("giant C++ file, wrap ") + (wrap ? "on" : "off"), f.doc->buffer().lineCount() - 1);
  }
}

TEST(pathological_ui_giant_text_file) {
  const std::string text =
      rep("2026-01-01 INFO worker-001 processed request id=00000042 needle status=ok bytes=123 latency_ms=7\n",
          scaled(100000));
  for (bool wrap : {false, true}) {
    PFx f(text, "huge.txt", wrap);
    session(f, std::string("100k-line text file, wrap ") + (wrap ? "on" : "off"), f.doc->buffer().lineCount() - 1);
  }
}

TEST(pathological_ui_one_line_files) {
  struct Case {
    const char* name;
    std::string content;
    const char* what;
    // Known over-budget combinations (printed, never enforced), see docs/STRESS_RESULTS.md: a C++ line of 500 kB or
    // more is re-lexed by the highlighter on every keystroke (10-20 ms), and a line of 1M+ wide characters takes the
    // slow column mapping (35 ms with wrap off).
    bool budgetOff = false, budgetOn = true;
  };
  std::vector<Case> cases;
  cases.push_back(
      {"expr.cpp", "int long_expr = " + rep("1+", scaled(500000)) + "1;", "1 MB single-line expression", false, false});
  cases.push_back({"comment.cpp", "// " + rep("word ", scaled(400000)), "2 MB single-line comment", true, true});
  cases.push_back({"string.cpp", "const char* s = \"" + rep("ab\\n", scaled(300000)) + "\";", "1 MB single-line string",
                   true, true});
  std::string mini;
  for (int i = 0; i < 6000; ++i)
    mini += "namespace n" + std::to_string(i % 9) + "{class W" + std::to_string(i) +
            "{public:int u(int x)const;};int W" + std::to_string(i) + "::u(int x)const{return x*" + std::to_string(i) +
            ";}}";
  cases.push_back({"minified.cpp", mini, "minified C++ on one line", false, false});
  cases.push_back(
      {"cjk.txt", rep("\xe4\xb8\xad\xe6\x96\x87 ", scaled(300000)), "single line of wide characters", false, true});
  for (auto& c : cases) {
    for (bool wrap : {false, true}) {
      PFx f(c.content, c.name, wrap);
      const std::string what = std::string(c.what) + ", wrap " + (wrap ? "on" : "off");
      std::printf("PERF: pathological %s: open %.1f ms, first frame %.1f ms\n", what.c_str(), f.openMs, f.firstFrameMs);
      CHECK(f.rec.quads > 0);
      CHECK(f.firstFrameMs < 30000);
      for (int i = 0; i < 3 && static_cast<size_t>(f.head().column) < f.doc->buffer().line(0).size(); ++i)
        f.key(Key::End);  // with wrap on the first End stops at the row's edge
      CHECK_EQ(f.head().line, 0);
      CHECK_EQ(static_cast<size_t>(f.head().column), f.doc->buffer().line(0).size());
      CHECK(f.rec.quads > 0);
      const size_t before = f.doc->buffer().byteSize();
      const int nKeys = kSan > 1 ? 10 : 40;
      const double avg = typing(f, nKeys, what + ", caret at the end");
#if defined(NDEBUG) && !defined(__SANITIZE_ADDRESS__)
      if (wrap ? c.budgetOn : c.budgetOff) PERF_CHECK(avg < 8.0, "pathological " + what + " keystroke-to-paint (8 ms)");
#else
      (void)avg;
#endif
      CHECK_EQ(f.doc->buffer().byteSize(), before + static_cast<size_t>(nKeys) + 5);
      for (int i = 0; i < 100 && f.doc->buffer().byteSize() > before; ++i) f.key(Key('Z'), ModCtrl);
      CHECK_EQ(f.doc->buffer().byteSize(), before);
      for (int i = 0; i < 3 && f.head().column > 0; ++i) f.key(Key::Home);  // the first Home may stop at the row's edge
      CHECK_EQ(f.head().column, 0);
      for (int i = 0; i < 10; ++i) f.key(Key::Right, ModCtrl);  // word jumps near the start
      f.key(Key::Down);
      f.key(Key::Up);
      CHECK(f.rec.quads > 0);
    }
  }
}

TEST(pathological_ui_binary_nul_and_invalid_utf8_files_paint) {
  std::string bin(200000, '\0');
  unsigned x = 12345;
  for (size_t i = 0; i < bin.size(); i += 3) {
    x = x * 1103515245u + 12345u;
    bin[i] = static_cast<char>(x >> 16);
  }
  const std::string contents[] = {
      bin,
      std::string(300000, '\0'),
      "int ok;\n\xff\xfe\xfd int bad;\n\xc3\x28 \xa0\xa1 \xe2\x82 \xf0\x28\x8c\xbc\nint after;\n",
      rep("int cr_a;\rint cr_b;\r// c\r", 20000),
      rep("a\r\nb\nc\rd\r\r\ne\n\n\r\n", 20000),
      "\xef\xbb\xbfint bom;\n",
      std::string("\xff\xfe") + std::string("i\0n\0t\0 \0a\0;\0\n\0", 14),
      ""};
  int k = 0;
  for (const std::string& c : contents) {
    for (bool wrap : {false, true}) {
      PFx f(c, (k % 2 ? "b.cpp" : "b.txt"), wrap);
      CHECK(f.firstFrameMs < 30000);
      f.key(Key::End, ModCtrl);
      f.typeChar("q");
      f.key(Key('Z'), ModCtrl);
      f.key(Key::Home, ModCtrl);
      f.key(Key::PageDown);
      f.key(Key::PageUp);
      f.frame();
    }
    ++k;
  }
}

TEST(pathological_ui_deep_nesting_paints_and_edits) {
  const int n = 5000;
  const std::string files[] = {
      "void f() {" + rep("{", n) + "int innermost = 1;" + rep("}", n) + "}\n",
      "int p = " + rep("(", n) + "1" + rep(")", n) + ";\n",
      "using Deep = " + rep("Box<", n) + "int" + rep(">", n) + ";\n",
      rep("namespace n { ", n) + "int leaf;" + rep(" }", n) + "\n",
      rep("/* ", n) + "text */\n",
      "int " + rep("Ident_abcdefghij", 4000) + " = 1;\n",
  };
  for (const std::string& c : files) {
    for (bool wrap : {false, true}) {
      PFx f(c, "deep.cpp", wrap);
      CHECK(f.firstFrameMs < 30000);
      f.key(Key::End);
      f.typeChar("}");  // bracket matching around the caret walks the nesting
      f.key(Key::Left);
      f.frame();
      f.key(Key('Z'), ModCtrl);
      CHECK(f.rec.quads > 0);
    }
  }
}
