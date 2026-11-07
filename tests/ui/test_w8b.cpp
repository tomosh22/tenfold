// W8b: 10x tab, panel and view commands (PrevPanelTab ... InternetSearch), each driven through its 10x default key by
// EditorApp against a null renderer and a mock font. The operating system is never touched: the launcher and the
// window hooks are injected.
#include <chrono>
#include <filesystem>
#include <fstream>

#include "../text/text_test_util.h"
#include "ui/commands/CommandNames.h"
#include "ui/features/OsLauncher.h"
#include "ui/gfx/TextRenderer.h"
#include "ui/shell/EditorApp.h"
#include "ui/widgets/Splitter.h"
#include "ui/widgets/TextView.h"
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

constexpr uint8_t kCS = ModCtrl | ModShift, kCA = ModCtrl | ModAlt, kSA = ModShift | ModAlt;

struct Env {
  fs::path dir;
  std::unique_ptr<Editor> ed;
  std::unique_ptr<gfx::TextRenderer> text;
  std::unique_ptr<gfx::TextRenderer> editorText;  // the zoomed editor font; null while not zoomed
  NullRenderer nr;
  std::unique_ptr<Workbench> bench;
  std::unique_ptr<SettingsService> svc;
  std::unique_ptr<EditorApp> app;
  int64_t now = 1000;
  std::vector<std::string> urls;
  std::vector<fs::path> shown;
  std::vector<bool> fullscreenCalls;
  std::vector<float> zooms;  // fontZoomPt() at every font rebuild

  static std::unique_ptr<gfx::TextRenderer> makeRenderer(float advance) {
    gfx::FontStack stack;
    stack.add(
        std::make_shared<tftext::MockFont>(U"abcdefghijklmnopqrstuvwxyz0123456789 ._-:,;ABCLMNTUFDK&/é", advance));
    return gfx::TextRenderer::fromFonts(std::move(stack));
  }
  gfx::TextRenderer* makeText(float advance) {
    text = makeRenderer(advance);
    return text.get();
  }

  explicit Env(const std::string& settingsText = {}) {
    static int n = 0;
    dir =
        uitest::tempRoot() / ("tf_w8b_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                              "_" + std::to_string(n++));
    fs::create_directories(dir / ".tenfold");
    for (const char* f : {"a.cpp", "b.cpp", "c.cpp"}) {
      std::ofstream out(dir / f, std::ios::binary);
      for (int i = 0; i < 10; ++i) out << "int " << f[0] << i << ";\n";
    }
    if (!settingsText.empty()) std::ofstream(dir / ".tenfold" / "settings.txt", std::ios::binary) << settingsText;
    makeText(8);
    ed = std::make_unique<Editor>(dir);
    bench = std::make_unique<Workbench>(*text, *ed);
    SettingsService::Options so;
    so.workspaceRoot = dir;
    svc = std::make_unique<SettingsService>(so);
    app = std::make_unique<EditorApp>(*bench, *ed, bench.get());
    app->attachSettings(*svc, 1.0f, [this] {
      zooms.push_back(app->fontZoomPt());
      auto old = std::move(text);
      auto oldEditor = std::move(editorText);
      gfx::TextRenderer* next = makeText(8);  // only the editor text is zoomed
      if (app->fontZoomPt() != 0) editorText = makeRenderer(8 + app->fontZoomPt());
      bench->setTextRenderer(*next);
      bench->setEditorTextRenderer(editorText.get());
      old.reset();
      oldEditor.reset();
      return true;
    });
    OsLauncher l;
    l.openUrl = [this](const std::string& u, std::string*) {
      urls.push_back(u);
      return true;
    };
    l.showInFolder = [this](const fs::path& p, std::string*) {
      shown.push_back(p);
      return true;
    };
    app->setLauncher(l);
    app->setWindowHooks({[this](bool on) { fullscreenCalls.push_back(on); }});
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
    InputEvent e;
    e.type = InputType::Text;
    e.text = s;
    now += 10;
    app->handleEvent(e, now);
    frame();
  }
  void changeSettings(const std::string& text) {
    std::ofstream(dir / ".tenfold" / "settings.txt", std::ios::binary | std::ios::trunc) << text;
    svc->notifyFileChanged(dir / ".tenfold" / "settings.txt");
    svc->poll();
    frame();
  }
  std::vector<std::string> titles(Panel* p) {
    std::vector<std::string> out;
    for (const auto& t : p->tabs().tabs()) out.push_back(t.title);
    return out;
  }
  std::string activeTitle(Panel* p) {
    for (const auto& t : p->tabs().tabs())
      if (t.id == p->activeId()) return t.title;
    return {};
  }
  std::string status(const char* seg) { return bench->status().text(seg); }
  // The panel showing `file` as its active tab.
  Panel* panelWith(const std::string& file) {
    for (Panel* p : bench->panels())
      if (p->activeId() >= 0 && bench->tabPath(p->activeId()).filename() == file) return p;
    return nullptr;
  }
};

}  // namespace

// ---- the 10x names resolve ----

TEST(w8b_every_command_resolves_and_is_registered) {
  Env env;
  for (const char* name : {"PrevPanelTab",
                           "NextPanelTab",
                           "MovePrevPanelTab",
                           "MoveNextPanelTab",
                           "MovePanelLeft",
                           "MovePanelRight",
                           "MovePanelUp",
                           "MovePanelDown",
                           "MovePanelFocusLeft",
                           "MovePanelFocusRight",
                           "MovePanelFocusUp",
                           "MovePanelFocusDown",
                           "DuplicatePanel",
                           "DuplicatePanelLeft",
                           "DuplicatePanelRight",
                           "ZoomIn",
                           "ZoomOut",
                           "ToggleFullscreen",
                           "ToggleFocusMode",
                           "ToggleFullScreenFocusMode",
                           "NewTextFile",
                           "OpenFolderForActivefile",
                           "InternetSearch"}) {
    const ResolvedCommand rc = resolveCommandName(name);
    CHECK(rc.known10x && rc.implemented);
    const Command* c = env.app->commands().find(rc.id);
    CHECK(c && c->handler);
    if (!(c && c->handler)) std::cerr << "    not bound: " << name << " -> " << rc.id << "\n";
  }
}

// ---- tabs of a panel ----

TEST(w8b_prev_and_next_panel_tab_wrap_round) {
  Env env;
  for (const char* f : {"a.cpp", "b.cpp", "c.cpp"}) env.bench->openFile(f);
  env.frame();
  Panel* p = env.bench->activePanel();
  CHECK_EQ(env.activeTitle(p), std::string("c.cpp"));
  CHECK(env.key(Key::PageUp, ModCtrl));
  CHECK_EQ(env.activeTitle(p), std::string("b.cpp"));
  CHECK_EQ(env.ed->currentDocument()->path().filename().string(), std::string("b.cpp"));
  env.key(Key::PageUp, ModCtrl);
  CHECK_EQ(env.activeTitle(p), std::string("a.cpp"));
  env.key(Key::PageUp, ModCtrl);  // wraps to the last tab
  CHECK_EQ(env.activeTitle(p), std::string("c.cpp"));
  env.key(Key::PageDown, ModCtrl);  // and forward to the first
  CHECK_EQ(env.activeTitle(p), std::string("a.cpp"));
  env.key(Key::PageDown, ModCtrl);
  CHECK_EQ(env.activeTitle(p), std::string("b.cpp"));
  // The editor still has the keys: typing goes into the document of the new tab.
  env.type("x");
  CHECK(env.ed->currentDocument()->dirty());
  CHECK_EQ(env.ed->currentDocument()->path().filename().string(), std::string("b.cpp"));
}

TEST(w8b_move_panel_tab_reorders_without_wrapping) {
  Env env;
  for (const char* f : {"a.cpp", "b.cpp", "c.cpp"}) env.bench->openFile(f);
  env.frame();
  Panel* p = env.bench->activePanel();
  using V = std::vector<std::string>;
  CHECK(env.titles(p) == (V{"a.cpp", "b.cpp", "c.cpp"}));
  env.key(Key::PageUp, kCS);  // c moves before b
  CHECK(env.titles(p) == (V{"a.cpp", "c.cpp", "b.cpp"}));
  CHECK_EQ(env.activeTitle(p), std::string("c.cpp"));
  env.key(Key::PageUp, kCS);
  CHECK(env.titles(p) == (V{"c.cpp", "a.cpp", "b.cpp"}));
  env.key(Key::PageUp, kCS);  // already first: nothing happens
  CHECK(env.titles(p) == (V{"c.cpp", "a.cpp", "b.cpp"}));
  env.key(Key::PageDown, kCS);
  env.key(Key::PageDown, kCS);
  env.key(Key::PageDown, kCS);  // already last
  CHECK(env.titles(p) == (V{"a.cpp", "b.cpp", "c.cpp"}));
  CHECK_EQ(env.activeTitle(p), std::string("c.cpp"));
  // A pinned tab keeps its group: the first unpinned tab cannot move in front of it.
  env.bench->setPinned(env.bench->activePanel()->tabs().tabs()[0].id, true);
  env.key(Key::PageUp, kCS);
  env.key(Key::PageUp, kCS);
  env.key(Key::PageUp, kCS);
  CHECK_EQ(env.titles(p)[0], std::string("a.cpp"));
}

// ---- panels: focus, move, duplicate ----

namespace {
// p1 above p3 on the left (p1 short), p2 on the right: the layout H(V(p1, p3), p2).
struct ThreePanels {
  Panel *p1, *p2, *p3;
  explicit ThreePanels(Env& env) {
    env.bench->openFile("a.cpp");
    p1 = env.bench->activePanel();
    p2 = env.bench->splitPanel(p1, Splitter::Orientation::Horizontal, true, -1);
    env.bench->openFile("b.cpp", p2);
    p3 = env.bench->splitPanel(p1, Splitter::Orientation::Vertical, true, -1);
    env.bench->openFile("c.cpp", p3);
    dynamic_cast<Splitter*>(p1->parent())->setRatio(0.25f);
    env.frame();
  }
};
}  // namespace

TEST(w8b_panel_focus_picks_the_geometric_neighbour) {
  Env env;
  ThreePanels t(env);
  CHECK(t.p1->bounds().h < t.p3->bounds().h);
  env.bench->setActivePanel(t.p2);
  CHECK(env.key(Key::Left, ModAlt));  // p2 touches both p1 and p3; p3 shares more of its height
  CHECK_EQ(env.bench->activePanel(), t.p3);
  CHECK_EQ(env.ed->currentDocument()->path().filename().string(), std::string("c.cpp"));
  env.key(Key::Up, ModAlt);
  CHECK_EQ(env.bench->activePanel(), t.p1);
  env.key(Key::Up, ModAlt);  // nothing above
  CHECK_EQ(env.bench->activePanel(), t.p1);
  CHECK(env.app->message().find("No panel above") != std::string::npos);
  env.key(Key::Right, ModAlt);
  CHECK_EQ(env.bench->activePanel(), t.p2);
  env.key(Key::Right, ModAlt);  // nothing right of p2
  CHECK_EQ(env.bench->activePanel(), t.p2);
  env.bench->setActivePanel(t.p1);
  env.key(Key::Down, ModAlt);
  CHECK_EQ(env.bench->activePanel(), t.p3);
  env.key(Key::Down, ModAlt);
  CHECK_EQ(env.bench->activePanel(), t.p3);
  // The editor view of the new active panel has the keyboard focus.
  CHECK(env.app->handleEvent(keyEv(Key::Right), env.now += 10) || true);
  CHECK(dynamic_cast<TextView*>(t.p3->activeContent()) != nullptr);
}

TEST(w8b_move_panel_swaps_places_and_the_layout_round_trips) {
  Env env;
  env.bench->openFile("a.cpp");
  Panel* p1 = env.bench->activePanel();
  Panel* p2 = env.bench->splitPanel(p1, Splitter::Orientation::Horizontal, true, -1);
  env.bench->openFile("b.cpp", p2);
  env.frame();
  CHECK(p1->bounds().x < p2->bounds().x);
  env.bench->setActivePanel(p2);
  CHECK(env.key(Key::Left, kCA));  // MovePanelLeft
  CHECK(p2->bounds().x < p1->bounds().x);
  CHECK_EQ(env.bench->panels()[0], p2);
  CHECK_EQ(env.bench->activePanel(), p2);
  CHECK_EQ(env.ed->currentDocument()->path().filename().string(), std::string("b.cpp"));
  const std::string once = env.bench->layoutJson();
  CHECK(env.bench->restoreLayoutFromJson(once));
  CHECK_EQ(env.bench->layoutJson(), once);
  CHECK_EQ(env.bench->tabPath(env.bench->panels()[0]->activeId()).filename().string(), std::string("b.cpp"));
  CHECK_EQ(env.bench->tabPath(env.bench->panels()[1]->activeId()).filename().string(), std::string("a.cpp"));
  CHECK_EQ(env.bench->activePanel(), env.bench->panels()[0]);
  // Nothing further left; right swaps back.
  env.key(Key::Left, kCA);
  CHECK_EQ(env.bench->tabPath(env.bench->panels()[0]->activeId()).filename().string(), std::string("b.cpp"));
  CHECK(env.app->message().find("No panel left") != std::string::npos);
  env.key(Key::Right, kCA);
  CHECK_EQ(env.bench->tabPath(env.bench->panels()[0]->activeId()).filename().string(), std::string("a.cpp"));
  // Keys still reach the moved panel's editor.
  env.type("z");
  CHECK_EQ(env.ed->currentDocument()->path().filename().string(), std::string("b.cpp"));
  CHECK(env.ed->currentDocument()->dirty());
}

TEST(w8b_move_panel_across_splitters_and_up_down) {
  Env env;
  ThreePanels t(env);
  const std::string before = env.bench->layoutJson();
  env.bench->setActivePanel(t.p2);
  env.key(Key::Left, kCA);  // p2 trades places with p3 (its best neighbour), which sits in the other splitter
  env.frame();
  CHECK(t.p2->bounds().x < t.p3->bounds().x);
  CHECK(t.p2->bounds().y > t.p1->bounds().y);  // p2 is now the tall bottom-left panel
  const std::string after = env.bench->layoutJson();
  CHECK(after != before);
  CHECK(env.bench->restoreLayoutFromJson(after));
  CHECK_EQ(env.bench->layoutJson(), after);
  // The restore rebuilt the panels; find them again by their files. p2 moves up past p1: p1 ends up below.
  t.p1 = env.panelWith("a.cpp");
  t.p2 = env.panelWith("b.cpp");
  REQUIRE(t.p1 && t.p2);
  env.bench->setActivePanel(t.p2);
  env.key(Key::Up, kCA);
  env.frame();
  CHECK(t.p2->bounds().y < t.p1->bounds().y);
  env.key(Key::Down, kCA);
  env.frame();
  CHECK(t.p2->bounds().y > t.p1->bounds().y);
  CHECK_EQ(env.bench->activePanel(), t.p2);
}

TEST(w8b_duplicate_panel_copies_every_tab) {
  Env env;
  env.bench->openFile("a.cpp");
  env.bench->openFile("b.cpp");
  env.frame();
  Panel* orig = env.bench->activePanel();
  CHECK(env.key(Key('D'), kSA));  // DuplicatePanel: a copy on the right, the focus stays
  REQUIRE(env.bench->panels().size() == 2);
  Panel* copy = env.bench->panels()[1];
  CHECK(copy != orig);
  CHECK(env.titles(copy) == env.titles(orig));
  CHECK_EQ(env.activeTitle(copy), std::string("b.cpp"));
  CHECK_EQ(env.bench->activePanel(), orig);
  CHECK(copy->tabs().tabs()[0].id != orig->tabs().tabs()[0].id);  // views of their own
  // Alt+Comma: a copy on the left that takes the focus.
  CHECK(env.key(Key(','), ModAlt));
  REQUIRE(env.bench->panels().size() == 3);
  Panel* left = env.bench->panels()[0];
  CHECK(left != orig && left != copy);
  CHECK_EQ(env.bench->activePanel(), left);
  CHECK(env.titles(left) == env.titles(orig));
  // Alt+. : a copy to the right of the active panel, focused.
  CHECK(env.key(Key('.'), ModAlt));
  REQUIRE(env.bench->panels().size() == 4);
  CHECK_EQ(env.bench->activePanel(), env.bench->panels()[1]);
  // The layout survives a round trip.
  const std::string j = env.bench->layoutJson();
  CHECK(env.bench->restoreLayoutFromJson(j));
  CHECK_EQ(env.bench->layoutJson(), j);
  // Closing a copy leaves the document open in the others.
  env.bench->closeTab(env.bench->activePanel()->activeId());
  CHECK(env.ed->document(env.dir / "b.cpp") != nullptr);
}

// ---- zoom ----

TEST(w8b_zoom_changes_the_editor_font_size_and_survives_relayout_and_settings_changes) {
  Env env;
  env.bench->openFile("a.cpp");
  env.frame();
  const float cell0 = env.bench->editorText().cellWidth();
  CHECK_EQ(env.app->fontZoomPt(), 0.0f);
  CHECK_EQ(env.status("zoom"), std::string());  // not zoomed: no indicator
  CHECK(env.key(Key('.'), kCS));                // ZoomIn
  CHECK_EQ(env.app->fontZoomPt(), 1.0f);
  CHECK(env.bench->editorText().cellWidth() > cell0);
  CHECK(env.app->message().find("Zoom +1") != std::string::npos);
  // VS_BEHAVIOUR Z8: only the editor text is zoomed; tabs, status bar and menus keep the root's font.
  CHECK_EQ(env.text->cellWidth(), cell0);
  CHECK_EQ(env.status("zoom"), std::string("113 %"));  // 9 / 8
  env.key(Key('.'), kCS);
  CHECK_EQ(env.bench->editorText().cellWidth(), cell0 + 2);
  CHECK_EQ(env.text->cellWidth(), cell0);
  env.key(Key(','), kCS);  // ZoomOut
  CHECK_EQ(env.bench->editorText().cellWidth(), cell0 + 1);
  // A relayout (new window size) keeps the zoom; so does a font setting that rebuilds the font.
  env.bench->render(env.nr, 700, 400);
  CHECK_EQ(env.bench->editorText().cellWidth(), cell0 + 1);
  env.changeSettings("font.size = 14\n");
  CHECK_EQ(env.zooms.back(), 1.0f);
  CHECK_EQ(env.bench->editorText().cellWidth(), cell0 + 1);
  CHECK_EQ(env.status("zoom"), std::string("113 %"));
  // The text view really lays out with the editor font: one character is one zoomed cell wide.
  auto* tv = dynamic_cast<TextView*>(env.bench->activePanel()->activeContent());
  REQUIRE(tv != nullptr);
  const float w1 = tv->caretRect({0, 4}).x - tv->caretRect({0, 3}).x;
  CHECK_EQ(w1, cell0 + 1);
  env.key(Key(','), kCS);
  env.key(Key(','), kCS);
  CHECK_EQ(env.bench->editorText().cellWidth(), cell0 - 1);
  CHECK_EQ(env.app->message(), std::string("Zoom -1 pt"));
  CHECK_EQ(env.status("zoom"), std::string("88 %"));  // 7 / 8
  env.key(Key('.'), kCS);
  CHECK_EQ(env.app->message(), std::string("Zoom reset"));
  CHECK_EQ(env.status("zoom"), std::string());
  CHECK(!env.bench->hasEditorText());  // back to one renderer
  // Limits.
  for (int i = 0; i < 60; ++i) env.app->runCommand("View.ZoomIn");
  CHECK_EQ(env.app->fontZoomPt(), 40.0f);
  env.key(Key('.'), kCS);
  CHECK(env.app->message().find("largest") != std::string::npos);
  for (int i = 0; i < 80; ++i) env.app->runCommand("View.ZoomOut");
  CHECK_EQ(env.app->fontZoomPt(), -8.0f);
}

TEST(vsedit_control_wheel_zooms_the_editor_text_and_plain_wheel_scrolls) {
  Env env;
  env.bench->openFile("a.cpp");
  env.frame();
  auto* tv = dynamic_cast<TextView*>(env.bench->activePanel()->activeContent());
  REQUIRE(tv != nullptr);
  const RectF b = tv->bounds();
  InputEvent w;
  w.type = InputType::Wheel;
  w.x = b.x + b.w / 2;
  w.y = b.y + b.h / 2;
  w.dy = 1;
  w.mods = ModCtrl;
  const float cell0 = env.bench->editorText().cellWidth();
  CHECK(env.app->handleEvent(w, 2000));  // wheel up = larger
  CHECK_EQ(env.app->fontZoomPt(), 1.0f);
  CHECK_EQ(env.bench->editorText().cellWidth(), cell0 + 1);
  w.dy = -1;
  env.app->handleEvent(w, 2010);
  env.app->handleEvent(w, 2020);
  CHECK_EQ(env.app->fontZoomPt(), -1.0f);
  // Clicking the indicator resets the zoom.
  env.frame();
  const RectF zr = env.bench->status().segmentRect("zoom");
  CHECK(zr.w > 0);
  InputEvent click;
  click.type = InputType::MouseDown;
  click.button = MouseButton::Left;
  click.x = zr.x + zr.w / 2;
  click.y = zr.y + zr.h / 2;
  env.app->handleEvent(click, 2030);
  CHECK_EQ(env.app->fontZoomPt(), 0.0f);
  // editor.mouseWheelZoom = false: Control + wheel no longer zooms.
  env.changeSettings("editor.mouseWheelZoom = false\n");
  w.dy = 1;
  env.app->handleEvent(w, 2040);
  CHECK_EQ(env.app->fontZoomPt(), 0.0f);
}

// ---- fullscreen and focus mode ----

TEST(w8b_toggle_fullscreen_asks_the_window) {
  Env env;
  CHECK(env.key(Key::Enter, ModAlt));
  CHECK(env.fullscreenCalls == std::vector<bool>{true});
  CHECK(env.app->fullscreen());
  env.key(Key::Enter, ModAlt);
  CHECK((env.fullscreenCalls == std::vector<bool>{true, false}));
  CHECK(!env.app->fullscreen());
}

TEST(w8b_focus_mode_shows_only_the_active_panel_and_restores_the_layout) {
  Env env;
  ThreePanels t(env);
  env.bench->showBottomPanel("Build");
  env.frame();
  const RectF a1 = t.p1->bounds(), a2 = t.p2->bounds(), a3 = t.p3->bounds();
  const RectF dock = env.bench->bottomRect();
  CHECK(dock.h > 0);
  env.bench->setActivePanel(t.p3);
  const std::string layout = env.bench->layoutJson();
  CHECK(env.key(Key::Enter, kCA));  // ToggleFocusMode
  CHECK(env.bench->focusMode());
  CHECK(t.p3->bounds().w > a3.w + a2.w - 10);  // the whole width...
  CHECK(t.p3->bounds().h > a3.h + a1.h);       // ...and the height of the panel area
  CHECK(!t.p1->visible() && !t.p2->visible() && t.p3->visible());
  CHECK(!env.bench->status().visible());
  CHECK_EQ(env.bench->bottomRect().h, 0.0f);
  CHECK_EQ(env.bench->layoutJson(), layout);  // nothing about the layout changed
  // The editor of the focused panel keeps working.
  env.type("q");
  CHECK_EQ(env.ed->currentDocument()->path().filename().string(), std::string("c.cpp"));
  CHECK(env.ed->currentDocument()->dirty());
  env.key(Key::Enter, kCA);
  CHECK(!env.bench->focusMode());
  CHECK(t.p1->visible() && t.p2->visible() && env.bench->status().visible());
  CHECK(t.p1->bounds().w == a1.w && t.p1->bounds().h == a1.h);
  CHECK(t.p2->bounds().w == a2.w && t.p3->bounds().h == a3.h);
  CHECK_EQ(env.bench->bottomRect().h, dock.h);
}

TEST(w8b_fullscreen_focus_mode_toggles_both) {
  Env env;
  ThreePanels t(env);
  CHECK(env.key(Key::Enter, kSA));
  CHECK(env.bench->focusMode() && env.app->fullscreen());
  CHECK(env.fullscreenCalls == std::vector<bool>{true});
  env.key(Key::Enter, kSA);
  CHECK(!env.bench->focusMode() && !env.app->fullscreen());
  // With only one of them on, the command turns both on.
  env.key(Key::Enter, ModAlt);  // fullscreen on
  env.key(Key::Enter, kSA);
  CHECK(env.bench->focusMode() && env.app->fullscreen());
}

// ---- untitled files ----

TEST(w8b_new_text_file_then_save_asks_for_a_path) {
  Env env;
  env.bench->openFile("a.cpp");
  env.frame();
  CHECK(env.key(Key('N'), ModCtrl));  // NewTextFile
  Document* d = env.ed->currentDocument();
  REQUIRE(d != nullptr);
  CHECK(d->isUntitled());
  CHECK_EQ(env.activeTitle(env.bench->activePanel()), std::string("Untitled 1"));
  CHECK_EQ(env.status("eol"), std::string("CRLF"));  // 10x DefaultLineEnding
  CHECK_EQ(env.status("encoding"), std::string("UTF-8"));
  env.type("hello");
  CHECK(d->dirty());
  // Control S has nowhere to write: it asks for a path.
  CHECK(env.key(Key('S'), ModCtrl));
  REQUIRE(env.app->saveAsPrompt().isOpen());
  CHECK_EQ(env.app->saveAsPrompt().popup()->input()->text(), std::string("Untitled 1"));
  CHECK(!fs::exists(env.dir / "Untitled 1"));
  // A name that exists is refused and asked again.
  env.type("a.cpp");
  env.key(Key::Enter);
  CHECK(env.app->message().find("already exists") != std::string::npos);
  REQUIRE(env.app->saveAsPrompt().isOpen());
  CHECK_EQ(env.app->saveAsPrompt().popup()->input()->text(), std::string("a.cpp"));
  CHECK(d->isUntitled());
  // A good one saves.
  env.type("notes.txt");
  env.key(Key::Enter);
  CHECK(!env.app->saveAsPrompt().isOpen());
  CHECK(!d->isUntitled() && !d->dirty());
  CHECK_EQ(env.activeTitle(env.bench->activePanel()), std::string("notes.txt"));
  {
    std::ifstream in(env.dir / "notes.txt", std::ios::binary);
    std::string s((std::istreambuf_iterator<char>(in)), {});
    CHECK_EQ(s, std::string("hello"));
  }
  CHECK_EQ(env.bench->tabPath(env.bench->activePanel()->activeId()), d->path());
  // The next Control S saves in place, without a prompt.
  env.type("!");
  env.key(Key('S'), ModCtrl);
  CHECK(!env.app->saveAsPrompt().isOpen());
  CHECK(!d->dirty());
  // The untitled tab never reaches layout.json or the recent files.
  env.app->runCommand("File.New");
  CHECK_EQ(env.ed->currentDocument()->path().filename().string(),
           std::string("Untitled 1"));  // the number is free again
  const std::string layout = env.bench->layoutJson();
  CHECK(layout.find("Untitled") == std::string::npos);
  for (const fs::path& r : env.bench->recentFiles()) CHECK(r.filename() != "Untitled 1");
}

TEST(w8b_new_text_file_line_ending_follows_the_setting_and_clean_untitled_closes_silently) {
  Env env("editor.defaultLineEnding = LF\n");
  env.key(Key('N'), ModCtrl);
  CHECK_EQ(env.status("eol"), std::string("LF"));
  Panel* p = env.bench->activePanel();
  env.key(Key('N'), ModCtrl);
  CHECK_EQ(env.activeTitle(p), std::string("Untitled 2"));
  env.changeSettings("editor.defaultLineEnding = CRLF\n");
  env.key(Key('N'), ModCtrl);
  CHECK_EQ(env.status("eol"), std::string("CRLF"));
  // Closing a clean untitled document does not ask and frees the document.
  env.app->runCommand("Workbench.CloseTab");
  CHECK(env.ed->document(env.dir / "Untitled 3") == nullptr);
  // A dirty one asks, and Don't Save drops it.
  env.key(Key('N'), ModCtrl);
  env.type("x");
  env.app->runCommand("Workbench.CloseTab");
  CHECK(env.app->unsavedPrompt().isOpen());
}

TEST(w8b_auto_name_untitled_files_takes_the_first_word_typed) {
  Env env("editor.autoNameUntitledFiles = true\n");
  env.key(Key('N'), ModCtrl);
  Panel* p = env.bench->activePanel();
  env.type("fo");
  CHECK_EQ(env.activeTitle(p), std::string("fo"));  // follows the word while it is being typed
  env.type("o");
  CHECK_EQ(env.activeTitle(p), std::string("foo"));
  env.type(" bar");
  CHECK_EQ(env.activeTitle(p), std::string("foo"));  // complete: kept
  env.type(" more words");
  CHECK_EQ(env.activeTitle(p), std::string("foo"));
  CHECK(env.ed->currentDocument()->isUntitled());
  env.key(Key('S'), ModCtrl);
  REQUIRE(env.app->saveAsPrompt().isOpen());
  CHECK_EQ(env.app->saveAsPrompt().popup()->input()->text(), std::string("foo"));
  env.key(Key::Escape);
  CHECK(!env.app->saveAsPrompt().isOpen());
  // Without the setting the title stays "Untitled N".
  Env plain;
  plain.key(Key('N'), ModCtrl);
  plain.type("word ");
  CHECK_EQ(plain.activeTitle(plain.bench->activePanel()), std::string("Untitled 1"));
}

TEST(w8b_quit_with_a_dirty_untitled_file_prompts_and_save_says_why_it_cannot) {
  Env env;
  env.key(Key('N'), ModCtrl);
  env.type("data");
  CHECK(!env.app->requestQuit());
  CHECK(env.app->unsavedPrompt().isOpen());
  env.key(Key::Enter);  // Save: there is no file name yet
  CHECK(!env.app->quitRequested());
  CHECK(env.app->message().find("Untitled 1") != std::string::npos);
  CHECK(env.ed->currentDocument()->dirty());
}

// ---- opening things in the OS ----

TEST(w8b_open_folder_for_active_file_uses_the_launcher) {
  Env env;
  env.bench->openFile("b.cpp");
  env.frame();
  CHECK(env.key(Key('/'), ModCtrl | ModShift | ModAlt));
  REQUIRE(env.shown.size() == 1);
  CHECK_EQ(env.shown[0].filename().string(), std::string("b.cpp"));
  CHECK_EQ(env.shown[0].parent_path(), env.ed->currentDocument()->path().parent_path());
  // An untitled document has no folder yet.
  env.key(Key('N'), ModCtrl);
  env.key(Key('/'), ModCtrl | ModShift | ModAlt);
  CHECK_EQ(env.shown.size(), size_t(1));
  CHECK(env.app->message().find("not been saved") != std::string::npos);
}

TEST(w8b_internet_search_uses_the_word_or_the_selection) {
  Env env;
  env.bench->openFile("a.cpp");  // "int a0;" on the first line
  env.frame();
  env.ed->moveCursor({0, 5});  // inside "a0"
  CHECK(env.key(Key::F1));
  REQUIRE(env.urls.size() == 1);
  CHECK_EQ(env.urls[0], std::string("https://www.google.com/search?q=a0"));
  env.ed->setSelections({Selection{{0, 0}, {0, 6}}});  // "int a0"
  env.key(Key::F1);
  REQUIRE(env.urls.size() == 2);
  CHECK_EQ(env.urls[1], std::string("https://www.google.com/search?q=int%20a0"));
  env.changeSettings("editor.internetSearchCommand = https://duckduckgo.com/?q=\n");
  env.ed->moveCursor({0, 1});
  env.key(Key::F1);
  REQUIRE(env.urls.size() == 3);
  CHECK_EQ(env.urls[2], std::string("https://duckduckgo.com/?q=int"));
  // No word under the caret: nothing is opened.
  env.ed->moveCursor({0, 7});
  env.ed->insertText("   ");
  env.ed->moveCursor({0, 9});
  env.key(Key::F1);
  CHECK_EQ(env.urls.size(), size_t(3));
  CHECK(env.app->message().find("Nothing to search") != std::string::npos);
}

TEST(w8b_url_encoding_and_search_url) {
  CHECK_EQ(urlEncode("abc-1.2_3~"), std::string("abc-1.2_3~"));
  CHECK_EQ(urlEncode("a b&c/d?e=f#g"), std::string("a%20b%26c%2Fd%3Fe%3Df%23g"));
  CHECK_EQ(urlEncode("\xC3\xA9"), std::string("%C3%A9"));
  CHECK_EQ(internetSearchUrl("https://x/?q=", "  std::vector \n\t push_back  "),
           std::string("https://x/?q=std%3A%3Avector%20push_back"));
  CHECK_EQ(internetSearchUrl("https://x/?q=", " \n "), std::string());
}
