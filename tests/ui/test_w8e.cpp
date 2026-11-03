// W8e: RebuildActiveWorkspace, BuildCurrentFile, ReplaceInFiles, ReplaceNextInFile / ReplaceAllInFile and
// NewSearchPanel through the UI. Every command is run by pressing its 10x default key (the real embedded key
// bindings), against a recording renderer and the mock font. The builds are real: a fake compiler prints GCC-style
// messages with `cmake -E echo`; the searches are real too.
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

#include "../text/text_test_util.h"
#include "ui/commands/CommandNames.h"
#include "ui/gfx/TextRenderer.h"
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

void writeFile(const fs::path& p, const std::string& text) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary) << text;
}
std::string readFile(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

constexpr uint8_t kCS = ModCtrl | ModShift;

struct Env {
  fs::path dir;
  std::unique_ptr<gfx::TextRenderer> text;
  NullRenderer nr;
  std::unique_ptr<Editor> ed;
  std::unique_ptr<Workbench> bench;
  std::unique_ptr<EditorApp> app;
  int64_t now = 1000;

  Env() {
    static int n = 0;
    dir =
        uitest::tempRoot() / ("tf_w8e_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                              "_" + std::to_string(n++));
    writeFile(dir / "a.cpp", "int alpha = 1;\nint foo = alpha;\nfoo(foo);\n");
    writeFile(dir / "b.cpp", "foo();\r\nplain\r\nfoo();\r\n");
    writeFile(dir / "sub" / "c.cpp", "// foo and bar\nint bar;\n");
    gfx::FontStack stack;
    stack.add(std::make_shared<tftext::MockFont>(
        U"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ._-:,;/()[]{}<>+*&#?=!\"'\\|"));
    text = gfx::TextRenderer::fromFonts(std::move(stack));
    Editor::Options o;
    o.semantic.useClangd = false;
    o.semantic.persistFastIndex = false;
    o.watchForcePolling = true;
    o.watchPollMs = 60000;
    ed = std::make_unique<Editor>(dir, o);
    std::string err;
    if (!ed->start(&err)) std::cerr << "    editor start: " << err << "\n";
    for (int i = 0; i < 2000 && !ed->semantic().status().fastIndexDone; ++i) {
      ed->pump();
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    bench = std::make_unique<Workbench>(*text, *ed);
    app = std::make_unique<EditorApp>(*bench, *ed, bench.get());
    bench->openFile("a.cpp");
    bench->sync();
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
    bench->render(nr, 1000, 600);
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
  void advance(int ms) {
    now += ms;
    frame();
  }
  // Runs frames until `pred` holds (real threads: searches and builds finish on their own).
  bool until(const std::function<bool()>& pred, int tries = 600) {
    for (int i = 0; i < tries; ++i) {
      advance(20);
      if (pred()) return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
  }
  bool buildDone() { return app->buildPanel() && app->buildPanel()->state() != BuildPanel::State::Running; }
  Document* doc() { return ed->currentDocument(); }
  std::string txt() { return doc()->buffer().text(); }
  TextView* view() { return dynamic_cast<TextView*>(bench->focused()); }
  void searchFinished() {
    REQUIRE(until([&] { return !app->replaceInFiles().running() && !app->replaceInFiles().entries().empty(); }));
  }
  void setWorkspaceJson(const std::string& json) {
    writeFile(dir / ".tenfold" / "workspace.json", json);
    ed->workspace().loadSettings();
  }
};

}  // namespace

// ---------------------------------------------------------------------------
// Names and default keys
// ---------------------------------------------------------------------------

TEST(w8e_names_resolve_and_defaults_bind_to_them) {
  const char* names[] = {"RebuildActiveWorkspace", "BuildCurrentFile", "ReplaceInFiles",
                         "ReplaceNextInFile",      "ReplaceAllInFile", "NewSearchPanel"};
  Env env;
  for (const char* n : names) {
    const ResolvedCommand rc = resolveCommandName(n);
    CHECK(rc.known10x);
    CHECK(rc.implemented);
    CHECK(env.app->commands().find(rc.id) != nullptr);
    CHECK(env.app->commands().find(rc.id)->handler != nullptr);
  }
  struct Bind {
    const char* chord;
    const char* command;
  };
  const Bind binds[] = {{"Control Alt F7", "RebuildActiveWorkspace"}, {"Control F7", "BuildCurrentFile"},
                        {"Control Shift H", "ReplaceInFiles"},        {"Alt R", "ReplaceNextInFile"},
                        {"Shift Alt R", "ReplaceAllInFile"},          {"Control Shift A", "NewSearchPanel"}};
  for (const Bind& b : binds) {
    bool found = false;
    for (const KeyBinding& kb : env.app->keys().bindings())
      if (resolveCommandName(kb.command).id == resolveCommandName(b.command).id && formatChords(kb.chords) == b.chord)
        found = true;
    CHECK(found);
  }
  env.key(Key::F7, ModCtrl | ModAlt);  // no "not implemented yet" message any more
  CHECK(env.app->message().find("not implemented") == std::string::npos);
}

// ---------------------------------------------------------------------------
// RebuildActiveWorkspace / BuildCurrentFile
// ---------------------------------------------------------------------------

TEST(w8e_rebuild_with_control_alt_f7_runs_the_rebuild_command_in_the_build_panel) {
  Env env;
  env.setWorkspaceJson(
      R"({"buildCommand":"cmake -E echo plain-build","rebuildCommand":"cmake -E echo a.cpp:2:5: error: rebuilt here"})");
  REQUIRE(env.app->buildPanel() != nullptr);
  CHECK(env.key(Key::F7, ModCtrl | ModAlt));
  CHECK_EQ(env.bench->bottomPanelName(), std::string("Build"));
  REQUIRE(env.until([&] { return env.buildDone(); }));
  BuildPanel& p = *env.app->buildPanel();
  CHECK(p.state() == BuildPanel::State::Failed || p.state() == BuildPanel::State::Succeeded);
  REQUIRE(!p.lines().empty());
  CHECK(p.lines()[0].text.find("rebuilt here") != std::string::npos);  // the command line the panel shows
  REQUIRE(p.messages().size() == 1);
  CHECK_EQ(p.messages()[0].line, 2);
  // The result feeds the same error navigation as a normal build (F8).
  CHECK(env.key(Key::F8));
  CHECK_EQ(env.doc()->path().filename().string(), std::string("a.cpp"));
  CHECK_EQ(env.ed->cursor().pos.line, 1);
  // F7 is still the plain build: the other command.
  CHECK(env.key(Key::F7));
  REQUIRE(env.until([&] { return env.buildDone(); }));
  CHECK(p.lines()[0].text.find("plain-build") != std::string::npos);
}

TEST(w8e_rebuild_of_a_custom_build_without_a_rebuild_command_says_why) {
  Env env;
  env.setWorkspaceJson(R"({"buildCommand":"cmake -E echo plain-build"})");
  CHECK(env.key(Key::F7, ModCtrl | ModAlt));
  CHECK(env.app->message().find("rebuildCommand") != std::string::npos);
}

TEST(w8e_build_current_file_with_control_f7_runs_the_compile_command_of_the_file) {
  Env env;
  const fs::path bld = env.dir / "build";
  fs::create_directories(bld);
  std::vector<CompileCommand> cmds;
  cmds.push_back({bld, env.dir / "a.cpp", {"cmake", "-E", "echo", "../a.cpp:3:1: warning: only this file [-Wfoo]"}});
  cmds.push_back({bld, env.dir / "b.cpp", {"cmake", "-E", "echo", "../b.cpp:1:1: error: other file"}});
  REQUIRE(CompileDatabase::save(cmds, bld / "compile_commands.json"));
  CHECK(env.key(Key::F7, ModCtrl));
  CHECK_EQ(env.bench->bottomPanelName(), std::string("Build"));
  REQUIRE(env.until([&] { return env.buildDone(); }));
  BuildPanel& p = *env.app->buildPanel();
  REQUIRE(p.messages().size() == 1);  // b.cpp's command did not run
  CHECK_EQ(p.messages()[0].line, 3);
  CHECK(p.messages()[0].severity == BuildSeverity::Warning);
  CHECK_EQ(env.ed->buildMessages().size(), size_t(1));
  CHECK(p.activate(static_cast<int>(p.lines().size()) - 1));  // clicking the line jumps there
  CHECK_EQ(env.ed->cursor().pos.line, 2);
}

TEST(w8e_build_current_file_reports_a_file_without_a_compile_command) {
  Env env;
  writeFile(env.dir / "notes.txt", "not compiled\n");
  env.bench->openFile("notes.txt");
  env.bench->sync();
  env.frame();
  REQUIRE(env.doc()->path().filename() == "notes.txt");
  CHECK(env.key(Key::F7, ModCtrl));  // the generated compile database has no entry for it
  CHECK(env.app->message().find("Build failed to start") != std::string::npos);
  CHECK(env.app->message().find("notes.txt") != std::string::npos);
  CHECK(!env.ed->buildRunning());
}

// ---------------------------------------------------------------------------
// ReplaceInFiles
// ---------------------------------------------------------------------------

TEST(w8e_replace_in_files_previews_and_replaces_checked_matches_in_open_and_closed_files) {
  Env env;
  const std::string aBefore = env.txt();
  CHECK(env.key(Key('H'), kCS));
  ReplaceInFiles& r = env.app->replaceInFiles();
  REQUIRE(r.isOpen());
  CHECK(env.bench->focused() == r.findInput());
  env.type("foo");
  env.searchFinished();
  REQUIRE(r.entries().size() == 6);  // a.cpp 3, b.cpp 2, sub/c.cpp 1
  CHECK_EQ(r.checkedCount(), size_t(6));
  CHECK_EQ(r.entries()[0].match.file.filename().string(), std::string("a.cpp"));  // sorted by relative path
  // Typing in the replace field: nothing is written yet.
  CHECK(env.key(Key::Tab));
  CHECK(env.bench->focused() == r.replaceInput());
  env.type("baz");
  CHECK_EQ(r.replaceInput()->text(), std::string("baz"));
  CHECK_EQ(env.txt(), aBefore);
  CHECK_EQ(readFile(env.dir / "b.cpp"), std::string("foo();\r\nplain\r\nfoo();\r\n"));

  // Insert unchecks the selected row (the first match); the others stay.
  CHECK(env.key(Key::Insert));
  CHECK_EQ(r.checkedCount(), size_t(5));
  CHECK(!r.entries()[0].checked);

  // Enter with closed files involved asks once; nothing is written, Escape goes back to the list.
  CHECK(env.key(Key::Enter));
  CHECK(r.confirming());
  CHECK(r.isOpen());
  CHECK(r.statusText().find("on disk") != std::string::npos);
  CHECK_EQ(readFile(env.dir / "b.cpp"), std::string("foo();\r\nplain\r\nfoo();\r\n"));
  CHECK(env.key(Key::Escape));
  CHECK(!r.confirming());
  CHECK(r.isOpen());
  CHECK(env.key(Key::Enter));
  CHECK(r.confirming());
  CHECK(env.key(Key::Enter));  // confirmed
  CHECK(!r.isOpen());
  CHECK_EQ(r.lastResult().replaced, size_t(5));
  CHECK_EQ(r.lastResult().openDocuments, size_t(1));
  CHECK_EQ(r.lastResult().filesWritten, size_t(2));

  // The open document: edited in place, the unchecked first match kept, unsaved.
  CHECK_EQ(env.txt(), std::string("int alpha = 1;\nint foo = alpha;\nbaz(baz);\n"));
}

TEST(w8e_replace_in_files_checked_matches_open_documents_one_undo_step_and_closed_files_on_disk) {
  Env env;
  env.key(Key('H'), kCS);
  ReplaceInFiles& r = env.app->replaceInFiles();
  env.type("foo");
  env.searchFinished();
  env.key(Key::Tab);
  env.type("baz");
  // Uncheck the very first match (a.cpp line 2, column 4) and replace the rest.
  REQUIRE(!r.entries().empty());
  r.setChecked(0, false);
  env.key(Key::Enter);
  REQUIRE(r.confirming());
  env.key(Key::Enter);
  REQUIRE(!r.isOpen());
  CHECK_EQ(env.txt(), std::string("int alpha = 1;\nint foo = alpha;\nbaz(baz);\n"));
  CHECK(env.doc()->dirty());
  CHECK_EQ(readFile(env.dir / "a.cpp"), std::string("int alpha = 1;\nint foo = alpha;\nfoo(foo);\n"));
  CHECK_EQ(readFile(env.dir / "b.cpp"), std::string("baz();\r\nplain\r\nbaz();\r\n"));
  CHECK_EQ(readFile(env.dir / "sub" / "c.cpp"), std::string("// baz and bar\nint bar;\n"));
  CHECK(env.app->message().find("Replaced 5 occurrences") != std::string::npos);

  // One Undo restores both replaced occurrences of the document at once.
  CHECK(env.key(Key('Z'), ModCtrl));
  CHECK_EQ(env.txt(), std::string("int alpha = 1;\nint foo = alpha;\nfoo(foo);\n"));
}

TEST(w8e_replace_in_files_needs_no_confirmation_for_open_documents_only) {
  Env env;
  env.key(Key('H'), kCS);
  ReplaceInFiles& r = env.app->replaceInFiles();
  env.type("alpha");
  env.searchFinished();
  REQUIRE(r.entries().size() == 2);
  env.key(Key::Tab);
  env.type("omega");
  CHECK(env.key(Key::Enter));  // both matches are in the open a.cpp: replaced at once
  CHECK(!r.isOpen());
  CHECK(!r.confirming());
  CHECK_EQ(env.txt(), std::string("int omega = 1;\nint foo = omega;\nfoo(foo);\n"));
  CHECK_EQ(r.lastResult().filesWritten, size_t(0));
}

TEST(w8e_replace_in_files_file_header_toggle_options_and_nothing_checked) {
  Env env;
  env.key(Key('H'), kCS);
  ReplaceInFiles& r = env.app->replaceInFiles();
  env.type("foo");
  env.searchFinished();
  env.key(Key::Tab);
  env.type("X");
  env.key(Key('T'), ModAlt);  // all unchecked
  CHECK_EQ(r.checkedCount(), size_t(0));
  CHECK(env.key(Key::Enter));
  CHECK(r.isOpen());  // nothing checked: nothing happens
  CHECK(env.app->message().find("No matches checked") != std::string::npos);
  env.key(Key('T'), ModAlt);  // all checked again
  CHECK_EQ(r.checkedCount(), r.entries().size());
  env.key(Key::Up);  // the first match row -> the header row of its file
  env.key(Key::Insert);
  CHECK(!r.entries()[0].checked);  // a header toggles every match of its file
  CHECK(!r.entries()[1].checked);
  CHECK(r.entries()[3].checked);
  // Alt+R toggles regex in the panel instead of replacing in the file behind it.
  env.key(Key('R'), ModAlt);
  CHECK(env.doc()->buffer().text().find("X") == std::string::npos);
  CHECK(r.statusText().find("[.*]") != std::string::npos);
  env.key(Key::Escape);
  CHECK(!r.isOpen());
  CHECK_EQ(readFile(env.dir / "b.cpp"), std::string("foo();\r\nplain\r\nfoo();\r\n"));
}

TEST(w8e_replace_in_files_skips_matches_changed_since_the_search) {
  Env env;
  env.key(Key('H'), kCS);
  ReplaceInFiles& r = env.app->replaceInFiles();
  env.type("plain");
  env.searchFinished();
  REQUIRE(r.entries().size() == 1);
  env.key(Key::Tab);
  env.type("simple");
  writeFile(env.dir / "b.cpp", "foo();\r\nplane\r\nfoo();\r\n");  // changed on disk after the search
  env.key(Key::Enter);
  env.key(Key::Enter);
  CHECK_EQ(readFile(env.dir / "b.cpp"), std::string("foo();\r\nplane\r\nfoo();\r\n"));
  CHECK_EQ(r.lastResult().stale, size_t(1));
  CHECK(env.app->message().find("skipped") != std::string::npos);
}

// ---------------------------------------------------------------------------
// ReplaceNextInFile / ReplaceAllInFile
// ---------------------------------------------------------------------------

TEST(w8e_replace_next_and_all_in_file_work_from_the_editor_with_the_bars_query) {
  Env env;
  env.key(Key('H'), ModCtrl);  // find / replace bar
  FindReplace& fr = env.app->findReplace();
  REQUIRE(fr.isOpen());
  fr.setQuery("foo");
  fr.setReplacement("bar");
  env.key(Key::Escape);  // focus back in the editor
  REQUIRE(!fr.isOpen());
  REQUIRE(env.view() != nullptr);
  env.ed->setSelections({caretAt({0, 0})});

  CHECK(env.key(Key('R'), ModAlt));  // ReplaceNextInFile: the first match from the caret
  CHECK_EQ(env.txt(), std::string("int alpha = 1;\nint bar = alpha;\nfoo(foo);\n"));
  CHECK(env.key(Key('R'), ModAlt));
  CHECK_EQ(env.txt(), std::string("int alpha = 1;\nint bar = alpha;\nbar(foo);\n"));
  CHECK(env.key(Key('R'), ModShift | ModAlt));  // ReplaceAllInFile: the rest, one undo step
  CHECK_EQ(env.txt(), std::string("int alpha = 1;\nint bar = alpha;\nbar(bar);\n"));
  CHECK(env.key(Key('Z'), ModCtrl));
  CHECK_EQ(env.txt(), std::string("int alpha = 1;\nint bar = alpha;\nbar(foo);\n"));
  // Each replacement is its own undo step.
  CHECK(env.key(Key('Z'), ModCtrl));
  CHECK(env.key(Key('Z'), ModCtrl));
  CHECK_EQ(env.txt(), std::string("int alpha = 1;\nint foo = alpha;\nfoo(foo);\n"));
}

TEST(w8e_alt_r_in_the_find_bar_toggles_regex_without_the_replace_row_and_replaces_with_it) {
  Env env;
  env.key(Key('F'), ModCtrl);  // find only
  FindReplace& fr = env.app->findReplace();
  REQUIRE(fr.isOpen());
  env.type("foo");
  const std::string before = env.txt();
  env.key(Key('R'), ModAlt);
  CHECK(fr.regex());
  CHECK_EQ(env.txt(), before);
  env.key(Key('E'), ModAlt);  // the alternative key
  CHECK(!fr.regex());
  env.key(Key::Escape);

  env.key(Key('H'), ModCtrl);  // with the replace row Alt+R replaces
  REQUIRE(fr.isOpen());
  fr.setQuery("alpha");
  fr.setReplacement("zeta");
  env.ed->setSelections({caretAt({0, 0})});
  CHECK(env.bench->focused() == fr.findInput() || env.bench->focused() == fr.replaceInput());
  env.key(Key('R'), ModAlt);
  CHECK(!fr.regex());
  CHECK(env.txt().find("zeta") != std::string::npos);
}

TEST(w8e_replace_next_without_a_query_says_so) {
  Env env;
  CHECK(env.key(Key('R'), ModAlt));
  CHECK(env.app->message().find("Nothing to replace") != std::string::npos);
  CHECK(env.key(Key('R'), ModShift | ModAlt));
  CHECK(env.app->message().find("Nothing to replace") != std::string::npos);
}

// ---------------------------------------------------------------------------
// NewSearchPanel
// ---------------------------------------------------------------------------

TEST(w8e_new_search_panel_keeps_the_previous_result_list_and_starts_an_independent_one) {
  Env env;
  FindInFiles& f = env.app->findInFiles();
  env.key(Key('F'), kCS);
  env.type("foo");
  REQUIRE(env.until([&] { return !f.running() && f.matches().size() == 6; }));
  CHECK_EQ(f.keptSets(), size_t(0));
  env.key(Key::Escape);

  CHECK(env.key(Key('A'), kCS));  // NewSearchPanel: an empty list, the old one kept
  REQUIRE(f.isOpen());
  CHECK_EQ(f.keptSets(), size_t(1));
  CHECK(f.matches().empty());
  env.type("bar");
  REQUIRE(env.until([&] { return !f.running() && f.matches().size() == 2; }));  // "bar": twice in c.cpp
  CHECK_EQ(f.keptSets(), size_t(1));

  // Alt PageUp brings the first list back (and parks this one); the lists rotate.
  CHECK(env.key(Key::PageUp, ModAlt));
  CHECK_EQ(f.matches().size(), size_t(6));
  CHECK_EQ(f.keptSets(), size_t(1));
  CHECK(env.key(Key::PageDown, ModAlt));
  CHECK_EQ(f.matches().size(), size_t(2));
  // F4 (GotoNextResult) steps through the list that is current.
  env.key(Key::Escape);
  CHECK(env.key(Key::F4));
  CHECK_EQ(env.doc()->path().filename().string(), std::string("c.cpp"));

  // A third list, and the cap of kept lists.
  for (int i = 0; i < 5; ++i) {
    env.key(Key('A'), kCS);
    env.type("a");
    env.advance(300);
    env.key(Key::Escape);
  }
  CHECK(f.keptSets() <= FindInFiles::kMaxKept);
}
