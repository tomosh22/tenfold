// SLN-OPEN (docs/VS_BEHAVIOUR.md section 16): opening a solution the way Visual Studio does, against the real AppHost
// with a null renderer, the mock font and a fake file dialog (no window opens):
//   * a .sln / .slnx path as the workspace argument roots the workspace at the solution's folder everywhere (settings
//     from <folder>/.tenfold, the file argument relative to the folder) and that solution is THE solution (flags,
//     Solution Explorer tree, build) even when the folder holds several,
//   * Ctrl+Shift+O (File.OpenSolution) shows the dialog filtered like Visual Studio's and opens the answer,
//   * opening a different solution closes the open one in the same window after the save prompt (Yes / No / Cancel),
//     saves the old layout.json and restores the new one's,
//   * the recent list (persistence, picker, missing entries) and dropped files.
#include <chrono>
#include <fstream>
#include <sstream>

#include "../text/text_test_util.h"
#include "build/BuildRunner.h"
#include "core/PathUtil.h"
#include "ui/features/WorkspaceExplorer.h"
#include "ui/platform/FileDialog.h"
#include "ui/settings/RecentSolutions.h"
#include "ui/shell/AppHost.h"
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

void makeSolution(const fs::path& dir, const std::string& name, const char* ext = ".sln") {
  write(dir / (name + ext),
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
        "  <ItemDefinitionGroup><ClCompile><PreprocessorDefinitions>" +
            name +
            "_DEF;%(PreprocessorDefinitions)</PreprocessorDefinitions></ClCompile></ItemDefinitionGroup>\n"
            "  <ItemGroup><ClCompile Include=\"src/" +
            name + ".cpp\" /></ItemGroup>\n</Project>\n");
  write(dir / "src" / (name + ".cpp"), "int " + name + "_value;\n");
}

InputEvent keyEv(Key k, uint8_t mods = 0) {
  InputEvent e;
  e.type = InputType::KeyDown;
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
  std::vector<fs::path> answer;  // what the fake dialog picks (empty: cancelled)
  std::vector<std::string> reports;
  bool failText = false;  // makeText fails (the new workspace cannot be built)
  int64_t now = 1000;

  Env() {
    static int n = 0;
    base = uitest::tempRoot() /
           ("tf_slnopen_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_" +
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
  AppHost::Options options() {
    AppHost::Options o;
    o.userDir = user;
    o.renderer = &rec;
    o.editor.semantic.useClangd = false;
    o.editor.semantic.persistFastIndex = false;
    o.editor.vcs.enabled = false;
    o.watchSettings = false;
    o.defaultFile = "";
    o.report = [this](const std::string& s) { reports.push_back(s); };
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
    return o;
  }
  bool open(const fs::path& target, const fs::path& file = {}) {
    if (!host) host = std::make_unique<AppHost>(options());
    std::string err;
    const bool ok = host->open(target, file, true, &err);
    if (!ok) reports.push_back(err);
    frame();
    return ok;
  }
  Editor& ed() { return *host->editor(); }
  EditorApp& app() { return *host->app(); }
  Workbench& bench() { return *host->bench(); }
  void frame() {
    host->editor()->pump();
    host->app()->tick(now);
    host->bench()->render(rec, 1000, 700);
  }
  void key(Key k, uint8_t mods = 0) {
    now += 10;
    host->app()->handleEvent(keyEv(k, mods), now);
    frame();
  }
  void drop(const fs::path& p) {
    now += 10;
    host->app()->handleEvent(dropEv(p), now);
    frame();
  }
  bool apply() {
    const bool r = host->applyPending();
    if (host->app()) frame();
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
  std::vector<std::string> tabTitles() {
    std::vector<std::string> out;
    for (Panel* p : bench().panels())
      for (const auto& t : p->tabs().tabs()) out.push_back(t.title);
    return out;
  }
  std::string treeRoot() {
    app().explorer()->show(false);
    frame();
    return app().explorer()->view()->model().node(0).name;
  }
};

const fs::path kNone;

}  // namespace

TEST(slnopen_filter_pattern_converts_visual_studio_patterns_to_sdl) {
  CHECK_EQ(sdlFilterPattern("*.sln;*.slnx;*.vcxproj"), std::string("sln;slnx;vcxproj"));
  CHECK_EQ(sdlFilterPattern("*.*"), std::string("*"));
  CHECK_EQ(sdlFilterPattern("*"), std::string("*"));
  CHECK_EQ(sdlFilterPattern("*.sln;*.*"), std::string("*"));
  CHECK_EQ(sdlFilterPattern(""), std::string("*"));
}

TEST(slnopen_command_line_sln_path_roots_the_workspace_at_its_folder) {
  Env env;
  write(env.game / ".tenfold" / "settings.txt", "editor.wordWrap = false\n");
  write(env.game / "src" / "main.cpp", "int main() {}\n");
  REQUIRE(env.open(env.game / "Beta.sln", "src/main.cpp"));
  CHECK_EQ(env.ed().workspace().root(), env.game);
  // The workspace's own settings were read (they sit in <folder>/.tenfold, not next to the .sln's name).
  CHECK(!env.host->settings()->settings().getBool("editor.wordWrap"));
  // The file argument is relative to the folder, not to "Beta.sln\".
  REQUIRE(env.ed().currentDocument() != nullptr);
  CHECK_EQ(env.ed().currentDocument()->path(), env.game / "src" / "main.cpp");
  CHECK_EQ(env.host->target().solution, env.game / "Beta.sln");
}

TEST(slnopen_slnx_path_is_treated_like_sln) {
  Env env;
  write(env.game / "Delta.slnx", "<Solution />\n");
  REQUIRE(env.open(env.game / "Delta.slnx"));
  CHECK_EQ(env.ed().workspace().root(), env.game);
  CHECK_EQ(env.ed().workspace().chosenSolution(), env.game / "Delta.slnx");
  CHECK(slurp(env.game / ".tenfold" / "workspace.json").find("\"solution\": \"Delta.slnx\"") != std::string::npos);
}

TEST(slnopen_the_chosen_solution_drives_flags_tree_and_build_with_two_solutions_in_a_folder) {
  Env env;
  REQUIRE(env.open(env.game / "Beta.sln"));
  CHECK(env.ed().workspace().solutionFiles() == std::vector<fs::path>{env.game / "Beta.sln"});
  CHECK_EQ(env.ed().compileCommandInfo(env.game / "src" / "Beta.cpp").solution, env.game / "Beta.sln");
  bool beta = false, alpha = false;
  for (const CompileCommand& c : *env.ed().semantic().compileCommands())
    for (const std::string& a : c.arguments) {
      beta = beta || a.find("Beta_DEF") != std::string::npos;
      alpha = alpha || a.find("Alpha_DEF") != std::string::npos;
    }
  CHECK(beta);
  CHECK(!alpha);
  CHECK(env.treeRoot().find("Beta") != std::string::npos);
  BuildCommand c;
  std::string err;
  REQUIRE(detectBuildCommand(env.ed().workspace(), {}, &c, &err));
  CHECK_EQ(fs::path(c.args.at(0)).filename(), "Beta.sln");

  // Persisted: reopening the folder later (no solution named) uses it.
  env.host.reset();
  REQUIRE(env.open(env.game));
  CHECK_EQ(env.ed().workspace().chosenSolution(), env.game / "Beta.sln");
  CHECK(env.treeRoot().find("Beta") != std::string::npos);
}

TEST(slnopen_a_folder_with_several_solutions_and_no_saved_choice_keeps_todays_behaviour) {
  Env env;
  REQUIRE(env.open(env.game));
  CHECK_EQ(env.ed().workspace().solutionFiles().size(), size_t(2));  // both feed the flags
  CHECK(env.ed().workspace().chosenSolution().empty());
  CHECK(env.treeRoot().find("Alpha") != std::string::npos);  // the first drives the tree
  CHECK(!fs::exists(env.game / ".tenfold" / "workspace.json") ||
        slurp(env.game / ".tenfold" / "workspace.json").find("\"solution\"") == std::string::npos);
}

TEST(slnopen_ctrl_shift_o_shows_the_dialog_like_visual_studio_and_opens_the_answer) {
  Env env;
  REQUIRE(env.open(env.game / "Alpha.sln"));
  const int gen = env.host->generation();
  env.answer.clear();  // cancelled
  env.key(static_cast<Key>('O'), ModCtrl | ModShift);
  REQUIRE(env.dialogs.size() == 1);
  CHECK(!env.dialogs[0].folder);
  CHECK_EQ(env.dialogs[0].startDir, env.game);  // starts in the open solution's folder
  REQUIRE(env.dialogs[0].filters.size() == 2);
  CHECK_EQ(env.dialogs[0].filters[0].name, std::string("All Project Files"));
  CHECK_EQ(env.dialogs[0].filters[0].pattern, std::string("*.sln;*.slnx;*.vcxproj"));
  CHECK_EQ(env.dialogs[0].filters[1].name, std::string("All Files"));
  CHECK(!env.host->hasPending());  // cancel: nothing happens
  CHECK_EQ(env.host->generation(), gen);

  env.answer = {env.other / "Gamma.sln"};
  env.key(static_cast<Key>('O'), ModCtrl | ModShift);
  CHECK_EQ(env.dialogs.size(), size_t(2));
  CHECK(env.host->hasPending());  // the swap waits for the loop: it cannot happen inside the key handler
  CHECK_EQ(env.host->generation(), gen);
  REQUIRE(env.apply());
  CHECK_EQ(env.host->generation(), gen + 1);
  CHECK_EQ(env.ed().workspace().root(), env.other);
  CHECK_EQ(env.ed().workspace().chosenSolution(), env.other / "Gamma.sln");
  CHECK(env.host->app()->workspaceOpener().canOpen());  // the new EditorApp has its hooks
}

TEST(slnopen_open_solution_is_mapped_to_the_10x_OpenSolution_command_and_runs_from_the_palette_name) {
  Env env;
  REQUIRE(env.open(env.game / "Alpha.sln"));
  const Command* c = env.app().commands().find("File.OpenSolution");
  REQUIRE(c != nullptr);
  CHECK_EQ(c->name, std::string("Open Project/Solution..."));
  CHECK(env.app().commands().find("File.OpenFolder") != nullptr);
  CHECK(env.app().commands().find("File.RecentSolutions") != nullptr);
  env.answer = {env.other};  // Open Folder
  env.app().runCommand("File.OpenFolder");
  REQUIRE(env.dialogs.size() == 1);
  CHECK(env.dialogs[0].folder);
  CHECK(env.host->hasPending());
  REQUIRE(env.apply());
  CHECK_EQ(env.ed().workspace().root(), env.other);
}

TEST(slnopen_the_commands_are_unavailable_without_a_host) {
  // An EditorApp built directly (tests, ShellDemo) has no hooks: the commands report it instead of doing something.
  Env env;
  REQUIRE(env.open(env.game / "Alpha.sln"));
  env.app().workspaceOpener().setHooks({});
  env.app().runCommand("File.OpenSolution");
  CHECK(env.dialogs.empty());
}

TEST(slnopen_opening_what_is_already_open_does_nothing) {
  Env env;
  REQUIRE(env.open(env.game / "Alpha.sln"));
  CHECK(env.app().workspaceOpener().request(env.game / "Alpha.sln"));
  CHECK(!env.host->hasPending());
  CHECK(env.app().message().find("already open") != std::string::npos);
}

TEST(slnopen_switching_without_modified_documents_needs_no_prompt) {
  Env env;
  REQUIRE(env.open(env.game / "Alpha.sln"));
  env.openDoc(env.game / "src" / "Alpha.cpp");
  env.app().workspaceOpener().request(env.other / "Gamma.sln");
  CHECK(!env.app().unsavedPrompt().isOpen());
  CHECK(env.host->hasPending());
  REQUIRE(env.apply());
  CHECK_EQ(env.ed().workspace().root(), env.other);
  CHECK(env.ed().documents().empty() || env.ed().documents().size() == 0);
}

TEST(slnopen_switching_with_a_modified_document_asks_yes_no_cancel) {
  Env env;
  REQUIRE(env.open(env.game / "Alpha.sln"));
  const fs::path a = env.game / "src" / "Alpha.cpp";
  env.openDoc(a);
  env.edit();
  REQUIRE(env.ed().document(a)->dirty());

  // Cancel (Escape): the solution stays open, the document stays modified.
  env.app().workspaceOpener().request(env.other / "Gamma.sln");
  REQUIRE(env.app().unsavedPrompt().isOpen());
  CHECK(env.rows(env.app().unsavedPrompt().popup()) ==
        (std::vector<std::string>{"Save", "Don't Save", "Cancel"}));  // one document: the per-file prompt
  CHECK(!env.host->hasPending());
  env.key(Key::Escape);
  CHECK(!env.app().unsavedPrompt().isOpen());
  CHECK(!env.host->hasPending());
  CHECK(!env.apply());
  CHECK_EQ(env.ed().workspace().root(), env.game);
  CHECK(env.ed().document(a)->dirty());

  // Don't Save: the new solution opens, the file on disk is untouched.
  env.app().workspaceOpener().request(env.other / "Gamma.sln");
  env.key(Key::Down);
  env.key(Key::Enter);
  CHECK(env.host->hasPending());
  REQUIRE(env.apply());
  CHECK_EQ(env.ed().workspace().root(), env.other);
  CHECK_EQ(slurp(a), std::string("int Alpha_value;\n"));
}

TEST(slnopen_yes_saves_before_switching) {
  Env env;
  REQUIRE(env.open(env.game / "Alpha.sln"));
  const fs::path a = env.game / "src" / "Alpha.cpp";
  env.openDoc(a);
  env.edit();
  env.app().workspaceOpener().request(env.other / "Gamma.sln");
  REQUIRE(env.app().unsavedPrompt().isOpen());
  env.key(Key::Enter);  // Save
  CHECK(env.host->hasPending());
  REQUIRE(env.apply());
  CHECK_EQ(slurp(a), std::string("// edit\nint Alpha_value;\n"));
  CHECK_EQ(env.ed().workspace().root(), env.other);
}

TEST(slnopen_several_modified_documents_use_the_list_dialog) {
  Env env;
  REQUIRE(env.open(env.game / "Alpha.sln"));
  for (const char* n : {"Alpha", "Beta"}) {
    env.openDoc(env.game / "src" / (std::string(n) + ".cpp"));
    env.edit();
  }
  env.app().workspaceOpener().request(env.other / "Gamma.sln");
  REQUIRE(env.app().unsavedPrompt().isOpen());
  CHECK(env.rows(env.app().unsavedPrompt().popup()).size() == 5);  // two items + Yes / No / Cancel
  env.key(Key::Down);
  env.key(Key::Down);
  env.key(Key::Down);  // Cancel
  env.key(Key::Enter);
  CHECK(!env.host->hasPending());
  CHECK_EQ(env.ed().workspace().root(), env.game);
}

TEST(slnopen_setting_confirmUnsaved_off_skips_the_prompt_like_quit_does) {
  Env env;
  REQUIRE(env.open(env.game / "Alpha.sln"));
  Settings s;
  s.loadTexts("editor.confirmUnsaved = false\n", "");
  env.app().applySettings(s);
  env.openDoc(env.game / "src" / "Alpha.cpp");
  env.edit();
  env.app().workspaceOpener().request(env.other / "Gamma.sln");
  CHECK(!env.app().unsavedPrompt().isOpen());
  CHECK(env.host->hasPending());
}

TEST(slnopen_the_old_layout_is_saved_and_the_new_ones_is_restored) {
  Env env;
  REQUIRE(env.open(env.game / "Alpha.sln"));
  env.openDoc(env.game / "src" / "Alpha.cpp");
  env.openDoc(env.game / "src" / "Beta.cpp");
  env.app().workspaceOpener().request(env.other / "Gamma.sln");
  REQUIRE(env.apply());
  CHECK(fs::exists(env.game / ".tenfold" / "layout.json"));  // the one being left was written
  CHECK(slurp(env.game / ".tenfold" / "layout.json").find("Alpha.cpp") != std::string::npos);
  CHECK(env.tabTitles().empty());  // Gamma has none
  env.openDoc(env.other / "src" / "Gamma.cpp");
  env.app().workspaceOpener().request(env.game / "Alpha.sln");
  REQUIRE(env.apply());
  const auto titles = env.tabTitles();  // the first workspace's tabs are back
  CHECK(std::find(titles.begin(), titles.end(), "Alpha.cpp") != titles.end());
  CHECK(std::find(titles.begin(), titles.end(), "Beta.cpp") != titles.end());
  CHECK(std::find(titles.begin(), titles.end(), "Gamma.cpp") == titles.end());
  CHECK(fs::exists(env.other / ".tenfold" / "layout.json"));
}

TEST(slnopen_a_failed_switch_leaves_the_open_workspace_alone) {
  Env env;
  REQUIRE(env.open(env.game / "Alpha.sln"));
  const int gen = env.host->generation();
  env.openDoc(env.game / "src" / "Alpha.cpp");
  env.failText = true;  // the new workspace's font cannot be built
  env.app().workspaceOpener().request(env.other / "Gamma.sln");
  CHECK(!env.apply());
  CHECK_EQ(env.host->generation(), gen);  // nothing was torn down
  CHECK_EQ(env.ed().workspace().root(), env.game);
  CHECK(env.ed().document(env.game / "src" / "Alpha.cpp") != nullptr);
  CHECK(env.app().message().find("Cannot open") != std::string::npos);
  CHECK(env.host->recent().entries().size() == 1);  // and the failed target is not remembered
}

TEST(slnopen_recent_list_is_newest_first_deduplicated_capped_and_persisted) {
  Env env;
  const fs::path file = env.base / "rec" / "recent.txt";
  RecentSolutions r(file, 3);
  CHECK(!r.load());  // no file yet
  r.add(env.game / "Alpha.sln");
  r.add(env.game / "Beta.sln");
  r.add(env.other);
  r.add(env.game / "Alpha.sln");  // again: moves to the front
  CHECK(r.entries() == (std::vector<fs::path>{env.game / "Alpha.sln", env.other, env.game / "Beta.sln"}));
  r.add(env.other / "Gamma.sln");  // the oldest falls off at 3
  CHECK_EQ(r.entries().size(), size_t(3));
  CHECK(r.entries().back() == env.other);
  CHECK(r.save());
  RecentSolutions again(file, 3);
  CHECK(again.load());
  CHECK(again.entries() == r.entries());
  CHECK(r.drop(env.other));
  CHECK(!r.drop(env.other));
  r.setMax(100);
  CHECK_EQ(r.max(), RecentSolutions::kMaxMax);  // Visual Studio allows 1 to 24
  r.setMax(0);
  CHECK_EQ(r.max(), size_t(1));
  CHECK_EQ(r.entries().size(), size_t(1));
  CHECK_EQ(RecentSolutions(file).max(), RecentSolutions::kDefaultMax);  // default 10, like Visual Studio
}

TEST(slnopen_recent_list_ignores_damaged_lines_and_prunes_missing_entries) {
  Env env;
  const fs::path file = env.base / "recent.txt";
  write(file, "# comment\n\nrelative/path.sln\n" + pathUtf8(env.game / "Alpha.sln") + "\r\n" +
                  pathUtf8(env.game / "Alpha.sln") + "\n" + pathUtf8(env.base / "gone" / "Old.sln") + "\n");
  RecentSolutions r(file);
  REQUIRE(r.load());
  CHECK_EQ(r.entries().size(), size_t(2));  // the relative line and the duplicate are dropped
  CHECK(RecentSolutions::exists(r.entries()[0]));
  CHECK(!RecentSolutions::exists(r.entries()[1]));
  CHECK_EQ(r.pruneMissing(), size_t(1));
  CHECK_EQ(r.entries().size(), size_t(1));
  CHECK(!RecentSolutions(env.base / "no" / "file.txt").load());
}

TEST(slnopen_opening_adds_to_the_recent_list_newest_first_and_it_survives_a_restart) {
  Env env;
  REQUIRE(env.open(env.game / "Alpha.sln"));
  env.app().workspaceOpener().request(env.other / "Gamma.sln");
  REQUIRE(env.apply());
  CHECK(env.host->recent().entries() == (std::vector<fs::path>{env.other / "Gamma.sln", env.game / "Alpha.sln"}));
  env.host.reset();
  RecentSolutions r(defaultRecentFile(env.user));
  REQUIRE(r.load());
  CHECK(r.entries() == (std::vector<fs::path>{env.other / "Gamma.sln", env.game / "Alpha.sln"}));
  // A new AppHost reads it back.
  REQUIRE(env.open(env.game));
  CHECK(env.host->recent().entries().front() == env.game);
  CHECK_EQ(env.host->recent().entries().size(), size_t(3));
}

TEST(slnopen_the_default_workspace_is_not_remembered) {
  Env env;
  env.host = std::make_unique<AppHost>(env.options());
  std::string err;
  REQUIRE(env.host->open(env.game, {}, false, &err));
  CHECK(env.host->recent().entries().empty());
  CHECK(!fs::exists(defaultRecentFile(env.user)));
}

TEST(slnopen_recent_picker_lists_marks_missing_and_opens_the_choice) {
  Env env;
  write(env.base / "gone" / "Old.sln", "x");
  REQUIRE(env.open(env.base / "gone" / "Old.sln"));  // becomes the oldest entry once others are opened
  REQUIRE(env.open(env.game / "Alpha.sln"));
  fs::remove_all(env.base / "gone");
  env.app().workspaceOpener().request(env.other / "Gamma.sln");
  REQUIRE(env.apply());

  env.app().runCommand("File.RecentSolutions");
  ListPopup* p = env.app().workspaceOpener().popup();
  REQUIRE(p != nullptr);
  CHECK(env.rows(p) == (std::vector<std::string>{"Gamma.sln", "Alpha.sln", "Old.sln"}));
  CHECK_EQ(p->rows()[0].badge, std::string("open"));
  CHECK_EQ(p->rows()[2].badge, std::string("missing"));

  // Choosing a missing entry says so and drops it; nothing opens.
  env.key(Key::Down);
  env.key(Key::Down);
  env.key(Key::Enter);
  CHECK(!env.host->hasPending());
  CHECK(env.app().message().find("no longer exists") != std::string::npos);
  CHECK_EQ(env.host->recent().entries().size(), size_t(2));
  RecentSolutions onDisk(defaultRecentFile(env.user));
  onDisk.load();
  CHECK_EQ(onDisk.entries().size(), size_t(2));

  // Choosing a present one opens it (after the save prompt, if any).
  env.app().runCommand("File.RecentSolutions");
  env.key(Key::Down);
  env.key(Key::Enter);
  CHECK(env.host->hasPending());
  REQUIRE(env.apply());
  CHECK_EQ(env.ed().workspace().chosenSolution(), env.game / "Alpha.sln");
}

TEST(slnopen_recent_picker_filters_by_name_and_path) {
  Env env;
  REQUIRE(env.open(env.game / "Alpha.sln"));
  env.app().workspaceOpener().request(env.other / "Gamma.sln");
  REQUIRE(env.apply());
  env.app().runCommand("File.RecentSolutions");
  ListPopup* p = env.app().workspaceOpener().popup();
  REQUIRE(p != nullptr);
  for (char c : std::string("alph")) {
    InputEvent t;
    t.type = InputType::Text;
    t.text = std::string(1, c);
    env.app().handleEvent(t, ++env.now);
  }
  CHECK(env.rows(p) == (std::vector<std::string>{"Alpha.sln"}));
  env.key(Key::Escape);
  CHECK(env.app().workspaceOpener().popup() == nullptr);
}

TEST(slnopen_dropping_a_solution_opens_it_through_the_same_flow) {
  Env env;
  REQUIRE(env.open(env.game / "Alpha.sln"));
  env.drop(env.other / "Gamma.sln");
  CHECK(env.host->hasPending());
  REQUIRE(env.apply());
  CHECK_EQ(env.ed().workspace().chosenSolution(), env.other / "Gamma.sln");
  // ... .slnx too, and with a modified document the prompt comes first.
  write(env.other / "Eps.slnx", "<Solution />\n");
  write(env.other / "src" / "g.cpp", "int g;\n");
  env.openDoc(env.other / "src" / "g.cpp");
  env.edit();
  env.drop(env.other / "Eps.slnx");
  CHECK(env.app().unsavedPrompt().isOpen());
  CHECK(!env.host->hasPending());
  env.key(Key::Escape);
  CHECK(!env.host->hasPending());
  CHECK(env.ed().document(env.other / "src" / "g.cpp")->dirty());
}

TEST(slnopen_dropping_other_files_opens_them_as_documents_and_a_folder_opens_as_a_workspace) {
  Env env;
  REQUIRE(env.open(env.game / "Alpha.sln"));
  env.drop(env.game / "src" / "Beta.cpp");
  CHECK(!env.host->hasPending());
  REQUIRE(env.ed().currentDocument() != nullptr);
  CHECK_EQ(env.ed().currentDocument()->path(), env.game / "src" / "Beta.cpp");
  CHECK(std::find(env.tabTitles().begin(), env.tabTitles().end(), "Beta.cpp") != env.tabTitles().end());
  env.drop(env.game / "src" / "nosuchfile.cpp");  // a vanished file: a message, no crash
  CHECK(env.app().message().find("Cannot open") != std::string::npos);
  env.drop(env.other);
  CHECK(env.host->hasPending());
  REQUIRE(env.apply());
  CHECK_EQ(env.ed().workspace().root(), env.other);
  CHECK(env.ed().workspace().chosenSolution().empty());
}

TEST(slnopen_a_vcxproj_argument_opens_its_folder_without_choosing_a_solution) {
  Env env;
  REQUIRE(env.open(env.game / "Alpha.vcxproj"));
  CHECK_EQ(env.ed().workspace().root(), env.game);
  CHECK(env.ed().workspace().chosenSolution().empty());
}

TEST(slnopen_unsaved_changes_in_the_old_workspace_do_not_leak_into_the_new_one) {
  Env env;
  REQUIRE(env.open(env.game / "Alpha.sln"));
  env.openDoc(env.game / "src" / "Alpha.cpp");
  env.edit();
  env.app().workspaceOpener().request(env.other / "Gamma.sln");
  env.key(Key::Down);
  env.key(Key::Enter);
  REQUIRE(env.apply());
  for (Document* d : env.ed().documents()) CHECK(!d->dirty());
  CHECK(env.app().unsavedPrompt().isOpen() == false);
  env.app().requestQuit();
  CHECK(env.app().quitRequested());  // nothing modified: quits without asking
}

// ---------------------------------------------------------------------------------------------------------------
// Item 7: projects of the solution that live outside the solution folder (`..\Engine\Engine.vcxproj`). What tenfold
// does today is pinned here (docs/OPEN_QUESTIONS.md, SLN-OPEN item 7): the Solution Explorer tree and opening such a
// file work because both read the project files; the workspace scan, and with it Open File, find in files and symbol
// search, is folder-based and does not see them.
// ---------------------------------------------------------------------------------------------------------------
namespace {
void makeOutsideProjectSolution(Env& env) {
  makeSolution(env.base / "Engine", "Engine");
  write(env.base / "Engine" / "include" / "engine.h", "int engine_api();\n");
  write(env.game / "Ext.sln",
        "\nMicrosoft Visual Studio Solution File, Format Version 12.00\n"
        "Project(\"{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}\") = \"Alpha\", \"Alpha.vcxproj\", "
        "\"{CCCCCCCC-0000-0000-0000-000000000001}\"\nEndProject\n"
        "Project(\"{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}\") = \"Engine\", \"..\\Engine\\Engine.vcxproj\", "
        "\"{CCCCCCCC-0000-0000-0000-000000000002}\"\nEndProject\nGlobal\n"
        "\tGlobalSection(SolutionConfigurationPlatforms) = preSolution\n"
        "\t\tDebug|x64 = Debug|x64\n\tEndGlobalSection\n"
        "\tGlobalSection(ProjectConfigurationPlatforms) = postSolution\n"
        "\t\t{CCCCCCCC-0000-0000-0000-000000000001}.Debug|x64.ActiveCfg = Debug|x64\n"
        "\t\t{CCCCCCCC-0000-0000-0000-000000000001}.Debug|x64.Build.0 = Debug|x64\n"
        "\t\t{CCCCCCCC-0000-0000-0000-000000000002}.Debug|x64.ActiveCfg = Debug|x64\n"
        "\t\t{CCCCCCCC-0000-0000-0000-000000000002}.Debug|x64.Build.0 = Debug|x64\n"
        "\tEndGlobalSection\nEndGlobal\n");
}
}  // namespace

TEST(slnopen_item7_projects_outside_the_solution_folder_today) {
  Env env;
  makeOutsideProjectSolution(env);
  REQUIRE(env.open(env.game / "Ext.sln"));
  // The compile database covers the outside project: clangd gets its flags (and the fast index reads what it is told).
  bool engineInDb = false;
  for (const CompileCommand& c : *env.ed().semantic().compileCommands())
    engineInDb = engineInDb || pathKeyUtf8(c.file).find("/Engine/src/Engine.cpp") != std::string::npos;
  CHECK(engineInDb);
  // The tree lists the project and its files, read from the .vcxproj.
  TreeView& tv = *env.app().explorer()->view();
  env.app().explorer()->show(false);
  env.frame();
  tv.expandAll(true);
  env.frame();
  bool project = false, file = false;
  for (size_t i = 0; i < tv.rows().size(); ++i) {
    const TreeNode& n = tv.model().node(tv.rows()[i].node);
    project = project || n.name == "Engine";
    file = file || n.name == "Engine.cpp";
  }
  CHECK(project);
  CHECK(file);
  // It opens like any file ...
  CHECK(env.openDoc(env.base / "Engine" / "src" / "Engine.cpp") != nullptr);
  // ... but the workspace scan (folder based) does not list it: Open File, find in files and symbol search miss it.
  bool listed = false;
  for (const fs::path& p : env.ed().workspace().sourceFiles()) listed = listed || p.filename() == "Engine.cpp";
  CHECK(!listed);
}
