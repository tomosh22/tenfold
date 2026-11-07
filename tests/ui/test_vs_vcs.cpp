// VS-VCS (VS_BEHAVIOUR 4 VC3, VC5, VC6): P4VS-style badges on explorer rows and tabs, the "Check out?" question of
// perforce.promptOnEdit and the action menu of PerforceHistory. Perforce is the fake p4 of tests/fake_p4_main.cpp
// (OPEN_QUESTIONS #17: there is no real server); Git only for the "Git is unchanged" check.
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#include "../test_w8f_p4util.h"
#include "../text/text_test_util.h"
#include "ui/features/VcsState.h"
#include "ui/gfx/TextRenderer.h"
#include "ui/settings/Settings.h"
#include "ui/settings/Theme.h"
#include "ui/shell/EditorApp.h"
#include "ui/widgets/ListPopup.h"
#include "ui/widgets/TreeView.h"
#include "ui/widgets/VcsBadge.h"
#include "ui_test.h"

using namespace tf;
using namespace tf::ui;
#define REQUIRE_EQ(a, b) REQUIRE((a) == (b))
namespace fs = std::filesystem;
using Mark = TabStrip::VcsMark;

namespace {

struct Recorder : gfx::Renderer {
  std::vector<std::pair<gfx::RectF, gfx::Color>> rects;
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
  size_t count(const gfx::Color& c) const {
    size_t n = 0;
    for (const auto& [r, col] : rects)
      if (std::abs(col.r - c.r) < 1e-4f && std::abs(col.g - c.g) < 1e-4f && std::abs(col.b - c.b) < 1e-4f &&
          std::abs(col.a - c.a) < 1e-4f)
        ++n;
    return n;
  }
};

InputEvent keyEv(Key k, uint8_t mods = 0) {
  InputEvent e;
  e.type = InputType::KeyDown;
  e.key = k;
  e.mods = mods;
  return e;
}

void setWritable(const fs::path& p, bool w) {
  std::error_code ec;
  const auto bits = fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write;
  fs::permissions(p, w ? fs::perms::owner_write : bits, w ? fs::perm_options::add : fs::perm_options::remove, ec);
}

#ifndef _WIN32
void makeExecutable(const fs::path& p, const std::string& text) {
  std::ofstream(p, std::ios::binary) << text;
  fs::permissions(p, fs::perms::owner_all, fs::perm_options::add);
}
#endif

// A Perforce workspace of the fake p4 with one file per badge, an editor and the app around it.
struct P4App {
  w8f::P4Ws p4;
  fs::path ws, tmp;
  std::unique_ptr<Editor> ed;
  std::unique_ptr<gfx::TextRenderer> text;
  Recorder rec;
  std::unique_ptr<Workbench> bench;
  std::unique_ptr<EditorApp> app;
  int64_t now = 1000;

  explicit P4App(bool badges = true) {
    PerforceProvider::clearDetectCache();
    ws = p4.ws;
    static int n = 0;
    tmp = uitest::tempRoot() / ("tf_vsvcs_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                                "_" + std::to_string(n++));
    fs::create_directories(tmp);
    p4.depotFile("a.cpp", "third\n");
    if (badges) {
      p4.depotFile("co.cpp", "co\n");
      p4.state["files"]["co.cpp"]["action"] = "edit";
      setWritable(ws / "co.cpp", true);
      p4.depotFile("old.cpp", "old\n");
      p4.state["files"]["old.cpp"]["headRev"] = 5;
      std::ofstream(ws / "add.cpp") << "add\n";
      p4.state["files"]["add.cpp"] = {{"inDepot", false}, {"action", "add"}, {"content", ""}};
      std::ofstream(ws / "new.cpp") << "new\n";
      p4.depotFile("res.cpp", "res\n");
      p4.state["files"]["res.cpp"]["action"] = "edit";
      p4.state["files"]["res.cpp"]["unresolved"] = true;
      setWritable(ws / "res.cpp", true);
    }
    p4.save();
    Editor::Options o;
    o.semantic.useClangd = false;
    o.semantic.persistFastIndex = false;
    o.watchForcePolling = true;
    o.watchPollMs = 60000;
    o.vcs.debounceMs = 20;
    o.vcs.provider = "perforce";
    o.vcs.p4Path = TENFOLD_FAKE_P4;
    ed = std::make_unique<Editor>(ws, o);
    ed->start();
    gfx::FontStack stack;
    stack.add(std::make_shared<tftext::MockFont>(
        U"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ._-:,;/()[]{}<>+*&#?=!\"'\\|", 8));
    text = gfx::TextRenderer::fromFonts(std::move(stack));
    bench = std::make_unique<Workbench>(*text, *ed);
    app = std::make_unique<EditorApp>(*bench, *ed, bench.get());
    app->perforceUi().setTempRoot(tmp / "p4");
    bench->openFile("a.cpp");
    bench->sync();
    frame();
  }
  ~P4App() {
    app.reset();
    bench.reset();
    ed.reset();
    std::error_code ec;
    fs::remove_all(tmp, ec);
  }
  void frame() {
    ed->pump();
    app->tick(now += 10);
    bench->render(rec, 1000, 600);
  }
  bool waitUntil(const std::function<bool()>& pred, int ms = 15000) {
    for (int waited = 0; waited < ms; waited += 5) {
      frame();
      if (pred()) return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return pred();
  }
  bool key(Key k, uint8_t mods = 0) {
    now += 10;
    const bool r = app->handleEvent(keyEv(k, mods), now);
    frame();
    return r;
  }
  Document* doc() { return ed->currentDocument(); }
  Mark treeMark(TreeView& tv, const char* name) {
    const int id = tv.model().findPath((ws / name).generic_string());
    return id < 0 ? Mark::None : tv.markOf(tv.model().node(id));
  }
  Mark tabMark() {
    for (Panel* p : bench->panels())
      for (const auto& t : p->tabs().tabs())
        if (t.id == p->tabs().activeId()) return t.vcs;
    return Mark::None;
  }
  size_t tabCount() {
    size_t n = 0;
    for (Panel* p : bench->panels()) n += p->tabs().tabs().size();
    return n;
  }
  void apply(const std::string& settings) {
    Settings s;
    s.loadTexts(settings, "");
    CHECK(s.diagnostics().empty());
    app->applySettings(s);
  }
};

}  // namespace

// ---- VC3: badges --------------------------------------------------------------------------------------------

TEST(vsvcs_explorer_marks_perforce_files_like_p4vs) {
  P4App a;
  REQUIRE(a.waitUntil([&] { return a.ed->vcs().active(); }));
  a.app->runCommand("View.ToggleWorkspaceExplorer");
  a.frame();
  TreeView& tv = *a.app->explorer()->view();
  tv.expandAll(true);
  a.frame();
  // Files that are only shown are asked for by the painter (VcsManager::probe), in one batch.
  REQUIRE(a.waitUntil([&] { return a.treeMark(tv, "new.cpp") == Mark::NotInDepot; }));
  REQUIRE(a.waitUntil([&] { return a.treeMark(tv, "old.cpp") == Mark::OutOfDate; }));
  CHECK(a.treeMark(tv, "co.cpp") == Mark::CheckedOut);
  CHECK(a.treeMark(tv, "add.cpp") == Mark::OpenedForAdd);
  CHECK(a.treeMark(tv, "res.cpp") == Mark::Conflict);
  CHECK(a.treeMark(tv, "a.cpp") == Mark::None);  // synced and read-only: P4VS gives it no badge
  // The glyphs are painted in the theme's colours: each badge adds rectangles of its colour.
  a.frame();
  const Theme& th = tv.theme();
  for (Mark m : {Mark::CheckedOut, Mark::OpenedForAdd, Mark::NotInDepot, Mark::OutOfDate, Mark::Conflict}) {
    CHECK(vcsMarkHasGlyph(m));
    CHECK(a.rec.count(vcsMarkColor(th, m)) > 0);
  }
  // Glyph shapes differ: a check, a plus, a question mark and a triangle do not paint the same number of cells.
  Recorder r;
  auto cells = [&](Mark m) {
    Recorder one;
    gfx::FontStack stack;
    stack.add(std::make_shared<tftext::MockFont>(U"a", 8));
    auto tr = gfx::TextRenderer::fromFonts(std::move(stack));
    PaintContext ctx{one, *tr};
    drawVcsGlyph(ctx, m, {0, 0, 10, 14}, gfx::Color{1, 1, 1, 1});
    return one.rects.size();
  };
  CHECK_EQ(cells(Mark::OpenedForAdd), size_t(9));
  CHECK_EQ(cells(Mark::OutOfDate), size_t(15));
  CHECK(cells(Mark::CheckedOut) != cells(Mark::NotInDepot));
}

TEST(vsvcs_tabs_carry_the_same_marks_and_status_text) {
  P4App a;
  REQUIRE(a.waitUntil([&] { return a.ed->vcs().active(); }));
  for (auto [file, mark] : std::vector<std::pair<const char*, Mark>>{{"new.cpp", Mark::NotInDepot},
                                                                      {"add.cpp", Mark::OpenedForAdd},
                                                                      {"old.cpp", Mark::OutOfDate},
                                                                      {"co.cpp", Mark::CheckedOut}}) {
    a.bench->openFile(file);
    a.bench->sync();
    CHECK(a.waitUntil([&] { return a.tabMark() == mark; }));
  }
  // the tab squares use the new theme keys (fallbacks without a theme: red / blue / yellow)
  const Theme& th = a.bench->theme();
  CHECK(vcsMarkColor(th, Mark::NotInDepot).b > vcsMarkColor(th, Mark::NotInDepot).r);
  CHECK(vcsMarkColor(th, Mark::OutOfDate).r > vcsMarkColor(th, Mark::OutOfDate).b);
}

TEST(vsvcs_git_marks_are_unchanged_untracked_stays_added) {
  SKIP_WITHOUT_GIT();
  tfvcs::TempRepo repo;
  repo.write("a.cpp", "int a;\n");
  repo.commitAll("base");
  repo.write("u.cpp", "int u;\n");  // untracked
  Editor::Options o;
  o.semantic.useClangd = false;
  o.vcs.debounceMs = 20;
  Editor ed(repo.dir, o);
  ed.start();
  gfx::FontStack stack;
  stack.add(std::make_shared<tftext::MockFont>(U"abcdefghijklmnopqrstuvwxyz0123456789 ._-:,;/", 8));
  auto tr = gfx::TextRenderer::fromFonts(std::move(stack));
  Recorder rec;
  Workbench wb(*tr, ed);
  EditorApp app(wb, ed, &wb);
  wb.openFile(repo.path("a.cpp"));
  wb.sync();
  bool ok = false;
  for (int i = 0; i < 3000 && !ok; ++i) {
    ed.pump();
    app.tick(1000 + i);
    ok = ed.vcs().fileStatus(repo.path("u.cpp")) == VcsStatus::Untracked;
    if (!ok) std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  REQUIRE(ok);
  CHECK(vcsMark(ed.vcs(), repo.path("u.cpp")) == Mark::Added);  // VC2: no "?" for Git (VS does not draw one)
  CHECK(vcsMark(ed.vcs(), repo.path("a.cpp")) == Mark::None);
  ed.vcs().probe(repo.path("a.cpp"));  // Perforce only: nothing happens
  CHECK(!vcsMarkHasGlyph(Mark::Added));
}

TEST(vsvcs_theme_keys_vs_dark_and_fallbacks) {
  std::vector<SettingsDiag> d;
  const Theme vs = loadTheme("vs-dark", {}, {}, d);
  CHECK(d.empty());
  CHECK(vs.tabVcsOpenedForAdd.a > 0 && vs.tabVcsNotInDepot.a > 0 && vs.tabVcsOutOfDate.a > 0 && vs.tabVcsConflict.a > 0);
  CHECK(vs.tabVcsNotInDepot.b > vs.tabVcsNotInDepot.r);  // blue ?
  CHECK(vs.tabVcsOutOfDate.r > vs.tabVcsOutOfDate.b);    // yellow triangle
  const Theme dark = loadTheme("dark", {}, {}, d);
  CHECK(dark.tabVcsNotInDepot.a == 0 && dark.tabVcsOutOfDate.a == 0);
  CHECK(vcsMarkColor(dark, Mark::NotInDepot).a > 0 && vcsMarkColor(dark, Mark::OutOfDate).a > 0 &&
        vcsMarkColor(dark, Mark::OpenedForAdd).a > 0 && vcsMarkColor(dark, Mark::Conflict).a > 0);
  Settings s;
  CHECK_EQ(s.getBool("perforce.promptOnEdit"), false);
  CHECK(s.diagnostics().empty());
}

// ---- VC5: the question -------------------------------------------------------------------------------------

TEST(vsvcs_prompt_on_edit_default_checks_out_silently) {
  P4App a(false);
  REQUIRE(a.waitUntil([&] { return a.ed->vcs().fileStatus(a.ws / "a.cpp") == VcsStatus::ReadOnlyNotCheckedOut; }));
  a.doc()->insert({0, 0}, "x");
  REQUIRE(a.waitUntil([&] { return a.ed->vcs().fileStatus(a.ws / "a.cpp") == VcsStatus::CheckedOut; }));
  CHECK(a.app->vcsUi().editPrompt() == nullptr);
}

TEST(vsvcs_prompt_on_edit_asks_and_check_out_runs_p4_edit) {
  P4App a(false);
  a.apply("perforce.promptOnEdit = true\n");
  REQUIRE(a.waitUntil([&] { return a.ed->vcs().fileStatus(a.ws / "a.cpp") == VcsStatus::ReadOnlyNotCheckedOut; }));
  a.doc()->insert({0, 0}, "x");
  REQUIRE(a.waitUntil([&] { return a.app->vcsUi().editPrompt() != nullptr; }));
  ListPopup* p = a.app->vcsUi().editPrompt();
  REQUIRE_EQ(p->rows().size(), size_t(3));
  CHECK_EQ(p->rows()[0].primary, std::string("Check out"));
  CHECK_EQ(p->rows()[2].primary, std::string("Do not check out"));
  a.waitUntil([] { return false; }, 200);
  CHECK(!a.p4.log().empty());  // fstat only
  for (const std::string& l : a.p4.log()) CHECK(l.find(" edit ") == std::string::npos);
  a.key(Key::Enter);  // "Check out" is the default row
  REQUIRE(a.waitUntil([&] { return a.ed->vcs().fileStatus(a.ws / "a.cpp") == VcsStatus::CheckedOut; }));
  CHECK(a.app->vcsUi().editPrompt() == nullptr);
  CHECK(a.doc()->buffer().line(0) == "xthird");
  a.doc()->insert({0, 0}, "y");  // asked once per file
  a.waitUntil([] { return false; }, 200);
  CHECK(a.app->vcsUi().editPrompt() == nullptr);
}

TEST(vsvcs_prompt_on_edit_escape_leaves_the_file_read_only_and_keeps_the_edit) {
  P4App a(false);
  a.apply("perforce.promptOnEdit = true\n");
  REQUIRE(a.waitUntil([&] { return a.ed->vcs().fileStatus(a.ws / "a.cpp") == VcsStatus::ReadOnlyNotCheckedOut; }));
  a.doc()->insert({0, 0}, "x");
  REQUIRE(a.waitUntil([&] { return a.app->vcsUi().editPrompt() != nullptr; }));
  a.key(Key::Escape);
  CHECK(a.app->vcsUi().editPrompt() == nullptr);
  a.waitUntil([] { return false; }, 300);
  CHECK(a.ed->vcs().fileStatus(a.ws / "a.cpp") == VcsStatus::ReadOnlyNotCheckedOut);
  CHECK(a.doc()->buffer().line(0) == "xthird");
  for (const std::string& l : a.p4.log()) CHECK(l.find(" edit ") == std::string::npos);
  // the explicit command still does it
  a.app->runCommand("Vcs.CheckOut");
  REQUIRE(a.waitUntil([&] { return a.ed->vcs().fileStatus(a.ws / "a.cpp") == VcsStatus::CheckedOut; }));
}

TEST(vsvcs_prompt_on_edit_do_not_ask_again_row_stops_the_questions) {
  P4App a(false);
  a.p4.depotFile("b.cpp", "b\n");
  a.p4.save();
  a.apply("perforce.promptOnEdit = true\n");
  a.bench->openFile("b.cpp");
  a.bench->sync();
  REQUIRE(a.waitUntil([&] { return a.ed->vcs().fileStatus(a.ws / "b.cpp") == VcsStatus::ReadOnlyNotCheckedOut; }));
  a.doc()->insert({0, 0}, "x");  // b.cpp is the current document
  REQUIRE(a.waitUntil([&] { return a.app->vcsUi().editPrompt() != nullptr; }));
  a.key(Key::Down);
  a.key(Key::Enter);  // "Check out, do not ask again"
  REQUIRE(a.waitUntil([&] { return a.ed->vcs().fileStatus(a.ws / "b.cpp") == VcsStatus::CheckedOut; }));
  CHECK(!a.ed->vcs().promptOnEdit());
  Document* d = a.ed->open(a.ws / "a.cpp");
  REQUIRE(d != nullptr);
  a.ed->activateDocument(*d);
  REQUIRE(a.waitUntil([&] { return a.ed->vcs().hasBase(a.ws / "a.cpp"); }));
  d->insert({0, 0}, "x");
  REQUIRE(a.waitUntil([&] { return a.ed->vcs().fileStatus(a.ws / "a.cpp") == VcsStatus::CheckedOut; }));
  CHECK(a.app->vcsUi().editPrompt() == nullptr);
}

// ---- VC6: the history action menu --------------------------------------------------------------------------

namespace {
void openHistory(P4App& a) {
  REQUIRE(a.waitUntil([&] { return a.ed->vcs().active(); }));
  a.key(Key('P'), ModCtrl);
  a.key(Key('H'), ModCtrl);
  REQUIRE(a.waitUntil([&] { return a.app->perforceUi().isOpen(); }));
}
}  // namespace

TEST(vsvcs_history_enter_opens_an_action_menu_open_is_first) {
  P4App a(false);
  openHistory(a);
  REQUIRE_EQ(a.app->perforceUi().popup()->rows().size(), size_t(3));
  a.key(Key::Down);  // #2
  a.key(Key::Enter);
  REQUIRE(a.app->perforceUi().actionMenu() != nullptr);
  ListPopup* m = a.app->perforceUi().actionMenu();
  REQUIRE_EQ(m->rows().size(), size_t(3));
  CHECK_EQ(m->rows()[0].primary, std::string("Open #2"));
  CHECK_EQ(m->rows()[1].primary, std::string("Diff against previous revision (#1)"));
  CHECK_EQ(m->rows()[2].primary, std::string("Diff against have revision (#3)"));
  a.key(Key::Escape);  // back to the list
  CHECK(a.app->perforceUi().actionMenu() == nullptr);
  CHECK(a.app->perforceUi().isOpen());
  a.key(Key::Enter);
  a.key(Key::Enter);  // Open
  REQUIRE(a.waitUntil([&] { return a.doc()->path().filename().string() == "a#2.cpp"; }));
  CHECK_EQ(a.doc()->buffer().text(), std::string("second\n"));
  CHECK(!a.app->perforceUi().isOpen());  // the list closed with the action
}

TEST(vsvcs_history_menu_omits_actions_that_make_no_sense) {
  P4App a(false);
  openHistory(a);
  a.key(Key::Enter);  // #3 is the have revision and the newest: no "against have"; "previous" is #2
  ListPopup* m = a.app->perforceUi().actionMenu();
  REQUIRE(m != nullptr);
  REQUIRE_EQ(m->rows().size(), size_t(2));
  CHECK_EQ(m->rows()[1].primary, std::string("Diff against previous revision (#2)"));
  a.key(Key::Escape);
  a.key(Key::Down);
  a.key(Key::Down);
  a.key(Key::Enter);  // #1 is the oldest: no "previous"
  m = a.app->perforceUi().actionMenu();
  REQUIRE(m != nullptr);
  REQUIRE_EQ(m->rows().size(), size_t(2));
  CHECK_EQ(m->rows()[0].primary, std::string("Open #1"));
  CHECK_EQ(m->rows()[1].primary, std::string("Diff against have revision (#3)"));
}

#ifndef _WIN32
TEST(vsvcs_history_diff_actions_run_the_diff_tool_on_the_two_revisions) {
  P4App a(false);
  const fs::path out = tfvcs::tempDir("vsvcs_difftool");
  makeExecutable(out / "fake-diff", "#!/bin/sh\nout='" + out.string() +
                                        "'\nprintf '%s\\n' \"$@\" > \"$out/args.txt\"\nfor a in \"$@\"; do case \"$a\" in "
                                        "*/base/*) cp \"$a\" \"$out/base.copy\";; */current/*) cp \"$a\" "
                                        "\"$out/current.copy\";; esac; done\necho done > \"$out/done\"\n");
  a.apply("vcs.diffTool = " + (out / "fake-diff").string() + "\n");
  openHistory(a);
  a.key(Key::Down);  // #2, against the previous one (#1)
  a.key(Key::Enter);
  a.key(Key::Down);
  a.key(Key::Enter);
  REQUIRE(a.waitUntil([&] { return fs::exists(out / "done"); }));
  CHECK_EQ(tfvcs::readFile(out / "base.copy"), std::string("first\n"));
  CHECK_EQ(tfvcs::readFile(out / "current.copy"), std::string("second\n"));
  CHECK(!a.app->perforceUi().isOpen());
  CHECK(a.app->message().find("fake-diff") != std::string::npos);
  CHECK_EQ(a.doc()->buffer().text(), std::string("third\n"));  // nothing was opened or changed
  CHECK_EQ(a.tabCount(), size_t(1));
  fs::remove(out / "done");

  // #1 against the have revision (#3)
  openHistory(a);
  a.key(Key::Down);
  a.key(Key::Down);
  a.key(Key::Enter);
  a.key(Key::Down);
  a.key(Key::Enter);
  REQUIRE(a.waitUntil([&] { return fs::exists(out / "done"); }));
  CHECK_EQ(tfvcs::readFile(out / "base.copy"), std::string("third\n"));
  CHECK_EQ(tfvcs::readFile(out / "current.copy"), std::string("first\n"));
  std::error_code ec;
  fs::remove_all(out, ec);
}
#endif
