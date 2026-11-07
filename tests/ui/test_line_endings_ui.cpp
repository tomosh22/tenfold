// Line endings like Visual Studio (W10): the "Check for consistent line endings on load" dialog, the status-bar
// indicator and its conversion menu, Advanced Save Options ("Save with Line Endings") and copy / paste of mixed text.
// Synthetic events against a null renderer and the mock font.
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "../text/text_test_util.h"
#include "ui/gfx/PngWrite.h"
#include "ui/gfx/TextRenderer.h"
#include "ui/platform/Clipboard.h"
#include "ui/settings/Settings.h"
#include "ui/settings/Theme.h"
#include "ui/shell/EditorApp.h"
#include "ui/features/LineEndingDialog.h"
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

  explicit Env(bool withUserDir = false) {
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
    app = std::make_unique<EditorApp>(*bench, *ed, bench.get(), withUserDir ? dir / "userconf" : fs::path());
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

LineEndingDialog* dlg(Env& e) { return prompt(e).dialog(); }

TEST(le_ui_mixed_file_asks_to_normalize_with_the_dominant_ending_preselected) {
  Env env;
  Document* d = env.open("mixed.cpp");
  REQUIRE(d != nullptr);
  env.frame();
  REQUIRE(prompt(env).isOpen());
  LineEndingDialog* dl = dlg(env);
  REQUIRE(dl != nullptr);
  CHECK_EQ(dl->message(), std::string("The line endings in the following files are not consistent. Do you want to "
                                      "normalize the line endings?"));
  REQUIRE(dl->files().size() == 1);
  CHECK(dl->files()[0].label.find("mixed.cpp") != std::string::npos);
  CHECK_EQ(dl->files()[0].detail, std::string("3 CR LF, 1 LF, 0 CR"));
  DropDown* dd = dl->dropDown();
  REQUIRE(dd->items().size() == 5);
  CHECK_EQ(dd->items()[0].label, std::string("Windows (CR LF)"));
  CHECK_EQ(dd->items()[1].label, std::string("Macintosh (CR)"));
  CHECK_EQ(dd->items()[2].label, std::string("Unix (LF)"));
  CHECK_EQ(dd->items()[3].label, std::string("Unicode Line Separator (LS)"));
  CHECK_EQ(dd->items()[4].label, std::string("Unicode Paragraph Separator (PS)"));
  CHECK_EQ(dd->selected(), 0);  // the dominant ending (3 CRLF against 1 LF)
  CHECK(!dl->checkBox()->checked());
  CHECK(!d->dirty());
  // layout: message, files, drop-down, check box, buttons, top to bottom, all inside the dialog
  const RectF box = dl->dialogRect();
  CHECK(dl->fileListRect().y > box.y);
  CHECK(dd->bounds().y > dl->fileListRect().y + dl->fileListRect().h - 1);
  CHECK(dl->checkBox()->bounds().y > dd->bounds().y + dd->bounds().h - 1);
  CHECK(dl->yesRect().y > dl->checkBox()->bounds().y + dl->checkBox()->bounds().h - 1);
  CHECK(dl->yesRect().x < dl->noRect().x);
  CHECK(dl->noRect().x + dl->noRect().w <= box.x + box.w);
  CHECK(dl->noRect().y + dl->noRect().h <= box.y + box.h);
  CHECK_EQ(dl->focusIndex(), 2);  // Yes has the focus: Enter is Yes
  // choose Unix (LF) with the keyboard: Tab to the drop-down, Down twice, Tab on to Yes, Enter
  env.key(Key::Tab);
  CHECK_EQ(dl->focusIndex(), 3);  // Tab from Yes goes to No (the order is drop-down, check box, Yes, No) ...
  env.key(Key::Tab);
  CHECK_EQ(dl->focusIndex(), 0);  // ... and wraps to the drop-down
  env.key(Key::Down);
  env.key(Key::Down);
  CHECK_EQ(dd->selected(), 2);
  CHECK(!dd->isOpen());
  env.key(Key::Enter);  // Enter is the default button, also with the drop-down focused
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

TEST(le_ui_yes_with_each_ending) {
  struct Case {
    int entry;
    const char* text;
  } cases[] = {{0, "int a;\r\nint b;\r\nint c;\r\nint d;\r\n"},
               {1, "int a;\rint b;\rint c;\rint d;\r"},
               {2, "int a;\nint b;\nint c;\nint d;\n"}};
  for (const Case& c : cases) {
    Env env;
    Document* d = env.open("mixed.cpp");
    REQUIRE(dlg(env) != nullptr);
    DropDown* dd = dlg(env)->dropDown();
    // open the list with the mouse and click the entry
    env.click(dd->bounds());
    CHECK(dd->isOpen());
    env.click(dd->itemRect(c.entry));
    CHECK(!dd->isOpen());
    CHECK_EQ(dd->selected(), c.entry);
    CHECK(prompt(env).isOpen());  // choosing only changes the drop-down
    env.click(dlg(env)->yesRect());
    CHECK(!prompt(env).isOpen());
    CHECK_EQ(env.disk(d), std::string(c.text));
    CHECK(d->dirty());
    CHECK(env.ed->undo());
    CHECK_EQ(env.disk(d), std::string("int a;\r\nint b;\r\nint c;\nint d;\r\n"));
  }
}

TEST(le_ui_unicode_separator_entries_are_listed_but_disabled_until_the_buffer_has_them) {
  Env env;
  env.open("mixed.cpp");
  DropDown* dd = dlg(env)->dropDown();
  REQUIRE(dd->items().size() == 5);
  // task/LE-UNICODE: when LineEnding::LS / PS exist these two become enabled (LineEndingPrompt::kEntries)
  CHECK(!dd->items()[3].enabled);
  CHECK(!dd->items()[4].enabled);
  env.key(Key::Tab);
  env.key(Key::Tab);  // drop-down
  env.key(Key::Down);
  env.key(Key::Down);
  env.key(Key::Down);  // cannot go past Unix (LF)
  CHECK_EQ(dd->selected(), 2);
  env.click(dd->bounds());
  env.click(dd->itemRect(4));  // a disabled row does nothing
  CHECK(dd->isOpen());
  CHECK_EQ(dd->selected(), 2);
  env.key(Key::Escape);  // closes the list only
  CHECK(!dd->isOpen());
  CHECK(prompt(env).isOpen());
}

TEST(le_ui_answering_no_keeps_the_file_mixed_and_does_not_ask_again) {
  Env env;
  Document* d = env.open("mixed2.cpp");
  REQUIRE(prompt(env).isOpen());
  CHECK_EQ(dlg(env)->dropDown()->selected(), 2);  // two LF lines against one CRLF: Unix (LF)
  env.key(Key::Escape);                           // Escape is No
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
  // a second file is a second question; the No button keeps it as it is
  env.open("mixed.cpp");
  env.frame();
  CHECK(prompt(env).isOpen());
  env.click(dlg(env)->noRect());
  CHECK(!prompt(env).isOpen());
  CHECK(env.ed->currentDocument()->buffer().mixedLineEndings());
  CHECK(!env.ed->currentDocument()->dirty());
  // Tab to No, Enter: No as well
  Env env2;
  Document* m = env2.open("mixed.cpp");
  env2.key(Key::Tab);
  CHECK_EQ(dlg(env2)->focusIndex(), 3);
  env2.key(Key::Enter);
  CHECK(!prompt(env2).isOpen());
  CHECK(m->buffer().mixedLineEndings());
  CHECK(!m->dirty());
}

TEST(le_ui_a_click_outside_the_dialog_does_nothing) {
  Env env;
  env.open("mixed.cpp");
  REQUIRE(dlg(env) != nullptr);
  env.send(mouseEv(InputType::MouseDown, 2, 2));
  env.send(mouseEv(InputType::MouseUp, 2, 2));
  CHECK(prompt(env).isOpen());
  env.key(Key::Escape);
  CHECK(!prompt(env).isOpen());
}

TEST(le_ui_do_not_show_again_writes_the_user_settings_file) {
  for (int how = 0; how < 3; ++how) {  // answered with Yes, No and Escape: the check box counts every time
    Env env(true);
    const fs::path settings = env.dir / "userconf" / "settings.txt";
    writeFile(settings, "# my settings\r\nui.theme = vs-dark\r\neditor.checkLineEndings = true\r\neditor.tabWidth = 2\r\n");
    Document* d = env.open("mixed.cpp");
    REQUIRE(dlg(env) != nullptr);
    CheckBox* cb = dlg(env)->checkBox();
    CHECK_EQ(cb->label(), std::string("Do not show this message again"));
    // Tab, Tab, Tab: Yes -> No -> drop-down -> check box
    env.key(Key::Tab);
    env.key(Key::Tab);
    env.key(Key::Tab);
    CHECK_EQ(dlg(env)->focusIndex(), 1);
    env.key(Key::Space);
    CHECK(cb->checked());
    CHECK_EQ(slurp(settings).find("false"), std::string::npos);  // nothing is written before the answer
    if (how == 0) env.click(dlg(env)->yesRect());
    else if (how == 1) env.click(dlg(env)->noRect());
    else env.key(Key::Escape);
    CHECK(!prompt(env).isOpen());
    CHECK_EQ(d->dirty(), how == 0);
    CHECK_EQ(slurp(settings), std::string("# my settings\r\nui.theme = vs-dark\r\neditor.checkLineEndings = false\r\n"
                                          "editor.tabWidth = 2\r\n"));
    CHECK(!env.ed->editing().checkLineEndings);
    // a settings file reload picks the same value up, and the next mixed file opens without a question
    Settings st;
    st.load({}, env.dir / "userconf");
    CHECK(!st.getBool("editor.checkLineEndings"));
    env.open("mixed2.cpp");
    env.frame();
    CHECK(!prompt(env).isOpen());
  }
}

TEST(le_ui_do_not_show_again_creates_the_settings_file_and_an_unticked_box_writes_nothing) {
  Env env(true);
  const fs::path settings = env.dir / "userconf" / "settings.txt";
  env.open("mixed.cpp");
  env.click(dlg(env)->noRect());
  CHECK(!fs::exists(settings));
  CHECK(env.ed->editing().checkLineEndings);
  env.open("mixed2.cpp");
  CheckBox* cb = dlg(env)->checkBox();
  env.click(cb->bounds());  // a click on the label ticks it
  CHECK(cb->checked());
  env.click(cb->boxRect());  // and on the box unticks it again
  CHECK(!cb->checked());
  env.click(cb->boxRect());
  CHECK(cb->checked());
  env.click(dlg(env)->noRect());
  CHECK_EQ(slurp(settings), std::string("editor.checkLineEndings = false\n"));
  CHECK(!env.ed->editing().checkLineEndings);
}

TEST(le_ui_several_files_share_one_dialog_and_get_the_same_answer) {
  Env env;
  const int a = env.bench->openFile(env.dir / "mixed.cpp");
  const int b = env.bench->openFile(env.dir / "mixed2.cpp");
  const int c = env.bench->openFile(env.dir / "lf.cpp");  // consistent: not listed
  REQUIRE(a >= 0 && b >= 0 && c >= 0);
  env.bench->sync();
  env.frame();
  REQUIRE(dlg(env) != nullptr);
  CHECK_EQ(dlg(env)->files().size(), size_t(2));
  CHECK(dlg(env)->files()[0].label.find("mixed.cpp") != std::string::npos);
  CHECK(dlg(env)->files()[1].label.find("mixed2.cpp") != std::string::npos);
  CHECK_EQ(prompt(env).pending(), size_t(0));
  // 3 CRLF + 1 CRLF against 1 LF + 2 LF: CRLF 4, LF 3 -> Windows (CR LF)
  CHECK_EQ(dlg(env)->dropDown()->selected(), 0);
  env.click(dlg(env)->dropDown()->bounds());
  env.click(dlg(env)->dropDown()->itemRect(2));  // Unix (LF)
  env.click(dlg(env)->yesRect());
  CHECK(!prompt(env).isOpen());
  Document* m1 = env.ed->document(env.dir / "mixed.cpp");
  Document* m2 = env.ed->document(env.dir / "mixed2.cpp");
  Document* lf = env.ed->document(env.dir / "lf.cpp");
  REQUIRE(m1 && m2 && lf);
  CHECK_EQ(env.disk(m1), std::string("int a;\nint b;\nint c;\nint d;\n"));
  CHECK_EQ(env.disk(m2), std::string("x;\ny;\nz;\n"));
  CHECK(m1->dirty() && m2->dirty() && !lf->dirty());
  env.open("mixed2.cpp");  // one undo step per document
  CHECK(env.ed->undo());
  CHECK_EQ(env.disk(m2), std::string("x;\ny;\nz;\r\n"));
  CHECK_EQ(env.disk(m1), std::string("int a;\nint b;\nint c;\nint d;\n"));
}

TEST(le_ui_many_files_scroll_in_the_list) {
  Env env;
  for (int i = 0; i < 9; ++i) {
    writeFile(env.dir / ("m" + std::to_string(i) + ".cpp"), "a\r\nb\nc\r\n");
    env.bench->openFile(env.dir / ("m" + std::to_string(i) + ".cpp"));
  }
  env.bench->sync();
  env.frame();
  REQUIRE(dlg(env) != nullptr);
  CHECK_EQ(dlg(env)->files().size(), size_t(9));
  CHECK_EQ(dlg(env)->firstFileRow(), 0);
  const RectF l = dlg(env)->fileListRect();
  InputEvent w;
  w.type = InputType::Wheel;
  w.x = l.x + 5;
  w.y = l.y + 5;
  w.dy = -1;
  env.send(w);
  CHECK(dlg(env)->firstFileRow() > 0);
  CHECK(dlg(env)->firstFileRow() <= 3);
  env.key(Key::Escape);
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

// --- golden: the dialog in vs-dark (open drop-down list on top of the buttons in the second image) -----------------

namespace {
bool compareGolden(const std::vector<uint8_t>& px, int w, int h, const char* name) {
  const std::string path = std::string(TENFOLD_SOURCE_DIR "/tests/golden/") + name + ".png";
  if (uitest::updateGoldens) {
    gfx::writePngFile(path, px.data(), w, h);
    std::cerr << "    golden updated: " << path << "\n";
    return true;
  }
  int gw = 0, gh = 0;
  std::vector<uint8_t> golden;
  if (!gfx::readPngFile(path, gw, gh, golden)) {
    std::cerr << "    golden missing or unreadable; run tenfold-ui-tests --update-goldens\n";
    return false;
  }
  if (gw != w || gh != h) return false;
  size_t bad = 0;
  for (size_t i = 0; i < golden.size(); i += 4) {
    int d = 0;
    for (int ch = 0; ch < 4; ++ch) d = std::max(d, std::abs(int(golden[i + ch]) - int(px[i + ch])));
    if (d > 3) ++bad;
  }
  if (bad > golden.size() / 4 / 500) {
    gfx::writePngFile(std::string(name) + ".actual.png", px.data(), w, h);
    std::cerr << "    " << bad << " pixels differ; wrote " << name << ".actual.png\n";
    return false;
  }
  return true;
}
}  // namespace

TEST(le_ui_golden_dialog_vs_dark) {
  SKIP_IF_NO_GL(fx);
  std::string font;
  for (const char* p : {"/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
                        "/usr/share/fonts/dejavu/DejaVuSansMono.ttf", "/usr/share/fonts/TTF/DejaVuSansMono.ttf"})
    if (std::ifstream(p).good()) {
      font = p;
      break;
    }
  if (font.empty()) {
    std::cerr << "    SKIP golden: DejaVuSansMono not installed; goldens are only comparable with the same font\n";
    return;
  }
  std::string err;
  auto tr = gfx::TextRenderer::create(font, 15.0f, 1.0f, &err);
  REQUIRE(tr != nullptr);
  std::vector<SettingsDiag> diags;
  const Theme vs = loadTheme("vs-dark", {}, {}, diags);
  CHECK(diags.empty());
  const fs::path tmp = uitest::tempRoot() / "tf_le_golden";  // the folder name is in the status bar: fixed
  std::error_code rm;
  fs::remove_all(tmp, rm);
  fs::create_directories(tmp / "src");
  writeFile(tmp / "src/main.cpp", "int main() {\r\n  return 0;\r\n}\n");
  writeFile(tmp / "src/util.cpp", "int a;\nint b;\nint c;\r\n");
  {
    Editor::Options o;
    o.semantic.useClangd = false;
    o.semantic.persistFastIndex = false;
    o.watchForcePolling = true;
    o.watchPollMs = 60000;
    Editor ed(tmp, o);
    Workbench wb(*tr, ed, vs);
    EditorApp app(wb, ed, &wb);
    wb.openFile("src/main.cpp");
    wb.openFile("src/util.cpp");
    wb.sync();
    uitest::wrapOff(wb);
    int64_t now = 1000;
    gfx::GlRenderer& r = *fx->renderer;
    r.setOffscreen(true);
    ed.pump();
    app.tick(now += 10);
    const int w = 1024, h = 640;
    wb.render(r, w, h);
    REQUIRE(app.lineEndingPrompt().dialog() != nullptr);
    wb.render(r, w, h);
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4);
    REQUIRE(r.readPixels(w, h, px.data()));
    CHECK(compareGolden(px, w, h, "le_dialog_vs_dark"));
    // the drop-down open: its list lies over the check box and the buttons
    DropDown* dd = app.lineEndingPrompt().dialog()->dropDown();
    dd->setFocus();
    dd->open();
    wb.render(r, w, h);
    REQUIRE(r.readPixels(w, h, px.data()));
    CHECK(compareGolden(px, w, h, "le_dialog_dropdown_vs_dark"));
    tr->releaseTexture(r);
    app.closePickers();
  }
  fs::remove_all(tmp, rm);
}
