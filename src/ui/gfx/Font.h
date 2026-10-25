#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace tf::gfx {

// Rasterization mode. Only Grey is implemented: FT_LOAD_TARGET_LIGHT (vertical-only auto-hinting, keeps glyph
// shapes and advances close to the design) + FT_RENDER_MODE_NORMAL (8-bit grey coverage). Lcd is a hook for the
// Windows subpixel option (needs an RGB atlas and a dual-source blend in the renderer); it currently behaves as Grey.
enum class RasterMode { Grey, Lcd };

struct FontOptions {
  float sizePx = 15.0f;   // nominal size in logical pixels
  float dpiScale = 1.0f;  // effective pixel size = sizePx * dpiScale
  RasterMode mode = RasterMode::Grey;
  int faceIndex = 0;  // for .ttc collections
};

// All values in device pixels. ascender/descender/underlinePos are positive distances from the baseline (up, down,
// down). advance is the monospace cell width, rounded to a whole pixel so cell columns stay on the pixel grid.
struct FontMetrics {
  float ascender = 0, descender = 0, lineHeight = 0, advance = 0, underlinePos = 0, underlineThickness = 0;
};

// Tightly packed R8 coverage. bearingX: pixels from the pen to the bitmap's left edge; bearingY: pixels from the
// baseline up to the bitmap's top edge. Empty (width == 0) for blank glyphs such as space.
struct GlyphBitmap {
  int width = 0, height = 0, bearingX = 0, bearingY = 0;
  float advance = 0;
  std::vector<uint8_t> pixels;
};

// Glyph index meaning "draw the box": synthesized by FontSource::rasterize, valid for any source.
constexpr uint32_t kNotdefGlyph = 0xFFFFFFFFu;

// A rasterizable font at one size. FtFont is the real implementation; tests use mocks.
class FontSource {
 public:
  FontSource();
  virtual ~FontSource() = default;
  FontSource(const FontSource&) = delete;
  FontSource& operator=(const FontSource&) = delete;

  uint32_t id() const { return id_; }  // process-unique, part of the atlas key
  virtual const FontMetrics& metrics() const = 0;
  virtual uint32_t sizeKey() const = 0;                // effective size, quantized; part of the atlas key
  virtual uint32_t glyphIndex(char32_t cp) const = 0;  // 0 = not in this font
  GlyphBitmap rasterize(uint32_t glyph);               // handles kNotdefGlyph

 protected:
  virtual GlyphBitmap rasterizeGlyph(uint32_t glyph) = 0;

 private:
  uint32_t id_;
};

class FtFont final : public FontSource {
 public:
  static std::unique_ptr<FtFont> fromFile(const std::string& path, const FontOptions& opt, std::string* err = nullptr);
  static std::unique_ptr<FtFont> fromMemory(std::vector<uint8_t> data, const FontOptions& opt,
                                            std::string* err = nullptr);
  ~FtFont() override;
  const FontMetrics& metrics() const override;
  uint32_t sizeKey() const override;
  uint32_t glyphIndex(char32_t cp) const override;

 protected:
  GlyphBitmap rasterizeGlyph(uint32_t glyph) override;

 private:
  struct Impl;
  explicit FtFont(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

struct GlyphRef {
  FontSource* font = nullptr;
  uint32_t glyph = 0;
};

// Primary font plus fallbacks; the first font that has the codepoint wins. A codepoint missing everywhere maps to the
// primary font's notdef box. The primary font defines the cell metrics. Not thread-safe (lookup caches ASCII).
class FontStack {
 public:
  void add(std::shared_ptr<FontSource> font);
  size_t size() const { return fonts_.size(); }
  const FontMetrics& metrics() const { return fonts_.front()->metrics(); }
  GlyphRef lookup(char32_t cp) const;

 private:
  std::vector<std::shared_ptr<FontSource>> fonts_;
  mutable GlyphRef ascii_[128];
  mutable bool asciiValid_[128] = {};
};

// Font discovery. Returns `configured` when it names an existing file; otherwise the first known monospace font found
// on the platform (Linux: scans /usr/share/fonts and ~/.local/share/fonts for JetBrainsMono, DejaVuSansMono,
// LiberationMono, ...; Windows: consola.ttf; macOS: Menlo.ttc). Empty string when nothing is found.
std::string discoverMonospaceFont(const std::string& configured);
// First existing file in the platform's wide-coverage fallback list (CJK/symbols); empty when none.
std::string discoverFallbackFont();

}  // namespace tf::gfx
