// tenfold-ui [workspace] [--screenshot out.png] [--size WxH]
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "app/Editor.h"
#include "ui/gfx/GlLoader.h"
#include "ui/gfx/GlRenderer.h"
#include "ui/gfx/PngWrite.h"
#include "ui/platform/Window.h"

using namespace tf;

namespace {

// A synthetic 64x64 R8 "atlas": a radial blob per 16x16 cell, enough to see coverage * colour work.
gfx::TextureId makeSyntheticAtlas(gfx::Renderer& r) {
  std::vector<uint8_t> px(64 * 64);
  for (int y = 0; y < 64; ++y)
    for (int x = 0; x < 64; ++x) {
      const int cx = x % 16 - 8, cy = y % 16 - 8, d2 = cx * cx + cy * cy;
      px[y * 64 + x] = d2 < 36 ? static_cast<uint8_t>(255 - d2 * 255 / 36) : 0;
    }
  gfx::TextureId t = r.createTexture(64, 64, gfx::TextureFormat::R8);
  r.updateTexture(t, 0, 0, 64, 64, px.data(), 64);
  return t;
}

void drawScene(gfx::Renderer& r, int w, int h, gfx::TextureId atlas, float mx, float my) {
  r.beginFrame(w, h, {0.10f, 0.11f, 0.13f, 1});
  r.drawRect({40, 40, 220, 120}, {0.85f, 0.35f, 0.20f, 1});
  r.drawRect({140, 100, 220, 120}, {0.20f, 0.55f, 0.90f, 0.6f});  // translucent overlap
  for (int i = 0; i < 6; ++i) r.drawLine(300, 50.0f + i * 20, 520, 60.0f + i * 35, 1.0f + i, {0.9f, 0.9f, 0.4f, 1});
  r.pushScissor({60, 220, 200, 100});
  r.drawRect({0, 0, 1000, 1000}, {0.15f, 0.30f, 0.20f, 1});  // clipped to the scissor
  r.drawLine(0, 200, 400, 400, 6, {1, 1, 1, 1});
  r.popScissor();
  std::vector<gfx::GlyphQuad> quads;
  for (int i = 0; i < 16; ++i) {
    const float u = (i % 4) / 4.0f, v = (i / 4) / 4.0f, x = 300.0f + i * 20, y = 300.0f;
    quads.push_back({x, y, x + 16, y + 16, u, v, u + 0.25f, v + 0.25f});
  }
  r.drawGlyphs(atlas, quads.data(), static_cast<int>(quads.size()), {0.95f, 0.95f, 0.95f, 1});
  r.drawRect({mx - 4, my - 4, 8, 8}, {1, 0.2f, 0.8f, 1});
  r.endFrame();
}

}  // namespace

int main(int argc, char** argv) {
  std::string workspace, shot;
  int width = 1024, height = 640;
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--screenshot") && i + 1 < argc) {
      shot = argv[++i];
    } else if (!std::strcmp(argv[i], "--size") && i + 1 < argc) {
      if (std::sscanf(argv[++i], "%dx%d", &width, &height) != 2 || width < 1 || height < 1) {
        std::fprintf(stderr, "bad --size (expected WxH)\n");
        return 2;
      }
    } else if (argv[i][0] != '-' && workspace.empty()) {
      workspace = argv[i];
    } else {
      std::fprintf(stderr, "usage: tenfold-ui [workspace] [--screenshot out.png] [--size WxH]\n");
      return 2;
    }
  }

  ui::WindowOptions wo;
  wo.title = "tenfold";
  wo.width = width;
  wo.height = height;
  wo.hidden = !shot.empty();
  wo.vsync = shot.empty();
  std::string err;
  auto win = ui::Window::create(wo, &err);
  if (!win || !gfx::gl::load(&ui::Window::glProc, &err)) {
    std::fprintf(stderr, "tenfold-ui: %s\n", err.c_str());
    return 1;
  }
  auto renderer = gfx::GlRenderer::create(&err);
  if (!renderer) {
    std::fprintf(stderr, "tenfold-ui: %s\n", err.c_str());
    return 1;
  }
  std::unique_ptr<Editor> editor;  // created only; wiring it to views is B2+
  if (!workspace.empty()) editor = std::make_unique<Editor>(workspace);
  const gfx::TextureId atlas = makeSyntheticAtlas(*renderer);

  if (!shot.empty()) {
    renderer->setOffscreen(true);
    drawScene(*renderer, width, height, atlas, -100, -100);
    std::vector<uint8_t> px(static_cast<size_t>(width) * height * 4);
    if (!renderer->readPixels(width, height, px.data()) || !gfx::writePngFile(shot, px.data(), width, height)) {
      std::fprintf(stderr, "tenfold-ui: failed to write %s\n", shot.c_str());
      return 1;
    }
    std::printf("wrote %s (%dx%d)\n", shot.c_str(), width, height);
    return 0;
  }

  // Event-driven loop: block until input arrives, redraw only when something changed.
  float mx = -100, my = -100;
  bool dirty = true, quit = false;
  std::vector<ui::InputEvent> events;
  while (!quit) {
    if (dirty) {
      int pw, ph;
      win->sizePx(pw, ph);
      drawScene(*renderer, pw, ph, atlas, mx, my);
      win->swap();
      dirty = false;
    }
    events.clear();
    win->waitEvent(-1, events);
    for (const ui::InputEvent& e : events) {
      switch (e.type) {
        case ui::InputType::Quit: quit = true; break;
        case ui::InputType::KeyDown:
          if (e.key == ui::Key::Escape) quit = true;
          break;
        case ui::InputType::MouseMove:
          mx = e.x;
          my = e.y;
          dirty = true;
          break;
        case ui::InputType::Resize:
        case ui::InputType::Expose:
        case ui::InputType::ScaleChanged: dirty = true; break;
        default: break;
      }
    }
  }
  return 0;
}
