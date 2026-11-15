// CLOSE-SLN (docs/VS_BEHAVIOUR.md section 18): File > Close Solution and the empty window (no solution, no folder):
//   * `File.CloseSolution` (10x's CloseWorkspace, Visual Studio's Close Solution; no key) closes the open workspace in
//     the same window: the save prompt (Yes / No / Cancel), the layout / bookmarks / breakpoints of the closing
//     workspace are saved, and the window shows the empty window: no Editor (so no clangd, scan, watcher, build,
//     debugger or version control), the File / Edit / View menus with everything that needs a workspace greyed,
//   * opening a solution, a folder, a recent entry or a dropped file from the empty window is the SLN-OPEN flow,
//   * Exit works from it, `tenfold-ui` without arguments starts in it (a subprocess test), teardown is leak clean,
//   * a golden of the empty window.
// Real AppHost, null renderer, mock font, fake file dialog.
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "../text/text_test_util.h"
#include "core/PathUtil.h"
#include "ui/commands/CommandNames.h"
#include "ui/features/WorkspaceExplorer.h"
#include "ui/gfx/PngWrite.h"
#include "ui/gfx/TextRenderer.h"
#include "ui/platform/FileDialog.h"
#include "ui/settings/Theme.h"
#include "ui/shell/AppHost.h"
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

void write(const fs::path& p, const std::string& text) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary | std::ios::trunc) << text;
}
std::string slurp(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

void makeSolution(const fs::path& dir, const std::string& name) {
  write(dir / (name + ".sln"),
        "\nMicrosoft Visual Studio Solution File, Format Version 12.00\n"
        "Project(\"{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}\") = \"" +
            name + "\", \"" + name +
            ".vcxproj\", \"{CCCCCCCC-0000-0000-0000-000000000001}\"\nEndProject\nGlobal\n"
            "\tGlobalSection(SolutionConfigurationPlatforms) = preSolution\n"
            "\t\tDebug|x64 = Debug|x64\n\tEndGlobalSection\n"
            "\tGlobalSection(ProjectConfigurationPlatforms) = postSolution\n"
            "\t\t{CCCCCCCC-0000-0000-0000-000000000001}.Debug|x64.ActiveCfg = Debug|x64\n"
            "\t\t{CCCCCCCC-0000-0000-0000-000000000001}.Debug|x64.Build.0 = Debug|x64\n"
            "\tEndGlobalSection\nEndGlobal\n");
  write(dir / (name + ".vcxproj"),
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<Project DefaultTargets=\"Build\" xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">\n"
        "  <ItemGroup Label=\"ProjectConfigurations\">\n"
        "    <ProjectConfiguration Include=\"Debug|x64\"><Configuration>Debug</Configuration>"
        "<Platform>x64</Platform></ProjectConfiguration>\n  </ItemGroup>\n"
        "  <ItemGroup><ClCompile Include=\"src/" +
            name + ".cpp\" /></ItemGroup>\n</Project>\n");
  write(dir / "src" / (name + ".cpp"), "int " + name + "_value;\n");
}

InputEvent keyEv(Key k, uint8_t mods = 0, InputType t = InputType::KeyDown) {
  InputEvent e;
  e.type = t;
  e.key = k;
  e.mods = mods;
  return e;
}
InputEvent dropEv(const fs::path& p) {
  InputEvent e;
  e.type = InputType::DropFile;
  e.text = pathUtf8(p);
  return e;
}

struct Env {
  fs::path base, game, other, user;
  NullRenderer rec;
  std::unique_ptr<AppHost> host;
  std::vector<FileDialogRequest> dialogs;
  std::vector<fs::path> answer;
  std::vector<bool> fullscreen;  // what the window hook was asked
  bool failText = false;
  int64_t now = 1000;

  Env() {
    static int n = 0;
    base = uitest::tempRoot() /
           ("tf_closesln_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_" +
            std::to_string(n++));
    game = base / "Game";
    other = base / "Other";
    user = base / "user";
    fs::create_directories(user);
    makeSolution(game, "Alpha");
    makeSolution(game, "Beta");
    makeSolution(other, "Gamma");
  }
  ~Env() {
    host.reset();
    std::error_code ec;
    fs::remove_all(base, ec);
  }
  AppHost::Options options(const std::string& userSettings = {}) {
    if (!userSettings.empty()) write(user / "settings.txt", userSettings);
    AppHost::Options o;
    o.userDir = user;
    o.renderer = &rec;
    o.editor.semantic.useClangd = false;
    o.editor.semantic.persistFastIndex = false;
    o.editor.vcs.enabled = false;
    o.watchSettings = false;
    o.defaultFile = "";
    o.report = [](const std::string&) {};
    o.makeText = [this](const Settings&, float, std::string* err) -> std::unique_ptr<gfx::TextRenderer> {
      if (failText) {
        if (err) *err = "no font";
        return nullptr;
      }
      gfx::FontStack stack;
      stack.add(std::make_shared<tftext::MockFont>(
          U"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ._-:,;/()[]{}<>+*&#?=!\"'\\|", 8));
      return gfx::TextRenderer::fromFonts(std::move(stack));
    };
    o.dialog = [this](const FileDialogRequest& r, FileDialogDone done) {
      dialogs.push_back(r);
      done(answer);
    };
    o.setFullscreen = [this](bool on) { fullscreen.push_back(on); };
    o.setupApp = [this](EditorApp& a) { a.setWindowHooks({[this](bool on) { fullscreen.push_back(on); }}); };
    return o;
  }
  void create(const std::string& userSettings = {}) { host = std::make_unique<AppHost>(options(userSettings)); }
  bool open(const fs::path& target, const fs::path& file = {}) {
    if (!host) create();
    std::string err;
    const bool ok = host->open(target, file, true, &err);
    frame();
    return ok;
  }
  bool startEmpty(const std::string& userSettings = {}) {
    if (!host) create(userSettings);
    std::string err;
    const bool ok = host->openEmpty(&err);
    frame();
    return ok;
  }
  Editor& ed() { return *host->editor(); }
  EditorApp& app() { return *host->app(); }
  Workbench& bench() { return *host->bench(); }
  EmptyWindow& empty() { return *host->empty(); }
  void frame() {
    if (host->app()) {
      host->editor()->pump();
      host->app()->tick(now);
    } else if (host->empty()) {
      host->empty()->tick(now);
    }
    host->root()->render(rec, 1000, 700);
  }
  void key(Key k, uint8_t mods = 0) {
    now += 10;
    if (host->app()) host->app()->handleEvent(keyEv(k, mods), now);
    else host->empty()->handleEvent(keyEv(k, mods), now);
    frame();
  }
  void drop(const fs::path& p) {
    now += 10;
    if (host->app()) host->app()->handleEvent(dropEv(p), now);
    else host->empty()->handleEvent(dropEv(p), now);
    frame();
  }
  bool apply() {
    const bool r = host->applyPending();
    frame();
    return r;
  }
  Document* openDoc(const fs::path& p) {
    if (bench().openFile(p) < 0) return nullptr;
    bench().sync();
    frame();
    return ed().currentDocument();
  }
  void edit() {
    ed().moveCursor({0, 0});
    ed().insertText("// edit\n");
    bench().sync();
    frame();
  }
  std::vector<std::string> rows(ListPopup* p) const {
    std::vector<std::string> out;
    if (p)
      for (const auto& r : p->rows()) out.push_back(r.primary);
    return out;
  }
  MenuBar& bar() { return host->empty() ? host->empty()->menuBar() : host->bench()->menuBar(); }
  // The items of the open drop-down by label.
  const MenuItem* item(const std::string& label) {
    ContextMenu* m = bar().dropdown();
    if (!m) return nullptr;
    for (const MenuItem& it : m->items())
      if (it.label == label) return &it;
    return nullptr;
  }
  void openMenu(int i) {
    bar().open(i, true);
    frame();
  }
  std::vector<std::string> labels() {
    std::vector<std::string> out;
    if (ContextMenu* m = bar().dropdown())
      for (const MenuItem& it : m->items()) out.push_back(it.separator ? "-" : it.label);
    return out;
  }
};

}  // namespace

// --- the command ----------------------------------------------------------------------------------------------------

TEST(closesln_the_command_is_10xs_CloseWorkspace_and_has_no_default_key) {
  const ResolvedCommand rc = resolveCommandName("CloseWorkspace");
  CHECK(rc.known10x && rc.implemented);
  CHECK_EQ(rc.id, std::string("File.CloseSolution"));
  Env env;
  REQUIRE(env.open(env.game / "Alpha.sln"));
  for (const KeyBinding& b : env.app().keys().bindings()) {
    CHECK(resolveCommandName(b.command).id != "File.CloseSolution");  // neither VS nor 10x's map has a key for it
  }
  const Command* c = env.app().commands().find("File.CloseSolution");
  REQUIRE(c != nullptr);
  CHECK(c->handler != nullptr);
}

TEST(closesln_the_file_menu_has_Close_Solution_after_Close_like_visual_studio) {
  Env env;
  REQUIRE(env.open(env.game / "Alpha.sln"));
  env.openMenu(0);
  const auto l = env.labels();
  const auto close = std::find(l.begin(), l.end(), "Close");
  REQUIRE(close != l.end());
  CHECK_EQ(*(close + 2), std::string("Close Solution"));  // "Close All Documents" sits between
  const MenuItem* it = env.item("Close Solution");
  REQUIRE(it != nullptr);
  CHECK(it->enabled);
  CHECK(it->shortcut.empty());
}

TEST(closesln_closing_without_modified_documents_needs_no_prompt_and_shows_the_empty_window) {
  Env env;
  REQUIRE(env.open(env.game / "Alpha.sln"));
  env.openDoc(env.game / "src" / "Alpha.cpp");
  const int gen = env.host->generation();
  CHECK(!env.host->isEmpty());
  env.app().runCommand("File.CloseSolution");
  CHECK(!env.app().unsavedPrompt().isOpen());
  CHECK(env.host->hasPending());  // like a switch: the swap waits for the loop
  CHECK(!env.host->isEmpty());
  REQUIRE(env.apply());
  CHECK_EQ(env.host->generation(), gen + 1);
  CHECK(env.host->isEmpty());
  CHECK(!env.host->hasWorkspace());
  CHECK(env.host->editor() == nullptr);  // no Editor: no clangd, scan, watcher, build, debugger, version control
  CHECK(env.host->bench() == nullptr);
  CHECK(env.host->app() == nullptr);
  CHECK(env.host->root() == &env.empty());
  CHECK(env.host->settings() != nullptr);  // the user's settings and theme still apply
  CHECK(env.host->target().root.empty());
}

TEST(closesln_the_closing_workspace_saves_layout_bookmarks_and_breakpoints) {
  Env env;
  REQUIRE(env.open(env.game / "Alpha.sln"));
  env.openDoc(env.game / "src" / "Alpha.cpp");
  env.openDoc(env.game / "src" / "Beta.cpp");
  CHECK(env.ed().toggleBookmark());
  CHECK(env.ed().toggleBreakpoint());
  env.app().runCommand("File.CloseSolution");
  REQUIRE(env.apply());
  const fs::path cache = env.game / ".tenfold";
  CHECK(slurp(cache / "layout.json").find("Beta.cpp") != std::string::npos);
  CHECK(slurp(cache / "bookmarks.json").find("Beta.cpp") != std::string::npos);
  CHECK(slurp(cache / "breakpoints.json").find("Beta.cpp") != std::string::npos);
  // ... and reopening restores them.
  REQUIRE(env.open(env.game / "Alpha.sln"));
  std::vector<std::string> titles;
  for (Panel* p : env.bench().panels())
    for (const auto& t : p->tabs().tabs()) titles.push_back(t.title);
  CHECK(std::find(titles.begin(), titles.end(), "Beta.cpp") != titles.end());
}

TEST(closesln_a_modified_document_asks_yes_no_cancel) {
  Env env;
  REQUIRE(env.open(env.game / "Alpha.sln"));
  const fs::path a = env.game / "src" / "Alpha.cpp";
  env.openDoc(a);
  env.edit();
  REQUIRE(env.ed().document(a)->dirty());

  // Cancel (Escape): nothing happens, the document stays modified.
  env.app().runCommand("File.CloseSolution");
  REQUIRE(env.app().unsavedPrompt().isOpen());
  CHECK(env.rows(env.app().unsavedPrompt().popup()) == (std::vector<std::string>{"Save", "Don't Save", "Cancel"}));
  CHECK(!env.host->hasPending());
  env.key(Key::Escape);
  CHECK(!env.app().unsavedPrompt().isOpen());
  CHECK(!env.host->hasPending());
  CHECK(!env.apply());
  CHECK(!env.host->isEmpty());
  CHECK(env.ed().document(a)->dirty());

  // Don't Save: the window empties, the file on disk is untouched.
  env.app().runCommand("File.CloseSolution");
  env.key(Key::Down);
  env.key(Key::Enter);
  CHECK(env.host->hasPending());
  REQUIRE(env.apply());
  CHECK(env.host->isEmpty());
  CHECK_EQ(slurp(a), std::string("int Alpha_value;\n"));
}

TEST(closesln_yes_saves_before_closing) {
  Env env;
  REQUIRE(env.open(env.game / "Alpha.sln"));
  const fs::path a = env.game / "src" / "Alpha.cpp";
  env.openDoc(a);
  env.edit();
  env.app().runCommand("File.CloseSolution");
  REQUIRE(env.app().unsavedPrompt().isOpen());
  env.key(Key::Enter);  // Save
  REQUIRE(env.apply());
  CHECK(env.host->isEmpty());
  CHECK_EQ(slurp(a), std::string("// edit\nint Alpha_value;\n"));
}

TEST(closesln_several_modified_documents_use_the_list_dialog_and_cancel_keeps_the_solution) {
  Env env;
  REQUIRE(env.open(env.game / "Alpha.sln"));
  for (const char* n : {"Alpha", "Beta"}) {
    env.openDoc(env.game / "src" / (std::string(n) + ".cpp"));
    env.edit();
  }
  env.app().runCommand("File.CloseSolution");
  REQUIRE(env.app().unsavedPrompt().isOpen());
  CHECK(env.rows(env.app().unsavedPrompt().popup()).size() == 5);  // two items + Yes / No / Cancel
  env.key(Key::Down);
  env.key(Key::Down);
  env.key(Key::Down);  // Cancel
  env.key(Key::Enter);
  CHECK(!env.host->hasPending());
  CHECK(!env.host->isEmpty());
  CHECK_EQ(env.ed().workspace().root(), env.game);
}

TEST(closesln_setting_confirmUnsaved_off_skips_the_prompt) {
  Env env;
  REQUIRE(env.open(env.game / "Alpha.sln"));
  Settings s;
  s.loadTexts("editor.confirmUnsaved = false\n", "");
  env.app().applySettings(s);
  env.openDoc(env.game / "src" / "Alpha.cpp");
  env.edit();
  env.app().runCommand("File.CloseSolution");
  CHECK(!env.app().unsavedPrompt().isOpen());
  CHECK(env.host->hasPending());
}

TEST(closesln_a_failed_font_leaves_the_open_solution_alone) {
  Env env;
  REQUIRE(env.open(env.game / "Alpha.sln"));
  const int gen = env.host->generation();
  env.failText = true;
  env.app().runCommand("File.CloseSolution");
  CHECK(!env.apply());
  CHECK_EQ(env.host->generation(), gen);
  CHECK(!env.host->isEmpty());
  CHECK(env.app().message().find("Cannot close") != std::string::npos);
}

// --- the empty window -----------------------------------------------------------------------------------------------

TEST(closesln_start_without_workspace_is_empty_and_touches_nothing_in_the_current_folder) {
  Env env;
  const fs::path cwd = env.base / "cwd";
  fs::create_directories(cwd);
  const fs::path old = fs::current_path();
  fs::current_path(cwd);
  REQUIRE(env.startEmpty());
  for (int i = 0; i < 3; ++i) env.frame();
  CHECK(env.host->isEmpty());
  CHECK(env.host->editor() == nullptr);
  CHECK(env.host->generation() == 1);
  CHECK(env.host->recent().entries().empty());
  env.host->shutdown();
  env.host.reset();
  const bool clean = fs::is_empty(cwd);  // no .tenfold folder, no log, nothing
  fs::current_path(old);
  CHECK(clean);
}

TEST(closesln_the_empty_window_has_file_edit_view_with_workspace_commands_greyed) {
  Env env;
  REQUIRE(env.startEmpty());
  REQUIRE(env.bar().menus().size() == 3);
  env.openMenu(0);
  const auto file = env.labels();
  for (const char* want : {"Open Solution...", "Open Folder...", "Recent Projects and Solutions", "Exit"}) {
    const MenuItem* it = env.item(want);
    REQUIRE(it != nullptr);
    CHECK(it->enabled);
  }
  for (const char* grey : {"Open File...", "New File", "Close", "Close All Documents", "Close Solution", "Save",
                           "Save All", "Copy Full Path", "Open Containing Folder"}) {
    const MenuItem* it = env.item(grey);
    REQUIRE(it != nullptr);
    CHECK(!it->enabled);
  }
  CHECK_EQ(env.item("Open Solution...")->shortcut, std::string("Control Shift O"));
  CHECK_EQ(env.item("Exit")->shortcut, std::string("Control Q"));
  // The same entries as the editor window's File menu, in the same order.
  Env other;
  REQUIRE(other.open(other.game / "Alpha.sln"));
  other.openMenu(0);
  CHECK(file == other.labels());
  env.bar().leave();
  for (int i = 1; i < 3; ++i) {
    env.openMenu(i);
    CHECK(!env.labels().empty());
    for (const MenuItem& it : env.bar().dropdown()->items()) {
      if (it.separator) continue;
      if (!it.sub.empty()) {
        for (const MenuItem& s : it.sub) CHECK(s.separator || !s.enabled);
        continue;
      }
      CHECK(it.label == "Full Screen" ? it.enabled : !it.enabled);  // only the window's full screen works
    }
    env.bar().leave();
  }
}

TEST(closesln_disabled_menu_items_do_nothing) {
  Env env;
  REQUIRE(env.startEmpty());
  CHECK(!env.empty().runCommand("File.Save"));
  CHECK(!env.empty().runCommand("Editor.Undo"));
  CHECK(!env.empty().runCommand("Build.Build"));
  CHECK(!env.empty().runCommand("File.CloseSolution"));
  CHECK(!env.host->hasPending());
  CHECK(env.dialogs.empty());
}

TEST(closesln_the_empty_window_shows_the_no_solution_status_bar_and_no_other_widget) {
  Env env;
  REQUIRE(env.startEmpty());
  EmptyWindow& w = env.empty();
  CHECK(w.menuBar().hasMenus());
  CHECK_EQ(w.children().size(), size_t(2));  // the bar and the status bar: nothing else
  CHECK(w.status().text("message").empty());
  CHECK(w.menuBar().bounds().h > 0);
  CHECK(w.status().bounds().y + w.status().bounds().h == 700);  // the status bar sits at the bottom
  CHECK(w.menuBar().bounds().y == 0 && w.status().bounds().y > w.menuBar().bounds().h);
}

TEST(closesln_open_solution_from_the_empty_window_uses_the_dialog_and_the_open_flow) {
  Env env;
  REQUIRE(env.startEmpty());
  const int gen = env.host->generation();
  env.answer.clear();  // cancelled
  env.key(static_cast<Key>('O'), ModCtrl | ModShift);
  REQUIRE(env.dialogs.size() == 1);
  CHECK(!env.dialogs[0].folder);
  CHECK_EQ(env.dialogs[0].filters[0].pattern, std::string("*.sln;*.slnx;*.vcxproj"));
  CHECK(!env.host->hasPending());
  env.answer = {env.other / "Gamma.sln"};
  env.key(static_cast<Key>('O'), ModCtrl | ModShift);
  CHECK(env.host->hasPending());
  CHECK_EQ(env.host->generation(), gen);
  REQUIRE(env.apply());
  CHECK(!env.host->isEmpty());
  CHECK(env.host->hasWorkspace());
  CHECK(env.host->empty() == nullptr);
  CHECK_EQ(env.ed().workspace().root(), env.other);
  CHECK_EQ(env.ed().workspace().chosenSolution(), env.other / "Gamma.sln");
  CHECK_EQ(env.host->recent().entries().size(), size_t(1));  // remembered like any open
  CHECK(env.app().workspaceOpener().canClose());             // and Close Solution works again
}

TEST(closesln_open_folder_and_recent_entry_from_the_empty_window) {
  Env env;
  env.create();
  env.host->recent().add(env.game / "Alpha.sln");
  env.host->recent().add(env.other);
  REQUIRE(env.startEmpty());
  env.answer = {env.game};
  env.empty().runCommand("File.OpenFolder");
  REQUIRE(env.dialogs.size() == 1);
  CHECK(env.dialogs[0].folder);
  REQUIRE(env.apply());
  CHECK_EQ(env.ed().workspace().root(), env.game);
  CHECK(env.ed().workspace().chosenSolution().empty());
  env.app().runCommand("File.CloseSolution");
  REQUIRE(env.apply());
  REQUIRE(env.host->isEmpty());
  env.empty().runCommand("File.RecentSolutions");
  ListPopup* p = env.empty().opener().popup();
  REQUIRE(p != nullptr);
  CHECK(env.rows(p).size() == 3);  // Game was added by the open above
  CHECK(env.empty().modal() != nullptr);
  env.key(Key::Down);  // second row: newest first: Game (just opened), Other, Alpha.sln
  env.key(Key::Enter);
  CHECK(env.host->hasPending());
  REQUIRE(env.apply());
  CHECK(!env.host->isEmpty());
  CHECK_EQ(env.ed().workspace().root(), env.other);
}

TEST(closesln_dropping_a_solution_a_folder_or_a_file_on_the_empty_window) {
  Env env;
  REQUIRE(env.startEmpty());
  env.drop(env.other / "Gamma.sln");
  REQUIRE(env.host->hasPending());
  REQUIRE(env.apply());
  CHECK_EQ(env.ed().workspace().chosenSolution(), env.other / "Gamma.sln");

  env.app().runCommand("File.CloseSolution");
  REQUIRE(env.apply());
  env.drop(env.game);  // a folder
  REQUIRE(env.apply());
  CHECK_EQ(env.ed().workspace().root(), env.game);
  CHECK(env.ed().workspace().chosenSolution().empty());

  env.app().runCommand("File.CloseSolution");
  REQUIRE(env.apply());
  env.drop(env.game / "src" / "Beta.cpp");  // a plain file: its folder opens with the file in a tab
  REQUIRE(env.apply());
  CHECK_EQ(env.ed().workspace().root(), env.game / "src");
  REQUIRE(env.ed().currentDocument() != nullptr);
  CHECK_EQ(env.ed().currentDocument()->path().filename(), fs::path("Beta.cpp"));

  env.app().runCommand("File.CloseSolution");
  REQUIRE(env.apply());
  env.drop(env.base / "missing.sln");  // a file that is not there: a message, nothing opens
  CHECK(!env.host->hasPending());
  CHECK(env.empty().status().text("message").find("Cannot open") != std::string::npos);
}

TEST(closesln_a_failed_open_from_the_empty_window_stays_empty_and_says_so) {
  Env env;
  REQUIRE(env.startEmpty());
  env.answer = {env.other / "Gamma.sln"};
  env.empty().runCommand("File.OpenSolution");
  REQUIRE(env.host->hasPending());
  env.failText = true;
  CHECK(!env.apply());
  CHECK(env.host->isEmpty());
  CHECK(env.empty().status().text("message").find("Cannot open") != std::string::npos);
  CHECK(env.host->recent().entries().empty());
}

TEST(closesln_exit_works_from_the_empty_window_and_asks_nothing) {
  Env env;
  REQUIRE(env.startEmpty());
  CHECK(!env.empty().quitRequested());
  env.key(static_cast<Key>('Q'), ModCtrl);
  CHECK(env.empty().quitRequested());

  Env env2;
  REQUIRE(env2.startEmpty());
  env2.empty().menuBar().open(0, true);
  env2.frame();
  const MenuItem* exit = env2.item("Exit");
  REQUIRE(exit != nullptr);
  exit->action();
  CHECK(env2.empty().quitRequested());
}

TEST(closesln_alt_opens_the_menus_in_the_empty_window) {
  Env env;
  REQUIRE(env.startEmpty());
  env.key(static_cast<Key>('F'), ModAlt);
  CHECK(env.bar().dropdown() != nullptr);
  env.key(Key::Escape);
  CHECK(env.bar().dropdown() == nullptr);
  env.key(Key::Unknown, ModAlt);  // Alt alone focuses the bar
  env.empty().handleEvent(keyEv(Key::Unknown, 0, InputType::KeyUp), env.now += 10);
  CHECK(env.bar().focusIndex() == 0);
}

TEST(closesln_closing_twice_and_reopening_is_stable_and_remembers_full_screen) {
  Env env;
  REQUIRE(env.open(env.game / "Alpha.sln"));
  env.app().runCommand("View.ToggleFullscreen");
  CHECK(env.app().fullscreen());
  env.app().runCommand("File.CloseSolution");
  REQUIRE(env.apply());
  CHECK(env.empty().fullscreen());  // the window stays full screen: the state travels
  CHECK(env.empty().runCommand("View.ToggleFullscreen"));
  CHECK(!env.empty().fullscreen());
  CHECK(env.fullscreen.size() >= 2 && env.fullscreen.back() == false);
  REQUIRE(env.open(env.other / "Gamma.sln"));
  CHECK(!env.app().fullscreen());
  for (int i = 0; i < 3; ++i) {
    env.app().runCommand("File.CloseSolution");
    REQUIRE(env.apply());
    CHECK(env.host->isEmpty());
    REQUIRE(env.open(env.game / "Alpha.sln"));
    CHECK(env.host->hasWorkspace());
  }
  CHECK_EQ(env.host->generation(), 2 + 2 * 3 + 1);
}

TEST(closesln_menu_settings_apply_to_the_empty_window) {
  Env env;
  REQUIRE(env.startEmpty("menu.hide = true\n"));
  CHECK(env.empty().menuBar().hidden());
  CHECK_EQ(env.empty().menuBar().reservedHeight(), 0.0f);
}

TEST(closesln_user_key_bindings_apply_in_the_empty_window) {
  Env env;
  write(env.user / "KeyMappings.10x_settings", "Control Alt O: OpenFolder\nControl Q:\n");
  REQUIRE(env.startEmpty());
  env.answer.clear();
  env.key(static_cast<Key>('O'), ModCtrl | ModAlt);
  REQUIRE(env.dialogs.size() == 1);
  CHECK(env.dialogs[0].folder);
  env.key(static_cast<Key>('Q'), ModCtrl);  // unbound by the user
  CHECK(!env.empty().quitRequested());
}

// --- subprocess: tenfold-ui without arguments ----------------------------------------------------------------------

#if defined(TENFOLD_UI_EXE) && !defined(_WIN32)
TEST(closesln_tenfold_ui_without_arguments_starts_empty_even_next_to_a_sample_project) {
  SKIP_IF_NO_GL(fx);
  (void)fx;
  Env env;
  const fs::path cwd = env.base / "cwd";
  fs::create_directories(cwd / "examples" / "sample_project" / "src");  // the old fallback would have found it
  write(cwd / "examples" / "sample_project" / "src" / "main.cpp", "int main() {}\n");
  const fs::path png = env.base / "empty.png";
  const std::string cmd = "cd '" + pathUtf8(cwd) + "' && XDG_CONFIG_HOME='" + pathUtf8(env.user) + "' HOME='" +
                          pathUtf8(env.user) + "' '" TENFOLD_UI_EXE "' --screenshot '" + pathUtf8(png) + "' > '" +
                          pathUtf8(env.base / "out.txt") + "' 2>&1";
  CHECK_EQ(std::system(cmd.c_str()), 0);
  CHECK(fs::exists(png));
  CHECK(!fs::exists(cwd / "examples" / "sample_project" / ".tenfold"));  // the sample project was not opened
  CHECK(!fs::exists(cwd / ".tenfold"));
  CHECK(!fs::exists(env.user / "tenfold" / "recent.txt") &&
        !fs::exists(env.user / ".config" / "tenfold" / "recent.txt"));
  // The same run with the sample project as an argument is the editor: its screenshot differs.
  const fs::path png2 = env.base / "sample.png";
  const std::string cmd2 = "cd '" + pathUtf8(cwd) + "' && XDG_CONFIG_HOME='" + pathUtf8(env.user) + "' HOME='" +
                           pathUtf8(env.user) + "' '" TENFOLD_UI_EXE "' examples/sample_project --screenshot '" +
                           pathUtf8(png2) + "' > '" + pathUtf8(env.base / "out2.txt") + "' 2>&1";
  CHECK_EQ(std::system(cmd2.c_str()), 0);
  CHECK(slurp(png) != slurp(png2));
}
#endif

// --- golden ---------------------------------------------------------------------------------------------------------

namespace {
std::string monoFont() {
  for (const char* p : {"/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
                        "/usr/share/fonts/dejavu/DejaVuSansMono.ttf", "/usr/share/fonts/TTF/DejaVuSansMono.ttf"})
    if (std::ifstream(p).good()) return p;
  return {};
}
}  // namespace

TEST(closesln_golden_vs_dark_empty_window_with_the_file_menu_open) {
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
  const fs::path user = uitest::tempRoot() / "tf_closesln_golden_user";
  std::error_code rm;
  fs::remove_all(user, rm);
  fs::create_directories(user);
  const int w = 1024, h = 640;
  std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4);
  std::vector<uint8_t> pxMenu(px.size());
  {
    EmptyWindow::Hooks hooks;
    hooks.open = [](const fs::path&) {};
    hooks.dialog = [](const FileDialogRequest&, FileDialogDone) {};
    EmptyWindow win(*tr, vs, user, std::move(hooks));
    int64_t now = 1000;
    gfx::GlRenderer& r = *fx->renderer;
    r.setOffscreen(true);
    win.render(r, w, h);
    win.tick(now += 10);
    win.render(r, w, h);
    REQUIRE(r.readPixels(w, h, px.data()));
    win.handleEvent(keyEv(static_cast<Key>('F'), ModAlt), now += 10);
    REQUIRE(win.menuBar().dropdown() != nullptr);
    win.menuBar().dropdown()->setSelected(1);  // Open Folder... highlighted
    win.render(r, w, h);
    REQUIRE(r.readPixels(w, h, pxMenu.data()));
    win.menuBar().leave();
    tr->releaseTexture(r);
  }
  fs::remove_all(user, rm);
  struct G {
    const char* file;
    std::vector<uint8_t>* pixels;
  };
  for (const G& g : {G{"empty_window_vs_dark.png", &px}, G{"empty_window_file_menu_vs_dark.png", &pxMenu}}) {
    const std::string path = std::string(TENFOLD_SOURCE_DIR "/tests/golden/") + g.file;
    if (uitest::updateGoldens) {
      REQUIRE(gfx::writePngFile(path, g.pixels->data(), w, h));
      std::cerr << "    golden updated: " << path << "\n";
      continue;
    }
    int gw = 0, gh = 0;
    std::vector<uint8_t> golden;
    if (!gfx::readPngFile(path, gw, gh, golden)) {
      CHECK(!"golden missing or unreadable; run tenfold-ui-tests --update-goldens");
      continue;
    }
    REQUIRE(gw == w && gh == h);
    size_t bad = 0;
    for (size_t i = 0; i < golden.size(); i += 4) {
      int d = 0;
      for (int ch = 0; ch < 4; ++ch) d = std::max(d, std::abs(int(golden[i + ch]) - int((*g.pixels)[i + ch])));
      if (d > 3) ++bad;
    }
    if (bad > golden.size() / 4 / 500) {
      gfx::writePngFile(std::string(g.file) + ".actual.png", g.pixels->data(), w, h);
      std::cerr << "    " << bad << " pixels differ; wrote " << g.file << ".actual.png\n";
    }
    CHECK(bad <= golden.size() / 4 / 500);
  }
}
