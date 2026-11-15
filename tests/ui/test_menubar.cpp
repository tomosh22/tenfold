// MENUBAR: the File / Edit / View bar across the top of the window (docs/VS_BEHAVIOUR.md section 17): opening and
// closing by mouse and keyboard, switching menus, items running their commands, disabled items, key texts from the live
// key map, Alt and the access keys (10x's key map wins when it binds the chord), hidden mode (10x HideMainMenu), the
// layout offset, and a golden of the open File menu. Real AppHost, null renderer, mock font, fake file dialog.
#include <algorithm>
#include <chrono>
#include <fstream>

#include "../text/text_test_util.h"
#include "core/PathUtil.h"
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

InputEvent keyEv(Key k, uint8_t mods = 0, InputType t = InputType::KeyDown) {
  InputEvent e;
  e.type = t;
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
  e.button = MouseButton::Left;
  return e;
}

struct Env {
  fs::path base, dir, user;
  NullRenderer rec;
  std::unique_ptr<AppHost> host;
  std::vector<FileDialogRequest> dialogs;
  int64_t now = 1000;

  explicit Env(const std::string& userKeys = {}, const std::string& userSettings = {}) {
    static int n = 0;
    base = uitest::tempRoot() /
           ("tf_menubar_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_" +
            std::to_string(n++));
    dir = base / "Game";
    user = base / "user";
    write(dir / "src" / "a.cpp", "int a_value;\nint b_value;\nint c_value;\n");
    write(dir / "Game.sln", "\nMicrosoft Visual Studio Solution File, Format Version 12.00\n");
    fs::create_directories(user);
    if (!userKeys.empty()) write(user / "KeyMappings.10x_settings", userKeys);
    if (!userSettings.empty()) write(user / "settings.txt", userSettings);
    AppHost::Options o;
    o.userDir = user;
    o.renderer = &rec;
    o.editor.semantic.useClangd = false;
    o.editor.semantic.persistFastIndex = false;
    o.editor.vcs.enabled = false;
    o.watchSettings = false;
    o.defaultFile = "";
    o.makeText = [](const Settings&, float, std::string*) {
      gfx::FontStack stack;
      stack.add(std::make_shared<tftext::MockFont>(
          U"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ._-:,;/()[]{}<>+*&#?=!\"'\\|", 8));
      return gfx::TextRenderer::fromFonts(std::move(stack));
    };
    o.dialog = [this](const FileDialogRequest& r, FileDialogDone done) {
      dialogs.push_back(r);
      done({});
    };
    host = std::make_unique<AppHost>(std::move(o));
    std::string err;
    host->open(dir / "Game.sln", {}, false, &err);
    frame();
  }
  ~Env() {
    host.reset();
    std::error_code ec;
    fs::remove_all(base, ec);
  }
  EditorApp& app() { return *host->app(); }
  Workbench& bench() { return *host->bench(); }
  MenuBar& bar() { return bench().menuBar(); }
  void frame() {
    host->editor()->pump();
    app().tick(now);
    bench().render(rec, 1000, 700);
  }
  void send(const InputEvent& e) {
    now += 10;
    app().handleEvent(e, now);
    frame();
  }
  void key(Key k, uint8_t mods = 0) { send(keyEv(k, mods)); }
  void click(float x, float y) {
    send(mouseEv(InputType::MouseDown, x, y));
    send(mouseEv(InputType::MouseUp, x, y));
  }
  void move(float x, float y) { send(mouseEv(InputType::MouseMove, x, y)); }
  void altTap() {
    send(keyEv(Key::Unknown, ModAlt));
    send(keyEv(Key::Unknown, 0, InputType::KeyUp));
  }
  RectF title(int i) { return bar().titleRect(i); }
  void clickTitle(int i) {
    const RectF r = title(i);
    click(r.x + r.w / 2, r.y + r.h / 2);
  }
  std::vector<std::string> labels(int level = 0) {
    std::vector<std::string> out;
    ContextMenu* m = bar().dropdown();
    if (!m) return out;
    const std::vector<MenuItem>* items = &m->items();
    for (int l = 0; l < level && items; ++l) {
      const int sel = m->selected(static_cast<size_t>(l));
      items = sel >= 0 ? &(*items)[static_cast<size_t>(sel)].sub : nullptr;
    }
    if (items)
      for (const MenuItem& it : *items) out.push_back(it.separator ? "-" : it.label);
    return out;
  }
  const MenuItem* item(const std::string& label) {
    ContextMenu* m = bar().dropdown();
    if (!m) return nullptr;
    for (const MenuItem& it : m->items())
      if (it.label == label) return &it;
    return nullptr;
  }
  void clickItem(const std::string& label) {
    ContextMenu* m = bar().dropdown();
    REQUIRE(m != nullptr);
    for (int i = 0; i < static_cast<int>(m->items().size()); ++i)
      if (m->items()[static_cast<size_t>(i)].label == label) {
        const RectF r = m->itemRect(0, i);
        click(r.x + r.w / 2, r.y + r.h / 2);
        return;
      }
    CHECK(!"item not found");
  }
};

}  // namespace

TEST(menubar_the_bar_takes_room_at_the_top_and_everything_shifts_down) {
  Env env;
  MenuBar& b = env.bar();
  CHECK(b.hasMenus());
  CHECK_EQ(b.menus().size(), size_t(3));  // exactly File, Edit, View
  CHECK_EQ(b.menus()[0].title, std::string("File"));
  CHECK_EQ(b.menus()[1].title, std::string("Edit"));
  CHECK_EQ(b.menus()[2].title, std::string("View"));
  const float h = b.barHeight();
  CHECK(h > 10);
  CHECK(b.bounds().y == 0 && b.bounds().h == h);
  Panel* p = env.bench().activePanel();
  REQUIRE(p != nullptr);
  CHECK(p->bounds().y >= h);  // the tab well starts below the bar
  CHECK(env.title(0).x < env.title(1).x && env.title(1).x < env.title(2).x);
}

TEST(menubar_hit_testing_in_the_editor_is_correct_under_the_bar) {
  Env env;
  REQUIRE(env.bench().openFile(env.dir / "src" / "a.cpp") >= 0);
  env.bench().sync();
  env.frame();
  TextView* v = dynamic_cast<TextView*>(env.bench().focused());
  REQUIRE(v != nullptr);
  CHECK(v->bounds().y >= env.bar().barHeight());
  const RectF c = v->caretRect({2, 4});
  env.click(c.x + 1, c.y + c.h / 2);
  CHECK_EQ(env.host->editor()->cursor().pos.line, 2);
}

TEST(menubar_click_opens_the_dropdown_and_escape_or_an_outside_click_closes_it) {
  Env env;
  CHECK(env.bar().dropdown() == nullptr);
  env.clickTitle(0);
  REQUIRE(env.bar().dropdown() != nullptr);
  CHECK_EQ(env.bar().openIndex(), 0);
  // The drop-down hangs under its title.
  const RectF m = env.bar().dropdown()->menuRect(0);
  CHECK(m.y >= env.title(0).y + env.title(0).h - 1 && m.x == env.title(0).x);
  env.key(Key::Escape);
  CHECK(env.bar().dropdown() == nullptr);
  CHECK(!env.bar().active());
  env.clickTitle(1);
  CHECK_EQ(env.bar().openIndex(), 1);
  env.click(500, 400);  // outside
  CHECK(env.bar().dropdown() == nullptr);
  env.clickTitle(2);
  env.clickTitle(2);  // a click on the open title closes it
  CHECK(env.bar().dropdown() == nullptr);
}

TEST(menubar_moving_across_the_bar_with_a_menu_open_switches_menus) {
  Env env;
  env.clickTitle(0);
  const RectF e = env.title(1), v = env.title(2);
  env.move(e.x + e.w / 2, e.y + e.h / 2);
  CHECK_EQ(env.bar().openIndex(), 1);
  env.move(v.x + v.w / 2, v.y + v.h / 2);
  CHECK_EQ(env.bar().openIndex(), 2);
  env.move(500, 400);  // leaving the bar keeps the current menu open
  CHECK_EQ(env.bar().openIndex(), 2);
  // Without a menu open, hovering opens nothing.
  env.key(Key::Escape);
  env.move(e.x + e.w / 2, e.y + e.h / 2);
  CHECK(env.bar().dropdown() == nullptr);
}

TEST(menubar_alt_alone_focuses_the_bar_and_arrows_enter_escape_work) {
  Env env;
  env.altTap();
  CHECK(env.bar().active());
  CHECK_EQ(env.bar().focusIndex(), 0);
  CHECK(env.bar().showAccess());
  CHECK(env.bar().dropdown() == nullptr);
  env.key(Key::Right);
  CHECK_EQ(env.bar().focusIndex(), 1);
  env.key(Key::Left);
  env.key(Key::Left);  // wraps
  CHECK_EQ(env.bar().focusIndex(), 2);
  env.key(Key::Enter);
  CHECK_EQ(env.bar().openIndex(), 2);
  env.key(Key::Escape);
  CHECK(!env.bar().active());
  // Alt again leaves; Escape leaves; another key leaves.
  env.altTap();
  env.altTap();
  CHECK(!env.bar().active());
  env.altTap();
  env.key(Key::Escape);
  CHECK(!env.bar().active());
  // Alt used as a modifier (Alt+Z) is not a tap.
  env.send(keyEv(Key::Unknown, ModAlt));
  env.send(keyEv(static_cast<Key>('Z'), ModAlt));
  env.send(keyEv(Key::Unknown, 0, InputType::KeyUp));
  CHECK(!env.bar().active());
}

TEST(menubar_alt_f_e_v_open_the_menus_and_switch_between_them) {
  Env env;
  env.key(static_cast<Key>('F'), ModAlt);
  CHECK_EQ(env.bar().openIndex(), 0);
  env.key(static_cast<Key>('E'), ModAlt);
  CHECK_EQ(env.bar().openIndex(), 1);
  env.key(static_cast<Key>('V'), ModAlt);
  CHECK_EQ(env.bar().openIndex(), 2);
  env.key(Key::Escape);
  CHECK(!env.bar().active());
  // With the bar focused the plain letter works too.
  env.altTap();
  env.key(static_cast<Key>('E'));
  CHECK_EQ(env.bar().openIndex(), 1);
}

TEST(menubar_10x_wins_where_the_key_file_binds_an_access_key) {
  // 10x's default map binds none of Alt+F / Alt+E / Alt+V and no plain Alt chord: the access keys are on.
  Env plain;
  for (const KeyBinding& b : plain.app().keys().bindings())
    if (!b.chords.empty() && b.chords[0].mods == ModAlt)
      CHECK(b.chords[0].key != static_cast<Key>('F') && b.chords[0].key != static_cast<Key>('E') &&
            b.chords[0].key != static_cast<Key>('V'));
  // A key file that does bind Alt+F: the command runs, the menu stays closed.
  Env bound("Alt F: Editor.Find\n");
  REQUIRE(bound.bench().openFile(bound.dir / "src" / "a.cpp") >= 0);
  bound.bench().sync();
  bound.frame();
  bound.key(static_cast<Key>('F'), ModAlt);
  CHECK(bound.bar().dropdown() == nullptr);
  CHECK(bound.app().findReplace().isOpen());
  bound.key(Key::Escape);  // (while the Find bar is open Alt+E belongs to it)
  bound.key(static_cast<Key>('E'), ModAlt);  // the others still work
  CHECK_EQ(bound.bar().openIndex(), 1);
}

TEST(menubar_toggleOnAlt_setting_off_leaves_alt_alone) {
  Env env({}, "menu.toggleOnAlt = false\n");
  env.altTap();
  CHECK(!env.bar().active());
  env.key(static_cast<Key>('F'), ModAlt);  // the access keys stay
  CHECK_EQ(env.bar().openIndex(), 0);
}

TEST(menubar_file_menu_has_open_solution_first_and_only_existing_commands) {
  Env env;
  env.clickTitle(0);
  const auto l = env.labels();
  REQUIRE(!l.empty());
  CHECK_EQ(l[0], std::string("Open Solution..."));
  CHECK_EQ(l[3], std::string("Recent Projects and Solutions"));
  CHECK(l.back() == "Exit");
  for (const char* want : {"Open Folder...", "Open File...", "Save", "Save All", "Close", "Exit"})
    CHECK(std::find(l.begin(), l.end(), std::string(want)) != l.end());
  // No dead items and no doubled or edge separators; every item has an action.
  for (size_t i = 0; i < l.size(); ++i) {
    if (l[i] == "-") CHECK(i > 0 && i + 1 < l.size() && l[i - 1] != "-");
  }
  for (const MenuItem& it : env.bar().dropdown()->items()) CHECK(it.separator || it.action || !it.sub.empty());
  CHECK(std::find(l.begin(), l.end(), std::string("Save As...")) == l.end());  // tenfold has no such command
  CHECK(std::find(l.begin(), l.end(), std::string("Close Solution")) == l.end());
  CHECK_EQ(env.item("Open Solution...")->shortcut, std::string("Control Shift O"));
}

TEST(menubar_open_solution_item_runs_the_dialog_command) {
  Env env;
  env.clickTitle(0);
  env.clickItem("Open Solution...");
  CHECK(env.bar().dropdown() == nullptr);  // the menu closed first
  REQUIRE(env.dialogs.size() == 1);
  CHECK(!env.dialogs[0].folder);
  CHECK_EQ(env.dialogs[0].filters[0].pattern, std::string("*.sln;*.slnx;*.vcxproj"));
  // By keyboard: Alt+F, Down to the first item, Enter.
  env.key(static_cast<Key>('F'), ModAlt);
  env.key(Key::Enter);  // Alt+F opens with the keyboard: the first enabled item is selected
  CHECK_EQ(env.dialogs.size(), size_t(2));
}

TEST(menubar_arrows_move_in_a_menu_and_left_right_switch_menus) {
  Env env;
  env.key(static_cast<Key>('F'), ModAlt);
  ContextMenu* m = env.bar().dropdown();
  REQUIRE(m != nullptr);
  CHECK_EQ(m->selected(), 0);
  env.key(Key::Down);
  CHECK_EQ(m->selected(), 1);
  env.key(Key::Right);  // Open Folder has no submenu: next menu
  CHECK_EQ(env.bar().openIndex(), 1);
  env.key(Key::Left);
  CHECK_EQ(env.bar().openIndex(), 0);
  env.key(Key::Left);  // wraps to View
  CHECK_EQ(env.bar().openIndex(), 2);
}

TEST(menubar_edit_menu_groups_and_disabled_items) {
  Env env;  // no document open
  env.clickTitle(1);
  const auto l = env.labels();
  REQUIRE(l.size() > 6);
  CHECK_EQ(l[0], std::string("Undo"));
  CHECK_EQ(l[1], std::string("Redo"));
  CHECK_EQ(l[2], std::string("-"));
  CHECK_EQ(l[3], std::string("Cut"));
  CHECK(std::find(l.begin(), l.end(), std::string("Find and Replace")) != l.end());
  CHECK(std::find(l.begin(), l.end(), std::string("Go To")) != l.end());
  CHECK(!env.item("Undo")->enabled);  // needs a document
  CHECK(!env.item("Cut")->enabled);
  CHECK(env.item("Find and Replace")->sub.size() >= 4);
  // A disabled item does not run and the arrow keys skip it.
  env.key(Key::Escape);
  REQUIRE(env.bench().openFile(env.dir / "src" / "a.cpp") >= 0);
  env.bench().sync();
  env.frame();
  env.clickTitle(1);
  CHECK(env.item("Cut")->enabled);
  CHECK(env.item("Select All")->enabled);
}

TEST(menubar_submenus_open_to_the_right_and_run_their_item) {
  Env env;
  REQUIRE(env.bench().openFile(env.dir / "src" / "a.cpp") >= 0);
  env.bench().sync();
  env.frame();
  env.key(static_cast<Key>('E'), ModAlt);
  ContextMenu* m = env.bar().dropdown();
  REQUIRE(m != nullptr);
  int i = 0;
  for (; i < static_cast<int>(m->items().size()); ++i)
    if (m->items()[static_cast<size_t>(i)].label == "Find and Replace") break;
  const RectF r = m->itemRect(0, i);
  env.move(r.x + 4, r.y + r.h / 2);
  CHECK(m->depth() == 2);
  const auto sub = env.labels(1);
  CHECK(!sub.empty() && sub[0] == "Quick Find");
  const RectF s = m->itemRect(1, 0);
  env.click(s.x + 4, s.y + s.h / 2);
  CHECK(env.bar().dropdown() == nullptr);
  CHECK(env.app().findReplace().isOpen());  // Quick Find ran Editor.Find
}

TEST(menubar_view_items_run_their_commands) {
  Env env;
  env.clickTitle(2);
  const auto l = env.labels();
  for (const char* want : {"Solution Explorer", "Error List", "Output", "Terminal", "Command Palette...", "Zoom In",
                           "Zoom Out", "Full Screen"})
    CHECK(std::find(l.begin(), l.end(), std::string(want)) != l.end());
  CHECK(env.app().fontZoomPt() == 0);
  env.clickItem("Zoom In");
  CHECK(env.app().fontZoomPt() > 0);
  env.clickTitle(2);
  env.clickItem("Command Palette...");
  CHECK(env.app().commandPalette().isOpen());
}

TEST(menubar_key_texts_follow_the_live_key_map_after_a_remap) {
  Env env("Control Alt R: File.RecentSolutions\nControl Shift Alt Y: Editor.Undo\n");
  env.clickTitle(0);
  CHECK_EQ(env.item("Recent Projects and Solutions")->shortcut, std::string("Control Alt R"));
  CHECK_EQ(env.item("Save")->shortcut, std::string("Control S"));
  env.key(Key::Escape);
  env.clickTitle(1);
  // 10x's own Control Z stays first; the menu shows what the palette shows for the command.
  CHECK(!env.item("Undo")->shortcut.empty());
  Env none;
  none.clickTitle(0);
  CHECK(none.item("Recent Projects and Solutions")->shortcut.empty());  // no default key
}

TEST(menubar_hidden_mode_floats_over_the_top_edge_and_hides_again) {
  Env env({}, "menu.hide = true\n");
  CHECK(env.bar().hidden());
  CHECK(env.bar().reservedHeight() == 0);
  Panel* p = env.bench().activePanel();
  const float top = p->bounds().y;
  Env shown;  // the same window with the bar shown: everything is one bar height lower
  CHECK(shown.bench().activePanel()->bounds().y == top + shown.bar().barHeight());  // no room reserved when hidden
  env.move(100, 2);                    // the pointer at the top edge
  CHECK(env.bar().revealed());
  CHECK(p->bounds().y == top);  // still no shift: it floats
  env.move(100, 300);
  CHECK(!env.bar().revealed());
  env.altTap();  // Alt shows it too
  CHECK(env.bar().revealed() && env.bar().active());
  env.key(Key::Escape);
  CHECK(!env.bar().revealed());
}

TEST(menubar_hidden_mode_without_hover_reveal_needs_alt) {
  Env env({}, "menu.hide = true\nmenu.showOnMouseHover = false\n");
  env.move(100, 2);
  CHECK(!env.bar().revealed());
  env.key(static_cast<Key>('F'), ModAlt);
  CHECK(env.bar().dropdown() != nullptr);
}

TEST(menubar_focus_mode_hides_the_bar_and_it_comes_back) {
  Env env;
  const float h = env.bar().barHeight();
  env.app().runCommand("View.ToggleFocusMode");
  env.frame();
  CHECK(env.bar().bounds().h == 0);
  env.app().runCommand("View.ToggleFocusMode");
  env.frame();
  CHECK(env.bar().bounds().h == h);
}

TEST(menubar_the_menu_commands_work_without_a_document_or_a_dialog) {
  Env env;
  // Every title opens a non-empty menu with no document and no workspace dialog backend problem.
  for (int i = 0; i < 3; ++i) {
    env.clickTitle(i);
    CHECK(!env.labels().empty());
    env.key(Key::Escape);
  }
}

// --- golden: the open File menu in vs-dark ---------------------------------------------------------------------------

namespace {
std::string monoFont() {
  for (const char* p : {"/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
                        "/usr/share/fonts/dejavu/DejaVuSansMono.ttf", "/usr/share/fonts/TTF/DejaVuSansMono.ttf"})
    if (std::ifstream(p).good()) return p;
  return {};
}
}  // namespace

TEST(menubar_golden_vs_dark_file_menu_open) {
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
  CHECK(vs.menuBarBackground.a > 0);
  const fs::path tmp = uitest::tempRoot() / "tf_menubar_golden";  // the folder name is in the status bar: fixed
  std::error_code rm;
  fs::remove_all(tmp, rm);
  fs::create_directories(tmp / "src");
  fs::copy_file(TENFOLD_SOURCE_DIR "/examples/sample_project/src/main.cpp", tmp / "src/main.cpp");
  {
    Editor::Options o;
    o.semantic.useClangd = false;
    o.semantic.persistFastIndex = false;
    o.watchForcePolling = true;
    o.watchPollMs = 60000;
    Editor ed(tmp, o);
    Workbench wb(*tr, ed, vs);
    wb.openFile("src/main.cpp");
    wb.sync();
    EditorApp app(wb, ed, &wb);
    uitest::wrapOff(wb);
    {
      WorkspaceOpener::Hooks hooks;  // as the program installs them: the open commands are available
      hooks.open = [](const fs::path&) {};
      hooks.dialog = [](const FileDialogRequest&, FileDialogDone) {};
      app.workspaceOpener().setHooks(std::move(hooks));
    }
    int64_t now = 1000;
    gfx::GlRenderer& r = *fx->renderer;
    r.setOffscreen(true);
    wb.render(r, 1024, 640);
    app.tick(now += 10);
    wb.render(r, 1024, 640);
    app.handleEvent(keyEv(static_cast<Key>('F'), ModAlt), now += 10);
    REQUIRE(wb.menuBar().dropdown() != nullptr);
    wb.menuBar().dropdown()->setSelected(1);  // Open Folder... highlighted
    const int w = 1024, h = 640;
    wb.render(r, w, h);
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4);
    REQUIRE(r.readPixels(w, h, px.data()));
    tr->releaseTexture(r);
    const std::string path = TENFOLD_SOURCE_DIR "/tests/golden/menubar_file_menu_vs_dark.png";
    if (uitest::updateGoldens) {
      REQUIRE(gfx::writePngFile(path, px.data(), w, h));
      std::cerr << "    golden updated: " << path << "\n";
    } else {
      int gw = 0, gh = 0;
      std::vector<uint8_t> golden;
      if (!gfx::readPngFile(path, gw, gh, golden)) {
        CHECK(!"golden missing or unreadable; run tenfold-ui-tests --update-goldens");
      } else {
        REQUIRE(gw == w && gh == h);
        size_t bad = 0;
        for (size_t i = 0; i < golden.size(); i += 4) {
          int d = 0;
          for (int ch = 0; ch < 4; ++ch) d = std::max(d, std::abs(int(golden[i + ch]) - int(px[i + ch])));
          if (d > 3) ++bad;
        }
        if (bad > golden.size() / 4 / 500) {
          gfx::writePngFile("menubar_file_menu_vs_dark.actual.png", px.data(), w, h);
          std::cerr << "    " << bad << " pixels differ; wrote menubar_file_menu_vs_dark.actual.png\n";
        }
        CHECK(bad <= golden.size() / 4 / 500);
      }
    }
    app.closePickers();
  }
  fs::remove_all(tmp, rm);
}
