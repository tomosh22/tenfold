// W8f: the workspace explorer tree (ToggleWorkspaceExplorer, ShowInWorkspaceTree[WithToggle]). Model tests from the
// tests/fixtures/vs/Explorer solution (filters file, folder grouping, solution folders, wildcards) and from scanned
// folders; TreeView keyboard / mouse / filter tests; the commands through their 10x default keys with EditorApp on a
// null renderer and a mock font; layout persistence; refresh on file-list and project changes; a golden (needs
// DejaVuSansMono and GL) and the 50k-file benchmark (budgets only enforced with TENFOLD_ENFORCE_PERF=1).
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <thread>

#include "../text/text_test_util.h"
#include "test_vcs_util.h"
#include "ui/commands/CommandNames.h"
#include "ui/features/WorkspaceExplorer.h"
#include "ui/gfx/PngWrite.h"
#include "ui/gfx/TextRenderer.h"
#include "ui/settings/Settings.h"
#include "ui/settings/Theme.h"
#include "ui/shell/EditorApp.h"
#include "ui/widgets/TreeView.h"
#include "ui_test.h"

using namespace tf;
using namespace tf::ui;
namespace fs = std::filesystem;

namespace {

struct NullRenderer : gfx::Renderer {
  size_t rects = 0;
  void beginFrame(int, int, gfx::Color) override { rects = 0; }
  void drawRect(const gfx::RectF&, gfx::Color) override { ++rects; }
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
InputEvent mouseEv(InputType t, float x, float y, int clicks = 1) {
  InputEvent e;
  e.type = t;
  e.x = x;
  e.y = y;
  e.clicks = clicks;
  e.button = MouseButton::Left;
  return e;
}

constexpr uint8_t kCS = ModCtrl | ModShift;
constexpr Key kKeyE = static_cast<Key>(0x45), kKeyT = static_cast<Key>(0x54);

// Indented names of the whole model (two spaces per level), one per line.
std::string outline(const TreeModel& m) {
  std::string out;
  std::function<void(int, int)> walk = [&](int id, int depth) {
    out += std::string(static_cast<size_t>(depth) * 2, ' ') + m.node(id).name + "\n";
    for (int k : m.node(id).kids) walk(k, depth + 1);
  };
  if (!m.empty()) walk(0, 0);
  return out;
}

fs::path copyFixture(const char* name, const std::string& tag) {
  static int n = 0;
  const fs::path d = uitest::tempRoot() / ("tf_w8f_" + tag + "_" + std::to_string(n++) + "_" +
                                           std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  fs::create_directories(d);
  fs::copy(fs::path(TENFOLD_SOURCE_DIR) / "tests/fixtures/vs" / name, d, fs::copy_options::recursive);
  return d;
}

void put(const fs::path& p, const std::string& text = "int x;\n") {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary) << text;
}

const std::string kSolutionOutline =
    "Solution 'Explorer' (2 projects)\n"
    "  App\n"
    "    Empty Filter\n"
    "    Generated\n"
    "      a.cpp\n"
    "      b.cpp\n"
    "    Header Files\n"
    "      app.h\n"
    "    Source Files\n"
    "      Util\n"
    "        helpers.cpp\n"
    "      main.cpp\n"
    "    common.h\n"
    "  Libraries\n"
    "    Lib\n"
    "      detail\n"
    "        impl.cpp\n"
    "      shared\n"
    "        common.h\n"
    "      lib.cpp\n"
    "      lib.h\n"
    "      notes.txt\n"
    "  Solution Items\n"
    "    README.md\n";

struct Env {
  fs::path dir;
  std::unique_ptr<Editor> ed;
  std::unique_ptr<gfx::TextRenderer> text;
  NullRenderer nr;
  std::unique_ptr<Workbench> bench;
  std::unique_ptr<SettingsService> svc;
  std::unique_ptr<EditorApp> app;
  int64_t now = 1000;

  // fixture: a directory under tests/fixtures/vs, or "" for a small plain folder workspace.
  explicit Env(const char* fixture = "", const std::string& settingsText = {}, bool restoreLayout = false) {
    dir = fixture[0] ? copyFixture(fixture, "ws") : copyFixture("Explorer", "plain");
    if (!fixture[0]) {  // a folder workspace: no solution, plus ignored directories
      fs::remove(dir / "Explorer.sln");
      put(dir / "build" / "out.cpp");
      put(dir / ".git" / "config.txt");
      put(dir / "docs" / "guide.md", "# guide\n");
    }
    fs::create_directories(dir / ".tenfold");
    if (!settingsText.empty()) std::ofstream(dir / ".tenfold" / "settings.txt", std::ios::binary) << settingsText;
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
    ed->start();  // scans the workspace
    bench = std::make_unique<Workbench>(*text, *ed);
    if (restoreLayout) bench->restoreLayout();
    SettingsService::Options so;
    so.workspaceRoot = dir;
    svc = std::make_unique<SettingsService>(so);
    app = std::make_unique<EditorApp>(*bench, *ed, bench.get());
    app->attachSettings(*svc, 1.0f, [] { return true; });
    bench->render(nr, 1000, 600);
  }
  ~Env() {
    app.reset();
    svc.reset();
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
    now += 10;
    app->handleEvent(textEv(s), now);
    frame();
  }
  void click(float x, float y, int clicks = 1) {
    now += 10;
    app->handleEvent(mouseEv(InputType::MouseDown, x, y, clicks), now);
    app->handleEvent(mouseEv(InputType::MouseUp, x, y, clicks), now);
    frame();
  }
  std::string status(const char* seg) { return bench->status().text(seg); }
  WorkspaceExplorer& ex() { return *app->explorer(); }
  TreeView& tree() { return *app->explorer()->view(); }
  fs::path current() {
    Document* d = ed->currentDocument();
    return d ? d->path() : fs::path();
  }
  int nodeOf(const fs::path& p) { return tree().model().findPath(p.generic_string()); }
  std::string selectedName() {
    return tree().selectedNode() < 0 ? "" : tree().model().node(tree().selectedNode()).name;
  }
  // Row index of the node named `name` (first), -1 when it is not shown.
  int rowNamed(const std::string& name) {
    for (size_t i = 0; i < tree().rows().size(); ++i)
      if (tree().model().node(tree().rows()[i].node).name == name) return static_cast<int>(i);
    return -1;
  }
  void clickRow(int row, int clicks = 1) {
    const RectF r = tree().rowRect(row);
    click(r.x + r.w * 0.6f, r.y + r.h / 2, clicks);
  }
  std::vector<std::string> shownNames() {
    std::vector<std::string> out;
    for (const auto& r : tree().rows()) out.push_back(tree().model().node(r.node).name);
    return out;
  }
};

bool has(const std::vector<std::string>& v, const std::string& s) {
  return std::find(v.begin(), v.end(), s) != v.end();
}

}  // namespace

// ---- the 10x names resolve ----

TEST(w8f_commands_resolve_and_are_bound) {
  Env env;
  for (const char* name : {"ToggleWorkspaceExplorer", "ShowInWorkspaceTree", "ShowInWorkspaceTreeWithToggle"}) {
    const ResolvedCommand rc = resolveCommandName(name);
    CHECK(rc.known10x && rc.implemented);
    const Command* c = env.app->commands().find(rc.id);
    CHECK(c && c->handler);
  }
  auto bound = [&](const std::string& command) {
    std::string out;
    for (const KeyBinding& b : env.app->keys().bindings())
      if (b.command == command) out = formatChords(b.chords);
    return out;
  };
  CHECK_EQ(bound("View.ToggleWorkspaceExplorer"), "Control Shift E");
  CHECK_EQ(bound("View.ShowInWorkspaceTree"), "Control T");
  CHECK(bound("View.ShowInWorkspaceTreeWithToggle").empty());  // unbound in 10x too
}

// ---- model ----

TEST(w8f_project_files_come_from_the_vcxproj_and_its_filters) {
  const fs::path dir = copyFixture("Explorer", "proj");
  const ProjectContents pc = WorkspaceExplorer::readProject(dir / "App" / "App.vcxproj");
  CHECK(pc.loaded && pc.hasFiltersFile);
  CHECK_EQ(pc.filters.size(), size_t(5));
  CHECK(std::find(pc.filters.begin(), pc.filters.end(), "Source Files\\Util") != pc.filters.end());
  std::map<std::string, std::string> byName;
  for (const ProjectFile& f : pc.files) byName[fs::path(f.path).filename().string()] = f.filter;
  CHECK_EQ(pc.files.size(), size_t(6));  // the wildcard matched a.cpp and b.cpp but not skip.txt
  CHECK_EQ(byName["main.cpp"], "Source Files");
  CHECK_EQ(byName["helpers.cpp"], "Source Files\\Util");
  CHECK_EQ(byName["app.h"], "Header Files");
  CHECK_EQ(byName["a.cpp"], "Generated");
  CHECK_EQ(byName["common.h"], "");  // listed in the project, not in the filters file: at the project root
  CHECK(byName.count("skip.txt") == 0);
  const ProjectContents lib = WorkspaceExplorer::readProject(dir / "Lib" / "Lib.vcxproj");
  CHECK(lib.loaded && !lib.hasFiltersFile);
  CHECK_EQ(lib.files.size(), size_t(5));
  const ProjectContents missing = WorkspaceExplorer::readProject(dir / "Nope" / "Nope.vcxproj");
  CHECK(!missing.loaded && missing.files.empty());
  std::error_code ec;
  fs::remove_all(dir, ec);
}

TEST(w8f_solution_tree_groups_by_filters_else_by_folder) {
  const fs::path dir = copyFixture("Explorer", "sln");
  const TreeModel m = WorkspaceExplorer::buildSolutionTree(dir / "Explorer.sln");
  CHECK_EQ(outline(m), kSolutionOutline);
  CHECK(m.node(0).kind == TreeKind::Solution);
  const int main = m.findPath((dir / "App/src/main.cpp").generic_string());
  REQUIRE(main >= 0);
  CHECK(m.node(main).kind == TreeKind::File);
  CHECK_EQ(m.keyOf(main), std::string("\x1f") + "App\x1f" + "Source Files\x1f" + "main.cpp");
  // The shared header is listed by both projects; the first one wins the path index.
  CHECK(m.findPath((dir / "shared/common.h").generic_string()) >= 0);
  std::error_code ec;
  fs::remove_all(dir, ec);
}

TEST(w8f_folder_tree_uses_the_scan_so_ignore_rules_apply) {
  const fs::path dir = copyFixture("Explorer", "folder");
  fs::remove(dir / "Explorer.sln");
  put(dir / "build" / "out.cpp");
  put(dir / ".git" / "config.txt");
  put(dir / "node_modules" / "x" / "y.txt");
  Workspace ws(dir);
  ws.scan();
  const TreeModel m = WorkspaceExplorer::buildTree(ws);
  const std::string o = outline(m);
  CHECK(o.find("build") == std::string::npos);
  CHECK(o.find(".git") == std::string::npos);
  CHECK(o.find("node_modules") == std::string::npos);
  CHECK(m.node(0).kind == TreeKind::Root);
  // Folders first, then files, each case-insensitive; the root lists README.md after its folders.
  const auto& top = m.node(0).kids;
  REQUIRE(top.size() >= size_t(4));
  CHECK_EQ(m.node(top[0]).name, "App");
  CHECK_EQ(m.node(top[1]).name, "Lib");
  CHECK_EQ(m.node(top[2]).name, "shared");
  CHECK_EQ(m.node(top.back()).name, "README.md");
  CHECK(m.findPath((dir / "App/src/util/helpers.cpp").generic_string()) >= 0);
  CHECK(m.findPath((dir / "build/out.cpp").generic_string()) < 0);
  std::error_code ec;
  fs::remove_all(dir, ec);
}

TEST(w8f_a_solution_without_projects_falls_back_to_the_folder_tree) {
  const fs::path dir = copyFixture("Explorer", "empty");
  std::ofstream(dir / "Explorer.sln", std::ios::binary | std::ios::trunc)
      << "Microsoft Visual Studio Solution File, Format Version 12.00\nGlobal\nEndGlobal\n";
  Workspace ws(dir);
  ws.scan();
  CHECK(WorkspaceExplorer::buildTree(ws).node(0).kind == TreeKind::Root);
  std::error_code ec;
  fs::remove_all(dir, ec);
}

// ---- TreeView on its own ----

namespace {
TreeModel sampleModel() {
  TreeModel m;
  const int root = m.add(-1, "root", TreeKind::Root);
  const int src = m.add(root, "src", TreeKind::Folder);
  const int util = m.add(src, "util", TreeKind::Folder);
  m.add(util, "helpers.cpp", TreeKind::File, "/w/src/util/helpers.cpp");
  m.add(src, "main.cpp", TreeKind::File, "/w/src/main.cpp");
  m.add(src, "Main.h", TreeKind::File, "/w/src/Main.h");
  const int docs = m.add(root, "docs", TreeKind::Folder);
  m.add(docs, "guide.md", TreeKind::File, "/w/docs/guide.md");
  m.add(root, "README.md", TreeKind::File, "/w/README.md");
  m.sortAll();
  m.indexPaths();
  return m;
}
std::vector<std::string> names(const TreeView& v) {
  std::vector<std::string> out;
  for (const auto& r : v.rows()) out.push_back(v.model().node(r.node).name);
  return out;
}
}  // namespace

TEST(w8f_treeview_keys_expand_collapse_select_and_activate) {
  TreeView v;
  v.layout({0, 0, 300, 400});
  v.setModel(sampleModel());
  CHECK((names(v) == std::vector<std::string>{"root", "docs", "src", "README.md"}));  // root open, folders first
  CHECK_EQ(v.selectedNode(), -1);
  v.onEvent(keyEv(Key::Down));
  CHECK_EQ(v.selectedRow(), 0);
  v.onEvent(keyEv(Key::Down));
  v.onEvent(keyEv(Key::Down));  // src
  CHECK_EQ(v.model().node(v.selectedNode()).name, "src");
  v.onEvent(keyEv(Key::Right));  // expand
  CHECK_EQ(v.rows().size(), size_t(7));
  v.onEvent(keyEv(Key::Right));  // already open: first child
  CHECK_EQ(v.model().node(v.selectedNode()).name, "util");
  v.onEvent(keyEv(Key::Left));  // util is closed: go to the parent
  CHECK_EQ(v.model().node(v.selectedNode()).name, "src");
  v.onEvent(keyEv(Key::Left));  // collapse
  CHECK_EQ(v.rows().size(), size_t(4));
  v.onEvent(keyEv(Key::Enter));  // Enter on a folder toggles it
  CHECK_EQ(v.rows().size(), size_t(7));
  int activated = -1;
  v.onActivate = [&](int n) { activated = n; };
  v.onEvent(keyEv(Key::Down));
  v.onEvent(keyEv(Key::Down));  // main.cpp (case-insensitive order: util, main.cpp, Main.h)
  CHECK_EQ(v.model().node(v.selectedNode()).name, "main.cpp");
  v.onEvent(keyEv(Key::Enter));
  CHECK_EQ(activated, v.selectedNode());
  v.onEvent(keyEv(Key::End));
  CHECK_EQ(v.model().node(v.selectedNode()).name, "README.md");
  v.onEvent(keyEv(Key::Home));
  CHECK_EQ(v.selectedNode(), 0);
  v.onEvent(keyEv(Key::Up));  // stays on the first row
  CHECK_EQ(v.selectedNode(), 0);
}

TEST(w8f_treeview_filter_is_fuzzy_keeps_ancestors_and_escape_clears) {
  TreeView v;
  v.layout({0, 0, 300, 400});
  v.setModel(sampleModel());
  v.onEvent(textEv("hlp"));  // fuzzy: helpers.cpp
  CHECK((names(v) == std::vector<std::string>{"root", "src", "util", "helpers.cpp"}));
  CHECK_EQ(v.model().node(v.selectedNode()).name, "helpers.cpp");  // lands on the first matching file
  v.onEvent(keyEv(Key::Backspace));
  v.onEvent(keyEv(Key::Backspace));
  v.onEvent(keyEv(Key::Backspace));
  CHECK(v.filter().empty());
  v.onEvent(textEv("md"));
  CHECK(has(names(v), "guide.md") && has(names(v), "README.md") && !has(names(v), "helpers.cpp"));
  v.onEvent(textEv(" gui"));  // every space-separated token must match
  CHECK((names(v) == std::vector<std::string>{"root", "docs", "guide.md"}));
  int closed = 0;
  v.onClose = [&] { ++closed; };
  v.onEvent(keyEv(Key::Escape));
  CHECK(v.filter().empty());
  CHECK_EQ(closed, 0);
  CHECK_EQ(v.model().node(v.selectedNode()).name, "guide.md");  // the selection survives the filter
  v.onEvent(keyEv(Key::Escape));
  CHECK_EQ(closed, 1);
  v.onEvent(textEv("zzz"));
  CHECK(v.rows().empty());
}

TEST(w8f_treeview_mouse_click_selects_arrow_toggles_double_click_activates) {
  TreeView v;
  v.layout({0, 0, 300, 400});
  v.setModel(sampleModel());
  v.onEvent(mouseEv(InputType::MouseDown, 100, v.rowRect(1).y + 2));  // docs
  CHECK_EQ(v.model().node(v.selectedNode()).name, "docs");
  CHECK_EQ(v.rows().size(), size_t(4));  // a click on the name does not expand
  const RectF a = v.arrowRect(1);
  v.onEvent(mouseEv(InputType::MouseDown, a.x + a.w / 2, a.y + a.h / 2));
  CHECK_EQ(v.rows().size(), size_t(5));                                  // the arrow does
  v.onEvent(mouseEv(InputType::MouseDown, 120, v.rowRect(4).y + 2, 2));  // double click README.md
  int activated = -1;
  v.onActivate = [&](int n) { activated = n; };
  v.onEvent(mouseEv(InputType::MouseDown, 120, v.rowRect(4).y + 2, 2));
  CHECK_EQ(v.model().node(activated).name, "README.md");
  v.onEvent(mouseEv(InputType::MouseDown, 100, v.rowRect(2).y + 2));  // guide.md inside docs
  CHECK_EQ(v.model().node(v.selectedNode()).name, "guide.md");
}

TEST(w8f_treeview_reveal_expands_ancestors_and_scrolls_into_view) {
  TreeModel m;
  const int root = m.add(-1, "root", TreeKind::Root);
  const int d = m.add(root, "d", TreeKind::Folder);
  for (int i = 0; i < 200; ++i)
    m.add(d, "f" + std::to_string(1000 + i) + ".cpp", TreeKind::File, "/w/d/f" + std::to_string(1000 + i));
  m.sortAll();
  m.indexPaths();
  TreeView v;
  v.layout({0, 0, 300, 200});
  v.setModel(std::move(m));
  CHECK_EQ(v.rows().size(), size_t(2));
  const int target = v.model().findPath("/w/d/f1150");
  REQUIRE(target >= 0);
  CHECK(v.reveal(target));
  CHECK(v.isExpanded(d));
  CHECK_EQ(v.selectedNode(), target);
  CHECK(v.shown(target));
  CHECK(v.scrollY() > 0);
  const RectF r = v.rowRect(v.selectedRow());
  CHECK(r.y >= v.listRect().y && r.y + r.h <= v.listRect().y + v.listRect().h + 1);
  // A filter that hides the node is dropped by reveal.
  v.onEvent(textEv("zzz"));
  CHECK(v.reveal(target));
  CHECK(v.filter().empty());
}

TEST(w8f_treeview_model_swap_keeps_expansion_and_selection_by_name) {
  TreeView v;
  v.layout({0, 0, 300, 400});
  v.setModel(sampleModel());
  const int src = v.model().node(0).kids[1];
  v.setExpanded(src, true);
  v.select(v.model().findPath("/w/src/main.cpp"));
  v.setModel(sampleModel());  // a rebuilt model (ids differ in general)
  CHECK(has(names(v), "main.cpp"));
  CHECK_EQ(v.model().node(v.selectedNode()).name, "main.cpp");
  CHECK(!has(names(v), "guide.md"));  // docs was closed before and still is
}

// ---- the commands through their 10x keys ----

TEST(w8f_toggle_workspace_explorer_opens_focuses_and_closes) {
  Env env("Explorer");
  CHECK(!env.bench->sideVisible());
  CHECK(!env.bench->sideRect().w);
  env.key(kKeyE, kCS);
  CHECK(env.bench->sideVisible());
  CHECK(env.ex().visible());
  CHECK(env.ex().solutionMode());
  CHECK(env.tree().hasFocus());
  CHECK(env.bench->sideRect().w > 100 && env.bench->sideRect().x == 0);
  // The editor area starts to the right of the dock.
  for (Panel* p : env.bench->panels()) CHECK(p->bounds().x >= env.bench->sideRect().w);
  CHECK_EQ(env.tree().rows().size(), size_t(4));  // solution + App + Libraries + Solution Items (root open only)
  env.key(kKeyE, kCS);
  CHECK(!env.bench->sideVisible());
  CHECK(!env.tree().hasFocus());
  for (Panel* p : env.bench->panels()) CHECK(p->bounds().x == 0);
}

TEST(w8f_enter_and_double_click_open_through_goto_location_and_back_returns) {
  Env env("Explorer");
  env.bench->openFile(env.dir / "README.md");
  env.bench->sync();
  const fs::path before = env.current();
  env.key(kKeyE, kCS);
  env.tree().expandAll(true);
  env.frame();
  const int row = env.rowNamed("main.cpp");
  REQUIRE(row >= 0);
  env.clickRow(row);  // single click only selects
  CHECK_EQ(env.current(), before);
  CHECK_EQ(env.selectedName(), "main.cpp");
  env.key(Key::Enter);
  CHECK_EQ(env.current().filename().string(), "main.cpp");
  CHECK(env.ed->navigation().canGoBack());
  CHECK(!env.tree().hasFocus());  // the keys went back to the editor
  // Double click on another file; the panel stayed open.
  CHECK(env.bench->sideVisible());
  env.clickRow(env.rowNamed("lib.cpp"), 2);
  CHECK_EQ(env.current().filename().string(), "lib.cpp");
  CHECK(env.ed->back());
  CHECK_EQ(env.current().filename().string(), "main.cpp");
  // A tab was opened for each.
  bool found = false;
  for (Panel* p : env.bench->panels())
    for (const auto& t : p->tabs().tabs()) found |= t.title == "lib.cpp";
  CHECK(found);
}

TEST(w8f_opening_a_file_keeps_the_caret_of_an_already_open_one) {
  Env env("Explorer");
  const fs::path f = env.dir / "App/src/main.cpp";
  env.bench->openFile(f);
  env.bench->sync();
  env.ed->moveCursor({0, 7});
  env.bench->sync();
  env.key(kKeyE, kCS);
  env.key(kKeyT, ModCtrl);
  env.key(Key::Enter);
  CHECK_EQ(env.current(), f);
  CHECK_EQ(env.ed->cursor().pos.column, 7);
}

TEST(w8f_show_in_workspace_tree_reveals_selects_and_opens_the_panel) {
  Env env("Explorer");
  env.bench->openFile(env.dir / "Lib/detail/impl.cpp");
  env.bench->sync();
  CHECK(!env.bench->sideVisible());
  env.key(kKeyT, ModCtrl);
  CHECK(env.bench->sideVisible());
  CHECK(env.tree().hasFocus());
  CHECK_EQ(env.selectedName(), "impl.cpp");
  CHECK(env.tree().shown(env.tree().selectedNode()));
  CHECK(env.rowNamed("detail") >= 0 && env.rowNamed("Lib") >= 0);  // ancestors opened
  // Another file, tree already open.
  env.bench->openFile(env.dir / "App/src/util/helpers.cpp");
  env.bench->sync();
  env.key(kKeyT, ModCtrl);
  CHECK_EQ(env.selectedName(), "helpers.cpp");
  CHECK(env.rowNamed("Util") >= 0 && env.rowNamed("Source Files") >= 0);
  CHECK(env.bench->sideVisible());  // plain ShowInWorkspaceTree never closes
  env.key(kKeyT, ModCtrl);
  CHECK(env.bench->sideVisible());
}

TEST(w8f_show_in_workspace_tree_with_toggle_closes_when_already_shown) {
  Env env("Explorer");
  env.bench->openFile(env.dir / "App/src/main.cpp");
  env.bench->sync();
  env.app->runCommand("View.ShowInWorkspaceTreeWithToggle");
  CHECK(env.bench->sideVisible());
  CHECK_EQ(env.selectedName(), "main.cpp");
  env.app->runCommand("View.ShowInWorkspaceTreeWithToggle");  // already shown: closes
  CHECK(!env.bench->sideVisible());
  // Not shown (selection elsewhere) -> reveals again instead of closing.
  env.app->runCommand("View.ShowInWorkspaceTreeWithToggle");
  CHECK(env.bench->sideVisible());
  env.tree().select(env.nodeOf(env.dir / "README.md"));
  env.app->runCommand("View.ShowInWorkspaceTreeWithToggle");
  CHECK(env.bench->sideVisible());
  CHECK_EQ(env.selectedName(), "main.cpp");
}

TEST(w8f_show_in_workspace_tree_without_a_tree_node_says_so) {
  Env env("Explorer");
  put(env.dir / "orphan.cpp");
  env.bench->openFile(env.dir / "orphan.cpp");  // on disk, in no project
  env.bench->sync();
  env.key(kKeyT, ModCtrl);
  CHECK(env.bench->sideVisible());
  CHECK(env.status("message").find("orphan.cpp is not in the workspace tree") != std::string::npos);
}

TEST(w8f_typing_in_the_tree_filters_and_enter_opens_the_match) {
  Env env("Explorer");
  env.key(kKeyE, kCS);
  env.type("impl");
  CHECK_EQ(env.tree().filter(), "impl");
  CHECK(has(env.shownNames(), "impl.cpp") && !has(env.shownNames(), "main.cpp"));
  CHECK_EQ(env.selectedName(), "impl.cpp");
  env.key(Key::Enter);
  CHECK_EQ(env.current().filename().string(), "impl.cpp");
  CHECK(env.bench->sideVisible());  // the panel stays open and keeps its filter
  CHECK_EQ(env.tree().filter(), "impl");
  env.tree().setFocus();
  env.key(Key::Escape);
  CHECK(env.tree().filter().empty());
}

TEST(w8f_a_plain_folder_workspace_shows_the_folder_tree) {
  Env env;
  env.key(kKeyE, kCS);
  CHECK(!env.ex().solutionMode());
  env.tree().expandAll(true);
  env.frame();
  const auto names = env.shownNames();
  CHECK(has(names, "docs") && has(names, "guide.md") && has(names, "main.cpp"));

  CHECK(!has(names, "build") && !has(names, ".git"));
}

// ---- refresh ----

TEST(w8f_file_list_changes_refresh_the_tree_debounced) {
  Env env;
  env.key(kKeyE, kCS);
  env.tree().expandAll(true);
  const int before = static_cast<int>(env.tree().rows().size());
  put(env.dir / "docs" / "new_file.md", "# n\n");
  CHECK(env.ed->workspace().noteFileAdded(env.dir / "docs" / "new_file.md"));
  env.now += 400;
  env.frame();  // sees the change, the rebuild waits for the debounce
  CHECK(!has(env.shownNames(), "new_file.md"));
  env.now += WorkspaceExplorer::kDebounceMs + 50;
  env.frame();
  CHECK(has(env.shownNames(), "new_file.md"));
  CHECK_EQ(static_cast<int>(env.tree().rows().size()), before + 1);
  // Removal.
  env.ed->workspace().noteFileRemoved(env.dir / "docs" / "new_file.md");
  env.now += 400;
  env.frame();
  env.now += WorkspaceExplorer::kDebounceMs + 50;
  env.frame();
  CHECK(!has(env.shownNames(), "new_file.md"));
}

TEST(w8f_expansion_and_selection_survive_a_refresh) {
  Env env("Explorer");
  env.key(kKeyE, kCS);
  env.tree().reveal(env.nodeOf(env.dir / "App/src/main.cpp"));
  env.frame();
  CHECK_EQ(env.selectedName(), "main.cpp");
  std::ofstream(env.dir / "Explorer.sln", std::ios::binary | std::ios::app) << "\n# touched\n";
  // The stamp covers size and time: the append changes the size at once.
  env.now += 400;
  env.frame();
  env.now += 400;
  env.frame();
  CHECK_EQ(env.selectedName(), "main.cpp");
  CHECK(env.rowNamed("Source Files") >= 0);
}

TEST(w8f_a_changed_project_or_filters_file_rebuilds_the_solution_tree) {
  Env env("Explorer");
  env.key(kKeyE, kCS);
  env.tree().expandAll(true);
  CHECK(env.rowNamed("Generated") >= 0);
  CHECK(env.rowNamed("extra.cpp") < 0);
  put(env.dir / "App" / "src" / "extra.cpp");
  std::ofstream(env.dir / "App" / "App.vcxproj", std::ios::binary | std::ios::trunc)
      << "<Project xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\"><ItemGroup>"
         "<ClCompile Include=\"src\\main.cpp\" /><ClCompile Include=\"src\\extra.cpp\" /></ItemGroup></Project>";
  std::error_code ec;
  fs::remove(env.dir / "App" / "App.vcxproj.filters", ec);  // no filters any more: grouped by folder
  env.now += 400;
  env.frame();
  env.now += WorkspaceExplorer::kDebounceMs + 50;
  env.frame();
  env.tree().expandAll(true);
  CHECK(env.rowNamed("extra.cpp") >= 0);
  CHECK(env.rowNamed("Generated") < 0);
  CHECK(env.rowNamed("src") >= 0);
}

TEST(w8f_nothing_is_built_or_polled_while_the_panel_is_closed) {
  Env env("Explorer");
  CHECK(env.tree().model().empty());
  CHECK_EQ(env.ex().tick(env.now + 5000), -1);
  CHECK(env.tree().model().empty());
}

// ---- settings ----

TEST(w8f_track_active_file_setting_follows_the_current_document) {
  Env env("Explorer", "explorer.trackActiveFile = true\n");
  env.key(kKeyE, kCS);
  env.bench->openFile(env.dir / "Lib/lib.cpp");
  env.bench->sync();
  env.now += 400;
  env.frame();
  CHECK_EQ(env.selectedName(), "lib.cpp");
  env.bench->openFile(env.dir / "App/src/main.cpp");
  env.bench->sync();
  env.now += 400;
  env.frame();
  CHECK_EQ(env.selectedName(), "main.cpp");
}

TEST(w8f_track_active_file_is_off_by_default) {
  Env env("Explorer");
  env.key(kKeyE, kCS);
  env.bench->openFile(env.dir / "Lib/lib.cpp");
  env.bench->sync();
  env.now += 400;
  env.frame();
  CHECK_EQ(env.tree().selectedNode(), -1);
}

// ---- layout.json ----

TEST(w8f_side_dock_is_saved_in_layout_json_and_restored) {
  Env env("Explorer");
  const std::string closed = env.bench->layoutJson();
  CHECK(closed.find("\"left\"") == std::string::npos);  // nothing written until the panel was used
  env.key(kKeyE, kCS);
  env.bench->setSideWidth(0.3f);
  env.frame();
  const std::string json = env.bench->layoutJson();
  CHECK(json.find("\"left\"") != std::string::npos);
  CHECK(json.find("\"Explorer\"") != std::string::npos);
  CHECK(env.bench->saveLayout());
  // A second window on the same workspace restores it open and builds the tree.
  Editor::Options o;
  o.semantic.useClangd = false;
  o.semantic.persistFastIndex = false;
  o.watchForcePolling = true;
  o.watchPollMs = 60000;
  env.app.reset();
  env.bench.reset();
  env.ed.reset();
  env.ed = std::make_unique<Editor>(env.dir, o);
  env.ed->start();
  env.bench = std::make_unique<Workbench>(*env.text, *env.ed);
  REQUIRE(env.bench->restoreLayout());
  env.app = std::make_unique<EditorApp>(*env.bench, *env.ed, env.bench.get());
  env.bench->render(env.nr, 1000, 600);
  CHECK(env.bench->sideVisible());
  CHECK(env.ex().visible());
  CHECK(!env.tree().model().empty());
  CHECK(std::abs(env.bench->sideWidth() - 0.3f) < 0.02f);
  // A layout from before the explorer has no "left" key: the dock stays hidden.
  env.bench->restoreLayoutFromJson(
      "{\"version\":1,\"activePanel\":0,\"root\":{\"type\":\"panel\",\"tabs\":[],\"active\":-1}}");
  CHECK(!env.bench->sideVisible());
}

TEST(w8f_focus_mode_hides_the_side_dock) {
  Env env("Explorer");
  env.key(kKeyE, kCS);
  CHECK(env.bench->sideVisible());
  env.bench->setFocusMode(true);
  env.frame();
  CHECK(!env.bench->sideVisible());
  env.bench->setFocusMode(false);
  env.frame();
  CHECK(env.bench->sideVisible());
}

// ---- version control marks ----

TEST(w8f_modified_files_get_the_tab_marker_state) {
  SKIP_WITHOUT_GIT();
  tfvcs::TempRepo repo;
  repo.write("src/a.cpp", "int a;\n");
  repo.write("src/b.cpp", "int b;\n");
  repo.commitAll("base");
  repo.write("src/a.cpp", "int a2;\n");
  Editor::Options o;
  o.semantic.useClangd = false;
  o.semantic.persistFastIndex = false;
  o.watchForcePolling = true;
  o.watchPollMs = 60000;
  o.vcs.debounceMs = 20;
  Editor ed(repo.dir, o);
  ed.start();
  gfx::FontStack stack;
  stack.add(std::make_shared<tftext::MockFont>(U"abcdefghijklmnopqrstuvwxyz0123456789 ._-:,;/", 8));
  auto tr = gfx::TextRenderer::fromFonts(std::move(stack));
  NullRenderer nr;
  Workbench wb(*tr, ed);
  EditorApp app(wb, ed, &wb);
  wb.openFile(repo.path("src/b.cpp"));  // version control is detected when the first file is opened
  wb.sync();
  int64_t now = 1000;
  auto frame = [&] {
    ed.pump();
    app.tick(now += 10);
    wb.render(nr, 1000, 600);
  };
  app.runCommand("View.ToggleWorkspaceExplorer");
  frame();
  TreeView& tv = *app.explorer()->view();
  tv.expandAll(true);
  frame();
  const int a = tv.model().findPath(repo.path("src/a.cpp").generic_string());
  const int b = tv.model().findPath(repo.path("src/b.cpp").generic_string());
  REQUIRE(a >= 0 && b >= 0);
  bool ok = false;
  for (int i = 0; i < 3000 && !ok; ++i) {
    frame();
    ok = tv.markOf(tv.model().node(a)) == TabStrip::VcsMark::Modified;
    if (!ok) std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  CHECK(ok);
  CHECK(tv.markOf(tv.model().node(b)) == TabStrip::VcsMark::None);
}

// ---- golden ----

namespace {
std::string monoFont() {
  for (const char* p : {"/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
                        "/usr/share/fonts/dejavu/DejaVuSansMono.ttf", "/usr/share/fonts/TTF/DejaVuSansMono.ttf"})
    if (std::ifstream(p).good()) return p;
  return {};
}

void checkGolden(gfx::GlRenderer& r, Workbench& wb, gfx::TextRenderer& tr, const char* name, int w, int h) {
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

TEST(w8f_golden_vs_dark_explorer_open) {
  SKIP_IF_NO_GL(fx);
  const std::string font = monoFont();
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
  const fs::path tmp = uitest::tempRoot() / "tf_w8f_golden";  // the folder name is in the status bar: fixed
  std::error_code rm;
  fs::remove_all(tmp, rm);
  fs::create_directories(tmp);
  fs::copy(fs::path(TENFOLD_SOURCE_DIR) / "tests/fixtures/vs/Explorer", tmp, fs::copy_options::recursive);
  {
    Editor::Options o;
    o.semantic.useClangd = false;
    o.semantic.persistFastIndex = false;
    o.watchForcePolling = true;
    o.watchPollMs = 60000;
    Editor ed(tmp, o);
    Workbench wb(*tr, ed, vs);
    wb.openFile("App/src/main.cpp");
    wb.sync();
    EditorApp app(wb, ed, &wb);
    uitest::wrapOff(wb);
    app.runCommand("View.ShowInWorkspaceTree");
    TreeView& tv = *app.explorer()->view();
    tv.setExpanded(tv.model().node(0).kids[1], true);                           // Libraries
    tv.setExpanded(tv.model().node(tv.model().node(0).kids[1]).kids[0], true);  // Lib
    app.tick(1000);
    NullRenderer nr;
    wb.render(nr, 900, 520);
    tr->releaseTexture(nr);
    checkGolden(*fx->renderer, wb, *tr, "w8f_explorer_open.png", 900, 520);
  }
  fs::remove_all(tmp, rm);
}

// ---- 50k files ----

TEST(w8f_perf_50k_file_tree_open_scroll_filter) {
  using clk = std::chrono::steady_clock;
  auto ms = [](clk::time_point a, clk::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
  };
  // A Godot-sized shape: 400 top-level directories x 5 subdirectories x 25 files = 50 000 files.
  const fs::path root = fs::path(uitest::tempRoot()) / "tf_w8f_perf_root";
  std::vector<fs::path> files;
  files.reserve(50000);
  for (int d = 0; d < 400; ++d)
    for (int s = 0; s < 5; ++s)
      for (int f = 0; f < 25; ++f)
        files.push_back(root / ("module_" + std::to_string(d)) / ("sub" + std::to_string(s)) /
                        ("file_" + std::to_string(f) + (f % 3 ? ".cpp" : ".h")));
  std::sort(files.begin(), files.end());

  const auto t0 = clk::now();
  TreeModel m = WorkspaceExplorer::buildFolderTree(root, files);
  const double buildMs = ms(t0, clk::now());
  CHECK_EQ(m.size(), size_t(1 + 400 + 2000 + 50000));

  gfx::FontStack stack;
  stack.add(std::make_shared<tftext::MockFont>(U"abcdefghijklmnopqrstuvwxyz0123456789 ._-:,;/", 8));
  auto tr = gfx::TextRenderer::fromFonts(std::move(stack));
  TreeView v;
  NullRenderer nr;
  // A RootWidget so the view paints with real metrics.
  RootWidget rw(*tr);
  TreeView* tv = static_cast<TreeView*>(rw.addChild(std::make_unique<TreeView>()));
  rw.render(nr, 400, 900);
  const auto t1 = clk::now();
  tv->setModel(std::move(m));
  tv->expandAll(true);  // the worst case: all 52k rows in the list
  rw.render(nr, 400, 900);
  const double openMs = buildMs + ms(t1, clk::now());
  const size_t allRows = tv->rows().size();
  CHECK_EQ(allRows, size_t(1 + 400 + 2000 + 50000));

  std::vector<double> scroll;
  for (int i = 0; i < 300; ++i) {
    const auto s0 = clk::now();
    tv->setScrollY(tv->scrollY() + 3 * tv->rowHeight() * (i % 2 ? 7 : 5));
    rw.render(nr, 400, 900);
    scroll.push_back(ms(s0, clk::now()));
  }
  std::sort(scroll.begin(), scroll.end());
  double sum = 0;
  for (double t : scroll) sum += t;

  std::vector<double> filt;
  for (const char* q : {"m", "mo", "mod", "modu", "module_1", "module_17 sub3", "f_3"}) {
    const auto f0 = clk::now();
    tv->setFilter(q);
    rw.render(nr, 400, 900);
    filt.push_back(ms(f0, clk::now()));
  }
  tv->setFilter({});
  const double filterWorst = *std::max_element(filt.begin(), filt.end());

  const auto r0 = clk::now();
  tv->expandAll(false);
  rw.render(nr, 400, 900);
  const double collapseMs = ms(r0, clk::now());

  std::printf(
      "PERF: workspace explorer, 50k files: build model %.1f ms, open (build + all %zu rows expanded + first paint) "
      "%.1f ms, scroll frame avg %.3f ms / p99 %.3f ms, filter keystroke worst %.1f ms, collapse all %.1f ms "
      "(budgets: open 400 ms, scroll 8 ms, filter 100 ms)\n",
      buildMs, allRows, openMs, sum / scroll.size(), scroll[static_cast<size_t>(scroll.size() * 0.99)], filterWorst,
      collapseMs);
#if defined(NDEBUG) && !defined(__SANITIZE_ADDRESS__)
  PERF_CHECK(openMs < 400.0, "explorer open on 50k files (400 ms)");
  PERF_CHECK(sum / scroll.size() < 8.0, "explorer scroll frame (8 ms)");
  PERF_CHECK(filterWorst < 100.0, "explorer filter keystroke (100 ms)");
#endif
}
