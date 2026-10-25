#pragma once
#include <memory>
#include <string>

#include "../test_main.h"
#include "ui/gfx/GlRenderer.h"
#include "ui/platform/Window.h"

namespace uitest {

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

}  // namespace uitest

#define SKIP_IF_NO_GL(fx)                                                     \
  auto* fx = uitest::glFixture();                                             \
  if (!fx) {                                                                  \
    std::cerr << "    SKIP: no display/OpenGL 3.3 (run under xvfb-run -a)\n"; \
    return;                                                                   \
  }
