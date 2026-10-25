#pragma once
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "ui/gfx/Font.h"
#include "ui/gfx/Renderer.h"

namespace tf::gfx {

struct AtlasRect {
  int x = 0, y = 0, w = 0, h = 0;
};

// A rasterized glyph living in the atlas. (x, y, w, h) is the bitmap's pixel rect; w == 0 means blank (draw nothing).
struct AtlasGlyph {
  int x = 0, y = 0, w = 0, h = 0;
  int bearingX = 0, bearingY = 0;
  float advance = 0;
};

// Single-channel (R8) glyph atlas on the CPU, uploaded through Renderer::updateTexture.
//
// Packing: shelf packer. Each glyph reserves its bitmap plus 1px padding on every side; it goes on the existing shelf
// with the least wasted height that still has room, else opens a new shelf (heights rounded up to a multiple of 4).
// Growth: when nothing fits, the square atlas doubles (up to maxSize). Existing glyphs keep their pixel positions (no
// repack), so cached AtlasGlyph pointers/rects stay valid; only UVs change, hence generation() and the "convert to UV
// last" rule in TextLayout. Growth recreates the texture and re-uploads everything on the next flush.
// Eviction: none (no LRU). Glyphs live as long as the atlas; if maxSize is exhausted, further glyphs are cached as
// blank. Fine for a monospace code font; revisit if CJK-heavy documents show up.
// Dirty tracking: every write records its rect (merged with the previous one when on the same row band and no more
// than the 2px padding gap away, so a run of glyphs on a shelf uploads as one block).
// Not thread-safe.
class Atlas {
 public:
  explicit Atlas(int initialSize = 512, int maxSize = 4096);

  // Cached lookup; rasterizes and packs on a miss. Key: (font id, glyph index, font size). Never null.
  const AtlasGlyph* glyph(FontSource& font, uint32_t glyphIndex);

  // Low-level packer, public for tests. Returns false only when maxSize is exhausted.
  bool allocate(int w, int h, AtlasRect& out);
  // Copies a w*h block into the atlas at r and marks it dirty.
  void write(const AtlasRect& r, const uint8_t* src, int srcPitch);

  int size() const { return size_; }
  uint32_t generation() const { return generation_; }  // bumped on growth
  const uint8_t* pixels() const { return pixels_.data(); }
  const std::vector<AtlasRect>& dirtyRects() const { return dirty_; }
  size_t glyphCount() const { return glyphs_.size(); }
  TextureId texture() const { return texture_; }

  // Creates the texture on first use (and after growth), then uploads only the dirty rects. A (re)created texture is
  // uploaded in full once because its initial contents are undefined.
  void flush(Renderer& r);
  void releaseTexture(Renderer& r);

 private:
  struct Key {
    uint32_t font, glyph, size;
    bool operator==(const Key& o) const { return font == o.font && glyph == o.glyph && size == o.size; }
  };
  struct KeyHash {
    size_t operator()(const Key& k) const {
      uint64_t h = (static_cast<uint64_t>(k.font) << 32 | k.glyph) * 0x9E3779B97F4A7C15ull;
      return static_cast<size_t>((h ^ (h >> 29)) + k.size * 0x85EBCA6Bu);
    }
  };
  struct Shelf {
    int y, h, x;
  };

  bool grow();

  int size_, maxSize_;
  uint32_t generation_ = 0;
  std::vector<uint8_t> pixels_;
  std::vector<Shelf> shelves_;
  int nextShelfY_ = 0;
  std::vector<AtlasRect> dirty_;
  std::unordered_map<Key, AtlasGlyph, KeyHash> glyphs_;  // node-based: returned pointers stay valid
  TextureId texture_ = 0;
  int textureSize_ = 0;
};

}  // namespace tf::gfx
