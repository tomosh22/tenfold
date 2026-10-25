// Test runner for tenfold-ui-tests. Usage: tenfold-ui-tests [name-filter] [--update-goldens]
#include <chrono>
#include <cstring>

#include "ui/gfx/GlLoader.h"
#include "ui_test.h"

namespace tftest {
std::vector<Case>& cases() {
  static std::vector<Case> c;
  return c;
}
int& failures() {
  static int f = 0;
  return f;
}
void fail(const char* file, int line, const std::string& msg) {
  std::cerr << "    " << file << ":" << line << ": " << msg << "\n";
  ++failures();
}
}  // namespace tftest

namespace uitest {
bool updateGoldens = false;

GlFixture* glFixture() {
  static GlFixture fx;
  static bool tried = false;
  if (!tried) {
    tried = true;
    std::string err;
    tf::ui::WindowOptions wo;
    wo.width = 320;
    wo.height = 240;
    wo.hidden = true;
    wo.vsync = false;
    fx.window = tf::ui::Window::create(wo, &err);
    if (fx.window && tf::gfx::gl::load(&tf::ui::Window::glProc, &err)) fx.renderer = tf::gfx::GlRenderer::create(&err);
    if (!fx.renderer) {
      std::cerr << "    GL unavailable: " << err << "\n";
      fx.window.reset();
    }
  }
  return fx.renderer ? &fx : nullptr;
}
}  // namespace uitest

int main(int argc, char** argv) {
  const char* filter = nullptr;
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--update-goldens")) uitest::updateGoldens = true;
    else filter = argv[i];
  }
  int run = 0, failedCases = 0;
  for (auto& c : tftest::cases()) {
    if (filter && std::strstr(c.name, filter) == nullptr) continue;
    int before = tftest::failures();
    auto t0 = std::chrono::steady_clock::now();
    std::cerr << "[ RUN  ] " << c.name << "\n";
    c.fn();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    bool ok = tftest::failures() == before;
    std::cerr << (ok ? "[  OK  ] " : "[ FAIL ] ") << c.name << " (" << ms << " ms)\n";
    ++run;
    if (!ok) ++failedCases;
  }
  std::cerr << run << " tests, " << failedCases << " failed\n";
  // The GL objects must die before SDL does; the fixture is a function-local static, so release it here.
  if (auto* fx = uitest::glFixture()) {
    fx->renderer.reset();
    fx->window.reset();
  }
  return failedCases == 0 ? 0 : 1;
}
