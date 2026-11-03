// B-INT: Workbench + TextView + commands/key bindings wired together by EditorApp. Synthetic events against a null
// renderer and a mock font; the default key bindings are the real embedded ones.
#include <chrono>
#include <filesystem>
#include <fstream>

#include "../text/text_test_util.h"
#include "ui/gfx/TextRenderer.h"
#include "ui/platform/Clipboard.h"
#include "ui/shell/EditorApp.h"
#include "ui/widgets/TextView.h"
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

// Replaces the system clipboard for the lifetime of the object.
struct FakeClipboard {
  std::string value;
  FakeClipboard() {
    setClipboardBackend({[this] { return value; },
                         [this](const std::string& s) {
                           value = s;
                           return true;
                         }});
  }
  ~FakeClipboard() { setClipboardBackend({}); }
};

InputEvent keyEv(Key k, uint8_t mods = 0, InputType t = InputType::KeyDown) {
  InputEvent e;
  e.type = t;
  e.key = k;
  e.mods = mods;
  return e;
}

struct Env {
  fs::path dir;
  std::unique_ptr<Editor> ed;
  std::unique_ptr<gfx::TextRenderer> text;
  NullRenderer nr;
  std::unique_ptr<Workbench> bench;
  std::unique_ptr<EditorApp> app;
  int64_t now = 1000;

  explicit Env(const std::string& userKeys = {}) {
    static int n = 0;
    dir =
        uitest::tempRoot() / ("tf_app_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                              "_" + std::to_string(n++));
    fs::create_directories(dir / ".tenfold");
    for (const char* f : {"a.cpp", "b.cpp", "c.cpp"}) {
      std::ofstream out(dir / f, std::ios::binary);
      for (int i = 0; i < 10; ++i) out << "int " << f[0] << i << ";\n";
    }
    if (!userKeys.empty()) std::ofstream(dir / ".tenfold" / "KeyMappings.10x_settings", std::ios::binary) << userKeys;
    gfx::FontStack stack;
    stack.add(std::make_shared<tftext::MockFont>(U"abcdefghijklmnopqrstuvwxyz0123456789 ._-:,;×●…ABCLMNTUFDK"));
    text = gfx::TextRenderer::fromFonts(std::move(stack));
    ed = std::make_unique<Editor>(dir);
    bench = std::make_unique<Workbench>(*text, *ed);
    app = std::make_unique<EditorApp>(*bench, *ed, bench.get());
    bench->render(nr, 1000, 600);
  }
  ~Env() {
    app.reset();
    bench.reset();
    ed.reset();
    std::error_code ec;
    fs::remove_all(dir, ec);
  }
  bool key(Key k, uint8_t mods = 0) {
    now += 10;
    const bool r = app->handleEvent(keyEv(k, mods), now);
    app->tick(now);
    bench->render(nr, 1000, 600);
    return r;
  }
  void type(const std::string& s) {
    InputEvent e;
    e.type = InputType::Text;
    e.text = s;
    now += 10;
    app->handleEvent(e, now);
  }
  std::string activeFile() {
    Panel* p = bench->activePanel();
    return bench->tabPath(p->activeId()).filename().string();
  }
  std::string curFile() { return ed->currentDocument()->path().filename().string(); }
  std::string status(const char* seg) { return bench->status().text(seg); }
};

}  // namespace

TEST(app_tab_switch_records_no_history_but_goto_does_and_alt_left_returns) {
  Env env;
  env.bench->openFile("a.cpp");
  env.bench->openFile("b.cpp");
  env.bench->sync();
  CHECK_EQ(env.activeFile(), std::string("b.cpp"));
  const size_t base = env.ed->navigation().entries().size();

  // Ctrl+Tab (MRU switch) and Ctrl+Shift+Tab: tab switches, not jumps.
  CHECK(env.key(Key::Tab, ModCtrl));
  env.app->handleEvent(keyEv(Key::Unknown, 0, InputType::KeyUp), env.now);  // Ctrl released
  CHECK_EQ(env.activeFile(), std::string("a.cpp"));
  CHECK_EQ(env.curFile(), std::string("a.cpp"));
  CHECK(env.key(Key::Tab, ModCtrl | ModShift));
  env.app->handleEvent(keyEv(Key::Unknown, 0, InputType::KeyUp), env.now);
  CHECK_EQ(env.activeFile(), std::string("b.cpp"));
  CHECK_EQ(env.ed->navigation().entries().size(), base);
  CHECK(!env.ed->navigation().canGoBack() || env.ed->navigation().entries().size() == base);

  // Clicking-style switch through the panel as well.
  Panel* p = env.bench->activePanel();
  const int idA = env.bench->openFile("a.cpp");  // focuses the existing tab
  CHECK(p->activeId() == idA);
  CHECK_EQ(env.ed->navigation().entries().size(), base);

  // A jump (what F12 does on a single result) into another file records history and reveals/creates its tab.
  env.ed->gotoLocation({env.dir / "c.cpp", {4, 0}});
  env.app->tick(env.now += 10);
  CHECK_EQ(env.activeFile(), std::string("c.cpp"));
  CHECK(env.ed->navigation().entries().size() > base);

  // Control - (PrevLocation, tenfold's Nav.Back) returns to the previous tab.
  CHECK(env.key(Key('-'), ModCtrl));
  CHECK_EQ(env.curFile(), std::string("a.cpp"));
  CHECK_EQ(env.activeFile(), std::string("a.cpp"));
  CHECK(env.key(Key('-'), ModCtrl | ModShift));
  CHECK_EQ(env.activeFile(), std::string("c.cpp"));
  // The revealed tab's view has focus: typing edits c.cpp.
  env.type("Z");
  CHECK(env.ed->currentDocument()->buffer().line(4).rfind("Z", 0) == 0);
}

TEST(app_pending_chord_text_in_status_bar) {
  Env env("Control K, Control U: Editor.Undo\n");
  env.bench->openFile("a.cpp");
  env.type("Q");
  CHECK_EQ(env.ed->currentDocument()->buffer().line(0), std::string("Qint a0;"));
  CHECK(env.key(Key('K'), ModCtrl));
  CHECK(env.status("message").find("Control K") != std::string::npos);
  CHECK_EQ(env.status("message"), env.app->message());
  CHECK(env.app->keys().pending());
  CHECK(env.key(Key('U'), ModCtrl));
  CHECK_EQ(env.status("message"), std::string());
  CHECK_EQ(env.ed->currentDocument()->buffer().line(0), std::string("int a0;"));

  // An unfinished chord expires (KeyMap::tick, driven by EditorApp::tick) and the text goes away.
  CHECK(env.key(Key('K'), ModCtrl));
  CHECK(!env.status("message").empty());
  env.app->tick(env.now + 5000);
  CHECK_EQ(env.status("message"), std::string());
  // A stroke that breaks a chord is swallowed and clears the text.
  CHECK(env.key(Key('K'), ModCtrl));
  env.type("X");  // text events are not strokes
  CHECK(env.key(Key('X'), ModCtrl));
  CHECK_EQ(env.status("message"), std::string());
}

TEST(app_unbound_ui_commands_say_not_available_and_escape_does_not_quit) {
  Env env;
  env.bench->openFile("a.cpp");
  CHECK(env.key(Key('O'),
                ModCtrl));  // OpenFile (File.OpenFile): B5a opens the picker (key context "picker" while it is open)
  CHECK(env.bench->modal() != nullptr);
  CHECK(env.key(Key::Escape));  // closes the picker; Escape never reaches the editor's CollapseCarets binding
  CHECK(env.bench->modal() == nullptr);
  CHECK(env.key(Key('F'), ModCtrl | ModShift));  // find in files: B5b opens its popup
  CHECK(env.bench->modal() != nullptr);
  CHECK(env.key(Key::Escape));
  CHECK(env.bench->modal() == nullptr);
  CHECK(env.key(Key::F4));  // GotoNextResult with no search yet
  CHECK(env.status("message").find("No search results") != std::string::npos);
  CHECK(env.key(Key(','), ModCtrl));  // symbol search needs a started Editor (this fixture has none): disabled
  CHECK(env.bench->modal() == nullptr);

  env.ed->addCaretBelow();
  CHECK_EQ(env.ed->selections().size(), size_t(2));
  env.key(Key::Escape);
  CHECK(!env.app->quitRequested());
  CHECK_EQ(env.ed->selections().size(), size_t(1));  // Escape collapses the carets
  env.key(Key::Escape);
  CHECK(!env.app->quitRequested());
  env.key(Key('Q'), ModCtrl);
  CHECK(env.app->quitRequested());
}

TEST(app_keybinding_file_errors_show_in_status_bar_and_other_bindings_work) {
  Env env("# comment\nControl Shift Bogus: Editor.Undo\nControl Shift J: Editor.CaretDocEnd\nF5 Foo\n");
  CHECK(!env.app->diagnostics().empty());
  const std::string m = env.status("message");
  CHECK(m.find("KeyMappings.10x_settings:2") != std::string::npos);
  CHECK(m.find("more") != std::string::npos);  // two bad lines: "(+1 more)"
  CHECK_EQ(m, formatDiag(env.app->diagnostics()[0]) + " (+1 more)");
  env.bench->openFile("a.cpp");
  CHECK(env.key(Key('J'), ModCtrl | ModShift));     // the good user binding
  CHECK_EQ(env.ed->selections()[0].head.line, 10);  // the last line is the empty one after the final newline
  CHECK(env.key(Key('S'), ModCtrl));                // a default binding
  env.type("!");
  CHECK(env.key(Key('Z'), ModCtrl));
  CHECK_EQ(env.ed->currentDocument()->buffer().line(9), std::string("int a9;"));
  // The diagnostic goes away after a while.
  env.app->tick(env.now + 60000);
  CHECK_EQ(env.status("message"), std::string());
}

TEST(app_copy_cut_paste_multi_caret) {
  FakeClipboard cb;
  Env env;
  env.bench->openFile("a.cpp");
  Editor& ed = *env.ed;
  Document& doc = *ed.currentDocument();
  // Two selections: copy joins them with '\n'.
  ed.setSelections({Selection{{0, 0}, {0, 3}}, Selection{{1, 0}, {1, 3}}});
  CHECK(env.key(Key('C'), ModCtrl));
  CHECK_EQ(cb.value, std::string("int\nint"));
  // Paste two lines into two carets: one line per caret.
  cb.value = "XX\nYY";
  ed.setSelections({Selection{{2, 0}, {2, 0}}, Selection{{3, 0}, {3, 0}}});
  CHECK(env.key(Key('V'), ModCtrl));
  CHECK_EQ(doc.buffer().line(2), std::string("XXint a2;"));
  CHECK_EQ(doc.buffer().line(3), std::string("YYint a3;"));
  CHECK_EQ(doc.buffer().lineCount(), 11);
  CHECK(env.key(Key('Z'), ModCtrl));  // one undo step for both carets
  CHECK_EQ(doc.buffer().line(2), std::string("int a2;"));
  CHECK_EQ(doc.buffer().line(3), std::string("int a3;"));
  // A trailing line break does not count as a line; the order is document order whatever the primary is.
  cb.value = "P\nQ\n";
  ed.setSelections({Selection{{5, 0}, {5, 0}}, Selection{{4, 0}, {4, 0}}});
  CHECK(env.key(Key('V'), ModCtrl));
  CHECK_EQ(doc.buffer().line(4), std::string("Pint a4;"));
  CHECK_EQ(doc.buffer().line(5), std::string("Qint a5;"));
  // Different line count: the whole text at every caret.
  cb.value = "A\nB\nC";
  ed.setSelections({Selection{{7, 0}, {7, 0}}, Selection{{8, 0}, {8, 0}}});
  CHECK(env.key(Key('V'), ModCtrl));
  CHECK_EQ(doc.buffer().line(7), std::string("A"));
  CHECK_EQ(doc.buffer().line(9), std::string("Cint a7;"));
  // One caret: the whole text, CR dropped.
  cb.value = "k\r\nl";
  ed.setSelections({Selection{{0, 0}, {0, 0}}});
  CHECK(env.key(Key('V'), ModCtrl));
  CHECK_EQ(doc.buffer().line(0), std::string("k"));
  CHECK_EQ(doc.buffer().line(1), std::string("lint a0;"));
  // Cut: the selections (and only those) go; empty carets are untouched.
  ed.setSelections({Selection{{0, 0}, {0, 1}}, Selection{{1, 1}, {1, 1}}});
  CHECK(env.key(Key('X'), ModCtrl));
  CHECK_EQ(cb.value, std::string("k"));
  CHECK_EQ(doc.buffer().line(0), std::string(""));
  CHECK_EQ(doc.buffer().line(1), std::string("lint a0;"));
  // No selection: copy / cut take the whole line.
  ed.setSelections({Selection{{1, 2}, {1, 2}}});
  CHECK(env.key(Key('C'), ModCtrl));
  CHECK_EQ(cb.value, std::string("lint a0;\n"));
  const int lines = doc.buffer().lineCount();
  CHECK(env.key(Key('X'), ModCtrl));
  CHECK_EQ(doc.buffer().lineCount(), lines - 1);
  CHECK_EQ(cb.value, std::string("lint a0;\n"));
}

TEST(app_panel_focus_and_save_commands) {
  Env env;
  env.bench->openFile("a.cpp");
  Panel* first = env.bench->activePanel();
  env.bench->splitActive(Splitter::Orientation::Horizontal);
  CHECK(env.bench->activePanel() != first);
  CHECK(env.key(Key('1'), ModAlt));  // Workbench.FocusPanel 1
  CHECK(env.bench->activePanel() == first);
  CHECK(env.key(Key('2'), ModAlt));
  CHECK(env.bench->activePanel() != first);
  CHECK(env.key(Key('4'), ModAlt));  // no such panel
  CHECK(env.status("message").find("failed") != std::string::npos);

  env.type("Hi");
  CHECK(env.ed->currentDocument()->dirty());
  CHECK(env.key(Key('S'), ModCtrl));
  CHECK(!env.ed->currentDocument()->dirty());
  std::ifstream in(env.dir / "a.cpp");
  std::string l;
  std::getline(in, l);
  CHECK_EQ(l, std::string("Hiint a0;"));
}

TEST(app_unimplemented_10x_commands_say_so_and_python_bindings_are_unsupported) {
  Env env("Alt H: HelloWorld()\nAlt J: SomethingUnknown\n");
  env.bench->openFile("a.cpp");
  // A 10x default binding whose command tenfold lacks.
  CHECK(env.key(Key('E'), ModCtrl | ModShift));  // ToggleWorkspaceExplorer
  CHECK_EQ(env.app->message(), std::string("not implemented yet: ToggleWorkspaceExplorer"));
  CHECK(env.key(Key('I'), ModCtrl | ModShift));  // ShowTerminal
  CHECK_EQ(env.app->message(), std::string("not implemented yet: ShowTerminal"));
  // The binding with a Python statement: a load warning, and the key says it is unsupported.
  bool warned = false;
  for (const auto& d : env.app->diagnostics())
    warned = warned || (d.warning && d.message.find("Python") != std::string::npos);
  CHECK(warned);
  CHECK(env.key(Key('H'), ModAlt));
  CHECK(env.app->message().find("Python statements are not supported") != std::string::npos);
  CHECK(env.app->message().find("HelloWorld()") != std::string::npos);
  // A name that is neither a tenfold nor a 10x command.
  CHECK(env.key(Key('J'), ModAlt));
  CHECK_EQ(env.app->message(), std::string("Unknown command SomethingUnknown"));
  // 10x and tenfold names run the same command.
  env.type("Q");
  CHECK_EQ(env.ed->currentDocument()->buffer().line(0), std::string("Qint a0;"));
  env.app->runCommand("Undo");
  CHECK_EQ(env.ed->currentDocument()->buffer().line(0), std::string("int a0;"));
  env.type("Q");
  env.app->runCommand("Editor.Undo");
  CHECK_EQ(env.ed->currentDocument()->buffer().line(0), std::string("int a0;"));
  env.app->runCommand("SaveAll");
  CHECK_EQ(env.app->message(), std::string("not implemented yet: SaveAll"));
}

TEST(app_control_s_saves_and_a_second_control_s_is_save_all) {
  Env env;
  env.bench->openFile("a.cpp");
  env.type("Hi");
  CHECK(env.ed->currentDocument()->dirty());
  CHECK(env.key(Key('S'), ModCtrl));  // SaveFile
  CHECK(!env.ed->currentDocument()->dirty());
  CHECK(env.app->message().empty());
  CHECK(env.key(Key('S'), ModCtrl));  // the second stroke of "Control S, Control S": SaveAll, not implemented yet
  CHECK_EQ(env.app->message(), std::string("not implemented yet: SaveAll"));
  // A different key after the first Control S is an ordinary key press.
  CHECK(env.key(Key('S'), ModCtrl));
  env.type("!");
  CHECK_EQ(env.ed->currentDocument()->buffer().line(0), std::string("Hi!int a0;"));
}

TEST(app_middle_click_still_reaches_the_widgets_while_its_commands_are_missing) {
  Env env;
  env.bench->openFile("a.cpp");
  InputEvent e;
  e.type = InputType::MouseDown;
  e.button = MouseButton::Middle;
  e.x = 500;
  e.y = 300;
  env.app->handleEvent(e, env.now += 10);
  // MouseMiddleButton is GotoSymbolDefinitionUnderMouse in 10x; unimplemented: no status message, no swallowing.
  CHECK(env.app->message().find("not implemented") == std::string::npos);
  e.mods = ModCtrl;  // Control MouseMiddleButton: AddCursorAtMousePos
  env.app->handleEvent(e, env.now += 10);
  CHECK(env.app->message().find("not implemented") == std::string::npos);
}

TEST(app_imports_the_10x_key_file_between_defaults_and_the_tenfold_file) {
  Env env("F5: Editor.CaretDocEnd\n");  // the workspace file wins over the import
  const fs::path imp = env.dir / "imported.10x_settings";
  {
    std::ofstream out(imp, std::ios::binary);
    out << "# a 10x user's file\nF5: Editor.CaretDocStart\nF6: Editor.CaretDocEnd\nAlt H: HelloWorld()\nbroken\n";
  }
  env.bench->openFile("a.cpp");
  env.app->setKeyImportFile(imp);
  CHECK(!env.app->diagnostics().empty());
  CHECK(env.key(Key::F6));  // from the 10x file
  CHECK_EQ(env.ed->selections()[0].head.line, 10);
  env.ed->setSelections({caretAt({3, 0})});
  CHECK(env.key(Key::F5));  // the workspace file beats the import
  CHECK_EQ(env.ed->selections()[0].head.line, 10);
  // keys.import10x off: the 10x file's bindings go away, the defaults come back.
  env.app->setKeyImport10x(false);
  env.ed->setSelections({caretAt({3, 0})});
  env.key(Key::F6);
  CHECK_EQ(env.ed->selections()[0].head.line, 3);  // F6 is unbound again
  env.app->setKeyImport10x(true);
  env.key(Key::F6);
  CHECK_EQ(env.ed->selections()[0].head.line, 10);
}
