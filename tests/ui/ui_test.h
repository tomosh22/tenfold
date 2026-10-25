#pragma once
#include <filesystem>
#include <memory>
#include <string>

#include "../test_main.h"
#include "ui/gfx/GlRenderer.h"
#include "ui/gfx/Renderer.h"
#include "ui/platform/Window.h"

namespace uitest {

// The temp directory with 8.3 short names expanded (Windows runners return C:/Users/RUNNER~1/...). The Editor and
// the file watcher canonicalise paths, so tests that compare against paths they built must start from the same form.
inline std::filesystem::path tempRoot() {
  std::error_code ec;
  auto p = std::filesystem::canonical(std::filesystem::temp_directory_path(), ec);
  return ec ? std::filesystem::temp_directory_path() : p;
}

// Set by `tenfold-ui-tests --update-goldens`: golden tests rewrite their reference images.
extern bool updateGoldens;

// A hidden GL window + renderer, created once and shared. Returns null (and prints a SKIP line, once per
// call) when there is no display / GL context, e.g. outside `xvfb-run -a`.
struct GlFixture {
  std::unique_ptr<tf::ui::Window> window;
  std::unique_ptr<tf::gfx::GlRenderer> renderer;
};
GlFixture* glFixture();
bool glFixtureExists();  // true once glFixture() has been called (the runner then checks glGetError around each test)

// A headless (offscreen, WARP) Direct3D 11 renderer, created once and shared. Null, with a printed reason, when
// the D3D11 backend is not built (non-Windows) or no device can be created.
struct D3dFixture {
  std::unique_ptr<tf::gfx::Renderer> renderer;  // a D3d11Renderer (its class is only linked on Windows)
};
D3dFixture* d3dFixture();

enum class Backend { Gl, D3d11 };

}  // namespace uitest

#define SKIP_IF_NO_D3D(fx)                                                            \
  auto* fx = uitest::d3dFixture();                                                    \
  if (!fx) {                                                                          \
    std::cerr << "    SKIP: Direct3D 11 backend unavailable (Windows builds only)\n"; \
    return;                                                                           \
  }

// A backend-parameterised renderer test: registers `name` (GL, offscreen) and `name_d3d11` (D3D11 on WARP), both
// running the same body. Select the D3D11 ones with `tenfold-ui-tests d3d11`.
#define RENDERER_TEST(name)                                          \
  static void body_##name(tf::gfx::Renderer& r, uitest::Backend be); \
  TEST(name) {                                                       \
    SKIP_IF_NO_GL(fx);                                               \
    fx->renderer->setOffscreen(true);                                \
    body_##name(*fx->renderer, uitest::Backend::Gl);                 \
  }                                                                  \
  TEST(name##_d3d11) {                                               \
    SKIP_IF_NO_D3D(fx);                                              \
    body_##name(*fx->renderer, uitest::Backend::D3d11);              \
  }                                                                  \
  static void body_##name(tf::gfx::Renderer& r, uitest::Backend be)

#define SKIP_IF_NO_GL(fx)                                                     \
  auto* fx = uitest::glFixture();                                             \
  if (!fx) {                                                                  \
    std::cerr << "    SKIP: no display/OpenGL 3.3 (run under xvfb-run -a)\n"; \
    return;                                                                   \
  }
