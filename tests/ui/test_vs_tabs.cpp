// VS-TABS: the Visual Studio 2022 tab-well rows of docs/VS_BEHAVIOUR.md section 7 that were implemented: T1 the switch
// panel (Ctrl+Tab: most recently used first, selection on the previous file, releasing Ctrl commits), T2 the preview
// tab, T6 one list dialog for several modified documents, T9 files changed on disk (detect / reload unless unsaved,
// Yes / Yes to All / No / No to All), T11 the pinned boundary and T12 Ctrl+K Ctrl+Z. Synthetic events against a null
// renderer and the mock font.
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <thread>

#include "../text/text_test_util.h"
#include "ui/features/WorkspaceExplorer.h"
#include "ui/gfx/TextRenderer.h"
#include "ui/settings/Settings.h"
#include "ui/shell/EditorApp.h"
#include "ui/widgets/ListPopup.h"
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

InputEvent keyEv(Key k, uint8_t mods = 0, InputType t = InputType::KeyDown) {
  InputEvent e;
  e.type = t;
  e.key = k;
  e.mods = mods;
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
bool contains(const std::string& s, const std::string& what) { return s.find(what) != std::string::npos; }

const std::string kHan = "\xE6\xBC\xA2";   // U+6F22
const std::string kLatin = "int \xF6;\n";  // not UTF-8: Windows-1252

struct Env {
  fs::path dir;
  std::unique_ptr<Editor> ed;
  std::unique_ptr<gfx::TextRenderer> text;
  NullRenderer rec;
  std::unique_ptr<Workbench> bench;
  std::unique_ptr<EditorApp> app;
  int64_t now = 1000;

  explicit Env(bool watch = false) {
    static int n = 0;
    dir = uitest::tempRoot() /
          ("tf_vstabs_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_" +
           std::to_string(n++));
    fs::create_directories(dir);
    for (const char* f : {"a", "b", "c", "d"})
      writeFile(dir / "src" / (std::string(f) + ".cpp"), std::string("int ") + f + "_value;\n");
    gfx::FontStack stack;
    stack.add(std::make_shared<tftext::MockFont>(
        U"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ._-:,;/()[]{}<>+*&#?=!\"'\\|漢", 8));
    text = gfx::TextRenderer::fromFonts(std::move(stack));
    Editor::Options o;
    o.semantic.useClangd = false;
    o.semantic.persistFastIndex = false;
    if (watch) {
      o.watchForcePolling = true;
      o.watchPollMs = 40;
    }
    ed = std::make_unique<Editor>(dir, o);
    ed->start();
    bench = std::make_unique<Workbench>(*text, *ed);
    app = std::make_unique<EditorApp>(*bench, *ed, bench.get());
    frame();
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
  void ctrlUp() {  // the Control key is released
    now += 10;
    app->handleEvent(keyEv(Key::Unknown, 0, InputType::KeyUp), now);
    frame();
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
    if (bench->openFile(dir / rel) < 0) return nullptr;
    bench->sync();
    frame();
    return ed->currentDocument();
  }
  void edit(const std::string& t = "// edit\n") {
    ed->moveCursor({0, 0});
    ed->insertText(t);
    bench->sync();
    frame();
  }
  int tabOf(const std::string& rel) const {
    for (Panel* p : bench->panels())
      for (const auto& t : p->tabs().tabs())
        if (bench->tabPath(t.id) == dir / rel) return t.id;
    return -1;
  }
  std::vector<std::string> titles(Panel* p = nullptr) const {
    std::vector<std::string> out;
    for (const auto& t : (p ? p : bench->activePanel())->tabs().tabs()) out.push_back(t.title);
    return out;
  }
  std::string active() const {
    Panel* p = bench->activePanel();
    return bench->tabPath(p->activeId()).filename().string();
  }
  const TabStrip::Tab* tab(int id) const {
    for (Panel* p : bench->panels())
      for (const auto& t : p->tabs().tabs())
        if (t.id == id) return &t;
    return nullptr;
  }
  std::vector<std::string> rows(ListPopup* p) const {
    std::vector<std::string> out;
    if (p)
      for (const auto& r : p->rows()) out.push_back(r.primary);
    return out;
  }
  // The workspace explorer, folders expanded.
  TreeView& tree() {
    app->explorer()->show(false);
    frame();
    app->explorer()->view()->expandAll(true);
    frame();
    return *app->explorer()->view();
  }
  int rowNamed(TreeView& t, const std::string& name) {
    for (size_t i = 0; i < t.rows().size(); ++i)
      if (t.model().node(t.rows()[i].node).name == name) return static_cast<int>(i);
    return -1;
  }
  void clickRow(TreeView& t, const std::string& name, int clicks = 1) {
    const int row = rowNamed(t, name);
    REQUIRE(row >= 0);
    const RectF r = t.rowRect(row);
    now += 10;
    app->handleEvent(mouseEv(InputType::MouseDown, r.x + r.w * 0.6f, r.y + r.h / 2, clicks), now);
    app->handleEvent(mouseEv(InputType::MouseUp, r.x + r.w * 0.6f, r.y + r.h / 2, clicks), now);
    frame();
  }
  void doubleClickTab(int id) {
    const RectF r = bench->panelOf(id)->tabs().tabRect(id);
    now += 10;
    app->handleEvent(mouseEv(InputType::MouseDown, r.x + r.w / 3, r.y + r.h / 2, 2), now);
    app->handleEvent(mouseEv(InputType::MouseUp, r.x + r.w / 3, r.y + r.h / 2, 2), now);
    frame();
  }
};

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// T1: the switch panel
// ---------------------------------------------------------------------------------------------------------------

namespace {
ListPopup* switchPopup(Env& env) { return env.app->switchPanel() ? env.app->switchPanel()->popup() : nullptr; }
}  // namespace

TEST(vstabs_t1_ctrl_tab_lists_files_most_recently_used_first_and_releasing_ctrl_commits) {
  Env env;
  env.open("src/a.cpp");
  env.open("src/b.cpp");
  env.open("src/c.cpp");
  env.open("src/a.cpp");  // MRU: a c b
  CHECK_EQ(env.active(), std::string("a.cpp"));
  const size_t history = env.ed->navigation().entries().size();
  env.key(Key::Tab, ModCtrl);
  ListPopup* p = switchPopup(env);
  REQUIRE(p != nullptr);
  CHECK_EQ(env.rows(p).size(), size_t(3));
  CHECK_EQ(env.rows(p)[0], std::string("a.cpp"));
  CHECK_EQ(env.rows(p)[1], std::string("c.cpp"));
  CHECK_EQ(env.rows(p)[2], std::string("b.cpp"));
  CHECK_EQ(p->selected(), 1);                    // the previous file, not the current one
  CHECK_EQ(env.active(), std::string("a.cpp"));  // nothing is switched while the panel is up
  env.key(Key::Tab, ModCtrl);                    // Tab moves down
  CHECK_EQ(switchPopup(env)->selected(), 2);
  env.key(Key::Tab, ModCtrl);  // wraps to the first row
  CHECK_EQ(switchPopup(env)->selected(), 0);
  env.key(Key::Tab, ModCtrl | ModShift);  // Shift+Tab moves up (wraps)
  CHECK_EQ(switchPopup(env)->selected(), 2);
  env.key(Key::Tab, ModCtrl | ModShift);
  CHECK_EQ(switchPopup(env)->selected(), 1);
  env.ctrlUp();  // releasing Ctrl commits
  CHECK(switchPopup(env) == nullptr);
  CHECK_EQ(env.active(), std::string("c.cpp"));
  CHECK_EQ(env.ed->currentDocument()->path().filename().string(), std::string("c.cpp"));
  CHECK_EQ(env.ed->navigation().entries().size(), history);  // a tab switch is not a jump
  // The order follows use: c is now the most recent, then a, then b. A quick Ctrl+Tab goes back to a.
  env.key(Key::Tab, ModCtrl);
  CHECK_EQ(env.rows(switchPopup(env))[1], std::string("a.cpp"));
  env.ctrlUp();
  CHECK_EQ(env.active(), std::string("a.cpp"));
}

TEST(vstabs_t1_ctrl_shift_tab_starts_on_the_oldest_escape_cancels_enter_commits_one_tab_does_not_open) {
  Env env;
  env.open("src/a.cpp");
  env.key(Key::Tab, ModCtrl);
  CHECK(switchPopup(env) == nullptr);  // nothing to switch to
  env.ctrlUp();
  env.open("src/b.cpp");
  env.open("src/c.cpp");  // MRU: c b a
  env.key(Key::Tab, ModCtrl | ModShift);
  REQUIRE(switchPopup(env) != nullptr);
  CHECK_EQ(switchPopup(env)->selected(), 2);  // the oldest
  env.key(Key::Escape, ModCtrl);
  CHECK(switchPopup(env) == nullptr);
  env.ctrlUp();
  CHECK_EQ(env.active(), std::string("c.cpp"));  // cancelled: nothing changed
  env.key(Key::Tab, ModCtrl);
  REQUIRE(switchPopup(env) != nullptr);
  env.key(Key::Down, ModCtrl);  // the arrow keys move as well
  CHECK_EQ(switchPopup(env)->selected(), 2);
  env.key(Key::Enter, ModCtrl);  // Enter commits too
  CHECK(switchPopup(env) == nullptr);
  CHECK_EQ(env.active(), std::string("a.cpp"));
}

// ---------------------------------------------------------------------------------------------------------------
// T2: the preview tab
// ---------------------------------------------------------------------------------------------------------------

TEST(vstabs_t2_single_click_in_the_explorer_opens_the_preview_tab_and_the_next_click_replaces_it) {
  Env env;
  env.open("src/c.cpp");  // a normal tab
  TreeView& t = env.tree();
  env.clickRow(t, "a.cpp");
  const int a = env.tabOf("src/a.cpp");
  REQUIRE(a >= 0);
  CHECK(env.tab(a)->preview);
  CHECK_EQ(env.bench->previewTabId(), a);
  CHECK_EQ(env.active(), std::string("a.cpp"));
  CHECK(env.titles() == (std::vector<std::string>{"c.cpp", "a.cpp"}));  // right end of the well
  CHECK(env.app->explorer()->view()->hasFocus());                       // the keys stay in the tree
  env.clickRow(t, "b.cpp");
  CHECK_EQ(env.tabOf("src/a.cpp"), -1);  // replaced, not added
  CHECK(env.ed->document(env.dir / "src/a.cpp") == nullptr);
  const int b = env.tabOf("src/b.cpp");
  REQUIRE(b >= 0);
  CHECK(env.tab(b)->preview);
  CHECK(env.titles() == (std::vector<std::string>{"c.cpp", "b.cpp"}));
  int previews = 0;
  for (const auto& tb : env.bench->activePanel()->tabs().tabs()) previews += tb.preview;
  CHECK_EQ(previews, 1);
  // A replaced preview is not a "recently closed" file; a normal tab opened later goes left of the preview tab.
  env.open("src/d.cpp");
  CHECK(env.titles() == (std::vector<std::string>{"c.cpp", "d.cpp", "b.cpp"}));
  CHECK(env.tab(env.tabOf("src/b.cpp"))->preview);
  // A file that already has a normal tab is shown as it is, not turned into the preview.
  env.clickRow(t, "c.cpp");
  CHECK(!env.tab(env.tabOf("src/c.cpp"))->preview);
  CHECK_EQ(env.active(), std::string("c.cpp"));
}

TEST(vstabs_t2_the_preview_tab_becomes_normal_on_edit_double_click_item_double_click_and_open) {
  Env env;
  TreeView& t = env.tree();
  // Edit.
  env.clickRow(t, "a.cpp");
  REQUIRE(env.bench->previewTabId() >= 0);
  env.edit();
  CHECK_EQ(env.bench->previewTabId(), -1);
  CHECK(!env.tab(env.tabOf("src/a.cpp"))->preview);
  // A new preview does not replace the (now normal) edited tab.
  env.clickRow(t, "b.cpp");
  CHECK(env.tabOf("src/a.cpp") >= 0);
  CHECK(env.tab(env.tabOf("src/b.cpp"))->preview);
  // Double click on the tab.
  env.doubleClickTab(env.tabOf("src/b.cpp"));
  CHECK_EQ(env.bench->previewTabId(), -1);
  CHECK(!env.tab(env.tabOf("src/b.cpp"))->preview);
  // Double click on the explorer item.
  env.clickRow(t, "c.cpp");
  CHECK(env.tab(env.tabOf("src/c.cpp"))->preview);
  env.clickRow(t, "c.cpp", 2);
  CHECK_EQ(env.bench->previewTabId(), -1);
  CHECK(!env.tab(env.tabOf("src/c.cpp"))->preview);
  // The explorer's Open (context menu item) on a previewed file.
  env.clickRow(t, "d.cpp");
  CHECK(env.tab(env.tabOf("src/d.cpp"))->preview);
  t.select(t.model().findPath((env.dir / "src/d.cpp").generic_string()));
  REQUIRE(t.onActivate);
  t.onActivate(t.selectedNode());  // what "Open" and Enter call
  CHECK_EQ(env.bench->previewTabId(), -1);
  CHECK(!env.tab(env.tabOf("src/d.cpp"))->preview);
  CHECK(env.titles() == (std::vector<std::string>{"a.cpp", "b.cpp", "c.cpp", "d.cpp"}));
}

TEST(vstabs_t2_a_preview_tab_cannot_be_pinned_and_a_file_opened_by_name_keeps_the_tab) {
  Env env;
  env.open("src/a.cpp");
  TreeView& t = env.tree();
  env.clickRow(t, "b.cpp");
  const int b = env.tabOf("src/b.cpp");
  REQUIRE(env.tab(b)->preview);
  env.bench->setPinned(b, true);
  CHECK(!env.tab(b)->pinned);
  CHECK(env.tab(b)->preview);
  env.run("Workbench.PinTab");  // the active tab is the preview tab
  CHECK(!env.tab(b)->pinned);
  CHECK(env.titles() == (std::vector<std::string>{"a.cpp", "b.cpp"}));
  // The preview tab cannot be reordered either.
  env.bench->activePanel()->tabs().moveTab(b, 0);
  CHECK(env.titles() == (std::vector<std::string>{"a.cpp", "b.cpp"}));
  // Pinning a normal tab puts it left of everything.
  env.bench->setPinned(env.tabOf("src/a.cpp"), true);
  CHECK(env.tab(env.tabOf("src/a.cpp"))->pinned);
  // Opening the previewed file by name (File.OpenFile) makes it a normal tab.
  env.bench->openFile(env.dir / "src/b.cpp");
  CHECK_EQ(env.bench->previewTabId(), -1);
  CHECK(!env.tab(b)->preview);
}

TEST(vstabs_t2_tabs_preview_tab_false_turns_it_off_and_promotes_the_current_preview) {
  Env env;
  TreeView& t = env.tree();
  env.clickRow(t, "a.cpp");
  REQUIRE(env.bench->previewTabId() >= 0);
  env.settings("tabs.previewTab = false\n");
  CHECK_EQ(env.bench->previewTabId(), -1);  // the open preview is kept as a normal tab
  CHECK(env.tabOf("src/a.cpp") >= 0);
  env.clickRow(t, "b.cpp");
  CHECK_EQ(env.tabOf("src/b.cpp"), -1);  // a single click only selects, as before
  env.clickRow(t, "b.cpp", 2);           // double click opens
  CHECK(env.tabOf("src/b.cpp") >= 0);
  CHECK(!env.tab(env.tabOf("src/b.cpp"))->preview);
  env.settings("tabs.previewTab = true\n");
  env.clickRow(t, "c.cpp");
  CHECK(env.tab(env.tabOf("src/c.cpp"))->preview);
}

TEST(vstabs_t2_find_results_open_the_preview_tab_on_click_not_on_enter_and_obey_the_setting) {
  Env env;
  env.open("src/a.cpp");
  // Find All References style list (NavResultPicker): a click previews, Enter opens a normal tab.
  auto loc = [&](const char* f, int line) {
    SymbolLocation s;
    s.loc.file = env.dir / f;
    s.loc.pos = {line, 4};
    s.name = "value";
    s.kind = "variable";
    return s;
  };
  env.app->navResults().show("Find All References", Confidence::Authoritative,
                             {loc("src/b.cpp", 0), loc("src/c.cpp", 0)}, "", 1);
  env.frame();
  ListPopup* p = env.app->navResults().popup();
  REQUIRE(p != nullptr);
  const RectF r0 = p->rowRect(0);
  env.now += 10;
  env.app->handleEvent(mouseEv(InputType::MouseDown, r0.x + 20, r0.y + r0.h / 2), env.now);
  env.frame();
  REQUIRE(env.tabOf("src/b.cpp") >= 0);
  CHECK(env.tab(env.tabOf("src/b.cpp"))->preview);
  CHECK_EQ(env.active(), std::string("b.cpp"));
  env.app->navResults().show("Find All References", Confidence::Authoritative,
                             {loc("src/b.cpp", 0), loc("src/c.cpp", 0)}, "", 1);
  env.frame();
  env.key(Key::Down);
  env.key(Key::Enter);
  REQUIRE(env.tabOf("src/c.cpp") >= 0);
  CHECK(!env.tab(env.tabOf("src/c.cpp"))->preview);
  CHECK(env.tab(env.tabOf("src/b.cpp"))->preview);  // untouched
  // tabs.previewFromFindResults = false: a click opens a normal tab.
  env.settings("tabs.previewFromFindResults = false\n");
  env.app->navResults().show("Find All References", Confidence::Authoritative, {loc("src/d.cpp", 0)}, "", 1);
  env.frame();
  p = env.app->navResults().popup();
  REQUIRE(p != nullptr);
  const RectF r1 = p->rowRect(0);
  env.now += 10;
  env.app->handleEvent(mouseEv(InputType::MouseDown, r1.x + 20, r1.y + r1.h / 2), env.now);
  env.frame();
  REQUIRE(env.tabOf("src/d.cpp") >= 0);
  CHECK(!env.tab(env.tabOf("src/d.cpp"))->preview);
  // Find in Files: the same rule for a click on a result row.
  env.settings("tabs.previewFromFindResults = true\n");
  env.run("Search.FindInWorkspace");
  env.now += 10;
  InputEvent te;
  te.type = InputType::Text;
  te.text = "a_value";
  env.app->handleEvent(te, env.now);
  for (int i = 0; i < 300 && (env.app->findInFiles().running() || env.app->findInFiles().matches().empty()); ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    env.now += 20;
    env.frame();
  }
  REQUIRE(!env.app->findInFiles().matches().empty());
  // Close a.cpp so the result opens a file without a tab.
  env.bench->closeTab(env.tabOf("src/a.cpp"));
  env.frame();
  ListPopup* fp = env.app->findInFiles().popup();
  REQUIRE(fp != nullptr);
  int row = -1;
  for (size_t i = 0; i < fp->rows().size(); ++i)
    if (contains(fp->rows()[i].primary, "a.cpp") && contains(fp->rows()[i].primary, ":")) row = static_cast<int>(i);
  if (row < 0) row = 1;  // the first match row (row 0 is the file header)
  const RectF rr = fp->rowRect(row);
  env.now += 10;
  env.app->handleEvent(mouseEv(InputType::MouseDown, rr.x + 20, rr.y + rr.h / 2), env.now);
  env.frame();
  REQUIRE(env.tabOf("src/a.cpp") >= 0);
  CHECK(env.tab(env.tabOf("src/a.cpp"))->preview);
  CHECK_EQ(env.tabOf("src/b.cpp"), -1);  // the older preview was replaced
}

// ---------------------------------------------------------------------------------------------------------------
// T6: one list dialog for several modified documents
// ---------------------------------------------------------------------------------------------------------------

namespace {
// Three open files: a and b modified, c clean.
void dirtyAB(Env& env) {
  env.open("src/a.cpp");
  env.edit("// one\n");
  env.open("src/b.cpp");
  env.edit("// two\n");
  env.open("src/c.cpp");
}
}  // namespace

TEST(vstabs_t6_close_all_with_several_modified_documents_asks_once_with_a_checked_list) {
  Env env;
  dirtyAB(env);
  env.run("Workbench.CloseAllDocuments");
  REQUIRE(env.app->unsavedPrompt().isOpen());
  ListPopup* p = env.app->unsavedPrompt().popup();
  CHECK(env.rows(p) == (std::vector<std::string>{"[x] src/a.cpp", "[x] src/b.cpp", "Yes", "No", "Cancel"}));
  CHECK_EQ(p->selected(), 2);                // Yes
  CHECK_EQ(env.titles().size(), size_t(3));  // nothing closed yet
  // Cancel aborts the whole close.
  env.key(Key::Down);
  env.key(Key::Down);
  env.key(Key::Enter);
  CHECK(!env.app->unsavedPrompt().isOpen());
  CHECK_EQ(env.titles().size(), size_t(3));
  CHECK(env.ed->document(env.dir / "src/a.cpp")->dirty());
  // Escape does too.
  env.run("Workbench.CloseAllDocuments");
  env.key(Key::Escape);
  CHECK_EQ(env.titles().size(), size_t(3));
  // No closes without saving.
  env.run("Workbench.CloseAllDocuments");
  env.key(Key::Down);
  env.key(Key::Enter);
  CHECK(!env.app->unsavedPrompt().isOpen());
  CHECK_EQ(env.titles().size(), size_t(0));
  CHECK(env.ed->document(env.dir / "src/a.cpp") == nullptr);
  CHECK_EQ(slurp(env.dir / "src/a.cpp"), std::string("int a_value;\n"));
  CHECK_EQ(slurp(env.dir / "src/b.cpp"), std::string("int b_value;\n"));
}

TEST(vstabs_t6_yes_saves_the_checked_items_and_an_unchecked_item_is_closed_without_saving) {
  Env env;
  dirtyAB(env);
  env.run("Workbench.CloseAllDocuments");
  REQUIRE(env.app->unsavedPrompt().isOpen());
  env.key(Key::Up);
  env.key(Key::Up);  // on a.cpp
  env.key(Key::Space);
  ListPopup* p = env.app->unsavedPrompt().popup();
  REQUIRE(p != nullptr);
  CHECK_EQ(env.rows(p)[0], std::string("[ ] src/a.cpp"));
  CHECK_EQ(env.rows(p)[1], std::string("[x] src/b.cpp"));
  env.key(Key::Space);  // toggles back
  CHECK_EQ(env.rows(p)[0], std::string("[x] src/a.cpp"));
  env.key(Key::Enter);  // Enter on an item toggles it as well
  CHECK_EQ(env.rows(p)[0], std::string("[ ] src/a.cpp"));
  env.key(Key::Down);
  env.key(Key::Down);
  CHECK_EQ(p->selected(), 2);
  env.key(Key::Enter);  // Yes
  CHECK(!env.app->unsavedPrompt().isOpen());
  CHECK_EQ(env.titles().size(), size_t(0));
  CHECK_EQ(slurp(env.dir / "src/a.cpp"), std::string("int a_value;\n"));          // unchecked: not saved
  CHECK_EQ(slurp(env.dir / "src/b.cpp"), std::string("// two\nint b_value;\n"));  // checked: saved
  // All checked: both are saved.
  Env env2;
  dirtyAB(env2);
  env2.run("Workbench.CloseAllDocuments");
  env2.key(Key::Enter);
  CHECK_EQ(env2.titles().size(), size_t(0));
  CHECK(contains(slurp(env2.dir / "src/a.cpp"), "// one"));
  CHECK(contains(slurp(env2.dir / "src/b.cpp"), "// two"));
}

TEST(vstabs_t6_a_refused_save_goes_through_the_save_refused_dialog_and_aborts_the_close) {
  Env env;
  writeFile(env.dir / "src/l.cpp", kLatin);
  env.open("src/a.cpp");
  env.edit("// one\n");
  Document* l = env.open("src/l.cpp");
  REQUIRE(l != nullptr);
  env.ed->moveCursor({0, 4});
  env.ed->insertText(kHan);
  env.bench->sync();
  env.run("Workbench.CloseAllDocuments");
  REQUIRE(env.app->unsavedPrompt().isOpen());
  env.key(Key::Enter);  // Yes
  CHECK(!env.app->unsavedPrompt().isOpen());
  REQUIRE(env.app->saveRefusedDialog().isOpen());
  CHECK_EQ(env.titles().size(), size_t(2));  // nothing was closed
  CHECK_EQ(slurp(env.dir / "src/l.cpp"), kLatin);
  CHECK(l->dirty());
  CHECK(contains(slurp(env.dir / "src/a.cpp"), "// one"));  // the one that could be written stays saved
  CHECK(!env.ed->document(env.dir / "src/a.cpp")->dirty());
}

TEST(vstabs_t6_a_single_modified_document_keeps_the_per_file_prompt_and_close_all_but_this_asks) {
  Env env;
  env.open("src/a.cpp");
  env.edit("// one\n");
  env.open("src/b.cpp");
  env.open("src/c.cpp");                // active
  env.run("Workbench.CloseOtherTabs");  // Close All But This: only a.cpp is modified
  REQUIRE(env.app->unsavedPrompt().isOpen());
  CHECK_EQ(env.rows(env.app->unsavedPrompt().popup())[0], std::string("Save"));
  CHECK_EQ(env.rows(env.app->unsavedPrompt().popup()).size(), size_t(3));
  env.key(Key::Escape);
  CHECK_EQ(env.titles().size(), size_t(3));
  env.key(Key::Down);  // (the popup is gone; Down goes to the editor)
  // Several modified documents: the list.
  env.open("src/b.cpp");
  env.edit("// two\n");
  env.open("src/c.cpp");
  env.run("Workbench.CloseOtherTabs");
  REQUIRE(env.app->unsavedPrompt().isOpen());
  CHECK_EQ(env.rows(env.app->unsavedPrompt().popup()).size(), size_t(5));
  env.key(Key::Down);
  env.key(Key::Enter);  // No
  CHECK(env.titles() == (std::vector<std::string>{"c.cpp"}));
  // Close All But Pinned asks the same way, and a pinned modified tab is not closed or asked about.
  env.open("src/a.cpp");
  env.edit("// three\n");
  env.bench->setPinned(env.tabOf("src/a.cpp"), true);
  env.open("src/d.cpp");
  env.run("Workbench.CloseAllButPinned");
  CHECK(!env.app->unsavedPrompt().isOpen());
  CHECK(env.titles() == (std::vector<std::string>{"a.cpp"}));
}

TEST(vstabs_t6_closing_a_panel_and_quitting_use_the_list_dialog) {
  Env env;
  env.open("src/a.cpp");
  env.edit("// one\n");
  env.open("src/b.cpp");
  env.edit("// two\n");
  env.bench->splitActive(Splitter::Orientation::Horizontal);  // duplicates the active tab into a new panel
  REQUIRE(env.bench->panels().size() == 2);
  // The new panel (active) holds a view of b.cpp only; b.cpp is also open in the first panel, so closing the panel
  // does not close the document and asks nothing.
  env.run("Workbench.ClosePanel");
  CHECK(!env.app->unsavedPrompt().isOpen());
  CHECK_EQ(env.bench->panels().size(), size_t(1));
  CHECK(env.ed->document(env.dir / "src/b.cpp")->dirty());
  // Now close the only panel's documents: the list dialog; Cancel keeps the panel and tabs.
  env.run("Workbench.ClosePanel");
  REQUIRE(env.app->unsavedPrompt().isOpen());
  CHECK_EQ(env.rows(env.app->unsavedPrompt().popup()).size(), size_t(5));
  env.key(Key::Escape);
  CHECK_EQ(env.titles().size(), size_t(2));
  // Quit lists every modified document, with the same dialog; Yes saves.
  env.run("App.Quit");
  REQUIRE(env.app->unsavedPrompt().isOpen());
  CHECK(env.rows(env.app->unsavedPrompt().popup()) ==
        (std::vector<std::string>{"[x] src/a.cpp", "[x] src/b.cpp", "Yes", "No", "Cancel"}));
  CHECK(!env.app->quitRequested());
  env.key(Key::Enter);
  CHECK(env.app->quitRequested());
  CHECK(contains(slurp(env.dir / "src/a.cpp"), "// one"));
  CHECK(contains(slurp(env.dir / "src/b.cpp"), "// two"));
}

// ---------------------------------------------------------------------------------------------------------------
// T9: files changed on disk
// ---------------------------------------------------------------------------------------------------------------

namespace {
bool changeOnDisk(Env& env, const std::string& rel, const std::string& content, std::function<bool()> seen) {
  writeFile(env.dir / rel, content);
  return env.ed->waitFor([&] { return seen(); }, 8000);
}
ListPopup* extPopup(Env& env) { return env.app->externalChanges().popup(); }
}  // namespace

TEST(vstabs_t9_defaults_ask_for_a_clean_document_with_visual_studios_question_and_buttons) {
  Env env(true);
  Document* d = env.open("src/a.cpp");
  REQUIRE(changeOnDisk(env, "src/a.cpp", "int one;\n", [&] { return env.app->externalChanges().pending() > 0; }));
  CHECK(contains(d->buffer().text(), "a_value"));  // nothing reloaded behind the user's back
  env.frame();
  REQUIRE(env.app->externalChanges().isOpen());
  CHECK(env.rows(extPopup(env)) == (std::vector<std::string>{"Yes", "Yes to All", "No", "No to All"}));
  CHECK_EQ(extPopup(env)->selected(), 0);
  env.key(Key::Enter);  // Yes
  CHECK_EQ(d->buffer().text(), std::string("int one;\n"));
  CHECK(!env.app->externalChanges().isOpen());
  // No keeps the buffer.
  REQUIRE(changeOnDisk(env, "src/a.cpp", "int two;\n", [&] { return env.app->externalChanges().pending() > 0; }));
  env.frame();
  REQUIRE(env.app->externalChanges().isOpen());
  env.key(Key::Down);
  env.key(Key::Down);
  env.key(Key::Enter);  // No
  CHECK_EQ(d->buffer().text(), std::string("int one;\n"));
  CHECK(!env.app->externalChanges().isOpen());
}

TEST(vstabs_t9_reload_unless_unsaved_reloads_a_clean_document_silently_a_dirty_one_always_asks) {
  Env env(true);
  env.settings("editor.reloadUnlessUnsaved = true\n");
  Document* d = env.open("src/a.cpp");
  REQUIRE(changeOnDisk(env, "src/a.cpp", "int one;\n", [&] { return d->buffer().text() == "int one;\n"; }));
  env.frame();
  CHECK(!env.app->externalChanges().isOpen());
  CHECK(contains(env.app->message(), "Reloaded src/a.cpp"));
  env.edit("// mine\n");
  REQUIRE(d->dirty());
  REQUIRE(changeOnDisk(env, "src/a.cpp", "int two;\n", [&] { return env.app->externalChanges().pending() > 0; }));
  env.frame();
  REQUIRE(env.app->externalChanges().isOpen());
  CHECK(env.rows(extPopup(env)) == (std::vector<std::string>{"Yes", "Yes to All", "No", "No to All"}));
  CHECK_EQ(extPopup(env)->selected(), 2);  // the safe choice
  CHECK(contains(d->buffer().text(), "// mine"));
  env.key(Key::Enter);  // No
  CHECK(contains(d->buffer().text(), "// mine"));
  CHECK(d->dirty());
  // Also dirty with reloadUnlessUnsaved off.
  env.settings("editor.reloadUnlessUnsaved = false\n");
  REQUIRE(changeOnDisk(env, "src/a.cpp", "int three;\n", [&] { return env.app->externalChanges().pending() > 0; }));
  env.frame();
  REQUIRE(env.app->externalChanges().isOpen());
  env.key(Key::Up);
  env.key(Key::Up);
  env.key(Key::Enter);  // Yes: the unsaved changes are lost
  CHECK_EQ(d->buffer().text(), std::string("int three;\n"));
  CHECK(!d->dirty());
}

TEST(vstabs_t9_yes_to_all_and_no_to_all_answer_for_the_files_waiting_and_detection_can_be_switched_off) {
  Env env(true);
  Document* a = env.open("src/a.cpp");
  Document* b = env.open("src/b.cpp");
  Document* c = env.open("src/c.cpp");
  // A modal popup keeps the prompts queued while all three files change.
  env.run("Editor.GotoLine");
  REQUIRE(env.app->goToLine().isOpen());
  writeFile(env.dir / "src/a.cpp", "int A;\n");
  writeFile(env.dir / "src/b.cpp", "int B;\n");
  writeFile(env.dir / "src/c.cpp", "int C;\n");
  REQUIRE(env.ed->waitFor([&] { return env.app->externalChanges().pending() >= 3; }, 8000));
  env.key(Key::Escape);
  env.frame();
  REQUIRE(env.app->externalChanges().isOpen());
  env.key(Key::Down);
  env.key(Key::Enter);  // Yes to All
  env.frame();
  CHECK(!env.app->externalChanges().isOpen());
  CHECK_EQ(env.app->externalChanges().pending(), size_t(0));
  CHECK_EQ(a->buffer().text() + b->buffer().text() + c->buffer().text(), std::string("int A;\nint B;\nint C;\n"));
  // No to All keeps every waiting file.
  env.run("Editor.GotoLine");
  writeFile(env.dir / "src/a.cpp", "int A2;\n");
  writeFile(env.dir / "src/b.cpp", "int B2;\n");
  REQUIRE(env.ed->waitFor([&] { return env.app->externalChanges().pending() >= 2; }, 8000));
  env.key(Key::Escape);
  env.frame();
  REQUIRE(env.app->externalChanges().isOpen());
  env.key(Key::Down);
  env.key(Key::Down);
  env.key(Key::Down);
  env.key(Key::Enter);  // No to All
  env.frame();
  CHECK(!env.app->externalChanges().isOpen());
  CHECK_EQ(env.app->externalChanges().pending(), size_t(0));
  CHECK_EQ(a->buffer().text(), std::string("int A;\n"));
  CHECK_EQ(b->buffer().text(), std::string("int B;\n"));
  // detectExternalChanges = false: nothing is noticed, nothing is said.
  env.settings("editor.detectExternalChanges = false\n");
  writeFile(env.dir / "src/c.cpp", "int C3;\n");
  env.ed->waitFor([] { return false; }, 400);
  env.frame();
  CHECK_EQ(env.app->externalChanges().pending(), size_t(0));
  CHECK(!env.app->externalChanges().isOpen());
  CHECK_EQ(c->buffer().text(), std::string("int C;\n"));
}

TEST(vstabs_t9_the_older_reload_external_changes_setting_still_wins_when_it_is_set) {
  using P = ExternalChanges::Policy;
  auto pol = [](bool detect, bool reload, const char* legacy) {
    return ExternalChanges::resolvePolicy(detect, reload, legacy);
  };
  P p = pol(true, false, "");
  CHECK(p.detect && !p.reloadUnlessUnsaved && !p.announce);  // Visual Studio's defaults
  p = pol(true, false, "auto");
  CHECK(p.detect && p.reloadUnlessUnsaved);
  p = pol(true, true, "prompt");
  CHECK(p.detect && !p.reloadUnlessUnsaved);
  p = pol(true, true, " Never ");
  CHECK(!p.detect && p.announce);
  p = pol(false, true, "nonsense");
  CHECK(!p.detect && p.reloadUnlessUnsaved);  // unknown text leaves the two flags alone
  Env env(true);
  env.settings("editor.reloadExternalChanges = auto\n");
  Document* d = env.open("src/a.cpp");
  REQUIRE(changeOnDisk(env, "src/a.cpp", "int one;\n", [&] { return d->buffer().text() == "int one;\n"; }));
  env.frame();
  CHECK(!env.app->externalChanges().isOpen());
}

// ---------------------------------------------------------------------------------------------------------------
// T11: pinned tabs stay left of unpinned ones
// ---------------------------------------------------------------------------------------------------------------

TEST(vstabs_t11_pinned_tabs_stay_left_and_moves_and_drags_cannot_cross_the_boundary) {
  Env env;
  for (const char* f : {"a", "b", "c", "d"}) env.open(std::string("src/") + f + ".cpp");
  Panel* p = env.bench->activePanel();
  env.bench->setPinned(env.tabOf("src/c.cpp"), true);
  env.bench->setPinned(env.tabOf("src/d.cpp"), true);
  CHECK(env.titles() == (std::vector<std::string>{"c.cpp", "d.cpp", "a.cpp", "b.cpp"}));
  TabStrip& st = p->tabs();
  // moveTab clamps to the group.
  st.moveTab(env.tabOf("src/c.cpp"), 3);
  CHECK(env.titles() == (std::vector<std::string>{"d.cpp", "c.cpp", "a.cpp", "b.cpp"}));
  st.moveTab(env.tabOf("src/b.cpp"), 0);
  CHECK(env.titles() == (std::vector<std::string>{"d.cpp", "c.cpp", "b.cpp", "a.cpp"}));
  // A mouse drag of an unpinned tab to the far left stops at the boundary.
  const RectF rb = st.tabRect(env.tabOf("src/a.cpp"));
  const float y = rb.y + rb.h / 2;
  env.app->handleEvent(mouseEv(InputType::MouseDown, rb.x + 5, y), env.now += 10);
  for (float x = rb.x; x > st.bounds().x - 5; x -= 6) {
    InputEvent mv = mouseEv(InputType::MouseMove, x, y);
    mv.buttons = 1;
    env.app->handleEvent(mv, env.now += 10);
  }
  env.app->handleEvent(mouseEv(InputType::MouseUp, st.bounds().x, y), env.now += 10);
  env.frame();
  CHECK(env.titles() == (std::vector<std::string>{"d.cpp", "c.cpp", "a.cpp", "b.cpp"}));
  // ... and a pinned tab dragged to the far right stays inside the pinned group.
  const RectF rc = st.tabRect(env.tabOf("src/d.cpp"));
  env.app->handleEvent(mouseEv(InputType::MouseDown, rc.x + 3, y), env.now += 10);
  for (float x = rc.x; x < st.bounds().x + st.bounds().w - 5; x += 6) {
    InputEvent mv = mouseEv(InputType::MouseMove, x, y);
    mv.buttons = 1;
    env.app->handleEvent(mv, env.now += 10);
  }
  env.app->handleEvent(mouseEv(InputType::MouseUp, st.bounds().x + st.bounds().w - 6, y), env.now += 10);
  env.frame();
  CHECK(env.titles() == (std::vector<std::string>{"c.cpp", "d.cpp", "a.cpp", "b.cpp"}));
  // MoveNextPanelTab never leaves the group either.
  env.bench->setActivePanel(p);
  p->setActive(env.tabOf("src/d.cpp"));
  CHECK(!env.bench->moveActiveTab(p, 1));
  CHECK(env.titles() == (std::vector<std::string>{"c.cpp", "d.cpp", "a.cpp", "b.cpp"}));
  // Moving tabs into another panel at an index keeps the boundary there too.
  Panel* q = env.bench->splitActive(Splitter::Orientation::Horizontal);
  REQUIRE(q != nullptr);
  env.bench->moveTab(env.tabOf("src/c.cpp"), q, 0);
  env.bench->moveTab(env.tabOf("src/a.cpp"), q, 0);  // unpinned, dropped at the very left
  const std::vector<std::string> qt = env.titles(q);
  REQUIRE(qt.size() >= 2);
  size_t firstUnpinned = 0;
  while (firstUnpinned < q->tabs().tabs().size() && q->tabs().tabs()[firstUnpinned].pinned) ++firstUnpinned;
  for (size_t i = firstUnpinned; i < q->tabs().tabs().size(); ++i) CHECK(!q->tabs().tabs()[i].pinned);
  CHECK(q->tabs().tabs()[0].pinned);
}

// ---------------------------------------------------------------------------------------------------------------
// T12: Ctrl+K, Ctrl+Z
// ---------------------------------------------------------------------------------------------------------------

TEST(vstabs_t12_ctrl_k_ctrl_z_reopens_the_last_closed_tab_and_ctrl_shift_t_still_does) {
  Env env;
  env.open("src/a.cpp");
  env.open("src/b.cpp");
  env.ed->moveCursor({0, 3});
  env.bench->sync();
  env.bench->closeTab(env.tabOf("src/b.cpp"));
  env.frame();
  CHECK_EQ(env.tabOf("src/b.cpp"), -1);
  env.key(Key('K'), ModCtrl);
  env.key(Key('Z'), ModCtrl);
  CHECK(env.tabOf("src/b.cpp") >= 0);
  CHECK_EQ(env.active(), std::string("b.cpp"));
  // 10x's own key is unchanged.
  env.bench->closeTab(env.tabOf("src/b.cpp"));
  env.frame();
  env.key(Key('T'), ModCtrl | ModShift);
  CHECK(env.tabOf("src/b.cpp") >= 0);
  // Ctrl+K, Ctrl+Z is bound to the same command as RestoreRecentlyClosedFile.
  bool found = false;
  for (const KeyBinding& b : env.app->keys().bindings())
    if (b.chords.size() == 2 && formatChords(b.chords) == "Control K, Control Z") {
      found = true;
      CHECK_EQ(b.command, std::string("Workbench.ReopenClosedTab"));  // the 10x name resolves to it
    }
  CHECK(found);
}
