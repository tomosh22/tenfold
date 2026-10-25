// P2: UI safety and visibility. Unsaved-changes prompts (tab close, App.Quit, window close), files changed or deleted
// on disk, the clangd health and "flags: heuristic" status segments, Clangd.Restart and the command palette. Synthetic
// events against a null renderer and the mock font; the clangd cases use the fake clangd of tests/test_crash.cpp.
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>

#include "../text/text_test_util.h"
#include "ui/gfx/TextRenderer.h"
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

InputEvent textEv(const std::string& t) {
  InputEvent e;
  e.type = InputType::Text;
  e.text = t;
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

struct Config {
  bool watch = false;                   // fast polling file watcher (external changes)
  bool fakeClangd = false;              // the fake clangd of tests/test_crash.cpp instead of "no clangd"
  std::vector<std::string> clangdArgs;  // extra fake clangd options
  int backoffInitialMs = 20, maxCrashes = 5;
  std::string fixture;  // a directory of tests/fixtures/vs copied into the workspace instead of the plain sources
};

struct Env {
  fs::path dir;
  std::unique_ptr<Editor> ed;
  std::unique_ptr<gfx::TextRenderer> text;
  NullRenderer rec;
  std::unique_ptr<Workbench> bench;
  std::unique_ptr<EditorApp> app;
  int64_t now = 1000;

  explicit Env(const Config& cfg = {}) {
    static int n = 0;
    dir = uitest::tempRoot() / ("tf_p2_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                                "_" + std::to_string(n++));
    fs::create_directories(dir);
    if (!cfg.fixture.empty()) {
      fs::copy(fs::path(TENFOLD_SOURCE_DIR) / "tests/fixtures/vs" / cfg.fixture, dir, fs::copy_options::recursive);
      std::error_code ec;
      dir = fs::weakly_canonical(dir, ec);
    } else {
      writeFile(dir / "src/a.cpp", "int add(int a, int b) { return a + b; }\n");
      writeFile(dir / "src/b.cpp", "int sub(int a, int b) { return a - b; }\n");
    }
    gfx::FontStack stack;
    stack.add(std::make_shared<tftext::MockFont>(
        U"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ._-:,;/()[]{}<>+*&#?=!\"'\\|"));
    text = gfx::TextRenderer::fromFonts(std::move(stack));
    Editor::Options o;
    o.semantic.useClangd = cfg.fakeClangd;
    if (cfg.fakeClangd) {
      o.semantic.clangdPath = TENFOLD_TEST_HELPER;
      o.semantic.clangdArgs = {"--fake-clangd", "--launch-file=" + (dir / "fake.launches").string()};
      for (const auto& a : cfg.clangdArgs) o.semantic.clangdArgs.push_back(a);
      o.semantic.semanticTokensDebounceMs = -1;
      o.semantic.restartBackoffInitialMs = cfg.backoffInitialMs;
      o.semantic.restartBackoffMaxMs = cfg.backoffInitialMs;
      o.semantic.restartMaxFastCrashes = cfg.maxCrashes;
    }
    if (cfg.watch) {
      o.watchForcePolling = true;
      o.watchPollMs = 40;
    }
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
    for (const auto& e : fs::recursive_directory_iterator(dir, ec)) fs::permissions(e.path(), fs::perms::owner_all, ec);
    fs::remove_all(dir, ec);
  }
  void frame() {
    ed->pump();
    app->tick(now);
    bench->render(rec, 1000, 700);
  }
  void key(Key k, uint8_t mods = 0) {
    now += 10;
    app->handleEvent(keyEv(k, mods), now);
    frame();
  }
  void type(const std::string& t) {
    now += 10;
    app->handleEvent(textEv(t), now);
    frame();
  }
  void click(float x, float y) {
    now += 10;
    app->handleEvent(mouseEv(InputType::MouseDown, x, y), now);
    app->handleEvent(mouseEv(InputType::MouseUp, x, y), now);
    frame();
  }
  void run(const std::string& id) {
    app->runCommand(id);
    frame();
  }
  void settings(const std::string& text) {
    Settings s;
    s.loadTexts(text, "");
    app->applySettings(s);
  }
  Document* open(const std::string& rel) {
    const int id = bench->openFile(dir / rel);
    if (id < 0) return nullptr;
    bench->sync();
    frame();
    return ed->currentDocument();
  }
  // Types `t` at the start of the current document: it becomes dirty.
  void edit(const std::string& t = "// edit\n") {
    ed->moveCursor({0, 0});
    ed->insertText(t);
    bench->sync();
    frame();
  }
  int tabOf(const std::string& rel) {
    for (Panel* p : bench->panels())
      for (const auto& t : p->tabs().tabs())
        if (bench->tabPath(t.id) == dir / rel) return t.id;
    return -1;
  }
  void closeTabByUser(int id) {
    bench->panelOf(id)->onCloseRequested(id);
    frame();
  }
  std::vector<std::string> rows(ListPopup* p) {
    std::vector<std::string> out;
    if (p)
      for (const auto& r : p->rows()) out.push_back(r.primary);
    return out;
  }
  std::string tabTitle(int id) {
    for (const auto& t : bench->panelOf(id)->tabs().tabs())
      if (t.id == id) return t.title;
    return {};
  }
};

std::string join(const std::vector<std::string>& v) {
  std::string out;
  for (const auto& x : v) out += (out.empty() ? "" : "|") + x;
  return out;
}

bool contains(const std::string& s, const std::string& what) { return s.find(what) != std::string::npos; }

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// 1. Unsaved changes
// ---------------------------------------------------------------------------------------------------------------

TEST(p2_close_dirty_tab_prompts_and_save_writes_then_closes) {
  Env env;
  env.open("src/a.cpp");
  env.edit();
  const int id = env.tabOf("src/a.cpp");
  REQUIRE(id >= 0);
  env.closeTabByUser(id);
  REQUIRE(env.app->unsavedPrompt().isOpen());
  CHECK_EQ(join(env.rows(env.app->unsavedPrompt().popup())), std::string("Save|Don't Save|Cancel"));
  CHECK(env.tabOf("src/a.cpp") >= 0);  // nothing closed yet
  env.key(Key::Enter);                 // Save is the default
  CHECK(!env.app->unsavedPrompt().isOpen());
  CHECK_EQ(env.tabOf("src/a.cpp"), -1);
  CHECK(env.ed->document(env.dir / "src/a.cpp") == nullptr);
  CHECK(contains(slurp(env.dir / "src/a.cpp"), "// edit"));
}

TEST(p2_close_dirty_tab_dont_save_discards) {
  Env env;
  env.open("src/a.cpp");
  env.edit();
  env.closeTabByUser(env.tabOf("src/a.cpp"));
  REQUIRE(env.app->unsavedPrompt().isOpen());
  env.key(Key::Down);
  env.key(Key::Enter);  // Don't Save
  CHECK_EQ(env.tabOf("src/a.cpp"), -1);
  CHECK(env.ed->document(env.dir / "src/a.cpp") == nullptr);  // the changes are gone, not parked in a hidden document
  CHECK(!contains(slurp(env.dir / "src/a.cpp"), "// edit"));
}

TEST(p2_close_dirty_tab_cancel_and_escape_keep_everything) {
  Env env;
  env.open("src/a.cpp");
  env.edit();
  env.closeTabByUser(env.tabOf("src/a.cpp"));
  env.key(Key::Down);
  env.key(Key::Down);
  env.key(Key::Enter);  // Cancel
  CHECK(!env.app->unsavedPrompt().isOpen());
  CHECK(env.tabOf("src/a.cpp") >= 0);
  CHECK(env.ed->document(env.dir / "src/a.cpp")->dirty());
  env.closeTabByUser(env.tabOf("src/a.cpp"));
  REQUIRE(env.app->unsavedPrompt().isOpen());
  env.key(Key::Escape);
  CHECK(!env.app->unsavedPrompt().isOpen());
  CHECK(env.tabOf("src/a.cpp") >= 0);
  CHECK(env.ed->document(env.dir / "src/a.cpp")->dirty());
  CHECK(!contains(slurp(env.dir / "src/a.cpp"), "// edit"));
}

TEST(p2_close_tab_that_is_not_the_last_view_or_not_dirty_does_not_ask) {
  Env env;
  env.open("src/a.cpp");
  env.open("src/b.cpp");
  env.closeTabByUser(env.tabOf("src/b.cpp"));  // clean
  CHECK(!env.app->unsavedPrompt().isOpen());
  CHECK_EQ(env.tabOf("src/b.cpp"), -1);
  env.open("src/a.cpp");
  env.edit();
  env.bench->splitActive(Splitter::Orientation::Horizontal);  // a second view of the dirty document
  env.bench->sync();
  int views = 0;
  for (Panel* p : env.bench->panels())
    for (const auto& t : p->tabs().tabs()) views += env.bench->tabPath(t.id) == env.dir / "src/a.cpp";
  REQUIRE(views == 2);
  env.closeTabByUser(env.bench->activePanel()->activeId());
  CHECK(!env.app->unsavedPrompt().isOpen());  // the document still has a view
  CHECK(env.ed->document(env.dir / "src/a.cpp")->dirty());
  CHECK(env.tabOf("src/a.cpp") >= 0);
}

TEST(p2_confirm_unsaved_false_skips_every_prompt) {
  Env env;
  env.settings("editor.confirmUnsaved = false\n");
  CHECK(!env.app->confirmUnsaved());
  env.open("src/a.cpp");
  env.edit();
  env.closeTabByUser(env.tabOf("src/a.cpp"));
  CHECK(!env.app->unsavedPrompt().isOpen());
  CHECK_EQ(env.tabOf("src/a.cpp"), -1);
  CHECK(env.ed->document(env.dir / "src/a.cpp") != nullptr);  // as before the prompt existed: the document is parked
  CHECK(env.ed->document(env.dir / "src/a.cpp")->dirty());
  env.run("App.Quit");
  CHECK(!env.app->unsavedPrompt().isOpen());
  CHECK(env.app->quitRequested());
  CHECK(!contains(slurp(env.dir / "src/a.cpp"), "// edit"));
}

TEST(p2_quit_without_changes_quits_at_once) {
  Env env;
  env.open("src/a.cpp");
  env.run("App.Quit");
  CHECK(env.app->quitRequested());
  CHECK(!env.app->unsavedPrompt().isOpen());
}

TEST(p2_quit_with_two_dirty_documents_lists_them_and_save_all_saves_both) {
  Env env;
  env.open("src/a.cpp");
  env.edit("// one\n");
  env.open("src/b.cpp");
  env.edit("// two\n");
  env.run("App.Quit");
  CHECK(!env.app->quitRequested());
  REQUIRE(env.app->unsavedPrompt().isOpen());
  const auto rows = env.rows(env.app->unsavedPrompt().popup());
  REQUIRE(rows.size() == 5);
  CHECK_EQ(rows[0], std::string("Save All"));
  CHECK_EQ(rows[1], std::string("Don't Save"));
  CHECK_EQ(rows[2], std::string("Cancel"));
  const std::string listed = rows[3] + "|" + rows[4];
  CHECK(contains(listed, "src/a.cpp") && contains(listed, "src/b.cpp"));
  env.key(Key::Enter);
  CHECK(env.app->quitRequested());
  CHECK(contains(slurp(env.dir / "src/a.cpp"), "// one"));
  CHECK(contains(slurp(env.dir / "src/b.cpp"), "// two"));
}

TEST(p2_quit_dont_save_and_cancel) {
  Env env;
  env.open("src/a.cpp");
  env.edit("// one\n");
  env.open("src/b.cpp");
  env.edit("// two\n");
  env.run("App.Quit");
  env.key(Key::Down);
  env.key(Key::Down);
  env.key(Key::Enter);  // Cancel
  CHECK(!env.app->quitRequested());
  CHECK(env.ed->document(env.dir / "src/a.cpp")->dirty());
  env.run("App.Quit");
  env.key(Key::Escape);
  CHECK(!env.app->quitRequested());
  env.key(Key('Q'), ModCtrl);  // the default binding
  REQUIRE(env.app->unsavedPrompt().isOpen());
  env.key(Key::Down);
  env.key(Key::Enter);  // Don't Save
  CHECK(env.app->quitRequested());
  CHECK(!contains(slurp(env.dir / "src/a.cpp"), "// one"));
  CHECK(!contains(slurp(env.dir / "src/b.cpp"), "// two"));
}

TEST(p2_quit_counts_dirty_documents_that_have_no_tab) {
  Env env;
  env.open("src/a.cpp");
  env.edit();
  env.settings("editor.confirmUnsaved = false\n");
  env.closeTabByUser(env.tabOf("src/a.cpp"));  // parked: dirty, no tab
  env.settings("editor.confirmUnsaved = true\n");
  env.run("App.Quit");
  CHECK(!env.app->quitRequested());
  CHECK(env.app->unsavedPrompt().isOpen());
}

TEST(p2_window_close_asks_like_quit) {
  Env env;
  env.open("src/a.cpp");
  env.edit();
  CHECK(!env.app->requestQuit());  // what the window's close button calls
  CHECK(env.app->unsavedPrompt().isOpen());
  CHECK(!env.app->quitRequested());
  env.key(Key::Enter);
  CHECK(env.app->quitRequested());
  CHECK(env.app->requestQuit());
}

TEST(p2_save_failure_keeps_the_app_open_and_shows_the_error) {
  Env env;
  env.open("src/a.cpp");
  env.edit();
  std::error_code ec;
  fs::permissions(env.dir / "src/a.cpp", fs::perms::owner_read, ec);  // a read-only (not checked out) file
  env.run("App.Quit");
  env.key(Key::Enter);  // Save All
  CHECK(!env.app->quitRequested());
  CHECK(env.ed->document(env.dir / "src/a.cpp")->dirty());
  CHECK(contains(env.app->message(), "Cannot save src/a.cpp"));
  CHECK(contains(env.app->message(), "read-only"));
  // The same through the tab: the tab stays.
  env.closeTabByUser(env.tabOf("src/a.cpp"));
  env.key(Key::Enter);
  CHECK(env.tabOf("src/a.cpp") >= 0);
  CHECK(contains(env.app->message(), "Cannot save"));
  // Ctrl+S reports the reason too.
  env.run("File.Save");
  CHECK(contains(env.app->message(), "read-only"));
}

// ---------------------------------------------------------------------------------------------------------------
// 2. Files changed on disk
// ---------------------------------------------------------------------------------------------------------------

namespace {
// Rewrites `rel` on disk and waits until the engine noticed (the handler ran, or a prompt is queued).
bool changeOnDisk(Env& env, const std::string& rel, const std::string& content, std::function<bool()> seen) {
  writeFile(env.dir / rel, content);
  return env.ed->waitFor([&] { return seen(); }, 8000);
}
}  // namespace

TEST(p2_external_change_of_a_clean_document_reloads_and_keeps_the_caret) {
  Config c;
  c.watch = true;
  Env env(c);
  Document* d = env.open("src/a.cpp");
  env.ed->moveCursor({0, 4});
  const std::string before = d->buffer().text();
  REQUIRE(changeOnDisk(env, "src/a.cpp", before + "int extra;\n", [&] { return d->buffer().text() != before; }));
  env.frame();
  CHECK(!d->dirty());
  CHECK(contains(d->buffer().text(), "int extra;"));
  CHECK_EQ(env.ed->cursor().pos.line, 0);  // a change below the caret leaves it alone
  CHECK_EQ(env.ed->cursor().pos.column, 4);
  CHECK(contains(env.app->message(), "Reloaded src/a.cpp"));
  CHECK(!env.app->externalChanges().isOpen());
  // Our own save is not an external change.
  env.edit("// mine\n");
  REQUIRE(env.ed->save(*d));
  env.ed->waitFor([] { return false; }, 300);
  env.frame();
  CHECK(!env.app->externalChanges().isOpen());
  CHECK_EQ(env.app->externalChanges().pending(), size_t(0));
}

TEST(p2_external_change_of_a_dirty_document_asks_reload_wins_when_chosen) {
  Config c;
  c.watch = true;
  Env env(c);
  Document* d = env.open("src/a.cpp");
  env.edit("// mine\n");
  REQUIRE(d->dirty());
  REQUIRE(changeOnDisk(env, "src/a.cpp", "int theirs;\n", [&] { return env.app->externalChanges().pending() > 0; }));
  CHECK(contains(d->buffer().text(), "// mine"));  // nothing happens behind the user's back
  env.frame();                                     // the prompt opens
  REQUIRE(env.app->externalChanges().isOpen());
  CHECK_EQ(join(env.rows(env.app->externalChanges().popup())), std::string("Reload (lose my changes)|Keep my version"));
  CHECK_EQ(env.app->externalChanges().popup()->selected(), 1);  // the safe choice is the default
  env.key(Key::Up);
  env.key(Key::Enter);  // Reload
  CHECK(!env.app->externalChanges().isOpen());
  CHECK_EQ(d->buffer().text(), std::string("int theirs;\n"));
  CHECK(!d->dirty());
}

TEST(p2_external_change_of_a_dirty_document_keep_my_version_keeps_edits_and_does_not_nag) {
  Config c;
  c.watch = true;
  Env env(c);
  Document* d = env.open("src/a.cpp");
  env.edit("// mine\n");
  REQUIRE(changeOnDisk(env, "src/a.cpp", "int theirs;\n", [&] { return env.app->externalChanges().pending() > 0; }));
  env.frame();
  REQUIRE(env.app->externalChanges().isOpen());
  env.key(Key::Enter);  // Keep my version
  CHECK(!env.app->externalChanges().isOpen());
  CHECK(contains(d->buffer().text(), "// mine"));
  CHECK(d->dirty());
  env.ed->waitFor([] { return false; }, 400);
  env.frame();
  CHECK(!env.app->externalChanges().isOpen());  // asked once per change on disk
  CHECK_EQ(env.app->externalChanges().pending(), size_t(0));
  // Escape is Keep as well.
  REQUIRE(changeOnDisk(env, "src/a.cpp", "int theirs2;\n", [&] { return env.app->externalChanges().pending() > 0; }));
  env.frame();
  REQUIRE(env.app->externalChanges().isOpen());
  env.key(Key::Escape);
  CHECK(contains(d->buffer().text(), "// mine"));
  env.frame();
  CHECK(!env.app->externalChanges().isOpen());
}

TEST(p2_external_change_prompt_mode_asks_for_clean_documents_and_never_mode_does_nothing) {
  Config c;
  c.watch = true;
  Env env(c);
  Document* d = env.open("src/a.cpp");
  env.settings("editor.reloadExternalChanges = prompt\n");
  REQUIRE(changeOnDisk(env, "src/a.cpp", "int one;\n", [&] { return env.app->externalChanges().pending() > 0; }));
  env.frame();
  REQUIRE(env.app->externalChanges().isOpen());
  CHECK_EQ(join(env.rows(env.app->externalChanges().popup())), std::string("Reload|Keep my version"));
  CHECK_EQ(env.app->externalChanges().popup()->selected(), 0);
  env.key(Key::Enter);
  CHECK_EQ(d->buffer().text(), std::string("int one;\n"));
  env.settings("editor.reloadExternalChanges = never\n");
  const std::string before = d->buffer().text();
  REQUIRE(
      changeOnDisk(env, "src/a.cpp", "int two;\n", [&] { return contains(env.app->message(), "changed on disk"); }));
  env.frame();
  CHECK(!env.app->externalChanges().isOpen());
  CHECK_EQ(d->buffer().text(), before);
}

TEST(p2_external_prompts_wait_for_other_popups_and_come_back) {
  Config c;
  c.watch = true;
  Env env(c);
  env.open("src/a.cpp");
  env.edit("// mine\n");
  env.run("Editor.GotoLine");
  REQUIRE(env.app->goToLine().isOpen());
  REQUIRE(changeOnDisk(env, "src/a.cpp", "int theirs;\n", [&] { return env.app->externalChanges().pending() > 0; }));
  env.frame();
  CHECK(!env.app->externalChanges().isOpen());  // queued behind the go-to-line popup
  env.key(Key::Escape);
  env.frame();
  CHECK(env.app->externalChanges().isOpen());
  // Another popup closing it only postpones the question.
  env.run("Editor.GotoLine");
  CHECK(!env.app->externalChanges().isOpen());
  CHECK_EQ(env.app->externalChanges().pending(), size_t(1));
  env.key(Key::Escape);
  env.frame();
  CHECK(env.app->externalChanges().isOpen());
}

TEST(p2_deleted_file_marks_the_tab_and_status_bar_and_keeps_the_buffer) {
  Env env;
  Document* d = env.open("src/a.cpp");
  const int id = env.tabOf("src/a.cpp");
  env.frame();
  CHECK_EQ(env.tabTitle(id), std::string("a.cpp"));
  CHECK_EQ(env.bench->status().text("disk"), std::string());
  const std::string before = d->buffer().text();
  fs::remove(env.dir / "src/a.cpp");
  env.now += 2000;
  env.frame();
  CHECK(env.app->externalChanges().isMissing(env.dir / "src/a.cpp"));
  CHECK_EQ(env.tabTitle(id), std::string("a.cpp (deleted)"));
  CHECK_EQ(env.bench->status().text("disk"), std::string("deleted on disk"));
  CHECK(contains(env.app->message(), "deleted"));
  CHECK_EQ(d->buffer().text(), before);  // the buffer is kept
  // Saving recreates the file and clears the marks.
  env.edit("// again\n");
  REQUIRE(env.ed->save(*d));
  env.now += 2000;
  env.frame();
  CHECK_EQ(env.tabTitle(id), std::string("a.cpp"));
  CHECK_EQ(env.bench->status().text("disk"), std::string());
  // A newly opened tab for a missing file carries the mark too.
  fs::remove(env.dir / "src/a.cpp");
  env.now += 2000;
  env.frame();
  env.bench->splitActive(Splitter::Orientation::Horizontal);
  env.frame();
  for (Panel* p : env.bench->panels())
    for (const auto& t : p->tabs().tabs()) CHECK_EQ(t.title, std::string("a.cpp (deleted)"));
}

// ---------------------------------------------------------------------------------------------------------------
// 3. clangd visibility
// ---------------------------------------------------------------------------------------------------------------

TEST(p2_clangd_segment_shows_the_engine_text_when_restarting) {
  Config c;
  c.fakeClangd = true;
  c.clangdArgs = {"--die-on-init-launches=1"};
  c.backoffInitialMs = 600000;  // stays in "restarting" for the whole test
  Env env(c);
  env.open("src/a.cpp");
  REQUIRE(env.ed->waitFor([&] { return !env.ed->clangdStatusText().empty(); }, 15000));
  env.frame();
  CHECK_EQ(env.ed->clangdStatusText(), std::string("clangd restarting (1)..."));
  CHECK_EQ(env.bench->status().text("clangd"), env.ed->clangdStatusText());
  CHECK(env.bench->status().segmentRect("clangd").w > 0);
}

TEST(p2_clangd_segment_shows_stopped_and_clangd_restart_recovers) {
  Config c;
  c.fakeClangd = true;
  c.clangdArgs = {"--die-on-init-launches=2"};
  c.maxCrashes = 2;
  Env env(c);
  env.open("src/a.cpp");
  REQUIRE(env.ed->waitFor([&] { return env.ed->clangdStatusText() == "clangd stopped: crashed 2 times"; }, 15000));
  env.frame();
  CHECK_EQ(env.bench->status().text("clangd"), std::string("clangd stopped: crashed 2 times"));
  // The palette finds the command; running it restarts clangd (the third launch is healthy).
  REQUIRE(env.app->commands().find("Clangd.Restart") != nullptr);
  env.run("Clangd.Restart");
  CHECK_EQ(env.app->message(), std::string("clangd restarted"));
  REQUIRE(env.ed->waitFor([&] { return env.ed->clangdStatusText().empty(); }, 5000));
  env.frame();
  CHECK(env.bench->status().text("clangd").rfind("clangd: ", 0) == 0);
  CHECK(!contains(env.bench->status().text("clangd"), "stopped"));
}

TEST(p2_clangd_restart_without_clangd_says_why) {
  Env env;  // engine started, clangd disabled
  env.run("Clangd.Restart");
  CHECK(contains(env.app->message(), "Restart clangd failed: clangd is disabled"));
}

TEST(p2_flags_heuristic_indicator_for_a_heuristic_workspace_and_its_click) {
  Config c;
  c.fakeClangd = true;
  Env env(c);
  env.open("src/a.cpp");
  REQUIRE(env.ed->waitFor([&] { return env.ed->semantic().status().clangdRunning; }, 10000));
  env.frame();
  CHECK_EQ(env.ed->semantic().status().compdbSource, std::string("heuristic"));
  CHECK_EQ(env.bench->status().text("flags"), std::string("flags: heuristic"));
  const RectF seg = env.bench->status().segmentRect("flags");
  REQUIRE(seg.w > 0);
  CHECK(!env.app->compileInfo().isOpen());
  env.click(seg.x + 3, seg.y + seg.h / 2);
  CHECK(env.app->compileInfo().isOpen());  // Help.ShowCompileCommand
  CHECK(contains(env.app->compileInfo().text(), "heuristic"));
}

TEST(p2_flags_heuristic_indicator_absent_for_a_vcxproj_workspace) {
  Config c;
  c.fakeClangd = true;
  c.fixture = "Multi";
  Env env(c);
  env.open("App/src/special.cpp");
  REQUIRE(env.ed->waitFor([&] { return env.ed->semantic().status().clangdRunning; }, 10000));
  env.frame();
  CHECK_EQ(env.ed->semantic().status().compdbSource, std::string("vcxproj"));
  CHECK_EQ(env.bench->status().text("flags"), std::string());
  CHECK_EQ(env.bench->status().segmentRect("flags").w, 0.0f);
}

// ---------------------------------------------------------------------------------------------------------------
// 4. Command palette
// ---------------------------------------------------------------------------------------------------------------

TEST(p2_palette_lists_commands_with_names_ids_and_bindings) {
  Env env;
  env.open("src/a.cpp");
  env.key(Key('P'), ModCtrl | ModShift);  // the provisional default binding
  REQUIRE(env.app->commandPalette().isOpen());
  ListPopup* p = env.app->commandPalette().popup();
  std::map<std::string, ListRow> byId;
  for (const auto& r : p->rows()) byId[r.secondary] = r;
  for (const char* id : {"File.Save", "Clangd.Restart", "Help.ShowCompileCommand", "Debug.OpenInVisualStudio",
                         "Build.SelectConfiguration", "Vcs.RevertFile", "Vcs.NextChange", "Editor.GotoDefinition"})
    CHECK(byId.count(id));
  CHECK_EQ(byId["File.Save"].primary, std::string("Save"));
  CHECK_EQ(byId["File.Save"].detail, std::string("Ctrl+S"));
  CHECK_EQ(byId["Editor.GotoDefinition"].detail, std::string("F12"));
  CHECK_EQ(byId["Clangd.Restart"].detail, std::string());  // unbound
  CHECK(!byId.count("App.CommandPalette"));
  CHECK(!byId.count("Workbench.FocusPanel"));  // needs an argument
  // Everything listed is runnable (bound by the front end).
  for (const auto& r : p->rows()) {
    const Command* c = env.app->commands().find(r.secondary);
    REQUIRE(c != nullptr);
    CHECK(static_cast<bool>(c->handler));
  }
  // Without a query the rows are sorted by name.
  std::vector<std::string> names;
  for (const auto& r : p->rows()) names.push_back(r.primary);
  CHECK(std::is_sorted(names.begin(), names.end(), [](const std::string& a, const std::string& b) {
    std::string x = a, y = b;
    for (auto& ch : x) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    for (auto& ch : y) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return x < y;
  }));
  env.key(Key::Escape);
  CHECK(!env.app->commandPalette().isOpen());
}

TEST(p2_palette_filters_fuzzily_by_name_and_id_and_enter_runs_the_command) {
  Env env;
  env.open("src/a.cpp");
  env.run("App.CommandPalette");
  REQUIRE(env.app->commandPalette().isOpen());
  env.type("restart clangd");
  REQUIRE(!env.app->commandPalette().shown().empty());
  CHECK_EQ(env.app->commandPalette().shown()[0], std::string("Clangd.Restart"));
  CHECK(!env.app->commandPalette().popup()->rows()[0].match.empty());  // matched characters are highlighted
  CHECK(env.app->commandPalette().shown().size() < 5);
  // Select all of the query and retype: an id fragment matches too.
  for (int i = 0; i < 14; ++i) env.key(Key::Backspace);
  env.type("vcsrevertf");
  REQUIRE(!env.app->commandPalette().shown().empty());
  CHECK_EQ(env.app->commandPalette().shown()[0], std::string("Vcs.RevertFile"));
  for (int i = 0; i < 10; ++i) env.key(Key::Backspace);
  env.type("zzzzqq");
  CHECK(env.app->commandPalette().shown().empty());
  for (int i = 0; i < 6; ++i) env.key(Key::Backspace);
  env.type("show compile");
  REQUIRE(!env.app->commandPalette().shown().empty());
  CHECK_EQ(env.app->commandPalette().shown()[0], std::string("Help.ShowCompileCommand"));
  env.key(Key::Enter);
  CHECK(!env.app->commandPalette().isOpen());
  CHECK(env.app->compileInfo().isOpen());  // it ran, and its own popup replaced the palette
}

TEST(p2_palette_enter_on_a_disabled_command_says_so) {
  Env env;  // no document open: File.Save is disabled
  env.run("App.CommandPalette");
  env.type("save");
  REQUIRE(!env.app->commandPalette().shown().empty());
  CHECK_EQ(env.app->commandPalette().shown()[0], std::string("File.Save"));
  CHECK(env.app->commandPalette().popup()->rows()[0].dim);
  env.key(Key::Enter);
  CHECK(contains(env.app->message(), "not available"));
  CHECK(!env.app->commandPalette().isOpen());
}
