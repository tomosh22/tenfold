// tenfold-ui [workspace] [file] [--demo] [--shell] [--screenshot out.png] [--size WxH]
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "app/Editor.h"
#include "ui/gfx/GlLoader.h"
#include "ui/gfx/GlRenderer.h"
#include "ui/gfx/PngWrite.h"
#include "ui/gfx/TextRenderer.h"
#include "ui/platform/Window.h"
#include "ui/widgets/ShellDemo.h"
#include "ui/widgets/TextView.h"

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

constexpr const char *kDefaultWorkspace = "examples/sample_project", *kDefaultFile = "src/main.cpp";

int64_t nowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

}  // namespace

int main(int argc, char** argv) {
  std::string workspace, file, shot;
  bool demo = false, shell = false;
  int width = 1024, height = 640;
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--screenshot") && i + 1 < argc) {
      shot = argv[++i];
    } else if (!std::strcmp(argv[i], "--size") && i + 1 < argc) {
      if (std::sscanf(argv[++i], "%dx%d", &width, &height) != 2 || width < 1 || height < 1) {
        std::fprintf(stderr, "bad --size (expected WxH)\n");
        return 2;
      }
    } else if (!std::strcmp(argv[i], "--demo")) {
      demo = true;
    } else if (!std::strcmp(argv[i], "--shell")) {
      shell = true;
    } else if (argv[i][0] != '-' && workspace.empty()) {
      workspace = argv[i];
    } else if (argv[i][0] != '-' && file.empty()) {
      file = argv[i];
    } else {
      std::fprintf(stderr,
                   "usage: tenfold-ui [workspace] [file] [--demo] [--shell] [--screenshot out.png] [--size WxH]\n");
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
  std::unique_ptr<Editor> editor;  // editor mode only; the --shell demo owns its own
  const gfx::TextureId atlas = demo ? makeSyntheticAtlas(*renderer) : 0;

  std::unique_ptr<gfx::TextRenderer> text;
  std::unique_ptr<ui::RootWidget> root;  // ShellDemo (--shell) or EditorShell (default)
  if (shell || !demo) {
    text = gfx::TextRenderer::create("", 15.0f, win->pixelScale(), &err);  // font setting arrives with B7
    if (!text) {
      std::fprintf(stderr, "tenfold-ui: %s\n", err.c_str());
      return 1;
    }
  }
  const ui::Theme theme = ui::Theme::darkDefault().scaled(win->pixelScale());
  if (shell) {  // B3a widget shell demo; the sample project directory is the workspace argument or a default
    root = std::make_unique<ui::ShellDemo>(*text, workspace.empty() ? kDefaultWorkspace : workspace, theme);
  } else if (!demo) {  // the editor: <workspace> [file], the file relative to the workspace or to the cwd
    namespace fs = std::filesystem;
    if (workspace.empty())
      workspace = file.empty() || !fs::exists(file) ? kDefaultWorkspace : fs::path(file).parent_path().string();
    if (workspace.empty()) workspace = ".";
    if (file.empty()) file = kDefaultFile;
    fs::path path = fs::absolute(fs::path(workspace) / file);
    if (!fs::exists(path) && fs::exists(file)) path = fs::absolute(file);
    editor = std::make_unique<Editor>(fs::absolute(workspace));
    Document* doc = editor->open(path, &err);
    if (!doc) {
      std::fprintf(stderr, "tenfold-ui: cannot open %s: %s\n", path.string().c_str(), err.c_str());
      return 1;
    }
    editor->gotoLocation({doc->path(), {0, 0}});
    if (shot.empty() && !editor->start(&err)) std::fprintf(stderr, "tenfold-ui: editor start: %s\n", err.c_str());
    root = std::make_unique<ui::EditorShell>(*text, *editor, theme);
  }
  auto drawFrame = [&](int w, int h, float mx, float my) {
    if (root) root->render(*renderer, w, h);
    else drawScene(*renderer, w, h, atlas, mx, my);
  };

  if (!shot.empty()) {
    renderer->setOffscreen(true);
    if (root) root->tick(nowMs());
    drawFrame(width, height, -100, -100);
    std::vector<uint8_t> px(static_cast<size_t>(width) * height * 4);
    if (!renderer->readPixels(width, height, px.data()) || !gfx::writePngFile(shot, px.data(), width, height)) {
      std::fprintf(stderr, "tenfold-ui: failed to write %s\n", shot.c_str());
      return 1;
    }
    std::printf("wrote %s (%dx%d)\n", shot.c_str(), width, height);
    return 0;
  }

  // Event-driven loop: block until input arrives (or until a widget wants a tick: caret blink, scroll animation, or
  // the Editor's queue needs pumping), redraw only when something changed.
  float mx = -100, my = -100;
  bool dirty = true, quit = false;
  std::vector<ui::InputEvent> events;
  while (!quit) {
    int wait = -1;
    if (editor) editor->pump();
    if (root) {
      wait = root->tick(nowMs());
      if (editor && (wait < 0 || wait > 100)) wait = 100;  // poll the Editor's main-thread queue
      if (root->dirty()) dirty = true;
    }
    if (dirty) {
      int pw, ph;
      win->sizePx(pw, ph);
      drawFrame(pw, ph, mx, my);
      win->swap();
      dirty = false;
    }
    events.clear();
    win->waitEvent(wait, events);
    for (const ui::InputEvent& e : events) {
      const bool ctrlQ = e.type == ui::InputType::KeyDown && e.key == ui::Key('Q') && (e.mods & ui::ModCtrl);
      if (e.type == ui::InputType::Quit || ctrlQ) {
        quit = true;
      } else if (e.type == ui::InputType::Resize || e.type == ui::InputType::Expose ||
                 e.type == ui::InputType::ScaleChanged) {
        dirty = true;
      } else if (root) {
        // Escape quits the shell demo; in the editor it belongs to the view (collapse carets), Ctrl+Q quits.
        if (!root->dispatch(e) && shell && e.type == ui::InputType::KeyDown && e.key == ui::Key::Escape) quit = true;
      } else if (e.type == ui::InputType::KeyDown && e.key == ui::Key::Escape) {
        quit = true;
      } else if (e.type == ui::InputType::MouseMove) {
        mx = e.x;
        my = e.y;
        dirty = true;  // the demo scene follows the pointer
      }
    }
  }
  if (editor) editor->shutdown();
  return 0;
}
