#pragma once
#include <memory>
#include <string>
#include <vector>

#include "ui/gfx/Renderer.h"

namespace tf::gfx {

class WindowsSwapChain;

// Direct3D 11 renderer (Windows only; D3d11Renderer.cpp is empty elsewhere). Behaves like GlRenderer:
//
//  * Colours are sRGB-encoded values blended as-is in a non-sRGB (R8G8B8A8_UNORM) target, "gamma-space" blending
//    with STRAIGHT alpha: rgb = src.rgb * a + dst.rgb * (1 - a); destination alpha accumulates with the same over
//    operator. R8 textures are coverage masks (colour comes from the vertex, coverage multiplies alpha), RGBA8
//    textures modulate the vertex colour.
//  * All draw calls of a frame go into one vertex array and are uploaded once in endFrame(); consecutive calls
//    with the same texture and scissor merge into one Draw, so draw order is preserved. Rects and lines use a 1x1
//    coverage-1 R8 texture and share batches with glyphs.
//  * Scissors round outwards to whole pixels and intersect with the enclosing one. Coordinates: pixels, top-left.
//
// Shaders: HLSL source embedded in the .cpp and compiled at start-up with D3DCompile (d3dcompiler_47.dll ships
// with Windows 10+), so the build needs neither fxc nor dxc. Two pixel shaders (coverage / modulate) avoid per-draw
// constants.
//
// Targets: with an HWND the renderer owns a flip-model swap chain (see WindowsSwapChain) and renders into its
// backbuffer; call present() after endFrame(). Offscreen mode (always on without an HWND) renders into a
// renderer-owned texture instead, for screenshots and golden tests.
class D3d11Renderer final : public Renderer {
 public:
  struct Options {
    void* hwnd = nullptr;    // null = offscreen only
    bool vsync = true;       // present() sync interval
    bool forceWarp = false;  // skip the hardware adapter (deterministic output, headless CI)
  };
  // Hardware device first, then WARP (software). Env TENFOLD_D3D11_WARP=1 behaves like forceWarp.
  static std::unique_ptr<D3d11Renderer> create(const Options& opt, std::string* err = nullptr);
  ~D3d11Renderer() override;

  void setOffscreen(bool on);  // ignored (stays on) when there is no swap chain
  void setVsync(bool on) { vsync_ = on; }
  // Flip the backbuffer to the screen. Returns false if the device was lost. No-op offscreen.
  bool present();
  const std::string& adapterName() const { return adapter_; }
  bool isWarp() const { return warp_; }

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
  D3d11Renderer();
  struct Impl;  // all COM objects
  struct Vertex {
    float x, y, u, v;
    uint8_t r, g, b, a;
  };
  struct IRect {
    int x0, y0, x1, y1;
  };  // top-left pixel coordinates, half-open
  struct Cmd {
    TextureId tex;
    bool rgba;
    bool scissor;
    IRect sc;
    int first, count;
  };

  void quad(TextureId tex, bool rgba, const Vertex& a, const Vertex& b, const Vertex& c, const Vertex& d);

  std::unique_ptr<Impl> d_;
  std::unique_ptr<WindowsSwapChain> swap_;
  TextureId white_ = 0, nextId_ = 1;
  int w_ = 0, h_ = 0;
  bool offscreen_ = true, vsync_ = true, warp_ = false;
  std::string adapter_;
  std::vector<Vertex> verts_;
  std::vector<Cmd> cmds_;
  std::vector<IRect> scissors_;
  Stats stats_;
};

}  // namespace tf::gfx
