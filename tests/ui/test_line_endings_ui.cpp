// Line endings like Visual Studio (W10): the "Check for consistent line endings on load" dialog, the status-bar
// indicator and its conversion menu, Advanced Save Options ("Save with Line Endings") and copy / paste of mixed text.
// Synthetic events against a null renderer and the mock font.
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "../text/text_test_util.h"
#include "ui/gfx/TextRenderer.h"
#include "ui/platform/Clipboard.h"
#include "ui/settings/Settings.h"
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
  return e;
}

std::string slurp(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

void writeFile(const fs::path& p, const std::string& text) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary | std::ios::trunc) << text;
}

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
    dir = uitest::tempRoot() / ("tf_le_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                                "_" + std::to_string(n++));
    fs::create_directories(dir);
    writeFile(dir / "mixed.cpp", "int a;\r\nint b;\r\nint c;\nint d;\r\n");  // 3 CRLF, 1 LF
    writeFile(dir / "crlf.cpp", "int a;\r\nint b;\r\n");
    writeFile(dir / "lf.cpp", "int a;\nint b;\n");
    writeFile(dir / "mixed2.cpp", "x;\ny;\nz;\r\n");  // 2 LF, 1 CRLF
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
  void send(const InputEvent& e) {
    now += 10;
    app->handleEvent(e, now);
    frame();
  }
  void key(Key k, uint8_t mods = 0) { send(keyEv(k, mods)); }
  void click(const RectF& r) {
    send(mouseEv(InputType::MouseDown, r.x + r.w / 2, r.y + r.h / 2));
    send(mouseEv(InputType::MouseUp, r.x + r.w / 2, r.y + r.h / 2));
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
  std::string status(const char* k) { return bench->status().text(k); }
  std::vector<std::string> rows(ListPopup* p) {
    std::vector<std::string> out;
    if (p)
      for (const auto& r : p->rows()) out.push_back(r.primary);
    return out;
  }
  ContextMenu* menu() { return app->contextMenu(); }
  int indexOf(const std::string& label) {
    ContextMenu* m = menu();
    if (!m) return -1;
    for (size_t i = 0; i < m->items().size(); ++i)
      if (m->items()[i].label == label) return static_cast<int>(i);
    return -1;
  }
  void clickItem(const std::string& label) {
    REQUIRE(menu() != nullptr);
    const int i = indexOf(label);
    REQUIRE(i >= 0);
    click(menu()->itemRect(0, i));
  }
  std::string disk(Document* d) { return d->buffer().textForDisk(); }
};

LineEndingPrompt& prompt(Env& e) { return e.app->lineEndingPrompt(); }

}  // namespace

TEST(le_ui_mixed_file_asks_to_normalize_with_the_dominant_ending_preselected) {
  Env env;
  Document* d = env.open("mixed.cpp");
  REQUIRE(d != nullptr);
  env.frame();
  REQUIRE(prompt(env).isOpen());
  ListPopup* p = prompt(env).popup();
  CHECK_EQ(p->title(), std::string("The line endings in the following files are not consistent. Do you want to "
                                   "normalize the line endings?"));
  const auto rows = env.rows(p);
  REQUIRE(rows.size() == 5);
  CHECK(rows[0].find("mixed.cpp") != std::string::npos);
  CHECK_EQ(rows[1], std::string("Yes: normalize line endings to Windows (CR LF)"));
  CHECK_EQ(rows[2], std::string("Yes: normalize line endings to Macintosh (CR)"));
  CHECK_EQ(rows[3], std::string("Yes: normalize line endings to Unix (LF)"));
  CHECK_EQ(rows[4], std::string("No"));
  CHECK_EQ(p->selected(), 1);  // the dominant ending (3 CRLF against 1 LF)
  CHECK(!d->dirty());
  // Yes -> Unix (LF): one undoable edit, the document is modified
  env.key(Key::Down);
  env.key(Key::Down);  // Unix (LF)
  env.key(Key::Enter);
  CHECK(!prompt(env).isOpen());
  CHECK_EQ(env.disk(d), std::string("int a;\nint b;\nint c;\nint d;\n"));
  CHECK(d->dirty());
  CHECK(!d->buffer().mixedLineEndings());
  env.frame();
  CHECK_EQ(env.status("eol"), std::string("LF"));
  CHECK(env.ed->undo());
  CHECK_EQ(env.disk(d), std::string("int a;\r\nint b;\r\nint c;\nint d;\r\n"));
  CHECK(env.ed->redo());
  CHECK(env.ed->save(*d));
  CHECK_EQ(slurp(env.dir / "mixed.cpp"), std::string("int a;\nint b;\nint c;\nint d;\n"));
}

TEST(le_ui_answering_no_keeps_the_file_mixed_and_does_not_ask_again) {
  Env env;
  Document* d = env.open("mixed2.cpp");
  REQUIRE(prompt(env).isOpen());
  CHECK_EQ(prompt(env).popup()->selected(), 3);  // two LF lines against one CRLF: Unix (LF)
  env.key(Key::Escape);                          // Escape is No
  CHECK(!prompt(env).isOpen());
  CHECK(!d->dirty());
  CHECK(d->buffer().mixedLineEndings());
  env.frame();
  CHECK(!prompt(env).isOpen());
  // typing and pasting later never asks; saving keeps the file exactly as it was
  env.ed->insertText("w", false);
  env.ed->insertText("a\r\nb", true);
  env.frame();
  CHECK(!prompt(env).isOpen());
  CHECK(env.ed->save(*d));
  CHECK(slurp(env.dir / "mixed2.cpp").find("y;\nz;\r\n") != std::string::npos);
  // a second file is a second question; Enter on "No" keeps it as it is
  env.open("mixed.cpp");
  env.frame();
  CHECK(prompt(env).isOpen());
  env.key(Key::Down);
  env.key(Key::Down);
  env.key(Key::Down);  // No
  env.key(Key::Enter);
  CHECK(!prompt(env).isOpen());
  CHECK(env.ed->currentDocument()->buffer().mixedLineEndings());
  CHECK(!env.ed->currentDocument()->dirty());
}

TEST(le_ui_consistent_files_and_the_off_switch_show_no_dialog) {
  Env env;
  env.open("crlf.cpp");
  env.open("lf.cpp");
  env.frame();
  CHECK(!prompt(env).isOpen());
  CHECK_EQ(prompt(env).pending(), size_t(0));
  env.ed->editing().checkLineEndings = false;
  env.open("mixed.cpp");
  env.frame();
  CHECK(!prompt(env).isOpen());
  CHECK(env.ed->currentDocument()->buffer().mixedLineEndings());
}

TEST(le_ui_status_bar_shows_the_document_ending_and_a_click_converts_the_document) {
  Env env;
  env.ed->editing().checkLineEndings = false;
  Document* d = env.open("crlf.cpp");
  env.frame();
  CHECK_EQ(env.status("eol"), std::string("CRLF"));
  env.open("lf.cpp");
  env.frame();
  CHECK_EQ(env.status("eol"), std::string("LF"));
  Document* m = env.open("mixed.cpp");
  env.frame();
  CHECK_EQ(env.status("eol"), std::string("CRLF"));  // one item for the document: the dominant ending
  // a click on the indicator offers the three endings and the Advanced Save Options
  const RectF eol = env.bench->status().segmentRect("eol");
  REQUIRE(eol.w > 0);
  env.click(eol);
  REQUIRE(env.menu() != nullptr);
  CHECK(env.indexOf("Windows (CR LF)") >= 0);
  CHECK(env.indexOf("Unix (LF)") >= 0);
  CHECK(env.indexOf("Macintosh (CR)") >= 0);
  CHECK(env.indexOf("Save with Line Endings...") >= 0);
  env.clickItem("Macintosh (CR)");
  CHECK_EQ(env.disk(m), std::string("int a;\rint b;\rint c;\rint d;\r"));
  CHECK(m->dirty());
  env.frame();
  CHECK_EQ(env.status("eol"), std::string("CR"));
  CHECK(env.ed->undo());
  CHECK_EQ(env.disk(m), std::string("int a;\r\nint b;\r\nint c;\nint d;\r\n"));
  (void)d;
}

TEST(le_ui_save_with_line_endings_lists_the_choices_and_converts_on_that_save) {
  Env env;
  env.ed->editing().checkLineEndings = false;
  Document* d = env.open("crlf.cpp");
  env.run("File.SaveWithLineEndings");
  REQUIRE(prompt(env).isOpen());
  CHECK(env.rows(prompt(env).popup()) ==
        (std::vector<std::string>{"Current Setting", "Windows (CR LF)", "Macintosh (CR)", "Unix (LF)"}));
  env.key(Key::Down);
  env.key(Key::Down);
  env.key(Key::Down);  // Unix (LF)
  env.key(Key::Enter);
  CHECK(!prompt(env).isOpen());
  CHECK_EQ(slurp(env.dir / "crlf.cpp"), std::string("int a;\nint b;\n"));
  CHECK(!d->dirty());
  // Current Setting is a plain save
  env.ed->insertText("//");
  env.run("File.SaveWithLineEndings");
  env.key(Key::Enter);
  CHECK_EQ(slurp(env.dir / "crlf.cpp"), std::string("//int a;\nint b;\n"));
}

TEST(le_ui_copy_puts_the_stored_breaks_on_the_clipboard_and_paste_keeps_them) {
  Env env;
  FakeClip clip;
  env.ed->editing().checkLineEndings = false;
  Document* m = env.open("mixed.cpp");
  env.ed->setSelections({Selection{{0, 0}, {3, 6}}});
  env.run("Editor.Copy");
  CHECK_EQ(clip.value, std::string("int a;\r\nint b;\r\nint c;\nint d;"));
  // no selection: the caret line with its own line break
  env.ed->setSelections({Selection{{2, 2}, {2, 2}}});
  env.run("Editor.Copy");
  CHECK_EQ(clip.value, std::string("int c;\n"));
  env.ed->setSelections({Selection{{0, 1}, {0, 1}}});
  env.run("Editor.Copy");
  CHECK_EQ(clip.value, std::string("int a;\r\n"));
  // paste into an LF file keeps the CRLF: the file becomes mixed (as in Visual Studio)
  Document* lf = env.open("lf.cpp");
  env.ed->setSelections({Selection{{1, 6}, {1, 6}}});
  clip.value = "p\r\nq";
  env.run("Editor.Paste");
  CHECK_EQ(env.disk(lf), std::string("int a;\nint b;p\r\nq\n"));
  CHECK(lf->buffer().mixedLineEndings());
  // CR text pastes as CR; with the setting off the pasted breaks follow the document
  clip.value = "r\rs";
  env.ed->setSelections({Selection{{0, 0}, {0, 0}}});
  env.run("Editor.Paste");
  CHECK_EQ(env.disk(lf), std::string("r\rsint a;\nint b;p\r\nq\n"));
  env.ed->editing().pasteKeepsLineBreaks = false;
  clip.value = "t\r\nu";
  env.ed->setSelections({Selection{{0, 0}, {0, 0}}});
  env.run("Editor.Paste");
  // line 0 ends with CR, so the pasted CRLF became CR
  CHECK_EQ(env.disk(lf), std::string("t\rur\rsint a;\nint b;p\r\nq\n"));
  (void)m;
}
