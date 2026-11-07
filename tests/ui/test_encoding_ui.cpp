// Encodings like Visual Studio (VS-ENC): the status bar's encoding segment and its menu, Reopen with Encoding (with the
// Save / Don't Save / Cancel question for a modified document), Save with Encoding (Advanced Save Options: encoding
// and line endings in one list), the way out of a refused save, and the settings editor.autoDetectUtf8 /
// editor.defaultEncoding. Synthetic events against a null renderer and the mock font.
#include <chrono>
#include <cmath>
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
    dir =
        uitest::tempRoot() / ("tf_enc_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                              "_" + std::to_string(n++));
    fs::create_directories(dir);
    writeFile(dir / "ascii.cpp", "int a;\r\nint b;\r\n");
    writeFile(dir / "utf8.cpp", "// caf\xC3\xA9\nint a;\n");
    writeFile(dir / "bom.cpp", "\xEF\xBB\xBFint a;\n");
    writeFile(dir / "latin.cpp", "// caf\xE9\nint a;\n");
    std::string le, be;
    encodeText("int a; // caf\xC3\xA9\r\n", Encoding::Utf16LE, le);
    encodeText("int b;\n", Encoding::Utf16BE, be);
    writeFile(dir / "u16le.cpp", le);
    writeFile(dir / "u16be.cpp", be);
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
  void rightClick(const RectF& r) {
    InputEvent e = mouseEv(InputType::MouseDown, r.x + r.w / 2, r.y + r.h / 2);
    e.button = MouseButton::Right;
    send(e);
    e.type = InputType::MouseUp;
    send(e);
  }
  // Takes the row with `key` of the open popup (select it, Enter).
  void pick(ListPopup* p, const std::string& key) {
    REQUIRE(p != nullptr);
    const int i = p->indexOfKey(key);
    REQUIRE(i >= 0);
    p->setSelected(i);
    key_(Key::Enter);
  }
  void key_(Key k) { key(k); }
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

EncodingPrompt& prompt(Env& e) { return e.app->encodingPrompt(); }

const ListRow* rowOf(ListPopup* p, const std::string& key) {
  if (p)
    for (const auto& r : p->rows())
      if (r.key == key) return &r;
  return nullptr;
}

}  // namespace

TEST(enc_ui_status_segment_shows_the_encoding_like_visual_studio) {
  Env env;
  const std::pair<const char*, const char*> want[] = {
      {"ascii.cpp", "UTF-8"},        {"utf8.cpp", "UTF-8"},      {"bom.cpp", "UTF-8 with signature"},
      {"latin.cpp", "Windows-1252"}, {"u16le.cpp", "UTF-16 LE"}, {"u16be.cpp", "UTF-16 BE"}};
  for (const auto& [file, label] : want) {
    Document* d = env.open(file);
    REQUIRE(d != nullptr);
    env.frame();
    CHECK_EQ(env.status("encoding"), std::string(label));
  }
  // The segment sits right next to the line-ending segment, on the right.
  const RectF enc = env.bench->status().segmentRect("encoding"), eol = env.bench->status().segmentRect("eol");
  REQUIRE(enc.w > 0 && eol.w > 0);
  CHECK(std::abs((enc.x + enc.w) - eol.x) < 1.0f || std::abs((eol.x + eol.w) - enc.x) < 1.0f);
}

TEST(enc_ui_clicking_the_segment_opens_the_reopen_and_save_menu_with_either_button) {
  Env env;
  env.open("utf8.cpp");
  env.frame();
  RectF seg = env.bench->status().segmentRect("encoding");
  REQUIRE(seg.w > 0);
  env.click(seg);  // left
  REQUIRE(env.menu() != nullptr);
  CHECK_EQ(env.menu()->items().size(), size_t(2));
  CHECK(env.indexOf("Reopen with Encoding...") == 0);
  CHECK(env.indexOf("Save with Encoding...") == 1);
  env.key(Key::Escape);
  CHECK(env.menu() == nullptr);
  env.rightClick(seg);  // right
  REQUIRE(env.menu() != nullptr);
  CHECK(env.indexOf("Reopen with Encoding...") >= 0 && env.indexOf("Save with Encoding...") >= 0);
  env.clickItem("Reopen with Encoding...");
  REQUIRE(prompt(env).isOpen());
  CHECK(prompt(env).popup()->title().find("Reopen utf8.cpp with Encoding") != std::string::npos);
  env.key(Key::Escape);
  CHECK(!prompt(env).isOpen());
  env.click(seg);
  env.clickItem("Save with Encoding...");
  REQUIRE(prompt(env).isOpen());
  CHECK(prompt(env).popup()->title().find("Advanced Save Options") != std::string::npos);
}

TEST(enc_ui_reopen_lists_the_decodable_encodings_and_decodes_the_file_again) {
  Env env;
  Document* d = env.open("utf8.cpp");
  REQUIRE(d != nullptr);
  env.run("File.ReopenWithEncoding");
  REQUIRE(prompt(env).isOpen());
  ListPopup* p = prompt(env).popup();
  CHECK(env.rows(p) == (std::vector<std::string>{"UTF-8", "UTF-16 LE", "UTF-16 BE", "Windows-1252"}));
  CHECK_EQ(p->selected(), 0);  // the current encoding
  CHECK_EQ(p->rows()[0].secondary, std::string("current"));
  env.pick(p, "enc:windows-1252");
  CHECK(!prompt(env).isOpen());
  CHECK(d->encoding() == Encoding::Windows1252);
  CHECK_EQ(d->buffer().line(0), std::string("// caf\xC3\x83\xC2\xA9"));
  CHECK(!d->dirty());
  env.frame();
  CHECK_EQ(env.status("encoding"), std::string("Windows-1252"));
  CHECK(env.app->message().find("Reopened utf8.cpp as Windows-1252") != std::string::npos);
  // The picker marks the new current encoding; UTF-16 LE file reopened as UTF-16 LE keeps its text.
  env.run("File.ReopenWithEncoding");
  CHECK_EQ(prompt(env).popup()->selected(), 3);
  env.key(Key::Escape);
  Document* u = env.open("u16le.cpp");
  env.run("File.ReopenWithEncoding");
  CHECK_EQ(prompt(env).popup()->selected(), 1);
  env.pick(prompt(env).popup(), "enc:utf-16be");  // the same bytes read the other way round: still decodes
  CHECK(u->encoding() == Encoding::Utf16BE);
  env.run("File.ReopenWithEncoding");
  env.pick(prompt(env).popup(), "enc:utf-16le");
  CHECK_EQ(u->buffer().line(0), std::string("int a; // caf\xC3\xA9"));
  // UTF-16 needs well-formed text: latin.cpp has an odd number of bytes -> says so, nothing changes.
  Document* o = env.open("latin.cpp");
  env.run("File.ReopenWithEncoding");
  env.pick(prompt(env).popup(), "enc:utf-16le");
  CHECK(o->encoding() == Encoding::Windows1252);
  CHECK(env.app->message().find("Cannot reopen latin.cpp as UTF-16 LE") != std::string::npos);
}

TEST(enc_ui_reopen_of_a_modified_document_asks_save_dont_save_or_cancel) {
  Env env;
  Document* d = env.open("utf8.cpp");
  env.ed->insertText("// x\n");
  REQUIRE(d->dirty());
  // Cancel: nothing happens.
  env.run("File.ReopenWithEncoding");
  env.pick(prompt(env).popup(), "enc:windows-1252");
  REQUIRE(env.app->unsavedPrompt().isOpen());
  CHECK(env.app->unsavedPrompt().popup()->title().find("Save changes to utf8.cpp?") != std::string::npos);
  CHECK(d->encoding() == Encoding::Utf8);
  env.key(Key::Escape);
  CHECK(!env.app->unsavedPrompt().isOpen());
  CHECK(d->encoding() == Encoding::Utf8 && d->dirty());
  // Don't Save: the edits are lost, the file is read in the chosen encoding.
  env.run("File.ReopenWithEncoding");
  env.pick(prompt(env).popup(), "enc:windows-1252");
  env.pick(env.app->unsavedPrompt().popup(), "discard");
  CHECK(d->encoding() == Encoding::Windows1252 && !d->dirty());
  CHECK_EQ(d->buffer().line(0), std::string("// caf\xC3\x83\xC2\xA9"));
  CHECK(!d->canUndo());
  // Save: the edit is written first (in the current encoding), then the file is read again.
  env.ed->moveCursor({0, 0});
  env.ed->insertText("// y\n");
  env.run("File.ReopenWithEncoding");
  env.pick(prompt(env).popup(), "enc:utf-8");
  env.pick(env.app->unsavedPrompt().popup(), "save");
  CHECK_EQ(slurp(env.dir / "utf8.cpp"), std::string("// y\n// caf\xC3\xA9\nint a;\n"));
  CHECK(d->encoding() == Encoding::Utf8);
  CHECK_EQ(d->buffer().line(1), std::string("// caf\xC3\xA9"));
  CHECK(!d->dirty());
}

TEST(enc_ui_reopen_save_that_is_refused_shows_the_characters_and_does_not_reopen) {
  Env env;
  Document* d = env.open("latin.cpp");
  REQUIRE(d->encoding() == Encoding::Windows1252);
  env.ed->insertText("\xE6\xBC\xA2");  // U+6F22 cannot be saved in Windows-1252
  env.run("File.ReopenWithEncoding");
  env.pick(prompt(env).popup(), "enc:utf-8");
  REQUIRE(env.app->unsavedPrompt().isOpen());
  env.pick(env.app->unsavedPrompt().popup(), "save");
  REQUIRE(env.app->saveRefusedDialog().isOpen());
  CHECK(d->encoding() == Encoding::Windows1252 && d->dirty());  // not reopened, nothing written
  CHECK_EQ(slurp(env.dir / "latin.cpp"), std::string("// caf\xE9\nint a;\n"));
}

TEST(enc_ui_save_with_encoding_is_one_list_with_encoding_and_line_endings) {
  Env env;
  Document* d = env.open("utf8.cpp");
  env.run("File.SaveWithEncoding");
  REQUIRE(prompt(env).isOpen());
  ListPopup* p = prompt(env).popup();
  CHECK_EQ(p->title(), std::string("Advanced Save Options: utf8.cpp"));
  CHECK(env.rows(p) ==
        (std::vector<std::string>{"Save", "Encoding:", "  (*) UTF-8", "  ( ) UTF-8 with signature", "  ( ) UTF-16 LE",
                                  "  ( ) UTF-16 BE", "  ( ) Windows-1252", "Line endings:", "  (*) Current Setting",
                                  "  ( ) Windows (CR LF)", "  ( ) Macintosh (CR)", "  ( ) Unix (LF)"}));
  CHECK_EQ(rowOf(p, "enc:0")->secondary, std::string("current"));
  CHECK_EQ(rowOf(p, "le:0")->secondary, std::string("Unix (LF)"));
  CHECK_EQ(p->rows()[0].secondary, std::string("UTF-8, Current Setting"));
  // Choose UTF-16 BE and Windows (CR LF): the list shows the choices; nothing is written yet.
  env.pick(p, "enc:3");
  REQUIRE(prompt(env).isOpen());
  p = prompt(env).popup();
  CHECK_EQ(p->selectedRow()->key, std::string("enc:3"));
  CHECK(rowOf(p, "enc:3")->primary.find("(*)") != std::string::npos);
  CHECK(rowOf(p, "enc:0")->primary.find("( )") != std::string::npos);
  env.pick(p, "le:1");
  p = prompt(env).popup();
  CHECK_EQ(p->rows()[0].secondary, std::string("UTF-16 BE, Windows (CR LF)"));
  CHECK_EQ(slurp(env.dir / "utf8.cpp"), std::string("// caf\xC3\xA9\nint a;\n"));
  CHECK(d->encoding() == Encoding::Utf8);
  env.pick(p, "save");
  CHECK(!prompt(env).isOpen());
  std::string want;
  encodeText("// caf\xC3\xA9\r\nint a;\r\n", Encoding::Utf16BE, want);
  CHECK_EQ(slurp(env.dir / "utf8.cpp"), want);
  CHECK(d->encoding() == Encoding::Utf16BE && !d->dirty());
  env.frame();
  CHECK_EQ(env.status("encoding"), std::string("UTF-16 BE"));
  CHECK(env.app->message().find("Saved utf8.cpp as UTF-16 BE") != std::string::npos);
  // Escape cancels everything.
  env.run("File.SaveWithEncoding");
  env.pick(prompt(env).popup(), "enc:4");
  env.key(Key::Escape);
  CHECK(!prompt(env).isOpen());
  CHECK(d->encoding() == Encoding::Utf16BE);
  // The signature: UTF-8 with signature from the list, then plain Save keeps it.
  env.run("File.SaveWithEncoding");
  env.pick(prompt(env).popup(), "enc:1");
  env.pick(prompt(env).popup(), "save");
  CHECK_EQ(slurp(env.dir / "utf8.cpp").substr(0, 3), std::string("\xEF\xBB\xBF"));
  env.frame();
  CHECK_EQ(env.status("encoding"), std::string("UTF-8 with signature"));
}

TEST(enc_ui_save_with_an_encoding_that_cannot_hold_the_text_is_refused_and_changes_nothing) {
  Env env;
  Document* d = env.open("utf8.cpp");
  env.ed->insertText("\xE6\xBC\xA2");
  env.run("File.SaveWithEncoding");
  env.pick(prompt(env).popup(), "enc:4");  // Windows-1252
  env.pick(prompt(env).popup(), "le:3");   // Unix (LF) ... already LF; use CR to see that it is not applied either
  env.pick(prompt(env).popup(), "le:2");
  env.pick(prompt(env).popup(), "save");
  REQUIRE(env.app->saveRefusedDialog().isOpen());
  CHECK(d->encoding() == Encoding::Utf8 && d->dirty());
  CHECK(d->buffer().lineEnding() == LineEnding::LF);
  CHECK_EQ(slurp(env.dir / "utf8.cpp"), std::string("// caf\xC3\xA9\nint a;\n"));
  CHECK(env.app->message().find("Windows-1252 cannot hold 1 character") != std::string::npos);
  env.key(Key::Escape);
  // UTF-8 with signature takes it.
  env.run("File.SaveWithEncoding");
  env.pick(prompt(env).popup(), "enc:1");
  env.pick(prompt(env).popup(), "save");
  CHECK(!env.app->saveRefusedDialog().isOpen());
  CHECK_EQ(slurp(env.dir / "utf8.cpp"), std::string("\xEF\xBB\xBF\xE6\xBC\xA2// caf\xC3\xA9\nint a;\n"));
}

TEST(enc_ui_refusal_dialog_offers_save_with_encoding_with_utf8_selected) {
  Env env;
  Document* d = env.open("latin.cpp");
  REQUIRE(d->encoding() == Encoding::Windows1252);
  env.ed->insertText("\xE6\xBC\xA2");
  env.run("File.Save");
  REQUIRE(env.app->saveRefusedDialog().isOpen());
  ListPopup* r = env.app->saveRefusedDialog().popup();
  const ListRow* row = rowOf(r, "encoding");
  REQUIRE(row != nullptr);
  CHECK_EQ(row->primary, std::string("Save with Encoding..."));
  CHECK_EQ(r->rows().back().key, std::string("encoding"));
  r->setSelected(r->indexOfKey("encoding"));
  env.key(Key::Enter);
  CHECK(!env.app->saveRefusedDialog().isOpen());
  REQUIRE(prompt(env).isOpen());
  CHECK_EQ(prompt(env).popup()->rows()[0].secondary, std::string("UTF-8, Current Setting"));  // UTF-8 preselected
  env.key(Key::Enter);                                                                        // Save
  CHECK(!prompt(env).isOpen() && !env.app->saveRefusedDialog().isOpen());
  CHECK(d->encoding() == Encoding::Utf8 && !d->dirty());
  CHECK_EQ(slurp(env.dir / "latin.cpp"), std::string("\xE6\xBC\xA2// caf\xC3\xA9\nint a;\n"));
  env.frame();
  CHECK_EQ(env.status("encoding"), std::string("UTF-8"));
}

TEST(enc_ui_settings_auto_detect_utf8_and_default_encoding) {
  // The settings: schema, defaults and spellings (EditorApp copies them into Editor::editing()).
  {
    Settings s;
    s.loadTexts("", "");
    CHECK(s.getBool("editor.autoDetectUtf8"));
    CHECK_EQ(s.getString("editor.defaultEncoding"), std::string("utf-8"));
    s.loadTexts("editor.autoDetectUtf8 = false\neditor.defaultEncoding = UTF-16LE\n", "");
    CHECK(!s.getBool("editor.autoDetectUtf8"));
    CHECK(parseEncodingChoice(s.getString("editor.defaultEncoding")) == EncodingChoice::Utf16LE);
  }
  Env env;
  env.ed->editing().autoDetectUtf8 = false;
  Document* d = env.open("utf8.cpp");
  CHECK(d->encoding() == Encoding::Windows1252);  // BOM-less non-ASCII is read with the fallback code page
  CHECK_EQ(env.status("encoding"), std::string("Windows-1252"));
  CHECK(env.open("bom.cpp")->encodingChoice() == EncodingChoice::Utf8Signature);
  CHECK(env.open("ascii.cpp")->encodingChoice() == EncodingChoice::Utf8);
  env.ed->editing().autoDetectUtf8 = true;
  struct C {
    EncodingChoice c;
    const char* label;
  };
  for (const C& c : {C{EncodingChoice::Utf8Signature, "UTF-8 with signature"}, C{EncodingChoice::Utf16LE, "UTF-16 LE"},
                     C{EncodingChoice::Windows1252, "Windows-1252"}, C{EncodingChoice::Utf8, "UTF-8"}}) {
    env.ed->editing().defaultEncoding = c.c;
    env.run("File.New");
    env.frame();
    CHECK_EQ(env.status("encoding"), std::string(c.label));
  }
}

TEST(enc_ui_untitled_document_only_takes_the_encoding_from_save_with_encoding) {
  Env env;
  env.run("File.New");
  Document* d = env.ed->currentDocument();
  REQUIRE(d != nullptr);
  env.run("File.ReopenWithEncoding");
  CHECK(!prompt(env).isOpen());  // no file to read
  env.ed->insertText("caf\xC3\xA9");
  env.run("File.SaveWithEncoding");
  env.pick(prompt(env).popup(), "enc:4");
  CHECK_EQ(prompt(env).popup()->rows()[0].primary, std::string("Use this encoding"));
  env.pick(prompt(env).popup(), "save");
  CHECK(d->isUntitled() && d->encoding() == Encoding::Windows1252);
  env.frame();
  CHECK_EQ(env.status("encoding"), std::string("Windows-1252"));
}
