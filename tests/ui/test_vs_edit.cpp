// VS-EDIT: Visual Studio editor behaviours (docs/VS_BEHAVIOUR.md section 14 rows Z1, Z4, Z14): Copy / Cut with no
// selection, the quote defaults and the name of a new text file. Synthetic events against a null renderer and the mock
// font.
#include <chrono>
#include <filesystem>
#include <fstream>

#include "../text/text_test_util.h"
#include "ui/gfx/TextRenderer.h"
#include "ui/platform/Clipboard.h"
#include "ui/settings/Settings.h"
#include "ui/shell/EditorApp.h"
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

struct FakeClip {
  std::string value;
  FakeClip() {
    setClipboardBackend({[this] { return value; },
                         [this](const std::string& s) {
                           value = s;
                           return true;
                         }});
  }
  ~FakeClip() { setClipboardBackend({}); }
};

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
    dir = uitest::tempRoot() /
          ("tf_vsedit_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_" +
           std::to_string(n++));
    fs::create_directories(dir);
    std::ofstream(dir / "a.cpp", std::ios::binary) << "one;\r\n  \r\nthree;\r\nfour;\n";
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
  void run(const std::string& id) {
    app->runCommand(id);
    frame();
  }
  void settings(const std::string& t) {
    Settings s;
    s.loadTexts(t, "");
    app->applySettings(s);
  }
  Document* open(const std::string& rel) {
    const int id = bench->openFile(dir / rel);
    if (id < 0) return nullptr;
    bench->sync();
    frame();
    return ed->currentDocument();
  }
  void type(const std::string& s) {
    ed->typeText(s);
    frame();
  }
  void caret(int line, int col) { ed->setSelections({Selection{{line, col}, {line, col}}}); }
};

}  // namespace

TEST(vsedit_defaults_follow_visual_studio) {
  Settings s;
  s.loadTexts("", "");
  CHECK(s.getBool("editor.autoCloseQuotes"));         // Z1
  CHECK(s.getBool("editor.copyLineWhenEmpty"));       // Z4
  CHECK(s.getBool("editor.copyBlankLines"));          // Z4
  CHECK(s.getBool("editor.mouseWheelZoom"));          // Z8 (10x MouseWheelZoom)
  CHECK_EQ(s.getInt("editor.wheelLines"), 3);         // Z7 (VS: 3 lines per scroll)
  CHECK_EQ(s.getInt("editor.pasteHistorySize"), 20);  // Z11 (VS clipboard ring: 20)
  CHECK(s.getBool("editor.wordWrap"));                // Z6 stays: the user's decision, not VS's default
}

TEST(vsedit_copy_and_cut_without_a_selection_take_the_whole_line_with_its_terminator) {
  Env env;
  FakeClip clip;
  env.ed->editing().checkLineEndings = false;
  Document* d = env.open("a.cpp");
  REQUIRE(d != nullptr);
  env.caret(2, 3);
  env.run("Editor.Copy");
  CHECK_EQ(clip.value, std::string("three;\r\n"));
  clip.value.clear();
  env.run("Editor.Cut");
  CHECK_EQ(clip.value, std::string("three;\r\n"));
  CHECK_EQ(d->buffer().text(), std::string("one;\n  \nfour;\n"));
  env.caret(2, 0);
  env.run("Editor.Cut");
  CHECK_EQ(clip.value, std::string("four;\n"));  // its own (LF) terminator
}

TEST(vsedit_blank_lines_are_copied_unless_the_option_is_off) {
  Env env;
  FakeClip clip;
  env.ed->editing().checkLineEndings = false;
  Document* d = env.open("a.cpp");
  REQUIRE(d != nullptr);
  env.caret(1, 1);
  env.run("Editor.Copy");
  CHECK_EQ(clip.value, std::string("  \r\n"));  // default: "Apply Cut or Copy commands to blank lines" is on
  clip.value = "keep";
  env.settings("editor.copyBlankLines = false\n");
  env.run("Editor.Copy");
  CHECK_EQ(clip.value, std::string("keep"));  // nothing copied
  const std::string before = d->buffer().text();
  env.run("Editor.Cut");
  CHECK_EQ(clip.value, std::string("keep"));
  CHECK_EQ(d->buffer().text(), before);  // and nothing cut
  env.caret(0, 0);
  env.run("Editor.Cut");  // a non-blank line still goes
  CHECK_EQ(clip.value, std::string("one;\r\n"));
}

TEST(vsedit_copy_line_when_empty_off_leaves_copy_and_cut_inert_without_a_selection) {
  Env env;
  FakeClip clip;
  env.ed->editing().checkLineEndings = false;
  Document* d = env.open("a.cpp");
  REQUIRE(d != nullptr);
  env.settings("editor.copyLineWhenEmpty = false\n");
  clip.value = "keep";
  const std::string before = d->buffer().text();
  env.caret(0, 2);
  env.run("Editor.Copy");
  env.run("Editor.Cut");
  CHECK_EQ(clip.value, std::string("keep"));
  CHECK_EQ(d->buffer().text(), before);
  // a real selection is unaffected
  env.ed->setSelections({Selection{{0, 0}, {0, 3}}});
  env.run("Editor.Copy");
  CHECK_EQ(clip.value, std::string("one"));
}

TEST(vsedit_typing_quotes_pairs_them_by_default_and_the_setting_turns_it_off) {
  Env env;
  Document* d = env.open("a.cpp");
  REQUIRE(d != nullptr);
  env.caret(3, 5);
  env.type("\"");
  CHECK_EQ(d->buffer().line(3), std::string("four;\"\""));
  env.type("x");
  env.type("\"");  // steps over the closing quote
  CHECK_EQ(d->buffer().line(3), std::string("four;\"x\""));
  env.ed->editing().autoCloseQuotes = false;
  env.type("'");
  CHECK_EQ(d->buffer().line(3), std::string("four;\"x\"'"));
}

TEST(vsedit_a_new_text_file_is_named_like_visual_studios) {
  Env env;
  std::ofstream(env.dir / "TextFile1.txt") << "x";  // taken on disk: the next free number
  env.run("File.New");
  Document* d = env.ed->currentDocument() ? env.ed->currentDocument() : nullptr;
  REQUIRE(d != nullptr);
  CHECK(d->isUntitled());
  CHECK_EQ(d->path().filename().string(), std::string("TextFile2.txt"));
  env.run("File.New");
  CHECK_EQ(env.ed->currentDocument()->path().filename().string(), std::string("TextFile3.txt"));
}
