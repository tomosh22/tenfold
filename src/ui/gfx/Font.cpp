#include "ui/gfx/Font.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace tf::gfx {

FontSource::FontSource() {
  static std::atomic<uint32_t> next{1};
  id_ = next.fetch_add(1);
}

GlyphBitmap FontSource::rasterize(uint32_t glyph) {
  if (glyph != kNotdefGlyph) return rasterizeGlyph(glyph);
  // Hollow box, 1px stroke, sized from the metrics so it works for any source.
  const FontMetrics& m = metrics();
  GlyphBitmap b;
  b.width = std::max(3, static_cast<int>(m.advance) - 2);
  b.height = std::max(3, static_cast<int>(m.ascender * 0.75f));
  b.bearingX = 1;
  b.bearingY = b.height;
  b.advance = m.advance;
  b.pixels.assign(static_cast<size_t>(b.width) * b.height, 0);
  for (int y = 0; y < b.height; ++y)
    for (int x = 0; x < b.width; ++x)
      if (x == 0 || y == 0 || x == b.width - 1 || y == b.height - 1)
        b.pixels[static_cast<size_t>(y) * b.width + x] = 255;
  return b;
}

struct FtFont::Impl {
  FT_Library lib = nullptr;
  FT_Face face = nullptr;
  std::vector<uint8_t> data;
  FontMetrics metrics;
  uint32_t sizeKey = 0;
  ~Impl() {
    if (face) FT_Done_Face(face);
    if (lib) FT_Done_FreeType(lib);
  }
};

FtFont::FtFont(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
FtFont::~FtFont() = default;
const FontMetrics& FtFont::metrics() const { return impl_->metrics; }
uint32_t FtFont::sizeKey() const { return impl_->sizeKey; }
uint32_t FtFont::glyphIndex(char32_t cp) const { return FT_Get_Char_Index(impl_->face, static_cast<FT_ULong>(cp)); }

std::unique_ptr<FtFont> FtFont::fromMemory(std::vector<uint8_t> data, const FontOptions& opt, std::string* err) {
  auto fail = [&](const char* msg) -> std::unique_ptr<FtFont> {
    if (err) *err = msg;
    return nullptr;
  };
  auto impl = std::make_unique<Impl>();
  impl->data = std::move(data);
  if (FT_Init_FreeType(&impl->lib)) return fail("FT_Init_FreeType failed");
  if (FT_New_Memory_Face(impl->lib, impl->data.data(), static_cast<FT_Long>(impl->data.size()), opt.faceIndex,
                         &impl->face))
    return fail("not a loadable font face");
  FT_Face face = impl->face;
  if (!FT_IS_SCALABLE(face)) return fail("font is not scalable");
  float eff = std::max(1.0f, opt.sizePx * opt.dpiScale);
  // 72 dpi makes one point equal one pixel, so the 26.6 size is the effective pixel size (fractions allowed).
  if (FT_Set_Char_Size(face, 0, static_cast<FT_F26Dot6>(std::lround(eff * 64.0f)), 72, 72)) return fail("bad size");
  impl->sizeKey = static_cast<uint32_t>(std::lround(eff * 64.0f));

  const FT_Size_Metrics& sm = face->size->metrics;
  FontMetrics& m = impl->metrics;
  m.ascender = std::round(sm.ascender / 64.0f);
  m.descender = std::round(-sm.descender / 64.0f);
  m.lineHeight = std::max(m.ascender + m.descender, std::round(sm.height / 64.0f));
  float adv = 0;
  if (FT_Get_Char_Index(face, '0') && !FT_Load_Char(face, '0', FT_LOAD_TARGET_LIGHT | FT_LOAD_NO_BITMAP))
    adv = face->glyph->advance.x / 64.0f;
  if (adv <= 0) adv = sm.max_advance / 64.0f;
  m.advance = std::max(1.0f, std::round(adv));
  m.underlinePos = std::round(-FT_MulFix(face->underline_position, sm.y_scale) / 64.0f);
  m.underlineThickness = std::max(1.0f, std::round(FT_MulFix(face->underline_thickness, sm.y_scale) / 64.0f));
  if (face->underline_position == 0) m.underlinePos = std::max(1.0f, std::round(m.descender * 0.5f));
  return std::unique_ptr<FtFont>(new FtFont(std::move(impl)));
}

std::unique_ptr<FtFont> FtFont::fromFile(const std::string& path, const FontOptions& opt, std::string* err) {
  std::ifstream f(std::filesystem::path(path), std::ios::binary);
  if (!f) {
    if (err) *err = "cannot open " + path;
    return nullptr;
  }
  std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  return fromMemory(std::move(data), opt, err);
}

GlyphBitmap FtFont::rasterizeGlyph(uint32_t glyph) {
  GlyphBitmap b;
  FT_Face face = impl_->face;
  // NO_BITMAP: colour/bitmap strikes (emoji) are not supported in the R8 atlas; they come out blank.
  if (FT_Load_Glyph(face, glyph, FT_LOAD_TARGET_LIGHT | FT_LOAD_NO_BITMAP)) return b;
  if (FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL)) return b;
  const FT_GlyphSlot slot = face->glyph;
  const FT_Bitmap& bm = slot->bitmap;
  b.advance = slot->advance.x / 64.0f;
  if (bm.width == 0 || bm.rows == 0 || bm.pixel_mode != FT_PIXEL_MODE_GRAY) return b;
  b.width = static_cast<int>(bm.width);
  b.height = static_cast<int>(bm.rows);
  b.bearingX = slot->bitmap_left;
  b.bearingY = slot->bitmap_top;
  b.pixels.resize(static_cast<size_t>(b.width) * b.height);
  for (int y = 0; y < b.height; ++y) {
    const unsigned char* src = bm.buffer + static_cast<ptrdiff_t>(y) * bm.pitch;  // pitch may be negative
    std::copy(src, src + b.width, b.pixels.begin() + static_cast<ptrdiff_t>(y) * b.width);
  }
  return b;
}

void FontStack::add(std::shared_ptr<FontSource> font) {
  fonts_.push_back(std::move(font));
  std::fill(std::begin(asciiValid_), std::end(asciiValid_), false);
}

GlyphRef FontStack::lookup(char32_t cp) const {
  if (cp < 128 && asciiValid_[cp]) return ascii_[cp];
  GlyphRef r{fonts_.front().get(), kNotdefGlyph};
  for (const auto& f : fonts_) {
    uint32_t g = f->glyphIndex(cp);
    if (g != 0) {
      r = {f.get(), g};
      break;
    }
  }
  if (cp < 128) {
    ascii_[cp] = r;
    asciiValid_[cp] = true;
  }
  return r;
}

namespace {
namespace fs = std::filesystem;

bool isFile(const fs::path& p) {
  std::error_code ec;
  return fs::is_regular_file(p, ec);
}

// Searches `roots` recursively; returns the match for the earliest name in priority order.
[[maybe_unused]] std::string findByName(const std::vector<fs::path>& roots, const std::vector<std::string>& names) {
  std::vector<std::string> found(names.size());
  for (const auto& root : roots) {
    std::error_code ec;
    if (!fs::is_directory(root, ec)) continue;
    for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
      std::string fn = it->path().filename().string();
      for (size_t i = 0; i < names.size(); ++i)
        if (found[i].empty() && fn == names[i]) found[i] = it->path().string();
    }
  }
  for (const auto& f : found)
    if (!f.empty()) return f;
  return {};
}

[[maybe_unused]] std::string firstExisting(const std::vector<std::string>& paths) {
  for (const auto& p : paths)
    if (isFile(p)) return p;
  return {};
}
}  // namespace

std::string discoverMonospaceFont(const std::string& configured) {
  if (!configured.empty() && isFile(configured)) return configured;
#if defined(_WIN32)
  return firstExisting({"C:\\Windows\\Fonts\\consola.ttf", "C:\\Windows\\Fonts\\cour.ttf"});
#elif defined(__APPLE__)
  return firstExisting({"/System/Library/Fonts/Menlo.ttc", "/System/Library/Fonts/Monaco.ttf"});
#else
  std::vector<fs::path> roots{"/usr/share/fonts", "/usr/local/share/fonts"};
  if (const char* home = std::getenv("HOME")) roots.push_back(fs::path(home) / ".local/share/fonts");
  return findByName(roots,
                    {"JetBrainsMono-Regular.ttf", "DejaVuSansMono.ttf", "LiberationMono-Regular.ttf", "FreeMono.ttf"});
#endif
}

std::string discoverFallbackFont() {
#if defined(_WIN32)
  return firstExisting({"C:\\Windows\\Fonts\\msgothic.ttc", "C:\\Windows\\Fonts\\seguisym.ttf"});
#elif defined(__APPLE__)
  return firstExisting({"/System/Library/Fonts/Apple Symbols.ttf", "/System/Library/Fonts/PingFang.ttc"});
#else
  return findByName({"/usr/share/fonts"}, {"wqy-zenhei.ttc", "NotoSansCJK-Regular.ttc", "DejaVuSans.ttf"});
#endif
}

}  // namespace tf::gfx
