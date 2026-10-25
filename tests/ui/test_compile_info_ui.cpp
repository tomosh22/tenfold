// D4: Help.ShowCompileCommand shows the compile-command report of the current file in a read-only popup. Synthetic
// events against a recording renderer and the mock font, a started Editor without clangd (heuristic flags).
#include <chrono>
#include <filesystem>
#include <fstream>

#include "../text/text_test_util.h"
#include "ui/gfx/TextRenderer.h"
#include "ui/shell/EditorApp.h"
#include "ui/widgets/ListPopup.h"
#include "ui_test.h"

using namespace tf;
using namespace tf::ui;
namespace fs = std::filesystem;

namespace {

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

InputEvent keyEv(Key k) {
  InputEvent e;
  e.type = InputType::KeyDown;
  e.key = k;
  return e;
}

struct Env {
  fs::path dir;
  std::unique_ptr<Editor> ed;
  std::unique_ptr<gfx::TextRenderer> text;
  NullRenderer rec;
  std::unique_ptr<Workbench> bench;
  std::unique_ptr<EditorApp> app;
  int64_t now = 1000;

  Env() {
    static int n = 0;
    dir = uitest::tempRoot() / ("tf_ci_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                                "_" + std::to_string(n++));
    fs::create_directories(dir / "src");
    std::ofstream(dir / "src/a.cpp") << "int add(int a, int b) { return a + b; }\n";
    gfx::FontStack stack;
    stack.add(std::make_shared<tftext::MockFont>(
        U"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ._-:,;/()[]{}<>+*&#?=!\"'\\|"));
    text = gfx::TextRenderer::fromFonts(std::move(stack));
    Editor::Options o;
    o.semantic.useClangd = false;
    ed = std::make_unique<Editor>(dir, o);
    std::string err;
    ed->start(&err);
    bench = std::make_unique<Workbench>(*text, *ed);
    app = std::make_unique<EditorApp>(*bench, *ed, bench.get());
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
    bench->render(rec, 1000, 700);
  }
  bool key(Key k) {
    now += 10;
    const bool r = app->handleEvent(keyEv(k), now);
    frame();
    return r;
  }
  std::string rowsText() {
    std::string all;
    if (ListPopup* p = app->compileInfo().popup())
      for (const auto& r : p->rows()) all += r.primary + "\n";
    return all;
  }
};

}  // namespace

TEST(d4_show_compile_command_opens_a_read_only_popup_with_the_report) {
  Env env;
  REQUIRE(env.app->commands().find("Help.ShowCompileCommand") != nullptr);
  env.bench->openFile("src/a.cpp");
  env.bench->sync();
  env.frame();
  CHECK(!env.app->compileInfo().isOpen());
  const auto r = env.app->commands().run("Help.ShowCompileCommand", "", CommandContext{env.ed.get(), "editor", {}, {}});
  CHECK(r.ok());
  REQUIRE(env.app->compileInfo().isOpen());
  env.frame();
  const std::string shown = env.rowsText();
  CHECK(shown.find("a.cpp") != std::string::npos);
  CHECK(shown.find("Compile flags:") != std::string::npos);
  CHECK(shown.find("heuristic") != std::string::npos);  // no project files in the workspace
  CHECK(shown.find("Command (argv):") != std::string::npos);
  CHECK(shown.find("-std=c++20") != std::string::npos);
  CHECK(shown.find("clangd is not running") != std::string::npos);
  // It is the engine's report, line for line.
  const std::string expect = env.ed->compileCommandInfo(env.ed->currentDocument()->path()).text(76);
  CHECK_EQ(env.app->compileInfo().text(), expect);
  // Read-only: typing does nothing to the document, Escape closes the popup.
  const std::string before = env.ed->currentDocument()->buffer().text();
  InputEvent t;
  t.type = InputType::Text;
  t.text = "x";
  env.app->handleEvent(t, env.now += 10);
  CHECK_EQ(env.ed->currentDocument()->buffer().text(), before);
  env.key(Key::Escape);
  CHECK(!env.app->compileInfo().isOpen());
  CHECK_EQ(env.ed->currentDocument()->buffer().text(), before);
}

TEST(d4_show_compile_command_without_a_file_says_so) {
  Env env;
  const auto r = env.app->commands().run("Help.ShowCompileCommand", "", CommandContext{env.ed.get(), "editor", {}, {}});
  CHECK(!r.ok());
  CHECK(!env.app->compileInfo().isOpen());
}
