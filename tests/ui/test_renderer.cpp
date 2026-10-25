// GlRenderer tests: pixel probes, the golden-image scene, and the CPU-time benchmark.
//
// Goldens are PNGs in tests/golden/ written by our own stored-block encoder (src/ui/gfx/PngWrite.cpp), so the
// tests can read them back with the matching tiny decoder. Regenerate with `tenfold-ui-tests --update-goldens`.
// Comparison tolerance: per-channel |diff| <= 3, and at most 0.2% of pixels may exceed that (different Mesa /
// driver versions rasterise triangle edges slightly differently).
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "ui/gfx/GlLoader.h"
#include "ui/gfx/PngWrite.h"
#include "ui_test.h"

using namespace tf::gfx;

namespace {

struct Img {
  int w, h;
  std::vector<uint8_t> px;
  const uint8_t* at(int x, int y) const { return &px[(static_cast<size_t>(y) * w + x) * 4]; }
};

Img render(GlRenderer& r, int w, int h, Color clear, void (*draw)(GlRenderer&)) {
  r.setOffscreen(true);
  r.beginFrame(w, h, clear);
  draw(r);
  r.endFrame();
  Img img{w, h, std::vector<uint8_t>(static_cast<size_t>(w) * h * 4)};
  CHECK(r.readPixels(w, h, img.px.data()));
  return img;
}

bool near(const uint8_t* p, int r, int g, int b, int a, int tol = 1) {
  return std::abs(p[0] - r) <= tol && std::abs(p[1] - g) <= tol && std::abs(p[2] - b) <= tol &&
         std::abs(p[3] - a) <= tol;
}

// A deterministic R8 atlas: 16x16 cells, each a radial blob with a different radius.
TextureId makeAtlas(Renderer& r) {
  std::vector<uint8_t> px(128 * 128);
  for (int y = 0; y < 128; ++y)
    for (int x = 0; x < 128; ++x) {
      const int cell = (y / 16) * 8 + x / 16, rad = 3 + cell % 6, cx = x % 16 - 8, cy = y % 16 - 8;
      const int d2 = cx * cx + cy * cy, r2 = rad * rad;
      px[y * 128 + x] = d2 < r2 ? static_cast<uint8_t>(255 - d2 * 255 / r2) : 0;
    }
  TextureId t = r.createTexture(128, 128, TextureFormat::R8);
  r.updateTexture(t, 0, 0, 128, 128, px.data(), 128);
  return t;
}

void sceneB1(GlRenderer& r) {
  static TextureId atlas = 0;
  if (!atlas) atlas = makeAtlas(r);
  r.drawRect({16, 16, 100, 70}, {0.9f, 0.3f, 0.2f, 1});
  r.drawRect({70, 50, 100, 70}, {0.2f, 0.5f, 0.9f, 0.5f});
  for (int i = 0; i < 5; ++i) r.drawLine(130, 20.0f + i * 14, 240, 30.0f + i * 30, 1.0f + i, {1, 1, 0.4f, 1});
  r.pushScissor({20, 140, 90, 60});
  r.drawRect({0, 0, 256, 256}, {0.15f, 0.45f, 0.25f, 1});
  r.drawLine(0, 120, 200, 240, 8, {1, 1, 1, 1});
  r.pushScissor({50, 160, 100, 100});  // nested: intersection is 50..110 x 160..200
  r.drawRect({0, 0, 256, 256}, {0.8f, 0.2f, 0.7f, 1});
  r.popScissor();
  r.popScissor();
  std::vector<GlyphQuad> q;
  for (int i = 0; i < 32; ++i) {
    const float u = (i % 8) / 8.0f, v = (i / 8) / 8.0f, x = 130.0f + (i % 8) * 14, y = 150.0f + (i / 8) * 18;
    q.push_back({x, y, x + 12, y + 16, u, v, u + 0.125f, v + 0.125f});
  }
  r.drawGlyphs(atlas, q.data(), static_cast<int>(q.size()), {0.95f, 0.95f, 0.95f, 1});
  r.drawGlyphs(atlas, q.data(), 4, {1, 0.5f, 0, 0.5f});  // translucent glyph colour over the first row
}

}  // namespace

TEST(renderer_rect_blend_and_orientation) {
  SKIP_IF_NO_GL(fx);
  Img img = render(*fx->renderer, 32, 24, {0, 0, 0, 1}, [](GlRenderer& r) {
    r.drawRect({0, 0, 8, 8}, {1, 0, 0, 1});      // top-left, opaque red
    r.drawRect({16, 8, 8, 8}, {1, 1, 1, 0.5f});  // 50% white over black
  });
  CHECK(near(img.at(2, 2), 255, 0, 0, 255));
  CHECK(near(img.at(2, 21), 0, 0, 0, 255));  // bottom-left is untouched => rows are top-down
  CHECK(near(img.at(20, 12), 128, 128, 128, 255));
  CHECK(near(img.at(8, 2), 0, 0, 0, 255));  // just outside the rect
}

TEST(renderer_scissor_stack) {
  SKIP_IF_NO_GL(fx);
  Img img = render(*fx->renderer, 32, 32, {0, 0, 0, 1}, [](GlRenderer& r) {
    r.pushScissor({4, 4, 20, 20});
    r.pushScissor({10, 0, 100, 12});  // intersection: x 10..24, y 4..12
    r.drawRect({0, 0, 32, 32}, {0, 1, 0, 1});
    r.popScissor();
    r.drawRect({0, 26, 32, 6}, {0, 0, 1, 1});  // outside the remaining scissor (y 4..24): clipped away
    r.popScissor();
    r.drawRect({0, 28, 4, 4}, {1, 0, 0, 1});  // no scissor again
  });
  CHECK(near(img.at(12, 6), 0, 255, 0, 255));
  CHECK(near(img.at(8, 6), 0, 0, 0, 255));    // left of nested scissor
  CHECK(near(img.at(12, 14), 0, 0, 0, 255));  // below nested scissor
  CHECK(near(img.at(12, 28), 0, 0, 0, 255));  // blue rect fully clipped
  CHECK(near(img.at(1, 30), 255, 0, 0, 255));
}

TEST(renderer_draw_glyphs_synthetic_r8) {
  SKIP_IF_NO_GL(fx);
  GlRenderer& r = *fx->renderer;
  static TextureId tex;
  tex = r.createTexture(8, 8, TextureFormat::R8);
  std::vector<uint8_t> half(64, 128);
  r.updateTexture(tex, 0, 0, 8, 8, half.data(), 8);
  // Partial update with a padded source stride: 4x4 block of 255 at (4,4), source rows are 16 bytes apart.
  std::vector<uint8_t> src(16 * 4, 0);
  for (int y = 0; y < 4; ++y)
    for (int x = 0; x < 4; ++x) src[y * 16 + x] = 255;
  r.updateTexture(tex, 4, 4, 4, 4, src.data(), 16);
  Img img = render(r, 40, 20, {0, 0, 0, 1}, [](GlRenderer& rr) {
    const GlyphQuad both[2] = {{2, 2, 6, 6, 0.0f, 0.0f, 0.4f, 0.4f},             // texels 0..3 => coverage 128
                               {12, 2, 16, 6, 0.625f, 0.625f, 0.875f, 0.875f}};  // texels 5..6 => coverage 255
    rr.drawGlyphs(tex, both, 2, {1, 1, 1, 1});
    rr.drawGlyphs(tex, both, 0, {1, 1, 1, 1});  // count 0 is a no-op
    const GlyphQuad red{22, 2, 26, 6, 0.625f, 0.625f, 0.875f, 0.875f};
    rr.drawGlyphs(tex, &red, 1, {1, 0, 0, 0.5f});  // coverage 255 * alpha 0.5 over black => 128 red
  });
  CHECK(near(img.at(4, 4), 128, 128, 128, 255));
  CHECK(near(img.at(14, 4), 255, 255, 255, 255));
  CHECK(near(img.at(24, 4), 128, 0, 0, 255));
  CHECK(near(img.at(30, 4), 0, 0, 0, 255));
  r.destroyTexture(tex);
  r.drawGlyphs(tex, nullptr, 0, {1, 1, 1, 1});  // unknown texture ids are ignored, not fatal
}

TEST(renderer_golden_b1_scene) {
  SKIP_IF_NO_GL(fx);
  Img img = render(*fx->renderer, 256, 256, {0.10f, 0.11f, 0.13f, 1}, sceneB1);
  const std::string path = TENFOLD_SOURCE_DIR "/tests/golden/b1_scene.png";
  if (uitest::updateGoldens) {
    REQUIRE(writePngFile(path, img.px.data(), img.w, img.h));
    std::cerr << "    golden updated: " << path << "\n";
    return;
  }
  int gw = 0, gh = 0;
  std::vector<uint8_t> golden;
  if (!readPngFile(path, gw, gh, golden)) {
    CHECK(!"golden missing or unreadable; run tenfold-ui-tests --update-goldens");
    return;
  }
  REQUIRE(gw == img.w && gh == img.h);
  size_t bad = 0;
  int worst = 0;
  for (size_t i = 0; i < golden.size(); i += 4) {
    int d = 0;
    for (int c = 0; c < 4; ++c) d = std::max(d, std::abs(int(golden[i + c]) - int(img.px[i + c])));
    if (d > 3) ++bad;
    worst = std::max(worst, d);
  }
  const size_t allowed = golden.size() / 4 / 500;  // 0.2%
  if (bad > allowed) {
    writePngFile("b1_scene.actual.png", img.px.data(), img.w, img.h);
    std::cerr << "    " << bad << " pixels differ (max channel diff " << worst << "); wrote b1_scene.actual.png\n";
  }
  CHECK(bad <= allowed);
}

// 10k-line file, visible region: 120 lines x 150 glyph quads = 18000 quads on a 1920x1080 frame.
TEST(renderer_perf_visible_region) {
  SKIP_IF_NO_GL(fx);
  GlRenderer& r = *fx->renderer;
  TextureId atlas = makeAtlas(r);
  std::vector<std::vector<GlyphQuad>> lines(120);
  for (int l = 0; l < 120; ++l)
    for (int c = 0; c < 150; ++c) {
      const int cell = (l * 7 + c * 3) % 64;
      const float u = (cell % 8) / 8.0f, v = (cell / 8) / 8.0f, x = 8.0f + c * 12, y = 4.0f + l * 9;
      lines[l].push_back({x, y, x + 10, y + 9, u, v, u + 0.125f, v + 0.125f});
    }
  r.setOffscreen(true);
  using clk = std::chrono::steady_clock;
  auto frame = [&](double& recordMs, double& submitMs) {
    auto t0 = clk::now();
    r.beginFrame(1920, 1080, {0.1f, 0.1f, 0.1f, 1});
    for (int l = 0; l < 120; ++l) {
      if (l % 2 == 0) r.drawRect({0, 4.0f + l * 9, 1920, 9}, {0.14f, 0.14f, 0.16f, 1});  // band fills
      r.drawGlyphs(atlas, lines[l].data(), 150, {0.9f, 0.9f, 0.9f, 1});
    }
    auto t1 = clk::now();
    r.endFrame();
    auto t2 = clk::now();
    recordMs += std::chrono::duration<double, std::milli>(t1 - t0).count();
    submitMs += std::chrono::duration<double, std::milli>(t2 - t1).count();
  };
  double rec = 0, sub = 0;
  for (int i = 0; i < 10; ++i) frame(rec, sub);  // warm up
  rec = sub = 0;
  const int n = 200;
  for (int i = 0; i < n; ++i) frame(rec, sub);
  auto st = r.lastFrameStats();
  std::printf(
      "PERF: %d quads/frame, %d draw calls, %d vertices: record %.3f ms + submit %.3f ms = %.3f ms CPU/frame (avg of "
      "%d)\n",
      120 * 150, st.drawCalls, st.vertices, rec / n, sub / n, (rec + sub) / n, n);
  r.destroyTexture(atlas);
#if defined(NDEBUG) && !defined(__SANITIZE_ADDRESS__)
  // Acceptance target: < 2 ms CPU per frame. Wall-clock budgets are flaky on loaded or shared machines, so the
  // numbers are always printed but only enforced when TENFOLD_ENFORCE_PERF=1 (see docs/BENCHMARKS.md). On a software
  // rasteriser (Mesa llvmpipe) glDrawArrays does the vertex/binning work on the calling thread, so only our own
  // recording time is meaningful there.
  const char* name = reinterpret_cast<const char*>(tf::gfx::gl::GetString(tf::gfx::gl::RENDERER));
  const std::string rn = name ? name : "";
  const bool software = rn.find("llvmpipe") != std::string::npos || rn.find("softpipe") != std::string::npos ||
                        rn.find("Software") != std::string::npos;
  std::printf("PERF: renderer \"%s\"%s\n", rn.c_str(), software ? " (software: submit time not representative)" : "");
  const char* enforce = std::getenv("TENFOLD_ENFORCE_PERF");
  if (enforce && std::string(enforce) == "1") {
    CHECK(rec / n < 2.0);
    if (!software) CHECK((rec + sub) / n < 2.0);
  } else if (rec / n >= 2.0 || (!software && (rec + sub) / n >= 2.0)) {
    std::printf("PERF: over the 2 ms budget (not enforced; set TENFOLD_ENFORCE_PERF=1 to enforce)\n");
  }
#endif
}
