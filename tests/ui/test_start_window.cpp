// START-WINDOW (docs/VS_BEHAVIOUR.md section 20): the Start window in the client area of the empty window:
//   * "Open recent": the recent list newest first, name + full path per row, missing entries kept and flagged,
//   * the "Search recent" filter (subsequence on the name, substring of the path), Escape clears it,
//   * Up / Down / Enter, a click, the context menu and Delete (remove from list, saved to recent.txt),
//   * a missing entry does not open: it is dropped with a message,
//   * "Get started": the two dialogs, an empty-list message, goldens.
// EmptyWindow with a mock font and a null renderer, plus one test through the real AppHost.
#include <algorithm>
#include <chrono>
#include <fstream>

#include "../text/text_test_util.h"
#include "core/PathUtil.h"
#include "ui/gfx/PngWrite.h"
#include "ui/gfx/TextRenderer.h"
#include "ui/settings/Theme.h"
#include "ui/shell/EmptyWindow.h"
#include "ui/widgets/ContextMenu.h"
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
InputEvent mouseEv(InputType t, float x, float y, MouseButton b = MouseButton::Left) {
  InputEvent e;
  e.type = t;
  e.x = x;
  e.y = y;
  e.button = b;
  e.clicks = 1;
  return e;
}

struct Fix {
  fs::path base, user, dirA, dirB, fileC, gone;
  NullRenderer rec;
  std::unique_ptr<gfx::TextRenderer> tr;
  std::unique_ptr<RecentSolutions> recent;
  std::unique_ptr<EmptyWindow> win;
  std::vector<fs::path> opened;
  std::vector<FileDialogRequest> dialogs;
  int64_t now = 1000;

  explicit Fix(bool populate = true) {
    static int n = 0;
    base = uitest::tempRoot() /
           ("tf_startwin_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_" +
            std::to_string(n++));
    user = base / "user";
    dirA = base / "Game";
    dirB = base / "Tools";
    fileC = dirA / "Alpha.sln";
    gone = base / "Old" / "Gone.sln";
    fs::create_directories(user);
    fs::create_directories(dirA);
    fs::create_directories(dirB);
    std::ofstream(fileC) << "\n";
    gfx::FontStack stack;
    stack.add(std::make_shared<tftext::MockFont>(
        U"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ._-:,;/()[]{}<>+*&#?=!\"'\\|", 8));
    tr = gfx::TextRenderer::fromFonts(std::move(stack));
    recent = std::make_unique<RecentSolutions>(defaultRecentFile(user));
    if (populate) {  // newest first after the adds: Alpha.sln, Tools, Gone.sln (missing)
      recent->add(gone);
      recent->add(dirB);
      recent->add(fileC);
      recent->save();
    }
    EmptyWindow::Hooks h;
    h.open = [this](const fs::path& p) { opened.push_back(p); };
    h.dialog = [this](const FileDialogRequest& r, FileDialogDone) { dialogs.push_back(r); };
    h.recent = recent.get();
    win = std::make_unique<EmptyWindow>(*tr, Theme::darkDefault(), user, std::move(h));
    frame();
  }
  ~Fix() {
    win.reset();
    std::error_code ec;
    fs::remove_all(base, ec);
  }
  StartWindow& sw() { return win->start(); }
  void frame() { win->render(rec, 1000, 700); }
  void key(Key k, uint8_t mods = 0) {
    now += 10;
    win->handleEvent(keyEv(k, mods), now);
    frame();
  }
  void type(const std::string& s) {
    for (char c : s) {
      now += 10;
      win->handleEvent(textEv(std::string(1, c)), now);
    }
    frame();
  }
  void mouse(InputType t, float x, float y, MouseButton b = MouseButton::Left) {
    now += 10;
    win->handleEvent(mouseEv(t, x, y, b), now);
    frame();
  }
  void click(const RectF& r, MouseButton b = MouseButton::Left) {
    mouse(InputType::MouseDown, r.x + r.w / 2, r.y + r.h / 2, b);
    mouse(InputType::MouseUp, r.x + r.w / 2, r.y + r.h / 2, b);
  }
  std::vector<std::string> names() {
    std::vector<std::string> v;
    for (const auto& r : sw().rows()) v.push_back(r.name);
    return v;
  }
  std::vector<fs::path> onDisk() const {
    RecentSolutions r(defaultRecentFile(user));
    r.load();
    return r.entries();
  }
};

}  // namespace

TEST(startwin_lists_recent_entries_newest_first_with_name_and_full_path) {
  Fix f;
  const auto& rows = f.sw().rows();
  REQUIRE(rows.size() == 3);
  CHECK_EQ(rows[0].name, std::string("Alpha.sln"));
  CHECK_EQ(rows[0].where, pathUtf8(f.fileC));
  CHECK(!rows[0].missing && !rows[0].folder);
  CHECK_EQ(rows[1].name, std::string("Tools"));
  CHECK(rows[1].folder && !rows[1].missing);
  CHECK_EQ(rows[2].name, std::string("Gone.sln"));
  CHECK(rows[2].missing);  // still listed, flagged
  CHECK_EQ(f.sw().selected(), 0);
  CHECK(f.win->message().empty());
}

TEST(startwin_the_list_is_the_one_file_recent_uses) {
  Fix f;
  f.recent->add(f.dirB);  // another window / an open moved it to the front
  f.frame();
  CHECK_EQ(f.names().front(), std::string("Tools"));
  CHECK(f.sw().rows().size() == 3);
}

TEST(startwin_an_empty_recent_list_shows_an_empty_state) {
  Fix f(false);
  CHECK(f.sw().rows().empty());
  CHECK_EQ(f.sw().selected(), -1);
  CHECK(f.sw().emptyText().find("No recent") == 0);
  f.key(Key::Enter);  // nothing to open
  f.key(Key::Down);
  f.key(Key::Delete);
  CHECK(f.opened.empty());
  CHECK(!f.win->quitRequested());
}

TEST(startwin_search_filters_by_name_and_path_and_escape_clears) {
  Fix f;
  f.type("tool");
  CHECK_EQ(f.names().size(), size_t(1));
  CHECK_EQ(f.names()[0], std::string("Tools"));
  CHECK(!f.sw().rows()[0].match.empty());
  f.key(Key::Escape);
  CHECK(f.sw().search()->text().empty());
  CHECK_EQ(f.names().size(), size_t(3));
  f.type("alsl");  // a subsequence of Alpha.sln
  CHECK_EQ(f.names().size(), size_t(1));
  CHECK_EQ(f.names()[0], std::string("Alpha.sln"));
  f.key(Key::Escape);
  f.type("old");  // the folder name of Gone.sln is in its path only ("Old")
  CHECK_EQ(f.names().size(), size_t(1));
  CHECK_EQ(f.names()[0], std::string("Gone.sln"));
  f.key(Key::Escape);
  f.type("zzzz");
  CHECK(f.sw().rows().empty());
  CHECK(f.sw().emptyText().find("zzzz") != std::string::npos);
  f.key(Key::Enter);
  CHECK(f.opened.empty());
}

TEST(startwin_enter_opens_the_selected_entry_and_up_down_move_it) {
  Fix f;
  f.key(Key::Down);
  CHECK_EQ(f.sw().selected(), 1);
  f.key(Key::Down);
  f.key(Key::Down);  // clamps at the last row
  CHECK_EQ(f.sw().selected(), 2);
  f.key(Key::Up);
  f.key(Key::Up);
  f.key(Key::Up);  // and at the first
  CHECK_EQ(f.sw().selected(), 0);
  f.key(Key::Down);
  f.key(Key::Enter);
  REQUIRE(f.opened.size() == 1);
  CHECK(f.opened[0] == f.dirB);
}

TEST(startwin_typing_keeps_the_keyboard_and_filtering_resets_the_selection) {
  Fix f;
  f.key(Key::Down);
  f.type("a");
  CHECK_EQ(f.sw().selected(), 0);
  f.key(Key::Enter);
  REQUIRE(f.opened.size() == 1);
  CHECK(f.opened[0] == f.fileC);  // newest entry matching "a"
}

TEST(startwin_a_click_on_a_row_opens_it) {
  Fix f;
  f.click(f.sw().rowRect(1));
  REQUIRE(f.opened.size() == 1);
  CHECK(f.opened[0] == f.dirB);
}

TEST(startwin_delete_removes_the_entry_and_saves_the_list) {
  Fix f;
  f.key(Key::Down);
  f.key(Key::Delete);  // Tools
  CHECK_EQ(f.names().size(), size_t(2));
  CHECK_EQ(f.names()[1], std::string("Gone.sln"));
  CHECK_EQ(f.sw().selected(), 1);  // the next entry took its place
  const auto disk = f.onDisk();
  REQUIRE(disk.size() == 2);
  CHECK(disk[0] == f.fileC && disk[1] == f.gone);
  f.key(Key::Delete);
  f.key(Key::Delete);  // the last one: selection moves up, nothing left to crash on
  f.key(Key::Delete);
  CHECK(f.sw().rows().empty());
  CHECK(f.onDisk().empty());
  CHECK(f.opened.empty());
}

TEST(startwin_delete_edits_the_search_text_while_the_caret_is_not_at_its_end) {
  Fix f;
  f.type("al");
  f.key(Key::Left);
  f.key(Key::Delete);  // deletes the "l", does not remove the entry
  CHECK_EQ(f.sw().search()->text(), std::string("a"));
  CHECK_EQ(f.recent->entries().size(), size_t(3));
}

TEST(startwin_the_context_menu_removes_from_the_list) {
  Fix f;
  const RectF r = f.sw().rowRect(1);
  f.mouse(InputType::MouseDown, r.x + 10, r.y + 5, MouseButton::Right);
  REQUIRE(f.win->modal() != nullptr);
  auto* menu = dynamic_cast<ContextMenu*>(f.win->modal());
  REQUIRE(menu != nullptr);
  REQUIRE(menu->items().size() == 2);
  CHECK_EQ(menu->items()[1].label, std::string("Remove from list"));
  f.key(Key::Down);
  f.key(Key::Down);
  f.key(Key::Enter);
  CHECK_EQ(f.names().size(), size_t(2));
  CHECK(!f.win->modal());
  for (const fs::path& p : f.onDisk()) CHECK(p != f.dirB);
  CHECK(f.opened.empty());
}

TEST(startwin_a_missing_entry_does_not_open_and_is_dropped_with_a_message) {
  Fix f;
  f.key(Key::Down);
  f.key(Key::Down);
  f.key(Key::Enter);  // Gone.sln
  CHECK(f.opened.empty());
  CHECK(f.win->message().find("no longer exists") != std::string::npos);
  CHECK(f.win->message().find("Gone.sln") != std::string::npos);
  CHECK_EQ(f.names().size(), size_t(2));
  CHECK_EQ(f.onDisk().size(), size_t(2));
}

TEST(startwin_get_started_actions_run_the_open_commands) {
  Fix f(false);
  f.click(f.sw().actionRect(0));
  REQUIRE(f.dialogs.size() == 1);
  CHECK(!f.dialogs[0].folder);
  f.click(f.sw().actionRect(1));
  REQUIRE(f.dialogs.size() == 2);
  CHECK(f.dialogs[1].folder);
}

TEST(startwin_has_no_clone_or_new_project_actions_and_the_menus_are_unchanged) {
  Fix f;
  CHECK_EQ(StartWindow::kActions, 2);
  CHECK(EmptyWindow::availableWithoutWorkspace("File.OpenSolution"));
  CHECK(!f.win->runCommand("Build.Build"));
}

TEST(startwin_layout_fills_the_area_between_the_menu_bar_and_the_status_bar) {
  Fix f;
  const RectF& b = f.sw().bounds();
  CHECK(b.y >= f.win->menuBar().bounds().y + f.win->menuBar().bounds().h - 0.5f);
  CHECK(b.y + b.h <= f.win->status().bounds().y + 0.5f);
  CHECK(f.sw().rowRect(0).y >= b.y && f.sw().rowRect(0).x >= b.x);
}

TEST(startwin_only_as_many_rows_as_recent_holds_and_many_entries_scroll) {
  Fix f(false);
  f.recent->setMax(24);
  for (int i = 0; i < 24; ++i) f.recent->add(f.base / ("proj" + std::to_string(i)) / "p.sln");
  f.frame();
  REQUIRE(f.sw().rows().size() == 24);
  for (int i = 0; i < 20; ++i) f.key(Key::Down);
  CHECK_EQ(f.sw().selected(), 20);
  const RectF r = f.sw().rowRect(20);
  CHECK(r.y + r.h <= f.sw().bounds().y + f.sw().bounds().h);  // scrolled into view
  f.key(Key::PageUp);
  CHECK(f.sw().selected() < 20);
}

// --- golden -------------------------------------------------------------------------------------------------------

namespace {
std::string monoFont() {
  for (const char* p : {"/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
                        "/usr/share/fonts/dejavu/DejaVuSansMono.ttf", "/usr/share/fonts/TTF/DejaVuSansMono.ttf"})
    if (std::ifstream(p).good()) return p;
  return {};
}
}  // namespace

TEST(startwin_golden_vs_dark_start_window_with_recent_entries) {
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
  const fs::path user = uitest::tempRoot() / "tf_startwin_golden_user";
  std::error_code rm;
  fs::remove_all(user, rm);
  fs::create_directories(user);
  const int w = 1024, h = 640;
  std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4);
  {
    // Fixed spellings, so the picture does not depend on the machine: /usr and /usr/share exist on Linux, the rest not.
    RecentSolutions recent(defaultRecentFile(user));
    recent.add("/home/dev/Tools/Tools.slnx");
    recent.add("/usr/share");
    recent.add("/home/dev/Game/Game.sln");
    EmptyWindow::Hooks hooks;
    hooks.open = [](const fs::path&) {};
    hooks.dialog = [](const FileDialogRequest&, FileDialogDone) {};
    hooks.recent = &recent;
    EmptyWindow win(*tr, vs, user, std::move(hooks));
    int64_t now = 1000;
    gfx::GlRenderer& r = *fx->renderer;
    r.setOffscreen(true);
    win.render(r, w, h);
    win.tick(now += 10);
    win.start().setSelected(1);
    win.render(r, w, h);
    REQUIRE(r.readPixels(w, h, px.data()));
    tr->releaseTexture(r);
  }
  fs::remove_all(user, rm);
  const std::string path = std::string(TENFOLD_SOURCE_DIR "/tests/golden/") + "start_window_recent_vs_dark.png";
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
    for (int ch = 0; ch < 4; ++ch) d = std::max(d, std::abs(int(golden[i + ch]) - int(px[i + ch])));
    if (d > 3) ++bad;
  }
  if (bad > golden.size() / 4 / 500) {
    gfx::writePngFile("start_window_recent_vs_dark.png.actual.png", px.data(), w, h);
    std::cerr << "    " << bad << " pixels differ\n";
  }
  CHECK(bad <= golden.size() / 4 / 500);
}
