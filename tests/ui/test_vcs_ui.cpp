// C3c: version-control UI. Scripted tests on temporary git repositories (they skip without git) and on fake
// providers: change strip and scrollbar marks (also on wrapped lines), next / previous change, the inline diff block
// and its row reservation, revert hunk / file, the external diff tool (a fake tool script), status bar and tab
// markers, error reporting and refresh on focus. Mock font (8 px cells, 14 px lines) and a recording renderer; one
// golden (pinned DejaVuSansMono) and the keystroke-to-paint benchmark need GL / the font and skip without.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <thread>

#include "../text/text_test_util.h"
#include "test_vcs_util.h"
#include "ui/features/DiffTool.h"
#include "ui/features/VcsState.h"
#include "ui/gfx/PngWrite.h"
#include "ui/gfx/TextRenderer.h"
#include "ui/settings/Settings.h"
#include "ui/settings/Theme.h"
#include "ui/shell/EditorApp.h"
#include "ui/view/RowBlocks.h"
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

bool same(const gfx::Color& a, const gfx::Color& b) {
  return std::abs(a.r - b.r) < 1e-4f && std::abs(a.g - b.g) < 1e-4f && std::abs(a.b - b.b) < 1e-4f &&
         std::abs(a.a - b.a) < 1e-4f;
}

InputEvent keyEv(Key k, uint8_t mods = 0) {
  InputEvent e;
  e.type = InputType::KeyDown;
  e.key = k;
  e.mods = mods;
  return e;
}
InputEvent mouseEv(InputType t, float x, float y) {
  InputEvent e;
  e.type = t;
  e.x = x;
  e.y = y;
  e.clicks = 1;
  e.buttons = t == InputType::MouseUp ? 0 : 1;
  return e;
}

std::unique_ptr<gfx::TextRenderer> mockText() {
  gfx::FontStack stack;
  stack.add(std::make_shared<tftext::MockFont>(
      U"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ._-:,;/()[]{}<>+*&#?=!\"'@\xD7"));
  return gfx::TextRenderer::fromFonts(std::move(stack));
}

std::string numbered(int n, const char* prefix = "line ") {
  std::string s;
  for (int i = 0; i < n; ++i) s += prefix + std::to_string(i) + "\n";
  return s;
}

TextView* findView(Widget& w) {
  if (auto* v = dynamic_cast<TextView*>(&w)) return v;
  for (const auto& c : w.children())
    if (auto* r = findView(*c)) return r;
  return nullptr;
}

constexpr float kLh = 14, kCw = 8;
constexpr int kW = 1000, kH = 600;
constexpr int kWaitMs = 15000;

Theme testTheme() {
  Theme t = Theme::darkDefault();
  t.diffAdded = {0, 0.8f, 0, 1};
  t.diffModified = {0, 0, 0.8f, 1};
  t.diffDeleted = {0.8f, 0, 0, 1};
  t.diffBlockBackground = {0.3f, 0.1f, 0.1f, 1};
  t.tabVcsModified = {0.9f, 0.8f, 0.1f, 1};
  t.tabVcsCheckedOut = {0.9f, 0.1f, 0.9f, 1};
  t.tabVcsReadOnly = {0.5f, 0.6f, 0.7f, 1};
  return t;
}

// A committed repository (a.txt, b.txt) with the whole UI on it.
struct Env {
  tfvcs::TempRepo repo;
  std::unique_ptr<gfx::TextRenderer> text = mockText();
  Recorder rec;
  Theme theme = testTheme();
  std::unique_ptr<Editor> ed;
  std::unique_ptr<Workbench> bench;
  std::unique_ptr<EditorApp> app;
  fs::path a;
  int64_t now = 1000;

  explicit Env(const std::string& aText = numbered(30), bool wrap = true,
               std::function<void(Editor::Options&)> tweak = {}) {
    repo.write("a.txt", aText);
    repo.write("b.txt", "other\n");
    repo.commitAll("base");
    a = repo.path("a.txt");
    Editor::Options o;
    o.semantic.useClangd = false;
    o.semantic.persistFastIndex = false;
    o.watchForcePolling = true;
    o.watchPollMs = 60000;
    o.vcs.debounceMs = 40;
    o.vcs.statusDebounceMs = 100;
    if (tweak) tweak(o);
    ed = std::make_unique<Editor>(repo.dir, o);
    bench = std::make_unique<Workbench>(*text, *ed, theme);
    app = std::make_unique<EditorApp>(*bench, *ed, bench.get());
    app->vcsUi().diffTool().setTempRoot(repo.dir.parent_path() / (repo.dir.filename().string() + "_difftmp"));
    app->vcsUi().diffTool().setKeepMs(0);
    bench->openFile(a);
    bench->sync();
    if (!wrap) uitest::wrapOff(*bench);
    frame();
  }
  ~Env() {
    app.reset();
    bench.reset();
    ed.reset();
    std::error_code ec;
    fs::remove_all(repo.dir.parent_path() / (repo.dir.filename().string() + "_difftmp"), ec);
  }
  void frame() {
    ed->pump();
    app->tick(now);
    bench->render(rec, kW, kH);
  }
  bool send(const InputEvent& e) {
    now += 10;
    const bool r = app->handleEvent(e, now);
    frame();
    return r;
  }
  template <class F>
  bool until(F f, int ms = kWaitMs) {
    for (int i = 0; i < ms / 5; ++i) {
      frame();
      if (f()) return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
  }
  Document* doc() { return ed->document(a); }
  TextView* view() { return findView(*bench); }
  VcsManager& vcs() { return ed->vcs(); }
  bool waitBase() {
    return until([&] { return vcs().hasBase(a) && vcs().hunksVersion(a) != 0; });
  }
  bool waitHunks(size_t n) {
    return until([&] {
      return vcs().hunks(a).size() == n && vcs().hunksVersion(a) == doc()->version() && vcs().hunksVersion(a) != 0;
    });
  }
  void cmd(const char* id) {
    app->runCommand(id);
    frame();
  }
  void caret(int line, int col = 0) {
    ed->moveCursor({line, col});
    frame();
  }
  std::string msg() const { return app->message(); }
  std::string vcsText() { return bench->status().text("vcs"); }
  // The rects of one colour that lie in the change strip column of the view.
  std::vector<gfx::RectF> stripRects(const gfx::Color& c) {
    std::vector<gfx::RectF> out;
    const RectF s = view()->diffStripRect();
    for (const auto& r : rec.rects)
      if (same(r.c, c) && r.r.x >= s.x - 0.5f && r.r.x + r.r.w <= s.x + s.w + 3.5f) out.push_back(r.r);
    return out;
  }
  float lineY(int line) { return view()->caretRect({line, 0}).y; }
  // Keys the user would use to end a modal.
  void key(Key k) { send(keyEv(k)); }
};

#define SKIP_NO_GIT() SKIP_WITHOUT_GIT()

}  // namespace

// --- pure pieces ----------------------------------------------------------------------------------------------------

TEST(rowblocks_maps_wrap_rows_to_display_rows) {
  RowBlocks m;
  CHECK(m.empty());
  CHECK_EQ(m.display(7), 7);
  CHECK_EQ(m.wrapRow(7), 7);
  m.set({{5, 3}, {2, 2}});  // sorted: block 0 before wrap row 2 (2 rows), block 1 before wrap row 5 (3 rows)
  CHECK_EQ(m.size(), static_cast<size_t>(2));
  CHECK_EQ(m.extraRows(), 5);
  CHECK_EQ(m.top(0), 2);
  CHECK_EQ(m.top(1), 7);
  CHECK_EQ(m.display(1), 1);
  CHECK_EQ(m.display(2), 4);  // the block's two rows are above it
  CHECK_EQ(m.display(4), 6);
  CHECK_EQ(m.display(5), 10);
  int b = -2, off = -2;
  CHECK_EQ(m.wrapRow(1, &b), 1);
  CHECK_EQ(b, -1);
  CHECK_EQ(m.wrapRow(3, &b, &off), 2);  // inside block 0, second row
  CHECK_EQ(b, 0);
  CHECK_EQ(off, 1);
  CHECK_EQ(m.wrapRow(4, &b), 2);
  CHECK_EQ(b, -1);
  CHECK_EQ(m.wrapRow(9, &b, &off), 5);
  CHECK_EQ(b, 1);
  CHECK_EQ(off, 2);
  CHECK_EQ(m.wrapRow(10, &b), 5);
  CHECK_EQ(m.firstBlockFrom(0), static_cast<size_t>(0));
  CHECK_EQ(m.firstBlockFrom(4), static_cast<size_t>(1));
  CHECK_EQ(m.firstBlockFrom(10), static_cast<size_t>(2));
  for (int w = 0; w < 30; ++w) {  // display and wrapRow are inverse where the row is text
    int blk = -2;
    CHECK_EQ(m.wrapRow(m.display(w), &blk), w);
    CHECK_EQ(blk, -1);
  }
}

TEST(diff_tool_splits_arguments_like_a_command_line) {
  using V = std::vector<std::string>;
  CHECK(DiffTool::splitArgs("\"{base}\" \"{current}\"") == (V{"{base}", "{current}"}));
  CHECK(DiffTool::splitArgs("-x  \"a b\" c\"d e\"f  \"\"") == (V{"-x", "a b", "cd ef", ""}));
  CHECK(DiffTool::splitArgs("").empty());
  // A settings value wrapped in quotes loses the outer pair; the splitter gives it back.
  CHECK(DiffTool::splitArgs("{base}\" \"{current}") == (V{"{base}", "{current}"}));
  CHECK(DiffTool::splitArgs("--left {base} --right \"{current}\"") == (V{"--left", "{base}", "--right", "{current}"}));
  CHECK(DiffTool::isP4Merge("/opt/p4v/bin/p4merge"));
  CHECK(DiffTool::isP4Merge("C:\\Program Files\\Perforce\\P4Merge.EXE"));
  CHECK(!DiffTool::isP4Merge("/usr/bin/meld"));
}

#ifndef _WIN32
namespace {
void makeExecutable(const fs::path& p, const std::string& body) {
  tfvcs::writeFile(p, body);
  fs::permissions(p, fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec, fs::perm_options::add);
}
}  // namespace

TEST(diff_tool_auto_detects_p4merge_in_path) {
  const fs::path dir = tfvcs::tempDir("p4m"), empty = tfvcs::tempDir("nop4m");
  makeExecutable(dir / "p4merge", "#!/bin/sh\nexit 0\n");
  tfvcs::writeFile(dir / "notexec", "x");
  std::string err;
  CHECK_EQ(DiffTool::resolveTool("", empty.string() + ":" + dir.string(), "", &err), (dir / "p4merge").string());
  CHECK(DiffTool::resolveTool("", empty.string(), "", &err).empty());
  CHECK(err.find("no diff tool found") != std::string::npos);
  CHECK_EQ(DiffTool::resolveTool("p4merge", dir.string(), "", &err), (dir / "p4merge").string());
  CHECK(DiffTool::resolveTool("notexec", dir.string(), "", &err).empty());
  CHECK(err.find("not found in PATH") != std::string::npos);
  CHECK_EQ(DiffTool::resolveTool((dir / "p4merge").string(), "", "", &err), (dir / "p4merge").string());
  CHECK(DiffTool::resolveTool((dir / "missing").string(), "", "", &err).empty());
  std::error_code ec;
  fs::remove_all(dir, ec);
  fs::remove_all(empty, ec);
}
#endif

TEST(vcs_theme_keys_and_settings_defaults) {
  std::vector<SettingsDiag> d;
  const Theme vs = loadTheme("vs-dark", {}, {}, d);
  CHECK(d.empty());
  CHECK(vs.diffAdded.a > 0 && vs.diffModified.a > 0 && vs.diffDeleted.a > 0 && vs.diffBlockBackground.a > 0 &&
        vs.diffBlockText.a > 0 && vs.diffBlockHeader.a > 0 && vs.tabVcsModified.a > 0 && vs.tabVcsAdded.a > 0 &&
        vs.tabVcsCheckedOut.a > 0 && vs.tabVcsReadOnly.a > 0);
  CHECK(std::abs(vs.diffAdded.g - 0xA6 / 255.f) < 0.01f);
  // dark / light leave them unset and fall back to built-in colours.
  const Theme dark = loadTheme("dark", {}, {}, d);
  CHECK(dark.diffAdded.a == 0 && dark.diffBlockBackground.a == 0 && dark.tabVcsModified.a == 0);
  CHECK(dark.vcsAdded().a > 0 && dark.vcsModified().a > 0 && dark.vcsDeleted().a > 0);
  Settings s;
  CHECK_EQ(s.getString("vcs.diffTool"), std::string());
  CHECK_EQ(s.getString("vcs.diffToolArgs"), std::string("\"{base}\" \"{current}\""));
  CHECK(s.diagnostics().empty());
}

// --- change strip
// -----------------------------------------------------------------------------------------------------

TEST(vcs_strip_marks_added_modified_deleted_at_their_rows) {
  SKIP_NO_GIT();
  Env env(numbered(30), false);
  REQUIRE(env.waitBase());
  CHECK(env.view()->diffStripActive());
  CHECK(env.stripRects(env.theme.diffModified).empty());  // nothing changed yet
  Document* d = env.doc();
  d->replace({{2, 0}, {2, 6}}, "changed");  // line 2 modified
  d->insert({6, 0}, "inserted\n");          // a new line 6
  d->erase({{10, 0}, {11, 0}});             // base line 9 deleted (it is line 10 now)
  REQUIRE(env.waitHunks(3));
  const auto hs = env.vcs().hunks(env.a);
  CHECK(hs[0].kind == HunkKind::Modified && hs[1].kind == HunkKind::Added && hs[2].kind == HunkKind::Deleted);
  env.frame();
  const RectF strip = env.view()->diffStripRect();
  // Between the numbers (end one cell before the 40 px gutter) and the text (44): 3 px wide at x = 35.
  CHECK_EQ(strip.x - env.view()->bounds().x, 35.0f);
  CHECK_EQ(strip.w, 3.0f);

  const auto mod = env.stripRects(env.theme.diffModified);
  REQUIRE(mod.size() == 1);
  CHECK_EQ(mod[0].y, env.lineY(2));
  CHECK_EQ(mod[0].h, kLh);
  CHECK_EQ(mod[0].w, 3.0f);
  const auto add = env.stripRects(env.theme.diffAdded);
  REQUIRE(add.size() == 1);
  CHECK_EQ(add[0].y, env.lineY(6));
  CHECK_EQ(add[0].h, kLh);
  // The deleted lines are a red wedge on the boundary above line hs[2].newStart: slices centred on that y.
  const auto del = env.stripRects(env.theme.diffDeleted);
  REQUIRE(del.size() == 7);
  const float boundary = env.lineY(hs[2].newStart);
  float lo = 1e9f, hi = -1e9f, widest = 0;
  for (const auto& r : del) {
    lo = std::min(lo, r.y);
    hi = std::max(hi, r.y + r.h);
    widest = std::max(widest, r.w);
  }
  CHECK_EQ(lo, boundary - 3);
  CHECK_EQ(hi, boundary + 4);
  CHECK_EQ(widest, 4.0f);
  // Marks in the scrollbar's change lane.
  const RectF sb = env.view()->scrollbar().bounds();
  int bar = 0;
  for (const auto& r : env.rec.rects)
    if (r.r.x >= sb.x && r.r.x < sb.x + sb.w && r.r.y >= sb.y && r.r.y < sb.y + sb.h &&
        (same(r.c, env.theme.diffModified) || same(r.c, env.theme.diffAdded) || same(r.c, env.theme.diffDeleted)))
      ++bar;
  CHECK(bar >= 3);
  // Reverting everything removes the marks again.
  while (d->undo()) {}
  REQUIRE(env.waitHunks(0));
  env.frame();
  CHECK(env.stripRects(env.theme.diffModified).empty() && env.stripRects(env.theme.diffAdded).empty());
}

TEST(vcs_strip_adds_no_width_and_marks_wrapped_lines_over_all_their_rows) {
  SKIP_NO_GIT();
  std::string text = numbered(12);
  text += std::string(330, 'w') + "\n" + numbered(4, "tail ");  // line 12 wraps into several rows
  Env env(text, true);
  REQUIRE(env.waitBase());
  const float textX = env.view()->caretRect({3, 0}).x;
  Document* d = env.doc();
  d->insert({12, 330}, "!");  // modify the long line
  REQUIRE(env.waitHunks(1));
  env.frame();
  CHECK_EQ(env.view()->caretRect({3, 0}).x, textX);  // text did not move when the strip appeared
  const int rows = env.view()->wrapLayout().rowsOf(12);
  REQUIRE(rows >= 3);
  const auto mod = env.stripRects(env.theme.diffModified);
  REQUIRE(mod.size() == 1);
  CHECK_EQ(mod[0].y, env.lineY(12));
  CHECK_EQ(mod[0].h, static_cast<float>(rows) * kLh);
  // A deletion right below the wrapped line puts the wedge under its LAST row.
  d->erase({{14, 0}, {15, 0}});  // "tail 1"; "tail 0" stays between the two changes
  REQUIRE(env.waitHunks(2));
  env.frame();
  const auto del = env.stripRects(env.theme.diffDeleted);
  REQUIRE(!del.empty());
  float lo = 1e9f;
  for (const auto& r : del) lo = std::min(lo, r.y);
  CHECK_EQ(lo + 3, env.lineY(14));
  CHECK_EQ(env.lineY(13), env.lineY(12) + static_cast<float>(rows) * kLh);
}

TEST(vcs_no_strip_without_repository) {
  tfvcs::TempRepo plain(false);
  tfvcs::writeFile(plain.dir / "x.txt", numbered(5));
  auto text = mockText();
  Editor::Options o;
  o.semantic.useClangd = false;
  o.semantic.persistFastIndex = false;
  o.watchForcePolling = true;
  o.watchPollMs = 60000;
  Editor ed(plain.dir, o);
  Workbench bench(*text, ed);
  EditorApp app(bench, ed, &bench);
  bench.openFile(plain.dir / "x.txt");
  bench.sync();
  Recorder rec;
  for (int i = 0; i < 20; ++i) {
    ed.pump();
    app.tick(100 + i);
    bench.render(rec, kW, kH);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  TextView* v = findView(bench);
  REQUIRE(v != nullptr);
  CHECK(!v->diffStripActive());
  CHECK(bench.status().text("vcs").empty());
  app.runCommand("Vcs.ToggleInlineDiff");
  CHECK(v->inlineBlocks().empty());
  CHECK(app.message().find("No version control") != std::string::npos);
}

// --- jumping
// ----------------------------------------------------------------------------------------------------------

TEST(vcs_next_and_previous_change_jump_through_navigation_history) {
  SKIP_NO_GIT();
  Env env(numbered(40), false);
  REQUIRE(env.waitBase());
  Document* d = env.doc();
  d->replace({{3, 0}, {3, 6}}, "changed");
  d->insert({10, 0}, "inserted\n");
  d->erase({{20, 0}, {21, 0}});
  REQUIRE(env.waitHunks(3));
  const auto hs = env.vcs().hunks(env.a);
  env.caret(0);
  env.cmd("Vcs.NextChange");
  CHECK_EQ(env.ed->cursor().pos.line, hs[0].newStart);
  env.cmd("Vcs.NextChange");
  CHECK_EQ(env.ed->cursor().pos.line, hs[1].newStart);
  env.cmd("Vcs.NextChange");
  CHECK_EQ(env.ed->cursor().pos.line, hs[2].newStart);
  env.cmd("Vcs.NextChange");  // wraps
  CHECK_EQ(env.ed->cursor().pos.line, hs[0].newStart);
  CHECK(env.msg().find("Wrapped") != std::string::npos);
  env.cmd("Vcs.PrevChange");  // before the first: wraps to the last
  CHECK_EQ(env.ed->cursor().pos.line, hs[2].newStart);
  env.cmd("Vcs.PrevChange");
  CHECK_EQ(env.ed->cursor().pos.line, hs[1].newStart);
  CHECK(env.ed->navigation().canGoBack());  // the jumps are in the history (constraint 1.3)
  env.caret(15);                            // inside no hunk: next goes to the first one below, previous the one above
  env.cmd("Vcs.NextChange");
  CHECK_EQ(env.ed->cursor().pos.line, hs[2].newStart);
  env.caret(15);
  env.cmd("Vcs.PrevChange");
  CHECK_EQ(env.ed->cursor().pos.line, hs[1].newStart);
  // A file without changes says so.
  while (d->undo()) {}
  REQUIRE(env.waitHunks(0));
  env.cmd("Vcs.NextChange");
  CHECK(env.msg().find("No changes") != std::string::npos);
}

// --- inline diff block
// ------------------------------------------------------------------------------------------------

TEST(vcs_inline_block_shows_base_lines_reserves_rows_and_leaves_the_document_alone) {
  SKIP_NO_GIT();
  Env env(numbered(30), false);
  REQUIRE(env.waitBase());
  Document* d = env.doc();
  d->replace({{4, 0}, {5, 6}}, "five\nsix\nseven");  // base lines 4 and 5 become 3 lines (4..6)
  REQUIRE(env.waitHunks(1));
  const int version = d->version();
  const std::string textBefore = d->buffer().text();
  const float y7 = env.lineY(7);
  env.caret(5);
  env.cmd("Vcs.ToggleInlineDiff");
  TextView* v = env.view();
  REQUIRE(v->inlineBlocks().size() == 1);
  const auto info = v->inlineBlocks()[0];
  CHECK(info.lines == (std::vector<std::string>{"line 4", "line 5"}));
  CHECK_EQ(info.rows, 3);  // header + 2 base lines
  CHECK_EQ(info.anchorLine, 6);
  CHECK_EQ(d->version(), version);  // a view decoration: the Document never changed
  CHECK_EQ(d->buffer().text(), textBefore);
  CHECK_EQ(d->buffer().lineCount(), 32);
  // The block takes the rows right under the hunk; the text below moved down by exactly its rows.
  CHECK_EQ(info.rect.y, env.lineY(6) + kLh);
  CHECK_EQ(info.rect.h, 3 * kLh);
  CHECK_EQ(env.lineY(7), y7 + 3 * kLh);
  CHECK_EQ(env.lineY(7), info.rect.y + info.rect.h);
  CHECK(env.vcs().hunks(env.a).size() == 1);
  // Painted: the tint fills the block, the strip column carries the red mark beside the base lines.
  bool tint = false;
  for (const auto& r : env.rec.rects)
    tint |= same(r.c, env.theme.diffBlockBackground) && r.r.y == info.rect.y && r.r.h == info.rect.h;
  CHECK(tint);
  // Hit testing and the caret agree with the shifted rows.
  const RectF cr = v->caretRect({9, 0});
  CHECK_EQ(v->positionAt(cr.x + 1, cr.y + 2).line, 9);
  CHECK_EQ(v->positionAt(info.rect.x + 60, info.rect.y + kLh + 3).line, 6);  // inside the block: the line above
  // Toggling again closes it; the command at a line without a change says so.
  env.cmd("Vcs.ToggleInlineDiff");
  CHECK(v->inlineBlocks().empty());
  CHECK_EQ(env.lineY(7), y7);
  env.caret(20);
  env.cmd("Vcs.ToggleInlineDiff");
  CHECK(v->inlineBlocks().empty());
  CHECK(env.msg().find("No change at the caret") != std::string::npos);
}

TEST(vcs_inline_block_for_added_and_deleted_hunks) {
  SKIP_NO_GIT();
  Env env(numbered(30), false);
  REQUIRE(env.waitBase());
  Document* d = env.doc();
  d->insert({3, 0}, "new\n");
  d->erase({{12, 0}, {14, 0}});  // two lines deleted
  REQUIRE(env.waitHunks(2));
  TextView* v = env.view();
  env.caret(3);
  env.cmd("Vcs.ToggleInlineDiff");
  REQUIRE(v->inlineBlocks().size() == 1);
  CHECK(v->inlineBlocks()[0].lines.empty());  // nothing existed before
  CHECK_EQ(v->inlineBlocks()[0].rows, 1);
  CHECK_EQ(v->inlineBlocks()[0].anchorLine, 3);
  const int delStart = env.vcs().hunks(env.a)[1].newStart;
  env.caret(delStart);  // on the line below the wedge
  env.cmd("Vcs.ToggleInlineDiff");
  REQUIRE(v->inlineBlocks().size() == 2);
  const auto del = v->inlineBlocks()[1];
  CHECK(del.hunk.kind == HunkKind::Deleted);
  CHECK(del.lines.size() == 2);
  CHECK_EQ(del.anchorLine, delStart - 1);  // under the line above the boundary
  CHECK_EQ(del.rect.y, env.lineY(delStart - 1) + kLh);
}

TEST(vcs_inline_block_follows_edits_above_and_goes_when_its_hunk_changes) {
  SKIP_NO_GIT();
  Env env(numbered(30), false);
  REQUIRE(env.waitBase());
  Document* d = env.doc();
  d->replace({{10, 0}, {10, 7}}, "ten");
  REQUIRE(env.waitHunks(1));
  env.caret(10);
  env.cmd("Vcs.ToggleInlineDiff");
  TextView* v = env.view();
  REQUIRE(v->inlineBlocks().size() == 1);
  CHECK_EQ(v->inlineBlocks()[0].anchorLine, 10);
  // Two lines inserted at the top: the block moves with its hunk, at once and after the recomputed hunks arrived.
  d->insert({0, 0}, "top\ntwo\n");
  env.frame();
  REQUIRE(v->inlineBlocks().size() == 1);
  CHECK_EQ(v->inlineBlocks()[0].anchorLine, 12);
  CHECK_EQ(v->inlineBlocks()[0].rect.y, env.lineY(12) + kLh);
  REQUIRE(env.waitHunks(2));
  env.frame();
  REQUIRE(v->inlineBlocks().size() == 1);
  CHECK_EQ(v->inlineBlocks()[0].anchorLine, 12);
  CHECK(v->inlineBlocks()[0].lines == std::vector<std::string>{"line 10"});
  CHECK_EQ(v->inlineBlocks()[0].rect.y, env.lineY(12) + kLh);
  // Typing inside the hunk's line keeps it (same base lines, same size).
  d->insert({12, 3}, "!");
  REQUIRE(env.waitHunks(2));
  CHECK_EQ(v->inlineBlocks().size(), static_cast<size_t>(1));
  // Making the hunk bigger (an extra line) changes it: the block is gone.
  d->insert({12, 0}, "extra\n");
  REQUIRE(env.waitHunks(2));
  env.frame();
  CHECK(v->inlineBlocks().empty());
  CHECK_EQ(env.lineY(13), env.lineY(12) + kLh);  // and its rows are given back
}

TEST(vcs_inline_block_escape_closes_buttons_work_and_revert_is_one_undo_step) {
  SKIP_NO_GIT();
  Env env(numbered(30), false);
  REQUIRE(env.waitBase());
  Document* d = env.doc();
  d->replace({{4, 0}, {4, 6}}, "four");
  d->replace({{9, 0}, {9, 6}}, "nine");
  REQUIRE(env.waitHunks(2));
  TextView* v = env.view();
  env.caret(4, 2);
  env.cmd("Vcs.ToggleInlineDiff");
  env.caret(9, 1);
  env.cmd("Vcs.ToggleInlineDiff");
  REQUIRE(v->inlineBlocks().size() == 2);
  // Escape closes the blocks first and leaves the caret alone.
  env.key(Key::Escape);
  CHECK(v->inlineBlocks().empty());
  CHECK_EQ(env.ed->cursor().pos.line, 9);
  // Clicking the change strip opens a block, clicking its close button ends it.
  const RectF strip = v->diffStripRect();
  env.send(mouseEv(InputType::MouseDown, strip.x + 1, env.lineY(4) + 3));
  env.send(mouseEv(InputType::MouseUp, strip.x + 1, env.lineY(4) + 3));
  REQUIRE(v->inlineBlocks().size() == 1);
  CHECK_EQ(env.ed->cursor().pos.line, 9);  // the caret did not move
  RectF close = v->inlineBlocks()[0].closeButton;
  env.send(mouseEv(InputType::MouseDown, close.x + 2, close.y + 2));
  env.send(mouseEv(InputType::MouseUp, close.x + 2, close.y + 2));
  CHECK(v->inlineBlocks().empty());
  // "Revert hunk" restores the base lines in one undo step; the other change stays.
  env.send(mouseEv(InputType::MouseDown, strip.x + 1, env.lineY(4) + 3));
  env.send(mouseEv(InputType::MouseUp, strip.x + 1, env.lineY(4) + 3));
  REQUIRE(v->inlineBlocks().size() == 1);
  const RectF rv = v->inlineBlocks()[0].revertButton;
  const int undoDepthProbe = d->version();
  env.send(mouseEv(InputType::MouseDown, rv.x + 4, rv.y + 2));
  env.send(mouseEv(InputType::MouseUp, rv.x + 4, rv.y + 2));
  CHECK_EQ(d->buffer().line(4), std::string("line 4"));
  CHECK_EQ(d->buffer().line(9), std::string("nine"));
  CHECK(d->version() > undoDepthProbe);
  CHECK(v->inlineBlocks().empty());
  REQUIRE(env.waitHunks(1));
  env.ed->undo();  // ONE undo brings the change back
  CHECK_EQ(d->buffer().line(4), std::string("four"));
  CHECK_EQ(d->buffer().line(9), std::string("nine"));
  REQUIRE(env.waitHunks(2));
}

TEST(vcs_inline_block_with_wrapped_lines_keeps_rows_consistent) {
  SKIP_NO_GIT();
  std::string text = numbered(6);
  for (int i = 0; i < 3; ++i) text += std::string(300, static_cast<char>('a' + i)) + "\n";  // lines 6..8 wrap
  text += numbered(10, "end ");
  Env env(text, true);
  REQUIRE(env.waitBase());
  Document* d = env.doc();
  d->replace({{2, 0}, {2, 6}}, "two");
  REQUIRE(env.waitHunks(1));
  TextView* v = env.view();
  const int rows6 = v->wrapLayout().rowsOf(6), rows7 = v->wrapLayout().rowsOf(7);
  REQUIRE(rows6 >= 3);
  const float y8 = env.lineY(8), y7 = env.lineY(7);
  env.caret(2);
  env.cmd("Vcs.ToggleInlineDiff");
  REQUIRE(v->inlineBlocks().size() == 1);
  const float shift = static_cast<float>(v->inlineBlocks()[0].rows) * kLh;
  CHECK_EQ(env.lineY(7), y7 + shift);
  CHECK_EQ(env.lineY(8), y8 + shift);
  CHECK_EQ(env.lineY(8), env.lineY(7) + static_cast<float>(rows7) * kLh);
  // Every row of the wrapped lines still hits its own line.
  for (int line : {5, 6, 7, 8, 12}) {
    const RectF cr = v->caretRect({line, 0});
    CHECK_EQ(v->positionAt(cr.x + 1, cr.y + 2).line, line);
  }
  // The scrollbar counts the block's rows too: scrolling to the end shows the last line.
  InputEvent wheel;
  wheel.type = InputType::Wheel;
  wheel.x = 400;
  wheel.y = 300;
  wheel.dy = -50;
  env.send(wheel);
  env.frame();
  CHECK(env.lineY(env.doc()->buffer().lineCount() - 1) < static_cast<float>(kH));
}

// --- revert
// -----------------------------------------------------------------------------------------------------------

TEST(vcs_revert_hunk_command_is_one_undo_step) {
  SKIP_NO_GIT();
  Env env(numbered(30), false);
  REQUIRE(env.waitBase());
  Document* d = env.doc();
  d->replace({{2, 0}, {3, 6}}, "x\ny\nz");  // base lines 2..3 -> three lines
  d->replace({{12, 0}, {12, 7}}, "other");
  REQUIRE(env.waitHunks(2));
  env.caret(3);
  env.cmd("Vcs.RevertHunk");
  CHECK_EQ(d->buffer().line(2), std::string("line 2"));
  CHECK_EQ(d->buffer().line(3), std::string("line 3"));
  CHECK_EQ(d->buffer().lineCount(), 31);
  CHECK(env.ed->undo());
  CHECK_EQ(d->buffer().line(2), std::string("x"));
  CHECK_EQ(d->buffer().line(12), std::string("other"));
  env.caret(20);
  env.cmd("Vcs.RevertHunk");
  CHECK(env.msg().find("No change at the caret") != std::string::npos);
}

TEST(vcs_revert_file_asks_first_and_reloads_from_disk) {
  SKIP_NO_GIT();
  Env env(numbered(30), false);
  REQUIRE(env.waitBase());
  Document* d = env.doc();
  env.cmd("Vcs.RevertFile");
  CHECK(env.app->vcsUi().confirm() == nullptr);  // nothing to revert
  CHECK(env.msg().find("No changes to revert") != std::string::npos);
  d->replace({{2, 0}, {2, 6}}, "changed");
  std::string err;
  REQUIRE(env.ed->save(*d, &err));  // the change is on disk too
  d->insert({0, 0}, "unsaved ");
  REQUIRE(env.waitHunks(2));
  const std::string edited = d->buffer().text();
  // Cancel keeps everything.
  env.cmd("Vcs.RevertFile");
  ListPopup* p = env.app->vcsUi().confirm();
  REQUIRE(p != nullptr);
  CHECK(p->rows().size() == 2);
  CHECK(p->rows()[0].primary.find("Discard") != std::string::npos);
  CHECK(env.bench->modal() == p);
  env.key(Key::Escape);
  CHECK(env.app->vcsUi().confirm() == nullptr);
  CHECK_EQ(d->buffer().text(), edited);
  // Down + Enter on "Cancel" keeps it too.
  env.cmd("Vcs.RevertFile");
  REQUIRE(env.app->vcsUi().confirm() != nullptr);
  env.key(Key::Down);
  env.key(Key::Enter);
  CHECK_EQ(d->buffer().text(), edited);
  // Enter on the first row reverts the file: disk and buffer go back to the committed text.
  env.cmd("Vcs.RevertFile");
  REQUIRE(env.app->vcsUi().confirm() != nullptr);
  env.key(Key::Enter);
  CHECK(env.app->vcsUi().confirm() == nullptr);
  REQUIRE(env.until([&] { return d->buffer().text() == numbered(30); }));
  CHECK(!d->dirty());
  CHECK_EQ(env.repo.read("a.txt"), numbered(30));
  REQUIRE(env.waitHunks(0));
  REQUIRE(env.until([&] { return env.msg().find("Reverted") != std::string::npos; }));
}

// --- external diff tool
// -----------------------------------------------------------------------------------------------

#ifndef _WIN32
namespace {

// A tool that records its arguments and copies the base / current files it is given.
std::string recordingTool(const fs::path& out) {
  return "#!/bin/sh\nout='" + out.string() +
         "'\nprintf '%s\\n' \"$@\" > \"$out/args.txt\"\nfor a in \"$@\"; do case \"$a\" in */base/*) cp \"$a\" "
         "\"$out/base.copy\";; */current/*) cp \"$a\" \"$out/current.copy\";; esac; done\necho done > \"$out/done\"\n";
}

std::vector<std::string> readLines(const fs::path& p) {
  std::vector<std::string> v;
  std::ifstream in(p);
  for (std::string l; std::getline(in, l);) v.push_back(l);
  return v;
}

void configure(Env& env, const std::string& toolPath, const std::string& args = {}) {
  Settings s;
  std::string text = "vcs.diffTool = " + toolPath + "\n";
  if (!args.empty()) text += "vcs.diffToolArgs = " + args + "\n";
  s.loadTexts(text, "");
  CHECK(s.diagnostics().empty());
  env.app->applySettings(s);
}

}  // namespace

TEST(vcs_external_diff_launches_the_tool_with_temp_files) {
  SKIP_NO_GIT();
  Env env(numbered(10), false);
  REQUIRE(env.waitBase());
  const fs::path out = tfvcs::tempDir("difftool");
  makeExecutable(out / "fake-diff", recordingTool(out));
  configure(env, (out / "fake-diff").string());
  Document* d = env.doc();
  d->replace({{1, 0}, {1, 6}}, "edited");
  REQUIRE(env.waitHunks(1));
  env.cmd("Vcs.DiffFileExternal");
  REQUIRE(env.until([&] { return fs::exists(out / "done"); }));
  const auto args = readLines(out / "args.txt");
  REQUIRE(args.size() == 2);  // the default template: base, current
  CHECK(fs::path(args[0]).filename() == "a.txt" && args[0].find("/base/") != std::string::npos);
  CHECK(fs::path(args[1]).filename() == "a.txt" && args[1].find("/current/") != std::string::npos);
  CHECK_EQ(tfvcs::readFile(out / "base.copy"), numbered(10));
  std::string cur = numbered(10);
  cur.replace(cur.find("line 1"), 6, "edited");
  CHECK_EQ(tfvcs::readFile(out / "current.copy"), cur);
  CHECK(env.msg().find("fake-diff") != std::string::npos);
  // The temp files are removed once the tool is gone (keep time 0 in the test).
  REQUIRE(env.until([&] { return env.app->vcsUi().diffTool().running() == 0; }));
  CHECK(!fs::exists(args[0]) && !fs::exists(args[1]));
  std::error_code ec;
  fs::remove_all(out, ec);
}

TEST(vcs_external_diff_p4merge_gets_titles_and_custom_arguments_substitute) {
  SKIP_NO_GIT();
  Env env(numbered(10), false);
  REQUIRE(env.waitBase());
  Document* d = env.doc();
  d->replace({{1, 0}, {1, 6}}, "edited");  // unsaved
  REQUIRE(env.waitHunks(1));
  const fs::path out = tfvcs::tempDir("difftool2");
  makeExecutable(out / "p4merge", recordingTool(out));
  configure(env, (out / "p4merge").string());
  env.cmd("Vcs.DiffFileExternal");
  REQUIRE(env.until([&] { return fs::exists(out / "done"); }));
  auto args = readLines(out / "args.txt");
  REQUIRE(args.size() == 6);
  CHECK_EQ(args[0], std::string("-nl"));
  CHECK_EQ(args[1], std::string("a.txt@HEAD"));
  CHECK_EQ(args[2], std::string("-nr"));
  CHECK_EQ(args[3], std::string("a.txt (unsaved)"));
  CHECK(args[4].find("/base/a.txt") != std::string::npos && args[5].find("/current/a.txt") != std::string::npos);
  // Custom arguments: placeholders replaced after splitting, quotes keep a title with spaces together.
  fs::remove(out / "done");
  configure(env, (out / "p4merge").string(), "\"\"--left={base}\" --title \"{file} vs base\" {current}\"\"");
  env.cmd("Vcs.DiffFileExternal");
  REQUIRE(env.until([&] { return fs::exists(out / "done"); }));
  args = readLines(out / "args.txt");
  REQUIRE(args.size() == 4);
  CHECK(args[0].rfind("--left=", 0) == 0 && args[0].find("/base/a.txt") != std::string::npos);
  CHECK_EQ(args[1], std::string("--title"));
  CHECK_EQ(args[2], env.a.string() + " vs base");
  CHECK(args[3].find("/current/a.txt") != std::string::npos);
  std::error_code ec;
  fs::remove_all(out, ec);
}

TEST(vcs_external_diff_reports_a_missing_tool_in_the_status_bar) {
  SKIP_NO_GIT();
  Env env(numbered(10), false);
  REQUIRE(env.waitBase());
  configure(env, "no-such-diff-tool-xyz");
  env.cmd("Vcs.DiffFileExternal");
  CHECK(env.msg().find("no-such-diff-tool-xyz") != std::string::npos);
  CHECK(env.msg().find("not found") != std::string::npos);
  CHECK_EQ(env.app->vcsUi().diffTool().running(), static_cast<size_t>(0));
  // Nothing configured and nothing installed: the message names the remedy.
  configure(env, "");
  env.app->vcsUi().diffTool().setPathOverride("/nonexistent-dir");
  env.cmd("Vcs.DiffFileExternal");
  CHECK(env.msg().find("no diff tool found") != std::string::npos);
  CHECK(env.msg().find("vcs.diffTool") != std::string::npos);
}
#endif

// --- status bar, tab markers, errors, refresh
// -------------------------------------------------------------------------

TEST(vcs_status_segment_and_tab_marker_follow_the_file_state) {
  SKIP_NO_GIT();
  Env env(numbered(10), false);
  REQUIRE(env.waitBase());
  CHECK(env.vcsText().empty());  // clean: nothing to say
  Panel* panel = env.bench->panels()[0];
  const int tab = panel->tabs().tabs()[0].id;
  CHECK(panel->tabs().tabs()[0].vcs == TabStrip::VcsMark::None);
  CHECK(panel->tabs().vcsMarkerRect(tab).w == 0);
  const float plainW = panel->tabs().tabRect(tab).w;
  env.doc()->replace({{1, 0}, {1, 6}}, "edited");
  REQUIRE(env.until([&] { return env.vcsText() == "Git: modified"; }));
  CHECK(panel->tabs().tabs()[0].vcs == TabStrip::VcsMark::Modified);
  const RectF m = panel->tabs().vcsMarkerRect(tab);
  CHECK(m.w >= 4 && m.h >= 4);
  CHECK(panel->tabs().tabRect(tab).w > plainW);
  bool painted = false;
  for (const auto& r : env.rec.rects) painted |= same(r.c, env.theme.tabVcsModified) && r.r.x == m.x && r.r.y == m.y;
  CHECK(painted);
  // A new, untracked file is "added".
  env.repo.write("c.txt", "new\n");
  env.bench->openFile(env.repo.path("c.txt"));
  env.bench->sync();
  env.vcs().refreshStatus();
  REQUIRE(env.until([&] { return env.vcsText() == "Git: untracked"; }));
  // Undo to clean again.
  env.bench->openFile(env.a);
  env.bench->sync();
  while (env.doc()->undo()) {}
  REQUIRE(env.until([&] { return env.vcsText().empty(); }));
  CHECK(panel->tabs().tabs()[0].vcs == TabStrip::VcsMark::None);
}

namespace {

// A "Perforce" that only scripts the answers the UI needs.
struct P4State {
  std::atomic<int> status{static_cast<int>(VcsStatus::ReadOnlyNotCheckedOut)};
  std::atomic<int> edits{0};
  std::mutex mu;
  std::string failWith;
};

class FakeP4 : public VcsProvider {
 public:
  explicit FakeP4(std::shared_ptr<P4State> s) : s_(std::move(s)) {}
  std::string name() const override { return "perforce"; }
  bool detect(const fs::path& p) override {
    root_ = p;
    return true;
  }
  fs::path root() const override { return root_; }
  std::vector<VcsFileInfo> status(const std::vector<fs::path>& files) override {
    std::vector<VcsFileInfo> v;
    for (const auto& f : files) v.push_back({f, static_cast<VcsStatus>(s_->status.load()), {}});
    if (files.empty() && s_->status.load() != static_cast<int>(VcsStatus::Unmodified))
      v.push_back({root_ / "f.txt", static_cast<VcsStatus>(s_->status.load()), {}});
    return v;
  }
  std::optional<std::string> baseText(const fs::path&, std::string*) override { return std::string("a\nb\nc\n"); }
  bool revert(const fs::path&, std::string*) override { return true; }
  bool beforeFirstEdit(const fs::path&, std::string* err) override {
    ++s_->edits;
    std::lock_guard<std::mutex> lock(s_->mu);
    if (!s_->failWith.empty()) {
      if (err) *err = s_->failWith;
      return false;
    }
    s_->status = static_cast<int>(VcsStatus::CheckedOut);
    return true;
  }

 private:
  std::shared_ptr<P4State> s_;
  fs::path root_;
};

struct P4Env {
  fs::path dir = tfvcs::tempDir("p4ui");
  std::shared_ptr<P4State> st = std::make_shared<P4State>();
  std::unique_ptr<gfx::TextRenderer> text = mockText();
  Recorder rec;
  std::unique_ptr<Editor> ed;
  std::unique_ptr<Workbench> bench;
  std::unique_ptr<EditorApp> app;
  fs::path f;
  int64_t now = 1000;

  P4Env() {
    tfvcs::writeFile(dir / "f.txt", "a\nb\nc\n");
    f = dir / "f.txt";
    Editor::Options o;
    o.semantic.useClangd = false;
    o.semantic.persistFastIndex = false;
    o.watchForcePolling = true;
    o.watchPollMs = 60000;
    o.vcs.debounceMs = 30;
    auto s = st;
    o.vcs.providers = {[s] { return std::make_unique<FakeP4>(s); }};
    ed = std::make_unique<Editor>(dir, o);
    bench = std::make_unique<Workbench>(*text, *ed);
    app = std::make_unique<EditorApp>(*bench, *ed, bench.get());
    bench->openFile(f);
    bench->sync();
    frame();
  }
  ~P4Env() {
    app.reset();
    bench.reset();
    ed.reset();
    std::error_code ec;
    fs::remove_all(dir, ec);
  }
  void frame() {
    ed->pump();
    app->tick(now);
    bench->render(rec, kW, kH);
  }
  template <class F>
  bool until(F fn, int ms = kWaitMs) {
    for (int i = 0; i < ms / 5; ++i) {
      frame();
      if (fn()) return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
  }
  Document* doc() { return ed->document(f); }
  std::string seg() { return bench->status().text("vcs"); }
  TabStrip::VcsMark mark() { return bench->panels()[0]->tabs().tabs()[0].vcs; }
};

}  // namespace

TEST(vcs_perforce_states_explicit_checkout_and_error_reporting) {
  P4Env env;
  REQUIRE(env.until([&] { return !env.seg().empty(); }));
  CHECK_EQ(env.seg(), std::string("P4: read-only \xE2\x80\x94 will check out on edit"));
  CHECK(env.mark() == TabStrip::VcsMark::ReadOnly);
  // Vcs.CheckOut runs the provider's check-out path now, once, and the first edit does not repeat it.
  env.app->runCommand("Vcs.CheckOut");
  REQUIRE(env.until([&] { return env.seg() == "P4: checked out"; }));
  CHECK(env.mark() == TabStrip::VcsMark::CheckedOut);
  CHECK_EQ(env.st->edits.load(), 1);
  CHECK(env.app->message().find("Checked out f.txt") != std::string::npos);
  env.doc()->insert({0, 0}, "x");
  env.until([&] { return false; }, 300);
  CHECK_EQ(env.st->edits.load(), 1);
  // A failing check-out (auto-checkout on the first edit) surfaces in the status bar message.
  P4Env bad;
  {
    std::lock_guard<std::mutex> lock(bad.st->mu);
    bad.st->failWith = "p4 edit: //depot/f.txt - locked by alice@ws";
  }
  REQUIRE(bad.until([&] { return !bad.seg().empty(); }));
  bad.doc()->insert({0, 0}, "y");
  REQUIRE(bad.until([&] { return bad.app->message().find("locked by alice") != std::string::npos; }));
  CHECK(bad.app->message().find("Version control") != std::string::npos);
  CHECK(bad.doc()->buffer().line(0) == "ya");  // the edit is kept
  // An explicit check-out that fails is reported the same way, without a second "Checked out" message.
  bad.app->runCommand("Vcs.CheckOut");
  REQUIRE(bad.until([&] { return bad.st->edits.load() == 2; }));
  bad.until([&] { return false; }, 100);
  CHECK(bad.app->message().find("locked by alice") != std::string::npos);
}

TEST(vcs_refresh_status_command_and_window_focus) {
  SKIP_NO_GIT();
  Env env(numbered(10), false);
  REQUIRE(env.waitBase());
  const fs::path b = env.repo.path("b.txt");
  CHECK(env.vcs().fileStatus(b) == VcsStatus::Unmodified);
  env.repo.write("b.txt", "changed outside\n");  // the engine does not watch for this (polling watcher: 60 s)
  env.until([&] { return false; }, 300);
  CHECK(env.vcs().fileStatus(b) == VcsStatus::Unmodified);
  InputEvent focus;
  focus.type = InputType::FocusGained;
  env.send(focus);
  REQUIRE(env.until([&] { return env.vcs().fileStatus(b) == VcsStatus::Modified; }));
  // The command does the same.
  env.repo.git({"checkout", "--", "b.txt"});
  env.cmd("Vcs.RefreshStatus");
  CHECK(env.msg().find("Refreshing") != std::string::npos);
  REQUIRE(env.until([&] { return env.vcs().fileStatus(b) == VcsStatus::Unmodified; }));
}

// --- golden
// -----------------------------------------------------------------------------------------------------------

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

TEST(vcs_golden_vs_dark_marks_and_inline_block) {
  SKIP_NO_GIT();
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
  const fs::path tmp = uitest::tempRoot() / "tf_c3c_golden";  // the folder name is in the status bar: fixed
  std::error_code rm;
  fs::remove_all(tmp, rm);
  fs::create_directories(tmp / "src");
  tfvcs::TempRepo repo(false);
  fs::remove_all(repo.dir, rm);
  repo.dir = tmp;
  REQUIRE(repo.git({"init", "-q"}).ok());
  repo.git({"config", "user.name", "Tenfold Test"});
  repo.git({"config", "user.email", "test@example.invalid"});
  repo.git({"config", "commit.gpgsign", "false"});
  repo.git({"config", "core.autocrlf", "false"});
  const std::string committed =
      "#include <vector>\n#include <string>\n\nstruct Shape {\n  virtual double area() const = 0;\n  virtual ~Shape() "
      "{}\n"
      "};\n\ndouble total(const std::vector<Shape*>& v) {\n  double sum = 0;\n  for (Shape* s : v) {\n"
      "    sum += s->area();\n  }\n  return sum;\n}\n\nint main() {\n  std::vector<Shape*> shapes;\n"
      "  return (int)total(shapes);\n}\n";
  repo.write("src/shapes.cpp", committed);
  repo.commitAll("base");
  {
    Editor::Options o;
    o.semantic.useClangd = false;
    o.semantic.persistFastIndex = false;
    o.watchForcePolling = true;
    o.watchPollMs = 60000;
    o.vcs.debounceMs = 20;
    Editor ed(tmp, o);
    Workbench wb(*tr, ed, vs);
    wb.openFile("src/shapes.cpp");
    wb.sync();
    EditorApp app(wb, ed, &wb);
    uitest::wrapOff(wb);
    const fs::path file = tmp / "src" / "shapes.cpp";
    int64_t now = 1000;
    Recorder rec;
    auto frame = [&] {
      ed.pump();
      app.tick(now += 10);
      wb.render(rec, 800, 560);
    };
    auto until = [&](const std::function<bool()>& f) {
      for (int i = 0; i < 3000 && !f(); ++i) {
        frame();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
      return f();
    };
    REQUIRE(until([&] { return ed.vcs().hasBase(file) && ed.vcs().hunksVersion(file) != 0; }));
    Document* d = ed.document(file);
    REQUIRE(d != nullptr);
    d->erase({{17, 0}, {18, 0}});    // the deleted line (red wedge)
    d->replace({{10, 0}, {11, 21}},  // two lines modified
               "  for (const Shape* s : v) {\n    sum += s->area() * 2.0;");
    d->insert({8, 0}, "// Sum of all areas.\n");  // one added line
    REQUIRE(until([&] { return ed.vcs().hunks(file).size() == 3 && ed.vcs().hunksVersion(file) == d->version(); }));
    ed.moveCursor({11, 4});
    app.runCommand("Vcs.ToggleInlineDiff");
    frame();
    CHECK(findView(wb)->inlineBlocks().size() == 1);
    tr->releaseTexture(rec);  // the atlas texture id came from the recording renderer; never hand it to GL
    checkGolden(*fx->renderer, wb, *tr, "c3c_vcs_marks_inline_block.png", 800, 560);
  }
  fs::remove_all(tmp, rm);
}

// --- performance
// ------------------------------------------------------------------------------------------------------

// Keystroke-to-paint (budget 8 ms, §1.7) on the 100k-line file of the B2b perf scenario, with change marks, a scrollbar
// change lane and an inline diff block on screen: key -> Editor edit -> view sync -> layout / paint recording.
TEST(vcs_perf_keystroke_to_paint_with_marks_100k_lines) {
  SKIP_NO_GIT();
  std::string content;
  content.reserve(100000 * 60);
  for (int i = 0; i < 100000; ++i)
    content +=
        "    for (int k" + std::to_string(i) + " = 0; k < n_" + std::to_string(i % 97) + "; ++k) { total += k; }\n";
  const std::string font = monoFont();
  std::string err;
  auto tr = font.empty() ? nullptr : gfx::TextRenderer::create(font, 15.0f, 1.0f, &err);
  if (!tr) {
    std::cerr << "    SKIP: DejaVuSansMono not available (" << err << ")\n";
    return;
  }
  tfvcs::TempRepo repo;
  repo.write("big.cpp", content);
  repo.commitAll("base");
  Editor::Options o;
  o.semantic.useClangd = false;
  o.semantic.persistFastIndex = false;
  o.watchForcePolling = true;
  o.watchPollMs = 60000;
  Editor ed(repo.dir, o);
  Recorder rec;
  Workbench wb(*tr, ed);
  EditorApp app(wb, ed, &wb);
  const fs::path file = repo.path("big.cpp");
  wb.openFile(file);
  wb.sync();
  int64_t now = 1000;
  auto frame = [&] {
    ed.pump();
    app.tick(now += 10);
    wb.render(rec, 1920, 1080);
  };
  auto until = [&](const std::function<bool()>& f) {
    for (int i = 0; i < 6000 && !f(); ++i) {
      frame();
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return f();
  };
  REQUIRE(until([&] { return ed.vcs().hasBase(file) && ed.vcs().hunksVersion(file) != 0; }));
  Document* d = ed.document(file);
  for (int line : {3, 7, 40, 90000}) d->replace({{line, 4}, {line, 7}}, "while");
  d->insert({20, 0}, "added\n");
  REQUIRE(until([&] { return ed.vcs().hunks(file).size() == 5 && ed.vcs().hunksVersion(file) == d->version(); }));
  ed.moveCursor({7, 0});
  app.runCommand("Vcs.ToggleInlineDiff");
  frame();
  TextView* v = findView(wb);
  REQUIRE(v != nullptr);
  CHECK(v->inlineBlocks().size() == 1);
  ed.moveCursor({7, 20});
  frame();
  using clk = std::chrono::steady_clock;
  auto ms = [](clk::time_point a, clk::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
  };
  InputEvent ev;
  ev.type = InputType::Text;
  ev.text = "a";
  for (int i = 0; i < 20; ++i) {  // warm the glyph atlas and caches
    app.handleEvent(ev, now += 10);
    frame();
  }
  const int n = 400;
  std::vector<double> total;
  for (int i = 0; i < n; ++i) {
    const auto t0 = clk::now();
    app.handleEvent(ev, now += 10);  // key -> Editor::insertText -> view sync
    wb.requestLayout();
    wb.render(rec, 1920, 1080);  // layout + paint + recording
    total.push_back(ms(t0, clk::now()));
  }
  std::sort(total.begin(), total.end());
  double sum = 0;
  for (double t : total) sum += t;
  std::printf(
      "PERF: vcs marks, 100k lines, 5 hunks + inline block @1920x1080: keystroke-to-paint avg %.3f ms / p99 %.3f ms "
      "(budget 8 ms, n=%d)\n",
      sum / n, total[static_cast<size_t>(n * 0.99)], n);
#if defined(NDEBUG) && !defined(__SANITIZE_ADDRESS__)
  CHECK(sum / n < 8.0);
#endif
}
