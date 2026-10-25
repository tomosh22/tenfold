#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "ui/gfx/Renderer.h"

namespace tf::gfx {

// OpenGL 3.3 core renderer.
//
// Colour/blending model (documented choice): colours are sRGB-encoded values taken as-is and blended
// in the (non-sRGB) framebuffer, i.e. "gamma-space" blending with STRAIGHT (non-premultiplied) alpha:
// rgb = src.rgb * src.a + dst.rgb * (1 - src.a). This is what text editors conventionally do and keeps
// glyph weight identical to what the colour picker shows; a linear-light pipeline would make light-on-dark
// text look thinner. Glyph coverage multiplies the colour's alpha. Destination alpha accumulates with
// the same over operator, so an opaque clear colour stays opaque.
//
// Batching: all draw calls of a frame are appended to one vertex array (6 vertices per quad) and
// uploaded once in endFrame(). Consecutive calls with the same texture and scissor merge into one
// glDrawArrays, so rects and lines (white 1x1 texture) share a batch and draw order is preserved.
// All coordinates are pixels, origin top-left, y down.
class GlRenderer final : public Renderer {
 public:
  // The GL context must be current and gl::load() must have succeeded.
  static std::unique_ptr<GlRenderer> create(std::string* err = nullptr);
  ~GlRenderer() override;

  // Offscreen mode renders into a renderer-owned FBO (size follows beginFrame) instead of the
  // window's default framebuffer; used for headless screenshots and golden tests.
  void setOffscreen(bool on) { offscreen_ = on; }

  struct Stats {
    int drawCalls = 0;
    int vertices = 0;
  };
  Stats lastFrameStats() const { return stats_; }

  void beginFrame(int widthPx, int heightPx, Color clear) override;
  void drawRect(const RectF& r, Color c) override;
  void drawLine(float x0, float y0, float x1, float y1, float thickness, Color c) override;
  void drawGlyphs(TextureId atlas, const GlyphQuad* quads, int count, Color c) override;
  void pushScissor(const RectF& r) override;
  void popScissor() override;
  TextureId createTexture(int w, int h, TextureFormat f) override;
  void updateTexture(TextureId t, int x, int y, int w, int h, const void* pixels, int rowBytes) override;
  void destroyTexture(TextureId t) override;
  void endFrame() override;
  bool readPixels(int w, int h, uint8_t* rgba) override;

 private:
  GlRenderer() = default;
  struct Vertex {
    float x, y, u, v;
    uint8_t r, g, b, a;
  };
  struct IRect {
    int x0, y0, x1, y1;
  };  // top-left pixel coordinates, half-open
  struct Cmd {
    unsigned tex;
    bool rgba;
    bool scissor;
    IRect sc;
    int first, count;
  };

  void quad(unsigned tex, bool rgba, const Vertex& a, const Vertex& b, const Vertex& c, const Vertex& d);
  void bindTarget();

  unsigned prog_ = 0, vao_ = 0, vbo_ = 0, white_ = 0, fbo_ = 0, fboTex_ = 0;
  int uView_ = -1, uTex_ = -1, uRgba_ = -1;
  int w_ = 0, h_ = 0, fboW_ = 0, fboH_ = 0;
  bool offscreen_ = false;
  std::vector<Vertex> verts_;
  std::vector<Cmd> cmds_;
  std::vector<IRect> scissors_;
  std::unordered_map<unsigned, TextureFormat> textures_;
  Stats stats_;
};

}  // namespace tf::gfx
