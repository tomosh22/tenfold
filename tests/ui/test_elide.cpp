// UI-POLISH: text elision (TextRenderer::fit / fitMiddle / ellipsis), tree rows that elide instead of clipping, and the
// explorer's file icon being geometry (never a glyph that could turn into a missing-glyph box).
#include <memory>
#include <string>

#include "../text/text_test_util.h"
#include "ui/gfx/TextRenderer.h"
#include "ui/settings/Theme.h"
#include "ui/widgets/TreeView.h"
#include "ui_test.h"

using namespace tf;
using namespace tf::ui;

namespace {

constexpr float kCell = 8;  // MockFont advance

std::unique_ptr<gfx::TextRenderer> mono(bool withEllipsis, std::u32string extra = U"") {
  gfx::FontStack stack;
  std::u32string cps = U"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 +._-:,;/\\()[]'*#" + extra;
  if (withEllipsis) cps += U'\u2026';
  stack.add(std::make_shared<tftext::MockFont>(cps, kCell));
  return gfx::TextRenderer::fromFonts(std::move(stack));
}

// Width of `cols` cells (fit works in whole cells; +0.5 proves a partial cell does not count).
float w(int cols) { return cols * kCell + 0.5f; }

struct Rec : gfx::Renderer {
  int rects = 0, lines = 0, quads = 0, tinted = 0;
  void beginFrame(int, int, gfx::Color) override { rects = lines = quads = tinted = 0; }
  void drawRect(const gfx::RectF&, gfx::Color c) override {
    ++rects;
    if (c.a < 1.0f) ++tinted;
  }
  void drawLine(float, float, float, float, float, gfx::Color) override { ++lines; }
  void drawGlyphs(gfx::TextureId, const gfx::GlyphQuad*, int n, gfx::Color) override { quads += n; }
  void pushScissor(const gfx::RectF&) override {}
  void popScissor() override {}
  gfx::TextureId createTexture(int, int, gfx::TextureFormat) override { return 1; }
  void updateTexture(gfx::TextureId, int, int, int, int, const void*, int) override {}
  void destroyTexture(gfx::TextureId) override {}
  void endFrame() override {}
  bool readPixels(int, int, uint8_t*) override { return false; }
};

}  // namespace

TEST(elide_ellipsis_follows_the_font_coverage) {
  CHECK_EQ(std::string(mono(true)->ellipsis()), std::string("\xE2\x80\xA6"));
  CHECK_EQ(std::string(mono(false)->ellipsis()), std::string("..."));
}

TEST(elide_fit_ascii_exact_fit_and_end_ellipsis) {
  auto tr = mono(true);
  CHECK_EQ(tr->fit("abcdef", w(6)), std::string("abcdef"));  // exact fit: untouched
  CHECK_EQ(tr->fit("abcdefg", w(6)), std::string("abcde\xE2\x80\xA6"));
  CHECK_EQ(tr->fit("abcdefg", w(7)), std::string("abcdefg"));
  CHECK_EQ(tr->fit("", w(0)), std::string());
  auto tr3 = mono(false);  // "..." takes three cells
  CHECK_EQ(tr3->fit("abcdefg", w(6)), std::string("abc..."));
}

TEST(elide_fit_width_smaller_than_the_ellipsis) {
  auto tr = mono(true);
  CHECK_EQ(tr->fit("abcdef", w(1)), std::string("\xE2\x80\xA6"));
  CHECK_EQ(tr->fit("abcdef", 3.0f), std::string());  // not even one cell
  CHECK_EQ(tr->fit("abcdef", 0.0f), std::string());
  auto tr3 = mono(false);
  CHECK_EQ(tr3->fit("abcdef", w(2)), std::string(".."));
  CHECK_EQ(tr3->fit("abcdef", w(1)), std::string("."));
  CHECK_EQ(tr3->fitMiddle("abcdef", w(2)), std::string(".."));
  CHECK_EQ(tr->fitMiddle("abcdef", w(1)), std::string("\xE2\x80\xA6"));
  CHECK_EQ(tr->fitMiddle("abcdef", 0.0f), std::string());
}

TEST(elide_fit_utf8_multibyte_cuts_on_characters) {
  auto tr = mono(true, U"\u00e9\u00fc");  // é ü
  const std::string s =
      "caf\xC3\xA9_\xC3\xBC"
      "ber";  // 9 characters, 11 bytes
  CHECK_EQ(tr->fit(s, w(9)), s);
  CHECK_EQ(tr->fit(s, w(5)), std::string("caf\xC3\xA9\xE2\x80\xA6"));  // never in the middle of a character
  CHECK_EQ(tr->fit(s, w(4)), std::string("caf\xE2\x80\xA6"));
  CHECK_EQ(tr->fitMiddle(s, w(5)), std::string("ca\xE2\x80\xA6"
                                               "er"));  // 2 + mark + 2 cells
}

TEST(elide_fit_middle_keeps_the_start_and_the_file_name_of_a_path) {
  auto tr = mono(true);
  const std::string p = "C:\\Users\\me\\Documents\\Projects\\Game\\Game.sln";  // 44 columns
  CHECK_EQ(tr->fitMiddle(p, w(44)), p);
  const std::string e = "\xE2\x80\xA6";
  CHECK_EQ(tr->fitMiddle(p, w(30)), "C:\\Users\\me\\" + e + "\\Game\\Game.sln");
  const std::string r30 = tr->fitMiddle(p, w(30));
  CHECK(r30.size() >= 3);
  // Always within the width and ending in the file name while that fits.
  for (int c = 14; c < 44; ++c) {
    const std::string r = tr->fitMiddle(p, w(c));
    CHECK(tr->measure(r) <= w(c));
    CHECK(r.size() >= 8 && r.substr(r.size() - 8) == "Game.sln");
    CHECK(r.find(e) != std::string::npos);
  }
  // A file name longer than the width: head + mark + the end of the name.
  const std::string longName = "C:\\a\\a_very_long_file_name_indeed.sln";
  const std::string r = tr->fitMiddle(longName, w(16));
  CHECK(tr->measure(r) <= w(16));
  CHECK(r.find(e) != std::string::npos);
  CHECK_EQ(r.substr(r.size() - 4), std::string(".sln"));
  // Forward slashes work the same.
  CHECK_EQ(tr->fitMiddle("/home/me/src/proj/lib/util.cpp", w(20)), "/home/me/" + e + "/util.cpp");
  // No separator at all: half and half.
  CHECK_EQ(tr->fitMiddle("abcdefghij", w(6)), "abc" + e + "ij");
}

TEST(elide_tree_rows_end_in_an_ellipsis_when_clipped) {
  auto tr = mono(false);
  const std::string name = std::string(40, 'x');
  auto run = [&](const std::string& n, float width) {
    RootWidget root{*tr};
    auto* tv = static_cast<TreeView*>(root.addChild(std::make_unique<TreeView>()));
    TreeModel m;
    const int r = m.add(-1, "root", TreeKind::Root);
    m.add(r, n, TreeKind::File, "/w/f");
    m.indexPaths();
    tv->setModel(std::move(m));
    Rec rec;
    root.layout({0, 0, width, 200});
    tv->layout({0, 0, width, 200});
    root.render(rec, static_cast<int>(width), 200);
    return rec;
  };
  const Rec base = run("x", 400), narrow = run(name, 220), wide = run(name, 700);
  // Full name when it fits: 40 glyphs more than the one-letter model; clipped: fewer, and the dots are among them.
  CHECK_EQ(wide.quads - base.quads, 39);
  CHECK(narrow.quads < wide.quads);
  CHECK(narrow.quads - base.quads >= 3);
}

TEST(elide_file_icon_is_geometry_not_a_hollow_box) {
  auto tr = mono(false);
  RootWidget root{*tr};
  auto* tv = static_cast<TreeView*>(root.addChild(std::make_unique<TreeView>()));
  TreeModel m;
  const int r = m.add(-1, "r", TreeKind::Root);
  m.add(r, "a.c", TreeKind::File, "/w/a.c");
  m.indexPaths();
  tv->setModel(std::move(m));
  Rec rec;
  root.layout({0, 0, 300, 200});
  tv->layout({0, 0, 300, 200});
  root.render(rec, 300, 200);
  CHECK(rec.lines >= 1);   // the folded corner
  CHECK(rec.tinted >= 2);  // the page body is filled, so the icon is never a bare outline
  // The row text is the name only: no icon glyph (header hint "Ctrl+; to filter" + "r" + "a.c").
  CHECK_EQ(rec.quads, 14 + 1 + 3);
}
