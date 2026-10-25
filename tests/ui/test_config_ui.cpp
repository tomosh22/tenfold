// C-INT: the solution configuration picker (Build.SelectConfiguration), the status-bar "config" segment and
// Debug.OpenInVisualStudio. Mock font, synthetic events through EditorApp; tests/fixtures/vs/Multi is copied to a
// temp directory like tests/test_vcxproj.cpp does. clangd is never started (the engine still regenerates the
// compile commands and reports the switch applied).
#include <chrono>
#include <filesystem>
#include <fstream>

#include "../text/text_test_util.h"
#include "debug/FakeDebugger.h"
#include "ui/gfx/Renderer.h"
#include "ui/gfx/TextRenderer.h"
#include "ui/settings/Theme.h"
#include "ui/shell/EditorApp.h"
#include "ui/widgets/TextView.h"
#include "ui_test.h"

#define REQUIRE_EQ(a, b) REQUIRE((a) == (b))

using namespace tf;
using namespace tf::ui;
namespace fs = std::filesystem;

namespace {

std::unique_ptr<gfx::TextRenderer> mockText() {
  gfx::FontStack stack;
  stack.add(std::make_shared<tftext::MockFont>(
      U"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ._-:,;/()[]{}<>+*&#?=!\"'|"));
  return gfx::TextRenderer::fromFonts(std::move(stack));
}

InputEvent keyEv(Key k, uint8_t mods = 0) {
  InputEvent e;
  e.type = InputType::KeyDown;
  e.key = k;
  e.mods = mods;
  return e;
}
InputEvent mouseEv(InputType t, float x, float y) {
  InputEvent e;
  e.type = t;
  e.x = x;
  e.y = y;
  e.clicks = 1;
  e.button = MouseButton::Left;
  return e;
}

struct NullRenderer : gfx::Renderer {
  void beginFrame(int, int, gfx::Color) override {}
  void drawRect(const gfx::RectF&, gfx::Color) override {}
  void drawLine(float, float, float, float, float, gfx::Color) override {}
  void drawGlyphs(gfx::TextureId, const gfx::GlyphQuad*, int, gfx::Color) override {}
  void pushScissor(const gfx::RectF&) override {}
  void popScissor() override {}
  gfx::TextureId createTexture(int, int, gfx::TextureFormat) override { return 1; }
  void updateTexture(gfx::TextureId, int, int, int, int, const void*, int) override {}
  void destroyTexture(gfx::TextureId) override {}
  void endFrame() override {}
  bool readPixels(int, int, uint8_t*) override { return false; }
};

struct Env {
  fs::path dir;
  std::unique_ptr<gfx::TextRenderer> text = mockText();
  NullRenderer rec;
  Theme theme = Theme::darkDefault();
  std::unique_ptr<Editor> ed;
  std::unique_ptr<Workbench> bench;
  std::unique_ptr<EditorApp> app;
  std::shared_ptr<FakeDebugger::State> fake;
  int64_t now = 1000;

  // fixture: a tests/fixtures/vs folder to copy, or empty for a plain workspace.
  explicit Env(const char* fixture, bool backend = false) {
    static int n = 0;
    dir =
        uitest::tempRoot() / ("tf_cint_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                              "_" + std::to_string(n++));
    if (*fixture)
      fs::copy(fs::path(TENFOLD_SOURCE_DIR) / "tests/fixtures/vs" / fixture, dir, fs::copy_options::recursive);
    else fs::create_directories(dir);
    std::string a;
    for (int i = 1; i <= 10; ++i) a += "int line" + std::to_string(i) + ";\n";
    std::ofstream(dir / "a.cpp", std::ios::binary) << a;
    Editor::Options o;
    o.semantic.useClangd = false;
    o.semantic.persistFastIndex = false;
    o.watchForcePolling = true;
    o.watchPollMs = 60000;
    ed = std::make_unique<Editor>(dir, o);
    std::string err;
    if (!ed->start(&err)) std::cerr << "    editor start: " << err << "\n";
    if (backend) {
      auto d = std::make_unique<FakeDebugger>();
      fake = d->state();
      ed->debug().setPollIntervals(5, 5);
      ed->debug().setBackend(std::move(d));
    }
    bench = std::make_unique<Workbench>(*text, *ed, theme);
    app = std::make_unique<EditorApp>(*bench, *ed, bench.get());
    if (backend) {
      ed->debug().connect();
      ed->waitFor([&] { return ed->debug().connected(); }, 5000);
    }
    frame();
  }
  ~Env() {
    app.reset();
    bench.reset();
    ed.reset();
    std::error_code ec;
    fs::remove_all(dir, ec);
  }
  void frame() {
    ed->pump();
    app->tick(now);
    bench->render(rec, 1000, 600);
  }
  bool key(Key k, uint8_t mods = 0) {
    now += 10;
    const bool r = app->handleEvent(keyEv(k, mods), now);
    frame();
    return r;
  }
  void run(const char* cmd) {
    app->runCommand(cmd);
    frame();
  }
  std::string seg(const char* k) { return bench->status().text(k); }
  void open(const char* rel) {
    bench->openFile(dir / rel);
    bench->sync();
    frame();
  }
  // Opens the picker and moves the selection to `name`.
  void pick(const char* name) {
    run("Build.SelectConfiguration");
    auto& pk = app->configurationPicker();
    REQUIRE(pk.isOpen());
    const int target = pk.popup()->indexOfKey(name);
    REQUIRE(target >= 0);
    while (pk.popup()->selected() < target) key(Key::Down);
  }
};

}  // namespace

TEST(cint_config_segment_shows_the_active_configuration_only_with_a_solution) {
  Env vs("Multi");
  CHECK_EQ(vs.seg("config"), "Debug|x64");
  Env plain("");
  CHECK_EQ(plain.seg("config"), "");
  CHECK(!plain.app->configurationPicker().hasSolution());
  plain.run("Build.SelectConfiguration");  // says so instead of opening an empty list
  CHECK(!plain.app->configurationPicker().isOpen());
  CHECK(!plain.app->message().empty());
}

TEST(cint_picker_lists_the_solution_configurations_with_the_active_one_marked) {
  Env env("Multi");
  env.run("Build.SelectConfiguration");
  auto& pk = env.app->configurationPicker();
  REQUIRE(pk.isOpen());
  const auto expect = env.ed->vsConfigurations();
  REQUIRE(expect.size() >= 2);
  REQUIRE_EQ(pk.popup()->rows().size(), expect.size());
  int marked = 0;
  for (size_t i = 0; i < expect.size(); ++i) {
    const ListRow& r = pk.popup()->rows()[i];
    CHECK_EQ(r.primary, expect[i].name());
    if (!r.badge.empty()) {
      ++marked;
      CHECK_EQ(r.primary, "Debug|x64");
      CHECK_EQ(pk.popup()->selected(), static_cast<int>(i));  // preselected
    }
  }
  CHECK_EQ(marked, 1);
  CHECK(env.key(Key::Escape));
  CHECK(!pk.isOpen());
}

TEST(cint_picker_enter_switches_the_configuration_and_the_segment_follows) {
  Env env("Multi");
  env.pick("Release|x64");
  auto& pk = env.app->configurationPicker();
  env.key(Key::Enter);
  CHECK(!pk.isOpen());
  CHECK_EQ(env.ed->workspace().activeVsConfiguration().name(), "Release|x64");  // the workspace switches at once
  CHECK(env.ed->waitFor([&] { return !pk.reloading(); }, 30000));
  env.frame();
  CHECK_EQ(env.seg("config"), "Release|x64");
  env.run("Build.SelectConfiguration");  // reopening marks the new one
  const ListRow* sel = pk.popup()->selectedRow();
  REQUIRE(sel != nullptr);
  CHECK_EQ(sel->key, "Release|x64");
  CHECK_EQ(sel->badge, "active");
  env.key(Key::Escape);
}

TEST(cint_segment_text_while_reloading) {
  Env env("Multi");
  env.pick("Release|x64");
  auto& pk = env.app->configurationPicker();
  // Enter and tick without pumping the editor: the "applied" callback is queued, not yet delivered.
  env.now += 10;
  env.app->handleEvent(keyEv(Key::Enter, 0), env.now);
  env.app->tick(env.now);
  CHECK(pk.reloading());
  CHECK_EQ(env.seg("config"), "Configuration: Release|x64 (reloading...)");
  CHECK(env.ed->waitFor([&] { return !pk.reloading(); }, 30000));
  env.frame();
  CHECK_EQ(env.seg("config"), "Release|x64");
}

TEST(cint_clicking_the_segment_opens_the_picker) {
  Env env("Multi");
  const RectF r = env.bench->status().segmentRect("config");
  REQUIRE(r.w > 0);
  env.now += 10;
  env.app->handleEvent(mouseEv(InputType::MouseDown, r.x + 2, r.y + r.h / 2), env.now);
  env.app->handleEvent(mouseEv(InputType::MouseUp, r.x + 2, r.y + r.h / 2), env.now);
  env.frame();
  CHECK(env.app->configurationPicker().isOpen());
  env.key(Key::Escape);
}

TEST(cint_open_in_visual_studio_reaches_the_backend_with_the_caret) {
  Env env("", true);
  env.open("a.cpp");
  env.ed->moveCursor({4, 3});
  env.frame();
  env.run("Debug.OpenInVisualStudio");
  REQUIRE(env.fake->waitUntil([](const FakeDebugger::State& s) { return s.count("openInEditor") >= 1; }, 5000));
  const auto calls = env.fake->callsOf("openInEditor");
  CHECK(calls[0].detail.find("a.cpp:4:3") != std::string::npos);
}

TEST(cint_open_in_visual_studio_reports_an_error_without_a_backend_or_on_failure) {
  {
    Env env("");
    env.open("a.cpp");
    CHECK(!env.ed->debug().hasBackend());
    env.run("Debug.OpenInVisualStudio");
    CHECK(env.app->message().find("Open in Visual Studio") != std::string::npos);
  }
  {
    Env env("", true);
    env.open("a.cpp");
    env.fake->failNext("openInEditor", "no such window");
    env.run("Debug.OpenInVisualStudio");
    REQUIRE(env.ed->waitFor([&] { return !env.ed->debug().lastError().empty(); }, 5000));
    env.frame();
    CHECK(env.app->message().find("no such window") != std::string::npos);
  }
}
