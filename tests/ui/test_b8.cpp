// B8: editing conveniences through the UI. Synthetic events against a recording renderer and the mock font (8 px cells,
// 14 px lines), the real embedded key bindings and settings defaults. Two goldens (pinned DejaVuSansMono, skipped
// without the font / GL): the find highlight and the bookmark gutter.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>

#include "../text/text_test_util.h"
#include "ui/gfx/PngWrite.h"
#include "ui/gfx/TextRenderer.h"
#include "ui/settings/KeyBindings.h"
#include "ui/settings/Settings.h"
#include "ui/shell/EditorApp.h"
#include "ui/widgets/ListPopup.h"
#include "ui/widgets/TextView.h"
#include "ui_test.h"

using namespace tf;
using namespace tf::ui;
namespace fs = std::filesystem;

namespace {

struct Recorder : gfx::Renderer {
  struct Rect {
    gfx::RectF r;
    gfx::Color c;
  };
  std::vector<Rect> rects;
  void beginFrame(int, int, gfx::Color) override { rects.clear(); }
  void drawRect(const gfx::RectF& r, gfx::Color c) override { rects.push_back({r, c}); }
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
InputEvent textEv(const std::string& s) {
  InputEvent e;
  e.type = InputType::Text;
  e.text = s;
  return e;
}

struct Env {
  fs::path dir;
  std::unique_ptr<Editor> ed;
  std::unique_ptr<gfx::TextRenderer> text;
  Recorder rec;
  std::unique_ptr<Workbench> bench;
  std::unique_ptr<EditorApp> app;
  int64_t now = 1000;

  explicit Env(const std::string& aText = "int alpha = 1;\nint beta = alpha;\n// Alpha\nint gamma;\nalpha();\n") {
    static int n = 0;
    dir = uitest::tempRoot() / ("tf_b8_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                                "_" + std::to_string(n++));
    fs::create_directories(dir);
    std::ofstream(dir / "a.cpp", std::ios::binary) << aText;
    std::ofstream(dir / "b.cpp", std::ios::binary) << "b0\nb1\nb2\nb3\n";
    gfx::FontStack stack;
    stack.add(std::make_shared<tftext::MockFont>(
        U"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ._-:,;/()[]{}<>+*&#?=!\"'\\|"));
    text = gfx::TextRenderer::fromFonts(std::move(stack));
    ed = std::make_unique<Editor>(dir);
    bench = std::make_unique<Workbench>(*text, *ed);
    app = std::make_unique<EditorApp>(*bench, *ed, bench.get());
    open("a.cpp");
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
  void type(const std::string& s) {
    for (char c : s) {
      now += 10;
      app->handleEvent(textEv(std::string(1, c)), now);
    }
    frame();
  }
  void open(const char* rel) {
    bench->openFile(rel);
    bench->sync();
    frame();
  }
  TextView* view() { return dynamic_cast<TextView*>(bench->focused()); }
  TextView* viewOf(const Document* d) {
    TextView* found = nullptr;
    std::function<void(Widget&)> walk = [&](Widget& w) {
      if (auto* v = dynamic_cast<TextView*>(&w))
        if (v->document() == d) found = v;
      for (const auto& c : w.children()) walk(*c);
    };
    walk(*bench);
    return found;
  }
  Document* doc() { return ed->currentDocument(); }
  std::string txt() { return doc()->buffer().text(); }
  std::string line(int n) { return doc()->buffer().line(n); }
  std::string sel() {
    const Selection s = ed->selections()[0];
    return toString(s.anchor) + "-" + toString(s.head);
  }
  void setCaret(int l, int c) { ed->setSelections({caretAt({l, c})}); }
};

}  // namespace

// --- defaults ---------------------------------------------------------------------------------------------------

TEST(b8_defaults_parse_clean_and_bind_the_new_commands) {
  Settings s;
  CHECK_EQ(s.diagnostics().size(), size_t(0));
  CHECK(s.getBool("editor.autoIndent"));
  CHECK(s.getBool("editor.autoClosePairs"));
  CHECK(s.getBool("editor.detectIndent"));
  CHECK(!s.getBool("editor.trimTrailingWhitespace"));
  CHECK(!s.getBool("editor.wordWrap"));
  CHECK_EQ(s.getString("editor.indentUnit"), std::string("4"));

  KeyMap map;
  std::vector<KeyBindingDiag> diags;
  parseKeyBindings(defaultKeyBindingsText(), "defaults", map, diags);
  CHECK_EQ(diags.size(), size_t(0));
  const std::pair<const char*, const char*> want[] = {{"Ctrl+/", "Editor.ToggleLineComment"},
                                                      {"Ctrl+Shift+/", "Editor.ToggleBlockComment"},
                                                      {"Ctrl+Shift+D", "Editor.Duplicate"},
                                                      {"Alt+Up", "Editor.MoveLinesUp"},
                                                      {"Alt+Down", "Editor.MoveLinesDown"},
                                                      {"Ctrl+J", "Editor.JoinLines"},
                                                      {"Ctrl+Shift+K", "Editor.DeleteLines"},
                                                      {"Ctrl+G", "Editor.GotoLine"},
                                                      {"Shift+Tab", "Editor.Outdent"},
                                                      {"Ctrl+F2", "Editor.ToggleBookmark"},
                                                      {"F2", "Editor.NextBookmark"},
                                                      {"Shift+F2", "Editor.PrevBookmark"},
                                                      {"Ctrl+Shift+F2", "Editor.ListBookmarks"},
                                                      {"Ctrl+F", "Editor.Find"},
                                                      {"Ctrl+H", "Editor.Replace"},
                                                      {"F3", "Editor.FindNext"},
                                                      {"Shift+F3", "Editor.FindPrev"}};
  for (const auto& [chord, cmd] : want) {
    bool found = false;
    for (const KeyBinding& b : map.bindings()) {
      KeyChord kc;
      std::string err;
      if (b.command == cmd && !b.chords.empty() && parseKeyChord(chord, kc, err) && b.chords[0] == kc) found = true;
    }
    if (!found) std::cerr << "    missing default: " << chord << " = " << cmd << "\n";
    CHECK(found);
  }
}

// --- typing: auto-close, auto-indent, indent keys -----------------------------------------------------------------

TEST(b8_typing_autoclose_autoindent_and_tab_keys) {
  Env env("");
  env.ed->editing().detectIndent = false;  // the scenario indents by 8 columns on purpose
  env.type("void f(");
  CHECK_EQ(env.txt(), std::string("void f()"));
  env.type(")");  // skips over the auto-inserted closer
  env.type(" {");
  CHECK_EQ(env.txt(), std::string("void f() {}"));
  env.key(Key::Enter);  // between { and }: the block opens
  CHECK_EQ(env.txt(), std::string("void f() {\n    \n}"));
  CHECK_EQ(env.sel(), std::string("2:5-2:5"));
  env.type("x = y;");
  env.key(Key::Enter);
  env.type("z();");
  CHECK_EQ(env.txt(), std::string("void f() {\n    x = y;\n    z();\n}"));
  // Tab on a caret inserts the unit; Shift+Tab outdents the line; Tab on a multi-line selection indents it.
  env.ed->setSelections({Selection{{1, 0}, {2, 2}}});
  env.key(Key::Tab);
  CHECK_EQ(env.line(1), std::string("        x = y;"));
  CHECK_EQ(env.line(2), std::string("        z();"));
  env.key(Key::Tab, ModShift);
  CHECK_EQ(env.line(1), std::string("    x = y;"));
  // Comment toggle and line commands through their default bindings.
  env.setCaret(1, 6);
  env.key(Key('/'), ModCtrl);
  CHECK_EQ(env.line(1), std::string("    // x = y;"));
  env.key(Key('/'), ModCtrl);
  CHECK_EQ(env.line(1), std::string("    x = y;"));
  env.key(Key('D'), ModCtrl | ModShift);
  CHECK_EQ(env.line(2), std::string("    x = y;"));
  env.key(Key('K'), ModCtrl | ModShift);
  CHECK_EQ(env.line(2), std::string("    z();"));
  env.key(Key::Up, ModAlt);
  CHECK_EQ(env.line(1), std::string("    z();"));
  CHECK_EQ(env.line(2), std::string("    x = y;"));
  env.key(Key('J'), ModCtrl);
  CHECK_EQ(env.line(1), std::string("    z(); x = y;"));
}

TEST(b8_settings_switch_the_behaviours) {
  Env env("");
  SettingsService svc({.workspaceRoot = env.dir, .userDir = {}});
  svc.settings().loadTexts("", "editor.autoClosePairs = false\neditor.indentUnit = tab\neditor.autoIndent = false\n");
  env.app->attachSettings(svc);
  CHECK(!env.ed->editing().autoClosePairs);
  CHECK(!env.ed->editing().autoIndent);
  CHECK_EQ(env.ed->editing().indentUnit, std::string("\t"));
  env.type("(");
  CHECK_EQ(env.txt(), std::string("("));
  svc.settings().loadTexts("", "editor.indentUnit = 2\neditor.trimTrailingWhitespace = true\n");
  CHECK(env.ed->editing().autoClosePairs);
  CHECK_EQ(env.ed->editing().indentUnit, std::string("  "));
  CHECK(env.ed->editing().trimTrailingWhitespace);
  env.app.reset();  // the app unregisters from the service: it must go first
}

// --- find / replace -----------------------------------------------------------------------------------------------

TEST(b8_find_highlights_steps_toggles_and_closes) {
  Env env;
  env.key(Key('F'), ModCtrl);
  FindReplace& fr = env.app->findReplace();
  REQUIRE(fr.isOpen());
  CHECK(env.bench->focused() == fr.findInput());
  env.type("alpha");
  CHECK_EQ(fr.matches().size(), size_t(4));  // alpha, alpha, Alpha, alpha: case-insensitive by default
  CHECK_EQ(fr.statusText(), std::string("1 of 4"));
  CHECK_EQ(env.sel(), std::string("1:5-1:10"));  // incremental: the first match is selected
  TextView* v = env.viewOf(env.doc());
  REQUIRE(v != nullptr);
  CHECK_EQ(v->findMatches().size(), size_t(4));
  env.key(Key::Enter);
  CHECK_EQ(env.sel(), std::string("2:12-2:17"));
  env.key(Key::Enter);
  CHECK_EQ(env.sel(), std::string("3:4-3:9"));
  env.key(Key::Enter, ModShift);
  CHECK_EQ(env.sel(), std::string("2:12-2:17"));
  env.key(Key::Enter);
  env.key(Key::Enter);
  env.key(Key::Enter);  // wraps around
  CHECK_EQ(env.sel(), std::string("1:5-1:10"));
  CHECK_EQ(fr.statusText(), std::string("1 of 4"));
  env.key(Key('C'), ModAlt);  // case sensitive: "Alpha" drops out
  CHECK(fr.caseSensitive());
  CHECK_EQ(fr.matches().size(), size_t(3));
  env.key(Key('R'), ModAlt);  // regex on: "alpha" is also a regex
  CHECK(fr.regex());
  env.key(Key('W'), ModAlt);
  CHECK(fr.wholeWord());
  CHECK_EQ(fr.matches().size(), size_t(3));
  fr.setQuery("al(");  // invalid regex: an error instead of matches
  CHECK(fr.matches().empty());
  CHECK(fr.statusText().find("invalid regex") != std::string::npos);
  fr.setQuery("^int \\w+");
  CHECK_EQ(fr.matches().size(), size_t(3));
  fr.setOptions(false, false, false);
  fr.setQuery("alpha");
  env.key(Key::Escape);
  CHECK(!fr.isOpen());
  CHECK(v->findMatches().empty());
  CHECK(env.bench->focused() == v);
  CHECK_EQ(env.sel(), std::string("1:5-1:10"));
  // F3 / Shift+F3 work with the bar closed, on the last query.
  env.key(Key::F3);
  CHECK_EQ(env.sel(), std::string("2:12-2:17"));
  env.key(Key::F3, ModShift);
  CHECK_EQ(env.sel(), std::string("1:5-1:10"));
}

TEST(b8_replace_one_and_all_are_undo_steps) {
  Env env;
  env.setCaret(0, 5);
  env.ed->setSelections({Selection{{0, 4}, {0, 9}}});  // "alpha" selected
  env.key(Key('H'), ModCtrl);
  FindReplace& fr = env.app->findReplace();
  REQUIRE(fr.isOpen());
  CHECK_EQ(fr.query(), std::string("alpha"));  // seeded from the selection
  env.key(Key::Tab);
  CHECK(env.bench->focused() == fr.replaceInput());
  env.type("omega");
  env.key(Key::Enter);  // replaces the selected match and selects the next
  CHECK_EQ(env.line(0), std::string("int omega = 1;"));
  CHECK_EQ(env.sel(), std::string("2:12-2:17"));
  CHECK_EQ(fr.matches().size(), size_t(3));
  env.key(Key::Enter, ModCtrl);  // replace the rest in one step
  CHECK_EQ(env.txt(), std::string("int omega = 1;\nint beta = omega;\n// omega\nint gamma;\nomega();\n"));
  CHECK(env.ed->undo());
  CHECK_EQ(env.txt(), std::string("int omega = 1;\nint beta = alpha;\n// Alpha\nint gamma;\nalpha();\n"));
  CHECK(env.ed->undo());
  CHECK_EQ(env.txt(), std::string("int alpha = 1;\nint beta = alpha;\n// Alpha\nint gamma;\nalpha();\n"));
  CHECK(!env.ed->undo());
}

// --- go to line ---------------------------------------------------------------------------------------------------

TEST(b8_go_to_line_popup_jumps_through_history) {
  std::string body;
  for (int i = 0; i < 120; ++i) body += "l" + std::to_string(i) + "\n";
  Env env(body);
  CHECK(env.key(Key('G'), ModCtrl));
  ListPopup* p = dynamic_cast<ListPopup*>(env.bench->modal());
  REQUIRE(p != nullptr);
  env.type("42");
  REQUIRE(p->rows().size() == 1);
  CHECK(p->rows()[0].primary.find("42") != std::string::npos);
  env.key(Key::Enter);
  CHECK(env.bench->modal() == nullptr);
  CHECK_EQ(env.sel(), std::string("42:1-42:1"));
  env.key(Key('G'), ModCtrl);
  env.type("100:2");
  env.key(Key::Enter);
  CHECK_EQ(env.sel(), std::string("100:2-100:2"));
  CHECK(env.key(Key::Left, ModAlt));  // the jump is in the navigation history
  CHECK_EQ(env.ed->selections()[0].head.line, 41);
  CHECK(env.key(Key::Left, ModAlt));
  CHECK_EQ(env.ed->selections()[0].head.line, 0);
  env.key(Key('G'), ModCtrl);
  env.type("x");  // not a number: Enter does nothing
  CHECK(env.bench->modal() != nullptr);
  env.key(Key::Enter);
  CHECK(env.bench->modal() != nullptr);
  env.key(Key::Escape);
  CHECK(env.bench->modal() == nullptr);
  int l = 0, c = 0;
  CHECK(GoToLine::parse("7:3", l, c) && l == 7 && c == 3);
  CHECK(!GoToLine::parse("7:", l, c));
  CHECK(!GoToLine::parse("-3", l, c));
  CHECK(!GoToLine::parse("0", l, c));
}

// --- bookmarks ----------------------------------------------------------------------------------------------------

TEST(b8_bookmarks_toggle_gutter_jump_list_persist_and_follow_edits) {
  std::string body;
  for (int i = 0; i < 60; ++i) body += "line " + std::to_string(i) + "\n";
  Env env(body);
  env.setCaret(10, 0);
  env.key(Key::F2, ModCtrl);
  env.setCaret(30, 0);
  env.key(Key::F2, ModCtrl);
  CHECK_EQ(env.ed->bookmarks().count(), size_t(2));
  CHECK(fs::exists(env.dir / ".tenfold" / "bookmarks.json"));
  // The gutter flag is a small accent-coloured square at the left edge of the line.
  TextView* v = env.viewOf(env.doc());
  REQUIRE(v != nullptr);
  env.setCaret(0, 0);
  env.frame();
  const RectF line10 = v->caretRect({10, 0});
  int flags = 0;
  for (const auto& r : env.rec.rects)
    if (r.c.r == env.bench->rootTheme().tabAccent.r && r.c.g == env.bench->rootTheme().tabAccent.g &&
        r.r.x < v->bounds().x + 8 && r.r.y >= line10.y - 1 && r.r.y + r.r.h <= line10.y + line10.h + 1 && r.r.w < 12)
      ++flags;
  CHECK_EQ(flags, 1);
  // F2 / Shift+F2 jump (and wrap) through gotoLocation.
  env.key(Key::F2);
  CHECK_EQ(env.ed->selections()[0].head.line, 10);
  env.key(Key::F2);
  CHECK_EQ(env.ed->selections()[0].head.line, 30);
  env.key(Key::F2);
  CHECK_EQ(env.ed->selections()[0].head.line, 10);
  env.key(Key::F2, ModShift);
  CHECK_EQ(env.ed->selections()[0].head.line, 30);
  CHECK(env.key(Key::Left, ModAlt));  // Back works from a bookmark jump
  CHECK(env.ed->selections()[0].head.line != 30);
  // The list popup.
  env.key(Key::F2, ModCtrl | ModShift);
  ListPopup* p = dynamic_cast<ListPopup*>(env.bench->modal());
  REQUIRE(p != nullptr);
  REQUIRE(p->rows().size() == 2);
  CHECK_EQ(p->rows()[0].primary, std::string("line 10"));
  CHECK_EQ(p->rows()[1].detail, std::string("a.cpp:31"));
  env.type("30");
  CHECK_EQ(p->rows().size(), size_t(1));
  env.key(Key::Enter);
  CHECK_EQ(env.ed->selections()[0].head.line, 30);
  // They follow edits: two lines inserted above move both, and the file is rewritten on save.
  env.ed->setSelections({caretAt({0, 0})});
  env.type("a");
  env.key(Key::Enter);
  env.key(Key::Enter);
  CHECK((env.ed->bookmarks().lines(env.doc()->path()) == std::vector<int>{12, 32}));
  env.key(Key('S'), ModCtrl);
  // A new session of the workspace brings them back.
  Editor second(env.dir);
  second.open("a.cpp");
  CHECK((second.bookmarks().lines(env.doc()->path()) == std::vector<int>{12, 32}));
}

// --- performance ----------------------------------------------------------------------------------------------------

TEST(b8_smart_typing_stays_in_the_keystroke_budget) {
  fs::path dir = uitest::tempRoot() /
                 ("tf_b8_perf_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  fs::create_directories(dir);
  {
    std::ofstream out(dir / "big.cpp", std::ios::binary);
    for (int i = 0; i < 100000; ++i) out << "    for (int k = 0; k < " << i << "; ++k) { total += k; }\n";
  }
  {
    Editor ed(dir);
    Document* d = ed.open("big.cpp");
    REQUIRE(d != nullptr);
    ed.gotoLocation({d->path(), {50000, 8}});
    using clk = std::chrono::steady_clock;
    const auto t0 = clk::now();
    const int n = 200;
    for (int i = 0; i < n; ++i) {
      ed.typeText("{");
      ed.insertNewline();
      ed.typeText("}");
      ed.typeText("(");
      ed.typeText(")");
    }
    const double ms = std::chrono::duration<double, std::milli>(clk::now() - t0).count() / (5.0 * n);
    std::printf("PERF: B8 smart typing ({ Enter } ( )) in a 100k-line file: %.4f ms per keystroke (budget 8 ms)\n", ms);
#if defined(NDEBUG) && !defined(__SANITIZE_ADDRESS__)
    CHECK(ms < 8.0);
#endif
  }
  std::error_code ec;
  fs::remove_all(dir, ec);
}

// --- goldens --------------------------------------------------------------------------------------------------------

namespace {
std::string monoFont() {
  for (const char* p : {"/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
                        "/usr/share/fonts/dejavu/DejaVuSansMono.ttf", "/usr/share/fonts/TTF/DejaVuSansMono.ttf"})
    if (std::ifstream(p).good()) return p;
  return {};
}

void checkGolden(gfx::GlRenderer& r, Workbench& wb, gfx::TextRenderer& tr, const char* name) {
  const int w = 1024, h = 640;
  r.setOffscreen(true);
  wb.render(r, w, h);
  std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4);
  REQUIRE(r.readPixels(w, h, px.data()));
  tr.releaseTexture(r);
  const std::string path = std::string(TENFOLD_SOURCE_DIR "/tests/golden/") + name;
  if (uitest::updateGoldens) {
    REQUIRE(gfx::writePngFile(path, px.data(), w, h));
    std::cerr << "    golden updated: " << path << "\n";
    return;
  }
  int gw = 0, gh = 0;
  std::vector<uint8_t> golden;
  if (!gfx::readPngFile(path, gw, gh, golden)) {
    CHECK(!"golden missing or unreadable; run tenfold-ui-tests --update-goldens");
    return;
  }
  REQUIRE(gw == w && gh == h);
  size_t bad = 0;
  for (size_t i = 0; i < golden.size(); i += 4) {
    int d = 0;
    for (int c = 0; c < 4; ++c) d = std::max(d, std::abs(int(golden[i + c]) - int(px[i + c])));
    if (d > 3) ++bad;
  }
  if (bad > golden.size() / 4 / 500) {
    gfx::writePngFile(std::string(name) + ".actual.png", px.data(), w, h);
    std::cerr << "    " << bad << " pixels differ; wrote " << name << ".actual.png\n";
  }
  CHECK(bad <= golden.size() / 4 / 500);
}
}  // namespace

TEST(b8_golden_find_highlight_and_bookmark_gutter) {
  SKIP_IF_NO_GL(fx);
  const std::string font = monoFont();
  if (font.empty()) {
    std::cerr << "    SKIP golden: DejaVuSansMono not installed; goldens are only comparable with the same font\n";
    return;
  }
  std::string err;
  auto tr = gfx::TextRenderer::create(font, 15.0f, 1.0f, &err);
  REQUIRE(tr != nullptr);
  Editor::Options o;
  o.semantic.useClangd = false;
  o.semantic.persistFastIndex = false;
  o.watchForcePolling = true;
  o.watchPollMs = 60000;
  const fs::path tmp = uitest::tempRoot() / "tf_b8_golden";  // the folder name is in the status bar: fixed
  std::error_code rm;
  fs::remove_all(tmp, rm);
  fs::create_directories(tmp / "src");
  fs::copy_file(TENFOLD_SOURCE_DIR "/examples/sample_project/src/main.cpp", tmp / "src/main.cpp");
  {
    Editor ed(tmp, o);  // no start(): no index needed, and bookmarks.json goes to the temp workspace
    Workbench wb(*tr, ed);
    wb.openFile("src/main.cpp");
    wb.sync();
    EditorApp app(wb, ed, &wb);
    int64_t now = 1000;
    auto send = [&](const InputEvent& e) {
      app.handleEvent(e, now += 10);
      ed.pump();
      app.tick(now);
    };
    fx->renderer->setOffscreen(true);
    wb.render(*fx->renderer, 1024, 640);
    send(keyEv(Key('F'), ModCtrl));
    for (char c : std::string("geo")) send(textEv(std::string(1, c)));
    send(keyEv(Key::Enter));
    send(keyEv(Key::Enter));
    REQUIRE(app.findReplace().matches().size() > 5);
    checkGolden(*fx->renderer, wb, *tr, "b8_find_highlight.png");
    send(keyEv(Key::Escape));
    for (int line : {6, 11, 16}) {
      ed.moveCursor({line, 0});
      send(keyEv(Key::F2, ModCtrl));
    }
    ed.moveCursor({0, 0});
    app.tick(now += 10);
    checkGolden(*fx->renderer, wb, *tr, "b8_bookmark_gutter.png");
  }
  std::error_code ec;
  fs::remove_all(tmp, ec);
}
