// B1c: text on screen. A golden screenshot of the sample file through TextRenderer, and a CPU-time benchmark of a
// 10k-line file scrolled to the middle at 1920x1080.
//
// The golden is only comparable between machines with the same font, so the test pins DejaVuSansMono (CI installs
// fonts-dejavu-core) and skips the comparison when it is not installed. Tolerance as for b1_scene.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

#include "ui/gfx/PngWrite.h"
#include "ui/gfx/TextRenderer.h"
#include "ui_test.h"

using namespace tf::gfx;

namespace {

std::string dejavuMono() {
  for (const char* p :
       {"/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", "/usr/share/fonts/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/TTF/DejaVuSansMono.ttf"})
    if (std::ifstream(p).good()) return p;
  const std::string d = discoverMonospaceFont("");
  return d.find("DejaVuSansMono") != std::string::npos ? d : std::string();
}

std::vector<std::string> sampleLines() {
  std::ifstream in(TENFOLD_SOURCE_DIR "/examples/sample_project/src/main.cpp", std::ios::binary);
  std::vector<std::string> lines;
  for (std::string l; std::getline(in, l);) lines.push_back(l);
  return lines;
}

}  // namespace

TEST(text_view_golden_b1_text) {
  SKIP_IF_NO_GL(fx);
  const std::string font = dejavuMono();
  if (font.empty()) {
    std::cerr << "    SKIP golden: DejaVuSansMono not installed (discovered: \"" << discoverMonospaceFont("")
              << "\"); goldens are only comparable with the same font\n";
    return;
  }
  std::string err;
  auto tr = TextRenderer::create(font, 15.0f, 1.0f, &err);
  REQUIRE(tr != nullptr);
  const std::vector<std::string> lines = sampleLines();
  REQUIRE(lines.size() > 10u);

  GlRenderer& r = *fx->renderer;
  const int w = 800, h = 600;
  r.setOffscreen(true);
  tr->draw(r, w, h, lines, 0);
  r.endFrame();
  std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4);
  REQUIRE(r.readPixels(w, h, px.data()));
  tr->releaseTexture(r);

  const std::string path = TENFOLD_SOURCE_DIR "/tests/golden/b1_text.png";
  if (uitest::updateGoldens) {
    REQUIRE(writePngFile(path, px.data(), w, h));
    std::cerr << "    golden updated: " << path << "\n";
    return;
  }
  int gw = 0, gh = 0;
  std::vector<uint8_t> golden;
  if (!readPngFile(path, gw, gh, golden)) {
    CHECK(!"golden missing or unreadable; run tenfold-ui-tests --update-goldens");
    return;
  }
  REQUIRE(gw == w && gh == h);
  size_t bad = 0;
  int worst = 0;
  for (size_t i = 0; i < golden.size(); i += 4) {
    int d = 0;
    for (int c = 0; c < 4; ++c) d = std::max(d, std::abs(int(golden[i + c]) - int(px[i + c])));
    if (d > 3) ++bad;
    worst = std::max(worst, d);
  }
  const size_t allowed = golden.size() / 4 / 500;  // 0.2%
  if (bad > allowed) {
    writePngFile("b1_text.actual.png", px.data(), w, h);
    std::cerr << "    " << bad << " pixels differ (max channel diff " << worst << "); wrote b1_text.actual.png\n";
  }
  CHECK(bad <= allowed);
}

// Layout + atlas flush + draw recording per frame (llvmpipe submit in endFrame excluded, as in renderer_perf).
TEST(text_view_perf_10k_lines_middle) {
  SKIP_IF_NO_GL(fx);
  std::string err;
  auto tr = TextRenderer::create(dejavuMono(), 15.0f, 1.0f, &err);
  if (!tr) {
    std::cerr << "    SKIP: " << err << "\n";
    return;
  }
  std::vector<std::string> lines;
  for (int i = 0; i < 10000; ++i) {
    std::ostringstream o;
    o << std::string(static_cast<size_t>(i % 5) * 2, ' ') << "for (int k" << i << " = 0; k" << i << " < n_" << i % 97
      << "; ++k" << i << ") { total += table[k" << i << " * 31 + " << i << "] ^ 0x" << std::hex << i * 2654435761u
      << "; }  // line " << std::dec << i;
    lines.push_back(o.str());
  }
  GlRenderer& r = *fx->renderer;
  r.setOffscreen(true);
  const int w = 1920, h = 1080;
  const float mid = static_cast<float>(lines.size() / 2) * tr->lineHeight();
  using clk = std::chrono::steady_clock;
  double rec = 0, sub = 0;
  auto frame = [&](int i) {
    auto t0 = clk::now();
    tr->draw(r, w, h, lines, mid + static_cast<float>(i % 7));
    auto t1 = clk::now();
    r.endFrame();
    auto t2 = clk::now();
    rec += std::chrono::duration<double, std::milli>(t1 - t0).count();
    sub += std::chrono::duration<double, std::milli>(t2 - t1).count();
  };
  frame(0);  // first frame fills the atlas (cold)
  const double cold = rec;
  for (int i = 0; i < 10; ++i) frame(i);
  rec = sub = 0;
  const int n = 200;
  for (int i = 0; i < n; ++i) frame(i);
  std::printf(
      "PERF: text view 10k lines @%dx%d: cold first frame %.3f ms; warm layout+flush+record %.3f ms/frame, "
      "submit %.3f ms/frame (avg of %d)\n",
      w, h, cold, rec / n, sub / n, n);
  tr->releaseTexture(r);
#if defined(NDEBUG) && !defined(__SANITIZE_ADDRESS__)
  CHECK(rec / n < 2.0);
#endif
}
