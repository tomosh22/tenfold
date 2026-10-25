#include "ui/gfx/Atlas.h"

#include <algorithm>
#include <cstring>

namespace tf::gfx {

Atlas::Atlas(int initialSize, int maxSize)
    : size_(std::max(64, initialSize)),
      maxSize_(std::max(size_, maxSize)),
      pixels_(static_cast<size_t>(size_) * size_, 0) {}

bool Atlas::grow() {
  if (size_ >= maxSize_) return false;
  int ns = std::min(size_ * 2, maxSize_);
  std::vector<uint8_t> np(static_cast<size_t>(ns) * ns, 0);
  for (int y = 0; y < size_; ++y)
    std::memcpy(&np[static_cast<size_t>(y) * ns], &pixels_[static_cast<size_t>(y) * size_], size_);
  pixels_ = std::move(np);
  size_ = ns;
  ++generation_;
  dirty_.clear();  // the whole texture is re-uploaded after growth
  return true;
}

bool Atlas::allocate(int w, int h, AtlasRect& out) {
  const int pw = w + 2, ph = h + 2;  // 1px padding on every side
  for (;;) {
    if (pw <= size_) {
      Shelf* best = nullptr;
      for (Shelf& s : shelves_)
        if (s.h >= ph && s.x + pw <= size_ && (!best || s.h < best->h)) best = &s;
      if (!best) {
        int sh = (ph + 3) & ~3;
        if (nextShelfY_ + sh <= size_) {
          shelves_.push_back({nextShelfY_, sh, 0});
          nextShelfY_ += sh;
          best = &shelves_.back();
        }
      }
      if (best) {
        out = {best->x + 1, best->y + 1, w, h};
        best->x += pw;
        return true;
      }
    }
    if (!grow()) return false;
  }
}

void Atlas::write(const AtlasRect& r, const uint8_t* src, int srcPitch) {
  for (int y = 0; y < r.h; ++y)
    std::memcpy(&pixels_[static_cast<size_t>(r.y + y) * size_ + r.x], src + static_cast<size_t>(y) * srcPitch, r.w);
  if (!dirty_.empty()) {
    AtlasRect& l = dirty_.back();
    if (l.y == r.y && l.h == r.h && r.x >= l.x && r.x <= l.x + l.w + 2) {
      l.w = std::max(l.x + l.w, r.x + r.w) - l.x;
      return;
    }
  }
  dirty_.push_back(r);
}

const AtlasGlyph* Atlas::glyph(FontSource& font, uint32_t glyphIndex) {
  Key key{font.id(), glyphIndex, font.sizeKey()};
  auto it = glyphs_.find(key);
  if (it != glyphs_.end()) return &it->second;
  GlyphBitmap bm = font.rasterize(glyphIndex);
  AtlasGlyph g;
  g.bearingX = bm.bearingX;
  g.bearingY = bm.bearingY;
  g.advance = bm.advance;
  if (bm.width > 0) {
    AtlasRect r;
    if (allocate(bm.width, bm.height, r)) {
      write(r, bm.pixels.data(), bm.width);
      g.x = r.x;
      g.y = r.y;
      g.w = r.w;
      g.h = r.h;
    }  // else: atlas full, cached as blank
  }
  return &glyphs_.emplace(key, g).first->second;
}

void Atlas::flush(Renderer& r) {
  bool full = false;
  if (texture_ == 0 || textureSize_ != size_) {
    if (texture_ != 0) r.destroyTexture(texture_);
    texture_ = r.createTexture(size_, size_, TextureFormat::R8);
    textureSize_ = size_;
    full = true;
  }
  if (full) {
    r.updateTexture(texture_, 0, 0, size_, size_, pixels_.data(), size_);
  } else {
    for (const AtlasRect& d : dirty_)
      r.updateTexture(texture_, d.x, d.y, d.w, d.h, &pixels_[static_cast<size_t>(d.y) * size_ + d.x], size_);
  }
  dirty_.clear();
}

void Atlas::releaseTexture(Renderer& r) {
  if (texture_ != 0) r.destroyTexture(texture_);
  texture_ = 0;
  textureSize_ = 0;
}

}  // namespace tf::gfx
