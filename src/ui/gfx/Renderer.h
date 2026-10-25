#pragma once
// Copy of the interface owned by B1a (renderer branch); the orchestrator keeps B1a's version on merge.
#include <cstdint>

namespace tf::gfx {
struct Color {
  float r, g, b, a;
};
struct RectF {
  float x, y, w, h;
};
using TextureId = uint32_t;
enum class TextureFormat { R8, RGBA8 };
struct GlyphQuad {
  float x0, y0, x1, y1;
  float u0, v0, u1, v1;
};  // pixels, atlas UVs in [0,1]

class Renderer {
 public:
  virtual ~Renderer() = default;
  virtual void beginFrame(int widthPx, int heightPx, Color clear) = 0;
  virtual void drawRect(const RectF&, Color) = 0;
  virtual void drawLine(float x0, float y0, float x1, float y1, float thickness, Color c) = 0;
  virtual void drawGlyphs(TextureId atlas, const GlyphQuad* quads, int count, Color c) = 0;
  virtual void pushScissor(const RectF&) = 0;
  virtual void popScissor() = 0;
  virtual TextureId createTexture(int w, int h, TextureFormat f) = 0;
  virtual void updateTexture(TextureId t, int x, int y, int w, int h, const void* pixels, int rowBytes) = 0;
  virtual void destroyTexture(TextureId t) = 0;
  virtual void endFrame() = 0;
  virtual bool readPixels(int w, int h, uint8_t* rgba) = 0;
};
}  // namespace tf::gfx
