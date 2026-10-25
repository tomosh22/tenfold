#include "ui/gfx/PngWrite.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>

namespace tf::gfx {
namespace {

void put32(std::vector<uint8_t>& o, uint32_t v) {
  for (int s = 24; s >= 0; s -= 8) o.push_back(static_cast<uint8_t>(v >> s));
}
uint32_t get32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

void chunk(std::vector<uint8_t>& o, const char* type, const std::vector<uint8_t>& data) {
  put32(o, static_cast<uint32_t>(data.size()));
  const size_t start = o.size();
  o.insert(o.end(), type, type + 4);
  o.insert(o.end(), data.begin(), data.end());
  put32(o, crc32(o.data() + start, o.size() - start));
}

const uint8_t kSig[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};

}  // namespace

uint32_t crc32(const uint8_t* d, size_t n, uint32_t crc) {
  static const auto table = [] {
    std::vector<uint32_t> t(256);
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t c = i;
      for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      t[i] = c;
    }
    return t;
  }();
  crc = ~crc;
  for (size_t i = 0; i < n; ++i) crc = table[(crc ^ d[i]) & 0xff] ^ (crc >> 8);
  return ~crc;
}

uint32_t adler32(const uint8_t* d, size_t n) {
  uint32_t a = 1, b = 0;
  for (size_t i = 0; i < n; ++i) {
    a = (a + d[i]) % 65521;
    b = (b + a) % 65521;
  }
  return (b << 16) | a;
}

std::vector<uint8_t> encodePngRgba(const uint8_t* rgba, int w, int h) {
  std::vector<uint8_t> raw;  // scanlines, each prefixed by filter type 0
  raw.reserve(static_cast<size_t>(h) * (static_cast<size_t>(w) * 4 + 1));
  for (int y = 0; y < h; ++y) {
    raw.push_back(0);
    raw.insert(raw.end(), rgba + static_cast<size_t>(y) * w * 4, rgba + static_cast<size_t>(y + 1) * w * 4);
  }
  std::vector<uint8_t> z = {0x78, 0x01};  // zlib header: deflate, 32K window, no compression
  for (size_t pos = 0; pos < raw.size() || pos == 0;) {
    const size_t n = std::min<size_t>(raw.size() - pos, 65535);
    const bool last = pos + n >= raw.size();
    z.push_back(static_cast<uint8_t>(last ? 1 : 0));  // BFINAL, BTYPE=00 (stored)
    z.push_back(static_cast<uint8_t>(n & 0xff));
    z.push_back(static_cast<uint8_t>(n >> 8));
    z.push_back(static_cast<uint8_t>(~n & 0xff));
    z.push_back((~n >> 8) & 0xff);
    z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
    pos += n;
    if (last) break;
  }
  put32(z, adler32(raw.data(), raw.size()));

  std::vector<uint8_t> out(kSig, kSig + 8), ihdr;
  put32(ihdr, w);
  put32(ihdr, h);
  ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});  // 8-bit RGBA, deflate, adaptive filtering, no interlace
  chunk(out, "IHDR", ihdr);
  chunk(out, "IDAT", z);
  chunk(out, "IEND", {});
  return out;
}

bool writePngFile(const std::string& path, const uint8_t* rgba, int w, int h) {
  const auto png = encodePngRgba(rgba, w, h);
  std::ofstream f(path, std::ios::binary);
  f.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
  return static_cast<bool>(f);
}

bool decodePngRgba(const std::vector<uint8_t>& png, int& w, int& h, std::vector<uint8_t>& rgba) {
  if (png.size() < 8 || std::memcmp(png.data(), kSig, 8) != 0) return false;
  std::vector<uint8_t> z;
  bool sawHeader = false;
  for (size_t p = 8; p + 12 <= png.size();) {
    const uint32_t len = get32(&png[p]);
    if (p + 12 + len > png.size()) return false;
    const uint8_t* type = &png[p + 4];
    if (crc32(type, 4 + len) != get32(&png[p + 8 + len])) return false;
    if (!std::memcmp(type, "IHDR", 4)) {
      if (len != 13 || png[p + 16] != 8 || png[p + 17] != 6 || png[p + 20] != 0) return false;
      w = static_cast<int>(get32(&png[p + 8]));
      h = static_cast<int>(get32(&png[p + 12]));
      sawHeader = true;
    } else if (!std::memcmp(type, "IDAT", 4)) {
      z.insert(z.end(), &png[p + 8], &png[p + 8] + len);
    }
    p += 12 + len;
  }
  if (!sawHeader || w <= 0 || h <= 0 || z.size() < 6) return false;
  std::vector<uint8_t> raw;
  size_t p = 2;  // skip zlib header
  for (bool last = false; !last;) {
    if (p + 5 > z.size() || (z[p] & 6) != 0) return false;  // only stored blocks
    last = z[p] & 1;
    const size_t n = z[p + 1] | (z[p + 2] << 8);
    if (static_cast<size_t>(z[p + 3] | (z[p + 4] << 8)) != (~n & 0xffff) || p + 5 + n > z.size()) return false;
    raw.insert(raw.end(), z.begin() + p + 5, z.begin() + p + 5 + n);
    p += 5 + n;
  }
  if (p + 4 != z.size() || get32(&z[p]) != adler32(raw.data(), raw.size())) return false;
  const size_t stride = static_cast<size_t>(w) * 4;
  if (raw.size() != (stride + 1) * static_cast<size_t>(h)) return false;
  rgba.resize(stride * h);
  for (int y = 0; y < h; ++y) {
    if (raw[y * (stride + 1)] != 0) return false;
    std::memcpy(&rgba[y * stride], &raw[y * (stride + 1) + 1], stride);
  }
  return true;
}

bool readPngFile(const std::string& path, int& w, int& h, std::vector<uint8_t>& rgba) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  return decodePngRgba(d, w, h, rgba);
}

}  // namespace tf::gfx
