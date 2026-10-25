#include <random>

#include "text/text_test_util.h"

using namespace tftext;

TEST(atlas_pack_2000_rects_no_overlap_with_growth) {
  Atlas a(256, 4096);
  std::mt19937 rng(42);
  std::vector<AtlasRect> rects;
  for (int i = 0; i < 2000; ++i) {
    int w = 3 + static_cast<int>(rng() % 38), h = 3 + static_cast<int>(rng() % 38);
    AtlasRect r;
    REQUIRE(a.allocate(w, h, r));
    rects.push_back(r);
  }
  CHECK(a.size() > 256);  // growth happened
  CHECK(a.generation() > 0);
  int bad = 0;
  for (size_t i = 0; i < rects.size(); ++i) {
    const AtlasRect& r = rects[i];
    if (r.x < 1 || r.y < 1 || r.x + r.w + 1 > a.size() || r.y + r.h + 1 > a.size()) ++bad;
    for (size_t j = i + 1; j < rects.size(); ++j) {
      const AtlasRect& s = rects[j];
      // Each glyph owns a 1px padded box [x-1, x+w+1); the boxes must be disjoint.
      if (r.x - 1 < s.x + s.w + 1 && s.x - 1 < r.x + r.w + 1 && r.y - 1 < s.y + s.h + 1 && s.y - 1 < r.y + r.h + 1)
        ++bad;
    }
  }
  CHECK_EQ(bad, 0);
}

TEST(atlas_growth_preserves_pixels) {
  Atlas a(64, 512);
  AtlasRect r;
  REQUIRE(a.allocate(20, 20, r));
  std::vector<uint8_t> blk(400, 77);
  a.write(r, blk.data(), 20);
  uint32_t gen = a.generation();
  AtlasRect big;
  REQUIRE(a.allocate(100, 100, big));  // forces growth
  CHECK(a.generation() > gen);
  CHECK_EQ(a.pixels()[static_cast<size_t>(r.y) * a.size() + r.x], 77);
  CHECK_EQ(a.pixels()[static_cast<size_t>(r.y + 19) * a.size() + r.x + 19], 77);
  AtlasRect huge;
  CHECK(!a.allocate(600, 10, huge));  // beyond maxSize
}

TEST(atlas_dirty_rects_cover_all_writes) {
  Atlas a(512, 512);
  std::mt19937 rng(7);
  std::vector<AtlasRect> written;
  for (int i = 0; i < 200; ++i) {
    int w = 2 + static_cast<int>(rng() % 15), h = 12;
    AtlasRect r;
    REQUIRE(a.allocate(w, h, r));
    std::vector<uint8_t> blk(static_cast<size_t>(w) * h, 255);
    a.write(r, blk.data(), w);
    written.push_back(r);
  }
  for (const AtlasRect& r : written) {
    bool covered = false;
    for (const AtlasRect& d : a.dirtyRects())
      if (r.x >= d.x && r.y >= d.y && r.x + r.w <= d.x + d.w && r.y + r.h <= d.y + d.h) covered = true;
    CHECK(covered);
  }
  CHECK(a.dirtyRects().size() < written.size());  // adjacent writes on a shelf were merged
}

TEST(atlas_flush_creates_once_then_updates_dirty_only) {
  Atlas a(256, 1024);
  FakeRenderer r;
  auto font = std::make_shared<MockFont>(U"abcdef");
  a.flush(r);
  CHECK_EQ(r.creates.size(), 1u);
  r.updates.clear();
  a.flush(r);  // nothing dirty
  CHECK_EQ(r.creates.size(), 1u);
  CHECK_EQ(r.updates.size(), 0u);

  const AtlasGlyph* g = a.glyph(*font, 1);
  REQUIRE(g->w == 5);
  a.flush(r);
  CHECK_EQ(r.creates.size(), 1u);
  REQUIRE(r.updates.size() == 1u);
  CHECK_EQ(r.updates[0].x, g->x);
  CHECK_EQ(r.updates[0].y, g->y);
  CHECK_EQ(r.updates[0].w, 5);
  CHECK_EQ(r.updates[0].h, 8);
  CHECK_EQ(r.updates[0].rowBytes, a.size());
  CHECK(a.dirtyRects().empty());

  // Cached lookup: same pointer, no new dirty region.
  CHECK(a.glyph(*font, 1) == g);
  CHECK(a.dirtyRects().empty());
}

TEST(atlas_growth_recreates_texture_and_uploads_full) {
  Atlas a(64, 512);
  FakeRenderer r;
  a.flush(r);
  AtlasRect big;
  REQUIRE(a.allocate(100, 100, big));
  r.updates.clear();
  a.flush(r);
  CHECK_EQ(r.creates.size(), 2u);
  CHECK_EQ(r.destroys, 1);
  REQUIRE(r.updates.size() == 1u);
  CHECK_EQ(r.updates[0].w, a.size());
}

TEST(atlas_key_distinguishes_font_and_size) {
  Atlas a;
  MockFont f1(U"a"), f2(U"a");
  const AtlasGlyph* g1 = a.glyph(f1, 1);
  const AtlasGlyph* g2 = a.glyph(f2, 1);
  CHECK(g1 != g2);
  CHECK_EQ(a.glyphCount(), 2u);
}

TEST(atlas_real_font_blank_glyph_has_no_rect) {
  auto f = loadMono(16);
  if (!f) return;
  Atlas a;
  CHECK_EQ(a.glyph(*f, f->glyphIndex(' '))->w, 0);
  CHECK(a.glyph(*f, f->glyphIndex('A'))->w > 0);
}
