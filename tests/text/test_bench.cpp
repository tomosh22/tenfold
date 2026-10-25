#include <chrono>
#include <fstream>

#include "text/text_test_util.h"
#include "ui/gfx/TextLayout.h"

using namespace tftext;

// Hot path: 120 visible lines x 150 columns from a real source file with a warm atlas. Budget: < 1 ms CPU
// (keystroke-to-paint is 8 ms in total, plan section 1.7). Numbers are printed; the hard limit is deliberately loose
// so debug/sanitizer builds do not fail.
TEST(bench_layout_120x150_warm_atlas) {
  auto f = loadMono(16);
  if (!f) return;
  std::ifstream in(std::string(TENFOLD_SOURCE_DIR) + "/src/app/Editor.cpp");
  std::vector<std::string> lines;
  for (std::string l; lines.size() < 120 && std::getline(in, l);) lines.push_back(l);
  REQUIRE(lines.size() == 120u);

  FontStack st;
  st.add(f);
  Atlas atlas;
  TextLayout tl(st, atlas, 4);
  const float vis = 150 * tl.cellWidth();
  std::vector<ColorSpan> spans{{0, 4, 1}, {8, 12, 2}, {20, 30, 3}};

  using clock = std::chrono::steady_clock;
  LineLayout out;
  auto c0 = clock::now();
  for (size_t i = 0; i < lines.size(); ++i)
    tl.layoutLine(lines[i], 0, 14.0f + static_cast<float>(i) * tl.lineHeight(), 0, vis, spans, 0, out);
  double coldMs = std::chrono::duration<double, std::milli>(clock::now() - c0).count();
  size_t quads = out.quads.size();

  const int iters = 200;
  double best = 1e9, sum = 0;
  for (int it = 0; it < iters; ++it) {
    out.clear();
    auto t0 = clock::now();
    for (size_t i = 0; i < lines.size(); ++i)
      tl.layoutLine(lines[i], 0, 14.0f + static_cast<float>(i) * tl.lineHeight(), 0, vis, spans, 0, out);
    double ms = std::chrono::duration<double, std::milli>(clock::now() - t0).count();
    best = std::min(best, ms);
    sum += ms;
  }
  std::cerr << "    BENCH layout 120 lines x 150 cols: " << quads << " quads; first pass (fills atlas) " << coldMs
            << " ms; warm best " << best << " ms, mean " << sum / iters << " ms\n";
  CHECK(best < 20.0);
}

TEST(bench_cold_atlas_ascii_fill) {
  auto f = loadMono(16);
  if (!f) return;
  FontStack st;
  st.add(f);
  Atlas atlas;
  auto t0 = std::chrono::steady_clock::now();
  for (char32_t c = 33; c < 127; ++c) {
    GlyphRef g = st.lookup(c);
    atlas.glyph(*g.font, g.glyph);
  }
  double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  std::cerr << "    BENCH cold atlas fill, 94 printable ASCII glyphs @16px: " << ms << " ms (" << atlas.glyphCount()
            << " glyphs, atlas " << atlas.size() << "px)\n";
  CHECK(ms < 200.0);
}
