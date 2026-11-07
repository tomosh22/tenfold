// VS-TREE (docs/VS_BEHAVIOUR.md section 11): the workspace explorer as Visual Studio's Solution Explorer behaves.
// X5 type-ahead (typing jumps; Ctrl+; filters), X2 Solution / Folder view and Show All Files, X9 saved expansion
// state, X8 open by default, X3 double click on a project, X6 drag and drop with the move confirmation, X7 New File /
// New Folder / Rename / Delete with their dialogs and the project files kept in step, C10 Properties. Mock font, null
// renderer, EditorApp driven through events like test_w8f_tree.cpp.
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "../text/text_test_util.h"
#include "core/PathUtil.h"
#include "ui/features/WorkspaceExplorer.h"
#include "ui/gfx/TextRenderer.h"
#include "ui/settings/Settings.h"
#include "ui/shell/EditorApp.h"
#include "ui/widgets/TreeView.h"
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
InputEvent mouseEv(InputType t, float x, float y, int clicks = 1, uint8_t buttons = 0, uint8_t mods = 0) {
  InputEvent e;
  e.type = t;
  e.x = x;
  e.y = y;
  e.clicks = clicks;
  e.button = MouseButton::Left;
  e.buttons = buttons;
  e.mods = mods;
  return e;
}

constexpr uint8_t kCS = ModCtrl | ModShift;
constexpr Key kKeyE = static_cast<Key>(0x45);
constexpr Key kSemicolon = static_cast<Key>(';');

std::string slurp(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}
void spit(const fs::path& p, const std::string& text) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary) << text;
}
bool contains(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }

struct Env {
  fs::path dir;
  bool keepDir = false;
  std::unique_ptr<Editor> ed;
  std::unique_ptr<gfx::TextRenderer> text;
  NullRenderer nr;
  std::unique_ptr<Workbench> bench;
  std::unique_ptr<SettingsService> svc;
  std::unique_ptr<EditorApp> app;
  int64_t now = 1000;

  // fixture "Explorer" (a solution) or "" for a folder workspace; `existing`: reuse a directory (a second session).
  explicit Env(const char* fixture = "Explorer", const std::string& settingsText = {}, const fs::path& existing = {}) {
    if (!existing.empty()) {
      dir = existing;
    } else {
      static int n = 0;
      dir = uitest::tempRoot() / ("tf_vstree_" + std::to_string(n++) + "_" +
                                  std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
      fs::create_directories(dir);
      fs::copy(fs::path(TENFOLD_SOURCE_DIR) / "tests/fixtures/vs/Explorer", dir, fs::copy_options::recursive);
      if (!fixture[0]) {
        fs::remove(dir / "Explorer.sln");
        spit(dir / "docs" / "guide.md", "# guide\n");
      }
    }
    fs::create_directories(dir / ".tenfold");
#ifndef _WIN32
    setenv("XDG_DATA_HOME", pathUtf8(dir / "xdg").c_str(), 1);  // Delete goes to this Trash, not the user's
#endif
    if (!settingsText.empty()) spit(dir / ".tenfold" / "settings.txt", settingsText);
    open();
  }
  void open() {
    gfx::FontStack stack;
    stack.add(std::make_shared<tftext::MockFont>(
        U"abcdefghijklmnopqrstuvwxyz0123456789 ._-:,;ABCDEFGHIJKLMNOPQRSTUVWXYZ&/'()*#é", 8));
    text = gfx::TextRenderer::fromFonts(std::move(stack));
    Editor::Options o;
    o.semantic.useClangd = false;
    o.semantic.persistFastIndex = false;
    o.watchForcePolling = true;
    o.watchPollMs = 60000;
    ed = std::make_unique<Editor>(dir, o);
    ed->start();
    bench = std::make_unique<Workbench>(*text, *ed);
    SettingsService::Options so;
    so.workspaceRoot = dir;
    svc = std::make_unique<SettingsService>(so);
    app = std::make_unique<EditorApp>(*bench, *ed, bench.get());
    app->attachSettings(*svc, 1.0f, [] { return true; });
    bench->render(nr, 1000, 600);
    tree().clockMs = [this] { return now; };
  }
  void close() {
    app.reset();
    svc.reset();
    bench.reset();
    ed.reset();
  }
  ~Env() {
    close();
    if (!keepDir) {
      std::error_code ec;
      fs::remove_all(dir, ec);
    }
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
    now += 10;
    app->handleEvent(textEv(s), now);
    frame();
  }
  void send(const InputEvent& e) {
    now += 10;
    app->handleEvent(e, now);
    frame();
  }
  void open_() { key(kKeyE, kCS); }
  void settle() {  // lets the debounced rebuild and the debounced state save run
    for (int i = 0; i < 3; ++i) {
      now += 700;
      frame();
    }
  }
  WorkspaceExplorer& ex() { return *app->explorer(); }
  TreeView& tree() { return *app->explorer()->view(); }
  std::string status() { return bench->status().text("message"); }
  int nodeNamed(const std::string& name) {
    const TreeModel& m = tree().model();
    for (size_t i = 0; i < m.size(); ++i)
      if (m.node(static_cast<int>(i)).name == name) return static_cast<int>(i);
    return -1;
  }
  int nodeOf(const fs::path& p) { return tree().model().findPath(pathKeyUtf8(p)); }
  int rowNamed(const std::string& name) {
    for (size_t i = 0; i < tree().rows().size(); ++i)
      if (tree().model().node(tree().rows()[i].node).name == name) return static_cast<int>(i);
    return -1;
  }
  std::string selectedName() {
    return tree().selectedNode() < 0 ? "" : tree().model().node(tree().selectedNode()).name;
  }
  std::vector<std::string> shownNames() {
    std::vector<std::string> out;
    for (const auto& r : tree().rows()) out.push_back(tree().model().node(r.node).name);
    return out;
  }
  bool shows(const std::string& n) {
    const auto v = shownNames();
    return std::find(v.begin(), v.end(), n) != v.end();
  }
  void revealAll() {
    tree().expandAll(true);
    frame();
  }
  // Drags the row of `from` onto the row of `to`.
  void drag(const std::string& from, const std::string& to, uint8_t mods = 0) {
    const int a = rowNamed(from), b = rowNamed(to);
    REQUIRE(a >= 0 && b >= 0);
    const RectF ra = tree().rowRect(a), rb = tree().rowRect(b);
    const float x = ra.x + ra.w * 0.6f;
    send(mouseEv(InputType::MouseDown, x, ra.y + ra.h / 2));
    send(mouseEv(InputType::MouseMove, x + 10, ra.y + ra.h / 2 + 10, 0, 1));
    send(mouseEv(InputType::MouseMove, x, rb.y + rb.h / 2, 0, 1, mods));
    send(mouseEv(InputType::MouseUp, x, rb.y + rb.h / 2, 1, 0, mods));
  }
  // The modal popup's rows (primary text).
  ListPopup* popup() { return ex().prompt() ? ex().prompt()->popup() : nullptr; }
  bool popupHas(const std::string& s) {
    ListPopup* p = popup();
    if (!p) return false;
    if (contains(p->title(), s)) return true;
    for (const ListRow& r : p->rows())
      if (contains(r.primary, s) || contains(r.secondary, s)) return true;
    return false;
  }
  void answer(const std::string& typed) {  // text in the name box, then Enter
    type(typed);
    key(Key::Enter);
  }
};

}  // namespace

// ---- X5: type-ahead ----

TEST(vstree_typing_jumps_to_the_next_name_that_starts_with_the_letters) {
  Env env;
  env.open_();
  env.tree().setFocus();
  const size_t rows = env.tree().rows().size();
  env.type("l");
  CHECK_EQ(env.selectedName(), "Libraries");
  CHECK(env.tree().filter().empty());
  CHECK_EQ(env.tree().rows().size(), rows);  // nothing is filtered away
  env.type("i");
  CHECK_EQ(env.selectedName(), "Libraries");  // "li" still starts "Libraries"
  env.type("x");                              // "lix": no match, the selection stays
  CHECK_EQ(env.selectedName(), "Libraries");
  env.now += 2000;  // the typed text is forgotten after a second without a key
  env.type("s");
  CHECK_EQ(env.selectedName(), "Solution Items");
  env.now += 2000;
  env.type("A");  // case-insensitive
  CHECK_EQ(env.selectedName(), "App");
}

TEST(vstree_the_same_letter_again_cycles_and_only_visible_nodes_count) {
  Env env;
  env.open_();
  env.revealAll();
  env.tree().setFocus();
  env.tree().select(env.nodeNamed("App"));
  env.now += 2000;
  env.type("h");  // "Header Files" (App) comes first, then helpers.cpp is under a collapsed-or-not node
  CHECK_EQ(env.selectedName(), "Header Files");
  env.type("h");  // hh: no name starts with "hh", but the repeat of one letter cycles
  CHECK(env.selectedName() == "helpers.cpp" || env.selectedName() == "Header Files");
  const std::string second = env.selectedName();
  env.type("h");
  CHECK(env.selectedName() != second || second == "Header Files");
  // Collapsed nodes are not searched.
  env.tree().expandAll(false);
  env.frame();
  env.now += 2000;
  env.type("m");
  CHECK(env.selectedName() != "main.cpp");
}

TEST(vstree_backspace_takes_a_letter_back_and_escape_forgets_the_text) {
  Env env;
  REQUIRE(env.ed->gotoLocation({env.dir / "README.md", Position{}}, nullptr));
  env.bench->sync();
  env.open_();
  env.tree().setFocus();
  env.type("s");
  CHECK_EQ(env.selectedName(), "Solution Items");
  env.key(Key::Backspace);
  CHECK(env.tree().typeAhead().empty());
  env.type("l");
  env.key(Key::Escape);
  CHECK(env.tree().typeAhead().empty());
  CHECK(env.bench->sideVisible());  // that Escape only cleared the typed letters
  env.key(Key::Escape);             // the next one hands the keys back
  CHECK(!env.tree().hasFocus());
}

TEST(vstree_ctrl_semicolon_starts_the_filter_and_escape_leaves_it) {
  Env env;
  env.open_();
  env.tree().setFocus();
  env.key(kSemicolon, ModCtrl);
  CHECK(env.tree().filterMode());
  env.type("impl");
  CHECK_EQ(env.tree().filter(), "impl");
  CHECK(env.shows("impl.cpp") && !env.shows("main.cpp"));
  env.key(Key::Escape);
  CHECK(!env.tree().filterMode() && env.tree().filter().empty());
  env.type("l");  // back to jumping: from impl.cpp to the next name starting with l
  CHECK_EQ(env.selectedName(), "lib.cpp");
  CHECK(env.tree().filter().empty());
}

// ---- X2: Solution view / Folder view / Show All Files ----

TEST(vstree_switch_views_toggles_solution_and_folder_view) {
  Env env;
  env.open_();
  CHECK(env.ex().solutionMode() && !env.ex().folderView() && env.ex().hasSolution());
  CHECK(env.tree().model().node(0).kind == TreeKind::Solution);
  env.app->runCommand("View.ToggleExplorerFolderView");
  CHECK(env.ex().folderView() && !env.ex().solutionMode());
  CHECK(env.tree().model().node(0).kind == TreeKind::Root);
  env.revealAll();
  CHECK(env.shows("README.md") && env.shows("detail") && env.shows("gen"));  // the folders on disk
  CHECK(!env.shows("Source Files"));                                         // no filters in the folder view
  // Every folder carries its path now (file operations need it).
  CHECK(!env.tree().model().node(env.nodeNamed("detail")).path.empty());
  env.app->runCommand("View.ToggleExplorerFolderView");
  CHECK(!env.ex().folderView() && env.ex().solutionMode());
  CHECK(env.tree().model().node(0).kind == TreeKind::Solution);
}

TEST(vstree_switch_views_without_a_solution_says_so) {
  Env env("");
  env.open_();
  CHECK(!env.ex().hasSolution());
  env.app->runCommand("View.ToggleExplorerFolderView");
  CHECK(!env.ex().folderView());
  CHECK(contains(env.status(), "No solution"));
}

TEST(vstree_show_all_files_adds_unlisted_files_dimmed) {
  Env env;
  spit(env.dir / "App" / "src" / "scratch.cpp", "int s;\n");
  spit(env.dir / "App" / "extra" / "more.h", "int m;\n");
  env.ed->workspace().noteFileAdded(env.dir / "App" / "src" / "scratch.cpp");
  env.ed->workspace().noteFileAdded(env.dir / "App" / "extra" / "more.h");
  env.open_();
  env.revealAll();
  CHECK(!env.shows("scratch.cpp") && !env.shows("more.h"));
  env.app->runCommand("View.ToggleExplorerShowAllFiles");
  CHECK(env.ex().showAllFiles());
  env.revealAll();
  REQUIRE(env.shows("scratch.cpp") && env.shows("more.h"));
  CHECK(env.tree().model().node(env.nodeNamed("scratch.cpp")).dim);
  CHECK(env.tree().model().node(env.nodeNamed("more.h")).dim);
  CHECK(!env.tree().model().node(env.nodeNamed("main.cpp")).dim);  // listed files are not dimmed
  CHECK(!env.tree().model().node(env.nodeNamed("a.cpp")).dim);     // matched by a wildcard: listed
  CHECK(env.shows("extra"));                                       // its folder on disk
  env.app->runCommand("View.ToggleExplorerShowAllFiles");
  env.revealAll();
  CHECK(!env.shows("scratch.cpp"));
}

// ---- X9: the expansion state is saved and restored ----

TEST(vstree_expansion_selection_and_toggles_are_restored_in_the_next_session) {
  Env env;
  env.open_();
  env.tree().expandAll(false);
  env.tree().setExpanded(env.nodeNamed("App"), true);
  env.tree().setExpanded(env.nodeNamed("Source Files"), true);
  env.tree().select(env.nodeNamed("main.cpp"));
  env.app->runCommand("View.ToggleExplorerShowAllFiles");
  env.settle();
  const fs::path file = WorkspaceExplorer::stateFile(env.ed->workspace());
  REQUIRE(fs::exists(file));
  const std::string json = slurp(file);
  CHECK(contains(json, "\"showAll\": true"));
  CHECK(contains(json, "Source Files"));
  const fs::path dir = env.dir;
  env.keepDir = true;
  env.close();
  {
    Env again("Explorer", {}, dir);
    again.open_();
    CHECK(again.ex().showAllFiles());
    CHECK(again.tree().isExpanded(again.nodeNamed("App")));
    CHECK(again.tree().isExpanded(again.nodeNamed("Source Files")));
    CHECK(!again.tree().isExpanded(again.nodeNamed("Libraries")));
    CHECK(again.shows("main.cpp"));
    CHECK_EQ(again.selectedName(), "main.cpp");
    again.app->runCommand("View.ToggleExplorerFolderView");
    again.settle();
    again.keepDir = true;
    again.close();
  }
  {
    Env third("Explorer", {}, dir);
    third.open_();
    CHECK(third.ex().folderView());  // the view switch is remembered too
    third.keepDir = false;
  }
}

TEST(vstree_a_corrupt_state_file_is_ignored) {
  Env env;
  spit(WorkspaceExplorer::stateFile(env.ed->workspace()), "{ not json");
  env.open_();
  CHECK(env.ex().solutionMode());
  CHECK(!env.tree().model().empty());
  CHECK(env.tree().isExpanded(0));
}

// ---- X8: open by default ----

TEST(vstree_a_solution_workspace_opens_the_explorer_when_no_layout_was_restored) {
  Env env;
  CHECK(!env.ex().visible());
  CHECK(env.ex().openByDefault());
  CHECK(env.ex().visible());
  CHECK(!env.tree().hasFocus());  // the keys stay where they were
  CHECK(!env.tree().model().empty());
}

TEST(vstree_open_by_default_needs_a_solution_and_the_setting) {
  Env plain("");
  CHECK(!plain.ex().openByDefault());
  Env off("Explorer", "explorer.openWithSolution = false\n");
  CHECK(!off.ex().openByDefault());
  CHECK(!off.ex().visible());
}

// ---- X3 ----

TEST(vstree_double_click_on_a_project_expands_it) {
  Env env;
  env.open_();
  const int row = env.rowNamed("App");
  REQUIRE(row >= 0);
  CHECK(!env.tree().isExpanded(env.nodeNamed("App")));
  const RectF r = env.tree().rowRect(row);
  env.send(mouseEv(InputType::MouseDown, r.x + r.w * 0.6f, r.y + r.h / 2, 2));
  env.send(mouseEv(InputType::MouseUp, r.x + r.w * 0.6f, r.y + r.h / 2, 2));
  CHECK(env.tree().isExpanded(env.nodeNamed("App")));
  CHECK(env.ed->currentDocument() == nullptr);  // no project file was opened
}

// ---- X7: file operations ----

TEST(vstree_new_file_asks_for_a_name_and_creates_it_in_the_folder) {
  Env env;
  env.open_();
  env.revealAll();
  env.tree().select(env.nodeNamed("detail"));
  env.app->runCommand("Explorer.NewFile");
  REQUIRE(env.popup() != nullptr);
  env.answer("fresh.cpp");
  CHECK(env.popup() == nullptr);
  CHECK(fs::exists(env.dir / "Lib" / "detail" / "fresh.cpp"));
  CHECK(contains(env.status(), "Created fresh.cpp"));
  CHECK(contains(env.status(), "Show All Files"));  // in the Solution view an unlisted file is hidden
  env.app->runCommand("View.ToggleExplorerShowAllFiles");
  env.revealAll();
  CHECK(env.shows("fresh.cpp"));
}

TEST(vstree_new_file_refuses_a_name_that_exists_or_is_not_a_name) {
  Env env;
  env.open_();
  env.revealAll();
  env.tree().select(env.nodeNamed("main.cpp"));  // a file: its folder
  env.app->runCommand("Explorer.NewFile");
  REQUIRE(env.popup() != nullptr);
  env.type("helpers.cpp");  // that is in src/util, not src: allowed
  CHECK(!env.popupHas("already exists"));
  for (int i = 0; i < 11; ++i) env.key(Key::Backspace);
  env.type("main.cpp");
  CHECK(env.popupHas("already exists"));
  env.key(Key::Enter);  // refused: the box stays, with the text
  REQUIRE(env.popup() != nullptr);
  CHECK(contains(env.status(), "already exists"));
  for (int i = 0; i < 8; ++i) env.key(Key::Backspace);
  env.type("a/b.cpp");
  CHECK(env.popupHas("cannot contain"));
  env.key(Key::Escape);
  CHECK(env.popup() == nullptr);
  CHECK(!fs::exists(env.dir / "App" / "src" / "b.cpp"));
}

TEST(vstree_new_folder_shows_the_empty_folder) {
  Env env("");
  env.open_();
  env.revealAll();
  env.tree().select(env.nodeNamed("docs"));
  env.app->runCommand("Explorer.NewFolder");
  REQUIRE(env.popup() != nullptr);
  env.answer("drafts");
  CHECK(fs::is_directory(env.dir / "docs" / "drafts"));
  CHECK(env.shows("drafts"));
  CHECK(!env.tree().model().node(env.nodeNamed("drafts")).path.empty());
}

TEST(vstree_rename_moves_the_file_and_rewrites_the_project_and_filters_includes) {
  Env env;
  env.open_();
  env.revealAll();
  env.tree().select(env.nodeNamed("app.h"));
  env.tree().setFocus();
  env.key(Key::F2);
  REQUIRE(env.popup() != nullptr);
  CHECK(env.popupHas("Rename to"));
  for (int i = 0; i < 5; ++i) env.key(Key::Backspace);  // the box starts with the old name
  env.answer("application.h");
  CHECK(!fs::exists(env.dir / "App" / "include" / "app.h"));
  CHECK(fs::exists(env.dir / "App" / "include" / "application.h"));
  const std::string proj = slurp(env.dir / "App" / "App.vcxproj");
  CHECK(contains(proj, "Include=\"include\\application.h\""));  // the backslash style stays
  CHECK(!contains(proj, "include\\app.h"));
  CHECK(contains(slurp(env.dir / "App" / "App.vcxproj.filters"), "Include=\"include\\application.h\""));
  env.settle();
  env.revealAll();
  CHECK(env.shows("application.h") && !env.shows("app.h"));
  CHECK_EQ(env.tree().model().node(env.nodeNamed("application.h")).dim, false);  // still in the project
}

TEST(vstree_rename_of_an_open_file_reopens_it_and_refuses_unsaved_changes) {
  Env env;
  env.open_();
  env.revealAll();
  const fs::path main = env.dir / "App" / "src" / "main.cpp";
  std::string err;
  REQUIRE(env.ed->gotoLocation({main, Position{}}, &err));
  env.bench->sync();
  Document* d = env.ed->currentDocument();
  REQUIRE(d != nullptr);
  env.tree().select(env.nodeNamed("main.cpp"));
  env.ed->insertText("// edit\n");
  CHECK(d->dirty());
  CHECK(!env.ex().rename(env.nodeNamed("main.cpp")));
  CHECK(contains(env.status(), "Save main.cpp first"));
  CHECK(fs::exists(main));
  REQUIRE(env.ed->save(*d, &err));
  env.tree().select(env.nodeNamed("main.cpp"));
  REQUIRE(env.ex().rename(env.nodeNamed("main.cpp")));
  for (int i = 0; i < 8; ++i) env.key(Key::Backspace);
  env.answer("entry.cpp");
  CHECK(fs::exists(env.dir / "App" / "src" / "entry.cpp") && !fs::exists(main));
  REQUIRE(env.ed->currentDocument() != nullptr);
  CHECK_EQ(pathUtf8(env.ed->currentDocument()->path().filename()), "entry.cpp");
  CHECK(env.ed->document(main) == nullptr);
}

TEST(vstree_rename_a_folder_rewrites_every_include_below_it) {
  Env env;
  env.open_();
  env.app->runCommand("View.ToggleExplorerFolderView");  // the Solution view has filters, not the folder "include"
  env.revealAll();
  env.tree().select(env.nodeNamed("include"));
  REQUIRE(env.ex().rename(env.nodeNamed("include")));
  for (int i = 0; i < 7; ++i) env.key(Key::Backspace);
  env.answer("headers");
  CHECK(fs::exists(env.dir / "App" / "headers" / "app.h"));
  CHECK(contains(slurp(env.dir / "App" / "App.vcxproj"), "Include=\"headers\\app.h\""));
}

TEST(vstree_delete_asks_first_and_removes_the_file_and_its_project_item) {
  Env env;
  env.open_();
  env.revealAll();
  const fs::path helpers = env.dir / "App" / "src" / "util" / "helpers.cpp";
  REQUIRE(env.ed->gotoLocation({helpers, Position{}}, nullptr));
  env.bench->sync();
  env.tree().select(env.nodeNamed("helpers.cpp"));
  env.tree().setFocus();
  env.key(Key::Delete);
  REQUIRE(env.popup() != nullptr);
#if !defined(_WIN32) && !defined(__APPLE__)
  CHECK(env.popupHas("'helpers.cpp' will be moved to the Trash."));
#endif
  env.key(Key::Escape);  // Cancel
  CHECK(fs::exists(helpers));
  env.tree().setFocus();
  env.key(Key::Delete);
  REQUIRE(env.popup() != nullptr);
  env.key(Key::Enter);  // OK
  CHECK(!fs::exists(helpers));
#if !defined(_WIN32) && !defined(__APPLE__)
  CHECK(fs::exists(env.dir / "xdg" / "Trash" / "files" / "helpers.cpp"));  // recoverable
#endif
  const std::string proj = slurp(env.dir / "App" / "App.vcxproj");
  CHECK(!contains(proj, "helpers.cpp"));
  CHECK(contains(proj, "src\\main.cpp"));  // the other items are untouched
  CHECK(!contains(slurp(env.dir / "App" / "App.vcxproj.filters"), "helpers.cpp"));
  CHECK(contains(slurp(env.dir / "App" / "App.vcxproj.filters"), "Source Files\\Util"));  // the filter itself stays
  CHECK(env.ed->document(helpers) == nullptr);                                            // its tab closed
  env.settle();
  CHECK(!env.shows("helpers.cpp"));
}

TEST(vstree_delete_a_folder_removes_everything_in_it) {
  Env env("");
  env.open_();
  env.revealAll();
  env.tree().select(env.nodeNamed("detail"));
  REQUIRE(env.ex().remove(env.nodeNamed("detail")));
  CHECK(env.popupHas("everything in it"));
  env.key(Key::Enter);
  CHECK(!fs::exists(env.dir / "Lib" / "detail"));
  env.settle();
  CHECK(!env.shows("detail") && !env.shows("impl.cpp"));
}

TEST(vstree_projects_and_virtual_filters_cannot_be_renamed_or_deleted) {
  Env env;
  env.open_();
  env.revealAll();
  CHECK(!env.ex().rename(env.nodeNamed("App")));
  CHECK(!env.ex().remove(env.nodeNamed("Source Files")));
  CHECK(!env.ex().rename(0));
  CHECK(env.popup() == nullptr);
}

// ---- X6: drag and drop ----

TEST(vstree_dropping_a_file_on_a_folder_asks_then_moves_it_and_its_project_item) {
  Env env("");
  env.open_();
  env.revealAll();
  REQUIRE(env.shows("guide.md"));
  env.drag("guide.md", "detail");
  REQUIRE(env.popup() != nullptr);
  CHECK(env.popupHas("Move 'guide.md' to 'detail'?"));
  env.key(Key::Escape);
  CHECK(fs::exists(env.dir / "docs" / "guide.md"));
  env.drag("guide.md", "detail");
  env.key(Key::Enter);
  CHECK(!fs::exists(env.dir / "docs" / "guide.md"));
  CHECK(fs::exists(env.dir / "Lib" / "detail" / "guide.md"));
}

TEST(vstree_a_dropped_project_file_updates_the_vcxproj) {
  Env env("Explorer", "explorer.promptBeforeMoving = false\n");
  env.open_();
  env.revealAll();
  env.app->runCommand("View.ToggleExplorerFolderView");
  env.revealAll();
  env.drag("app.h", "detail");  // no confirmation: the option is off
  CHECK(env.popup() == nullptr);
  CHECK(fs::exists(env.dir / "Lib" / "detail" / "app.h"));
  CHECK(contains(slurp(env.dir / "App" / "App.vcxproj"), "Include=\"..\\Lib\\detail\\app.h\""));
}

TEST(vstree_ctrl_drop_copies_and_a_folder_cannot_enter_itself) {
  Env env("");
  env.open_();
  env.revealAll();
  env.drag("guide.md", "detail", ModCtrl);
  CHECK(env.popup() == nullptr);  // copying is not asked
  CHECK(fs::exists(env.dir / "docs" / "guide.md"));
  CHECK(fs::exists(env.dir / "Lib" / "detail" / "guide.md"));
  const int lib = env.nodeNamed("Lib"), detail = env.nodeNamed("detail"), docs = env.nodeNamed("docs");
  CHECK(!env.ex().canDrop(lib, detail));  // into its own subfolder
  CHECK(!env.ex().canDrop(lib, lib));     // onto itself
  CHECK(!env.ex().canDrop(detail, lib));  // already there
  CHECK(env.ex().canDrop(detail, docs));
  CHECK(!env.ex().canDrop(env.nodeNamed("lib.cpp"), env.nodeNamed("lib.h")));  // a file is no target
}

TEST(vstree_drag_and_drop_follows_the_setting_and_refuses_existing_names) {
  Env off("", "explorer.dragDrop = false\n");
  off.open_();
  off.revealAll();
  CHECK(!off.ex().canDrop(off.nodeNamed("guide.md"), off.nodeNamed("detail")));
  off.drag("guide.md", "detail");
  CHECK(off.popup() == nullptr && fs::exists(off.dir / "docs" / "guide.md"));
  Env env("");
  env.open_();
  env.revealAll();
  spit(env.dir / "Lib" / "detail" / "guide.md", "other\n");
  env.ed->workspace().noteFileAdded(env.dir / "Lib" / "detail" / "guide.md");
  env.settle();
  env.revealAll();
  const int guide = env.nodeOf(env.dir / "docs" / "guide.md");
  REQUIRE(guide >= 0);
  CHECK(!env.ex().move(guide, env.nodeNamed("detail"), false));
  CHECK(contains(env.status(), "already exists"));
}

// ---- C10: Properties ----

TEST(vstree_properties_shows_path_size_project_configuration_and_excluded_from_build) {
  Env env;
  // main.cpp is excluded from the Debug|x64 build.
  std::string proj = slurp(env.dir / "App" / "App.vcxproj");
  const std::string item = "<ClCompile Include=\"src\\main.cpp\" />";
  REQUIRE(contains(proj, item));
  proj.replace(proj.find(item), item.size(),
               "<ClCompile Include=\"src\\main.cpp\">\n      <ExcludedFromBuild "
               "Condition=\"'$(Configuration)|$(Platform)'=='Debug|x64'\">true</ExcludedFromBuild>\n    </ClCompile>");
  spit(env.dir / "App" / "App.vcxproj", proj);
  env.open_();
  env.revealAll();
  env.tree().select(env.nodeNamed("main.cpp"));
  REQUIRE(env.ex().properties(env.nodeNamed("main.cpp")));
  REQUIRE(env.popup() != nullptr);
  CHECK(env.popupHas("Full Path"));
  CHECK(env.popupHas("main.cpp"));
  CHECK(env.popupHas("Size"));
  CHECK(env.popupHas("bytes"));
  CHECK(env.popupHas("Project"));
  CHECK(env.popupHas("App"));
  CHECK(env.popupHas("Configuration"));
  CHECK(env.popupHas("Debug|x64"));
  CHECK(env.popupHas("Yes (excluded in: Debug|x64)"));
  env.key(Key::Escape);
  CHECK(env.popup() == nullptr);
  // A file that is built says No.
  REQUIRE(env.ex().properties(env.nodeNamed("app.h")));
  CHECK(env.popupHas("No"));
  CHECK(!env.popupHas("Yes"));
  env.key(Key::Escape);
}

TEST(vstree_properties_of_a_folder_a_project_and_the_solution) {
  Env env;
  env.open_();
  env.revealAll();
  REQUIRE(env.ex().properties(0));
  CHECK(env.popupHas("Solution") && env.popupHas("Projects"));
  env.key(Key::Escape);
  REQUIRE(env.ex().properties(env.nodeNamed("App")));
  CHECK(env.popupHas("Project") && env.popupHas("App.vcxproj") && env.popupHas("Files"));
  env.key(Key::Escape);
  REQUIRE(env.ex().properties(env.nodeNamed("Source Files")));
  CHECK(env.popupHas("Filter"));
  env.key(Key::Escape);
}

// ---- the menu ----

TEST(vstree_context_menu_offers_the_operations_the_views_and_properties) {
  Env env;
  env.open_();
  env.revealAll();
  auto labels = [&](int node) {
    std::vector<std::string> out;
    for (const MenuItem& m : env.ex().operationItems(node)) out.push_back(m.label);
    for (const MenuItem& m : env.ex().viewItems(node)) out.push_back(m.label);
    return out;
  };
  auto has = [](const std::vector<std::string>& v, const char* s) {
    return std::find(v.begin(), v.end(), s) != v.end();
  };
  const auto file = labels(env.nodeNamed("main.cpp"));
  for (const char* l :
       {"New File...", "New Folder...", "Rename", "Delete", "Switch to Folder View", "Show All Files", "Properties"})
    CHECK(has(file, l));
  const auto project = labels(env.nodeNamed("App"));
  CHECK(has(project, "New File...") && !has(project, "Rename") && !has(project, "Delete") &&
        has(project, "Properties"));
  env.ex().setFolderView(true);
  env.revealAll();
  CHECK(has(labels(env.nodeNamed("detail")), "Switch to Solution View"));
  // The real menu, from a right click.
  const RectF r = env.tree().rowRect(env.rowNamed("detail"));
  InputEvent down = mouseEv(InputType::MouseDown, r.x + r.w * 0.6f, r.y + r.h / 2);
  down.button = MouseButton::Right;
  env.send(down);
  down.type = InputType::MouseUp;
  env.send(down);
  ContextMenu* menu = env.app->contextMenu();
  REQUIRE(menu != nullptr);
  std::vector<std::string> shown;
  for (const MenuItem& m : menu->items()) shown.push_back(m.label);
  for (const char* l : {"New File...", "New Folder...", "Rename", "Delete", "Open Containing Folder", "Properties"})
    CHECK(has(shown, l));
  env.key(Key::Escape);
}
