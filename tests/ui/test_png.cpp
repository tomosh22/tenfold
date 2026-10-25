#include <cstdio>
#include <cstdlib>
#include <fstream>

#include "ui/gfx/PngWrite.h"
#include "ui_test.h"

using namespace tf::gfx;

namespace {
std::vector<uint8_t> pattern(int w, int h) {
  std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4);
  for (size_t i = 0; i < px.size(); ++i) px[i] = static_cast<uint8_t>((i * 7 + i / 13) & 0xff);
  return px;
}
}  // namespace

TEST(png_checksums) {
  const char* s = "123456789";
  CHECK_EQ(crc32(reinterpret_cast<const uint8_t*>(s), 9), 0xCBF43926u);
  const char* w = "Wikipedia";
  CHECK_EQ(adler32(reinterpret_cast<const uint8_t*>(w), 9), 0x11E60398u);
}

TEST(png_roundtrip) {
  for (auto [w, h] : {std::pair{1, 1}, {3, 5}, {64, 64}, {300, 200}}) {  // 300x200x4 spans several stored blocks
    auto px = pattern(w, h);
    auto png = encodePngRgba(px.data(), w, h);
    int rw = 0, rh = 0;
    std::vector<uint8_t> back;
    REQUIRE(decodePngRgba(png, rw, rh, back));
    CHECK_EQ(rw, w);
    CHECK_EQ(rh, h);
    CHECK(back == px);
    png[png.size() / 2] ^= 1;  // corruption is detected (CRC / Adler)
    CHECK(!decodePngRgba(png, rw, rh, back));
  }
}

// Independent check: decode with python3's zlib (skipped if python3 is missing).
TEST(png_python_zlib_decode) {
#ifdef _WIN32
  std::cerr << "    SKIP: python check is POSIX-only\n";
#else
  if (std::system("python3 -c 'import zlib' >/dev/null 2>&1") != 0) {
    std::cerr << "    SKIP: python3 not available\n";
    return;
  }
  const int w = 300, h = 200;
  auto px = pattern(w, h);
  const std::string path = "tf_png_check.png";
  REQUIRE(writePngFile(path, px.data(), w, h));
  uint32_t sum = 0;
  for (uint8_t b : px) sum = sum * 31 + b;
  const std::string cmd =
      "python3 -I " TENFOLD_SOURCE_DIR "/tests/ui/check_png.py " + path + " 300 200 " + std::to_string(sum);
  CHECK_EQ(std::system(cmd.c_str()), 0);
  std::remove(path.c_str());
#endif
}
