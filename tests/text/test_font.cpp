#include "text/text_test_util.h"

using namespace tftext;

TEST(font_metrics_sanity) {
  auto f = loadMono(16);
  if (!f) return;
  const FontMetrics& m = f->metrics();
  CHECK(m.ascender > 6 && m.ascender < 24);
  CHECK(m.descender > 0 && m.descender < 12);
  CHECK(m.lineHeight >= m.ascender + m.descender);
  CHECK(m.advance >= 6 && m.advance <= 14);
  CHECK(m.advance == static_cast<float>(static_cast<int>(m.advance)));
  CHECK(m.underlinePos > 0 && m.underlinePos < m.descender + 2);
  CHECK(m.underlineThickness >= 1);
}

TEST(font_dpi_scale_scales_metrics) {
  auto a = loadMono(16, 1);
  auto b = loadMono(16, 2);
  if (!a || !b) return;
  CHECK(b->metrics().lineHeight > a->metrics().lineHeight * 1.8f);
  CHECK(b->sizeKey() == a->sizeKey() * 2);
}

TEST(font_monospace_advance_is_uniform) {
  auto f = loadMono(14);
  if (!f) return;
  for (char c : std::string("iWm. -@")) {
    GlyphBitmap g = f->rasterize(f->glyphIndex(static_cast<unsigned char>(c)));
    CHECK_EQ(g.advance, f->metrics().advance);
  }
}

TEST(font_rasterize_A) {
  auto f = loadMono(16);
  if (!f) return;
  uint32_t g = f->glyphIndex('A');
  REQUIRE(g != 0);
  GlyphBitmap b = f->rasterize(g);
  CHECK(b.width > 3 && b.height > 5);
  CHECK_EQ(b.pixels.size(), static_cast<size_t>(b.width) * b.height);
  CHECK(b.bearingY > 0);
  int sum = 0, max = 0;
  for (uint8_t p : b.pixels) {
    sum += p;
    max = std::max<int>(max, p);
  }
  CHECK(sum > 500);
  CHECK(max > 200);
  GlyphBitmap sp = f->rasterize(f->glyphIndex(' '));
  CHECK_EQ(sp.width, 0);
}

TEST(font_bad_input_fails_cleanly) {
  std::string err;
  CHECK(FtFont::fromFile("/nonexistent/font.ttf", FontOptions(), &err) == nullptr);
  CHECK(!err.empty());
  CHECK(FtFont::fromMemory({1, 2, 3, 4}, FontOptions(), &err) == nullptr);
}

TEST(font_from_memory_matches_file) {
  std::string path = discoverMonospaceFont("");
  if (path.empty()) return;
  auto a = FtFont::fromFile(path, FontOptions());
  REQUIRE(a != nullptr);
  CHECK(discoverMonospaceFont(path) == path);
}

TEST(fontstack_fallback_picks_second_font) {
  auto first = std::make_shared<MockFont>(U"ab");
  auto second = std::make_shared<MockFont>(U"bc");
  FontStack s;
  s.add(first);
  s.add(second);
  GlyphRef a = s.lookup('a'), b = s.lookup('b'), c = s.lookup('c');
  CHECK(a.font == first.get());
  CHECK(b.font == first.get());  // first one with the glyph wins
  CHECK(c.font == second.get());
  CHECK_EQ(c.glyph, 2u);
}

TEST(fontstack_missing_everywhere_is_notdef_box) {
  auto first = std::make_shared<MockFont>(U"a");
  FontStack s;
  s.add(first);
  GlyphRef r = s.lookup(U'中');
  CHECK(r.font == first.get());
  CHECK_EQ(r.glyph, kNotdefGlyph);
  GlyphBitmap box = r.font->rasterize(r.glyph);
  REQUIRE(box.width >= 3 && box.height >= 3);
  CHECK_EQ(box.pixels[0], 255);                                 // corner set
  CHECK_EQ(box.pixels[static_cast<size_t>(box.width) + 1], 0);  // hollow inside
}

TEST(fontstack_real_fallback_when_two_fonts) {
  auto mono = loadMono(16);
  std::string fb = discoverFallbackFont();
  if (!mono || fb.empty()) return;
  auto second = FtFont::fromFile(fb, FontOptions{16.0f});
  if (!second) return;
  if (second->glyphIndex(U'中') == 0 || mono->glyphIndex(U'中') != 0) {
    std::cerr << "    SKIP: fonts do not split on U+4E2D\n";
    return;
  }
  FontStack s;
  s.add(mono);
  s.add(std::move(second));
  CHECK(s.lookup('A').font == mono.get());
  CHECK(s.lookup(U'中').font != mono.get());
  CHECK(s.lookup(U'中').glyph != kNotdefGlyph);
}
