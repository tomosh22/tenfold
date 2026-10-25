// tenfold-ui [workspace] [file] [--demo] [--shell] [--screenshot out.png] [--size WxH] [--renderer=gl|d3d11]
//   default: the editor (Workbench of TextView tabs, key bindings, layout.json); --shell: the B3a widget demo;
//   --demo: the renderer scene; --renderer: graphics backend (default d3d11 on Windows when built in, else gl)
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
#ifdef _WIN32
#include "debug/VsDebugger_win32.h"
#endif
#include "core/CrashHandler.h"
#include "core/Log.h"
#include "tenfold_version.h"
#include "ui/gfx/D3d11Renderer.h"
#include "ui/gfx/GlLoader.h"
#include "ui/gfx/GlRenderer.h"
#include "ui/gfx/PngWrite.h"
#include "ui/gfx/TextRenderer.h"
#include "ui/platform/Window.h"
#include "ui/settings/KeyBindings.h"
#include "ui/settings/Settings.h"
#include "ui/shell/EditorApp.h"
#include "ui/shell/FontSetup.h"
#include "ui/shell/Workbench.h"
#include "ui/widgets/ShellDemo.h"

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
  tf::installCrashHandler();
  tf::Log::init();  // local diagnostic log (TENFOLD_LOG=debug|info|warn|error|off); flushed again by its atexit hook
  std::string workspace, file, shot;
  bool demo = false, shell = false;
#ifdef TENFOLD_HAVE_D3D11
  bool useD3d11 = true;
#else
  bool useD3d11 = false;
#endif
  int width = 1024, height = 640;
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--screenshot") && i + 1 < argc) {
      shot = argv[++i];
    } else if (!std::strcmp(argv[i], "--size") && i + 1 < argc) {
      if (std::sscanf(argv[++i], "%dx%d", &width, &height) != 2 || width < 1 || height < 1) {
        std::fprintf(stderr, "bad --size (expected WxH)\n");
        return 2;
      }
    } else if (!std::strncmp(argv[i], "--renderer=", 11)) {
      const char* v = argv[i] + 11;
      if (!std::strcmp(v, "gl")) {
        useD3d11 = false;
      } else if (!std::strcmp(v, "d3d11")) {
#ifdef TENFOLD_HAVE_D3D11
        useD3d11 = true;
#else
        std::fprintf(stderr, "tenfold-ui: --renderer=d3d11 is not available in this build\n");
        return 2;
#endif
      } else {
        std::fprintf(stderr, "bad --renderer (expected gl or d3d11)\n");
        return 2;
      }
    } else if (!std::strcmp(argv[i], "--version")) {
      std::printf("%s\n", TENFOLD_VERSION_STRING);
      return 0;
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
                   "usage: tenfold-ui [workspace] [file] [--demo] [--shell] [--screenshot out.png] [--size WxH] "
                   "[--renderer=gl|d3d11] [--version]\n");
      return 2;
    }
  }

  ui::WindowOptions wo;
  wo.title = TENFOLD_VERSION_STRING;  // "tenfold 0.1.0 (commit)"
  wo.width = width;
  wo.height = height;
  wo.hidden = !shot.empty();
  wo.vsync = shot.empty();
  wo.openGl = !useD3d11;
  std::string err;
  auto win = ui::Window::create(wo, &err);
  if (!win || (!useD3d11 && !gfx::gl::load(&ui::Window::glProc, &err))) {
    std::fprintf(stderr, "tenfold-ui: %s\n", err.c_str());
    return 1;
  }
  std::unique_ptr<gfx::Renderer> renderer;
  gfx::GlRenderer* glRenderer = nullptr;
  [[maybe_unused]] gfx::D3d11Renderer* d3dRenderer = nullptr;  // only used with TENFOLD_HAVE_D3D11
  if (useD3d11) {
#ifdef TENFOLD_HAVE_D3D11
    gfx::D3d11Renderer::Options ro;
    ro.hwnd = shot.empty() ? win->nativeHandle() : nullptr;  // screenshots render offscreen: no swap chain
    ro.vsync = shot.empty();
    auto d3d = gfx::D3d11Renderer::create(ro, &err);
    if (d3d) {
      std::fprintf(stderr, "tenfold-ui: Direct3D 11 on %s%s\n", d3d->adapterName().c_str(),
                   d3d->isWarp() ? " (WARP)" : "");
      d3dRenderer = d3d.get();
      renderer = std::move(d3d);
    }
#endif
  } else {
    auto gl = gfx::GlRenderer::create(&err);
    glRenderer = gl.get();
    renderer = std::move(gl);
  }
  if (!renderer) {
    std::fprintf(stderr, "tenfold-ui: %s\n", err.c_str());
    return 1;
  }
  // D3D11 calls stay behind the define: without D3D11 the class is declared but not linked, and an unoptimised build
  // keeps the (never taken) branch.
  auto presentFrame = [&] {
#ifdef TENFOLD_HAVE_D3D11
    if (d3dRenderer) {
      d3dRenderer->present();
      return;
    }
#endif
    win->swap();
  };
  std::unique_ptr<Editor> editor;  // editor mode only; the --shell demo owns its own
  const gfx::TextureId atlas = demo ? makeSyntheticAtlas(*renderer) : 0;

  std::unique_ptr<gfx::TextRenderer> text;
  std::unique_ptr<ui::SettingsService> settings;  // layered settings + theme, hot-reloaded (B7)
  std::unique_ptr<ui::RootWidget> demoRoot;       // --shell: ShellDemo
  std::unique_ptr<ui::Workbench> bench;           // default: the editor
  std::unique_ptr<ui::EditorApp> app;
  namespace fs = std::filesystem;
  if (shell || !demo) {
    if (workspace.empty())
      workspace =
          shell || file.empty() || !fs::exists(file) ? kDefaultWorkspace : fs::path(file).parent_path().string();
    if (workspace.empty()) workspace = ".";
    ui::SettingsService::Options so;
    so.workspaceRoot = fs::absolute(workspace);
    so.userDir = ui::userConfigDir();
    so.wake = [] { ui::Window::wake(); };  // the watcher thread asks the UI thread to poll()
    settings = std::make_unique<ui::SettingsService>(so);
    text = ui::makeEditorTextRenderer(settings->settings(), win->pixelScale(), &err);
    if (!text) {
      std::fprintf(stderr, "tenfold-ui: %s\n", err.c_str());
      return 1;
    }
  }
  const ui::Theme theme = (settings ? settings->theme() : ui::Theme::darkDefault()).scaled(win->pixelScale());
  if (shell) {  // B3a widget shell demo; the sample project directory is the workspace argument or a default
    auto sd = std::make_unique<ui::ShellDemo>(*text, workspace, theme);
    app = std::make_unique<ui::EditorApp>(*sd, sd->editor(), nullptr, ui::userConfigDir());
    demoRoot = std::move(sd);
  } else if (!demo) {  // the editor: <workspace> [file], the file relative to the workspace or to the cwd
    editor = std::make_unique<Editor>(fs::absolute(workspace));
#ifdef _WIN32
    editor->setDebuggerFactory([] { return createVisualStudioDebugger(); });  // C2b: Visual Studio via EnvDTE
#endif
    bench = std::make_unique<ui::Workbench>(*text, *editor, theme);
    const bool restored = bench->restoreLayout();
    if (!file.empty()) {
      fs::path path = fs::absolute(fs::path(workspace) / file);
      if (!fs::exists(path) && fs::exists(file)) path = fs::absolute(file);
      if (bench->openFile(path) < 0) std::fprintf(stderr, "tenfold-ui: cannot open %s\n", path.string().c_str());
    } else if (!restored) {
      bench->openFile(kDefaultFile);  // first run: don't start blank (no-op if absent)
    }
    bench->sync();
    app = std::make_unique<ui::EditorApp>(*bench, *editor, bench.get(), ui::userConfigDir());
    if (shot.empty() && !editor->start(&err)) std::fprintf(stderr, "tenfold-ui: editor start: %s\n", err.c_str());
  }
  ui::RootWidget* root = bench ? static_cast<ui::RootWidget*>(bench.get()) : demoRoot.get();
  if (app && settings) {
    // A changed font.* setting rebuilds the TextRenderer; the old one lives until the root points at the new one.
    app->attachSettings(*settings, win->pixelScale(), [&] {
      auto next = ui::makeEditorTextRenderer(settings->settings(), win->pixelScale());
      if (!next) return false;
      root->setTextRenderer(*next);
      text->releaseTexture(*renderer);
      text = std::move(next);
      return true;
    });
    if (shot.empty()) settings->startWatching();
  }
  auto drawFrame = [&](int w, int h, float mx, float my) {
    if (root) root->render(*renderer, w, h);
    else drawScene(*renderer, w, h, atlas, mx, my);
  };

  if (!shot.empty()) {
    if (glRenderer) glRenderer->setOffscreen(true);
#ifdef TENFOLD_HAVE_D3D11
    if (d3dRenderer) d3dRenderer->setOffscreen(true);
#endif
    if (app) app->tick(nowMs());
    drawFrame(width, height, -100, -100);
    std::vector<uint8_t> px(static_cast<size_t>(width) * height * 4);
    if (!renderer->readPixels(width, height, px.data()) || !gfx::writePngFile(shot, px.data(), width, height)) {
      std::fprintf(stderr, "tenfold-ui: failed to write %s\n", shot.c_str());
      return 1;
    }
    std::printf("wrote %s (%dx%d)\n", shot.c_str(), width, height);
    return 0;
  }

  // Event-driven loop: block until input arrives (or until a widget wants a tick: caret blink, scroll animation, a
  // pending key chord, or the Editor's queue needs pumping), redraw only when something changed.
  float mx = -100, my = -100;
  bool dirty = true, quit = false;
  std::vector<ui::InputEvent> events;
  while (!quit) {
    int wait = -1;
    if (settings && settings->poll()) dirty = true;  // settings.txt / theme file changed (woken by the watcher)
    if (editor) editor->pump();
    if (app) {
      wait = app->tick(nowMs());
      if (editor && (wait < 0 || wait > 100)) wait = 100;  // poll the Editor's main-thread queue
      if (root->dirty()) dirty = true;
    }
    if (dirty) {
      int pw, ph;
      win->sizePx(pw, ph);
      drawFrame(pw, ph, mx, my);
      presentFrame();
      dirty = false;
    }
    events.clear();
    win->waitEvent(wait, events);
    for (const ui::InputEvent& e : events) {
      if (e.type == ui::InputType::Quit) {
        // Closing the window asks about unsaved changes first (editor.confirmUnsaved); the answer arrives as a later
        // event.
        if (app) app->requestQuit();
        if (!app || app->quitRequested()) quit = true;
      } else if (e.type == ui::InputType::Resize || e.type == ui::InputType::Expose ||
                 e.type == ui::InputType::ScaleChanged) {
        dirty = true;
      } else if (app) {
        // Ctrl+Q (App.Quit) and closing the window quit. Escape belongs to the editor (collapse carets); only the
        // --shell demo quits on an unhandled Escape.
        if (!app->handleEvent(e, nowMs()) && shell && e.type == ui::InputType::KeyDown && e.key == ui::Key::Escape)
          quit = true;
        if (app->quitRequested()) quit = true;
      } else if (e.type == ui::InputType::KeyDown && e.key == ui::Key::Escape) {
        quit = true;
      } else if (e.type == ui::InputType::MouseMove) {
        mx = e.x;
        my = e.y;
        dirty = true;  // the demo scene follows the pointer
      }
    }
  }
  // Shutdown order: the workbench saves layout.json, then the Editor shuts down.
  if (bench) bench->saveLayout();
  if (editor) editor->shutdown();
  tf::Log::shutdown();
  return 0;
}
