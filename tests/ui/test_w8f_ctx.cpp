// W8f: context menus. The ContextMenu widget on its own (keys, mouse, placement), then the menus through EditorApp:
// right click and Shift F10 / AppsKey in the editor, on a tab, on a status bar segment, and the provider hook for
// widgets of the application (the workspace explorer). Mock font (8 px cells, 14 px lines) and a null renderer; one
// golden (pinned DejaVuSansMono, vs-dark) needs GL and skips without.
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#include "../text/text_test_util.h"
#include "term/Pty.h"
#include "test_vcs_util.h"
#include "ui/commands/CommandNames.h"
#include "ui/gfx/PngWrite.h"
#include "ui/gfx/TextRenderer.h"
#include "ui/platform/Clipboard.h"
#include "ui/settings/Settings.h"
#include "ui/settings/Theme.h"
#include "ui/shell/EditorApp.h"
#include "ui/widgets/ContextMenu.h"
#include "ui/widgets/TerminalView.h"
#include "ui/widgets/TextView.h"
#include "ui_test.h"

using namespace tf;
using namespace tf::ui;
#define REQUIRE_EQ(a, b) REQUIRE((a) == (b))
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
InputEvent textEv(const std::string& t) {
  InputEvent e;
  e.type = InputType::Text;
  e.text = t;
  return e;
}
InputEvent mouseEv(InputType t, float x, float y, MouseButton b = MouseButton::Left) {
  InputEvent e;
  e.type = t;
  e.button = b;
  e.x = x;
  e.y = y;
  e.clicks = 1;
  return e;
}

std::unique_ptr<gfx::TextRenderer> mockText() {
  gfx::FontStack stack;
  stack.add(std::make_shared<tftext::MockFont>(
      U"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ._-:,;/()[]{}<>+*&#?=!\"'\\|"));
  return gfx::TextRenderer::fromFonts(std::move(stack));
}

// --- the widget on its own ------------------------------------------------------------------------------------------

struct WidgetEnv {
  std::unique_ptr<gfx::TextRenderer> text = mockText();
  NullRenderer nr;
  RootWidget root{*text};
  std::vector<std::string> ran;
  bool closed = false;
  ContextMenu* menu = nullptr;

  MenuItem item(const std::string& label, bool enabled = true, const std::string& key = {}) {
    MenuItem m;
    m.label = label;
    m.shortcut = key;
    m.enabled = enabled;
    m.action = [this, label] { ran.push_back(label); };
    return m;
  }
  ContextMenu* open(std::vector<MenuItem> items, RectF anchor, bool keyboard, int w = 400, int h = 300) {
    root.render(nr, w, h);
    menu = ContextMenu::open(root, std::make_unique<ContextMenu>(std::move(items)), anchor, keyboard);
    menu->onClosed([this] { closed = true; });
    root.render(nr, w, h);
    return menu;
  }
  bool send(const InputEvent& e) {
    const bool r = root.dispatch(e);
    root.render(nr, 400, 300);
    return r;
  }
  void click(const RectF& r) {
    send(mouseEv(InputType::MouseDown, r.x + r.w / 2, r.y + r.h / 2));
    send(mouseEv(InputType::MouseUp, r.x + r.w / 2, r.y + r.h / 2));
  }
};

std::vector<MenuItem> sample(WidgetEnv& e) {
  std::vector<MenuItem> v;
  v.push_back(e.item("Cut", true, "Control X"));
  v.push_back(e.item("Copy", true, "Control C"));
  v.push_back(e.item("Paste", false, "Control V"));
  v.push_back(MenuItem::sep());
  v.push_back(e.item("Rename"));
  return v;
}

}  // namespace

TEST(ctxmenu_keys_navigate_skip_disabled_and_separators_and_run_after_closing) {
  WidgetEnv e;
  ContextMenu* m = e.open(sample(e), {20, 20, 0, 0}, true);
  REQUIRE(m != nullptr);
  CHECK(e.root.modal() == m);
  CHECK_EQ(m->selected(), 0);  // keyboard: the first item starts selected
  e.send(keyEv(Key::Down));
  CHECK_EQ(m->selected(), 1);
  e.send(keyEv(Key::Down));
  CHECK_EQ(m->selected(), 4);  // Paste is disabled, the separator is not an item
  e.send(keyEv(Key::Down));
  CHECK_EQ(m->selected(), 0);  // wraps
  e.send(keyEv(Key::Up));
  CHECK_EQ(m->selected(), 4);
  e.send(keyEv(Key::Home));
  CHECK_EQ(m->selected(), 0);
  e.send(keyEv(Key::End));
  CHECK_EQ(m->selected(), 4);
  e.send(keyEv(Key::Up));
  e.send(keyEv(Key::Enter));
  CHECK(e.root.modal() == nullptr);
  CHECK(e.closed);
  REQUIRE_EQ(e.ran.size(), static_cast<size_t>(1));
  CHECK_EQ(e.ran[0], std::string("Copy"));
}

TEST(ctxmenu_first_letter_jumps_and_escape_closes) {
  WidgetEnv e;
  ContextMenu* m = e.open(sample(e), {20, 20, 0, 0}, false);
  CHECK_EQ(m->selected(), -1);  // mouse: nothing selected yet
  e.send(textEv("r"));
  CHECK_EQ(m->selected(), 4);
  e.send(textEv("c"));
  CHECK_EQ(m->selected(), 0);  // Cut, after wrapping
  e.send(textEv("c"));
  CHECK_EQ(m->selected(), 1);  // the next C
  e.send(textEv("p"));
  CHECK_EQ(m->selected(), 1);  // Paste is disabled: no jump
  e.send(keyEv(Key::Down));    // Escape is not needed to leave Enter on a disabled item to do nothing
  e.send(keyEv(Key::Escape));
  CHECK(e.root.modal() == nullptr);
  CHECK(e.closed);
  CHECK(e.ran.empty());
}

TEST(ctxmenu_mouse_hover_click_outside_and_disabled) {
  WidgetEnv e;
  ContextMenu* m = e.open(sample(e), {50, 40, 0, 0}, false);
  const RectF r1 = m->itemRect(0, 1), r2 = m->itemRect(0, 2), sep = m->itemRect(0, 3);
  e.send(mouseEv(InputType::MouseMove, r1.x + 4, r1.y + 2));
  CHECK_EQ(m->selected(), 1);
  e.send(mouseEv(InputType::MouseMove, r2.x + 4, r2.y + 2));
  CHECK_EQ(m->selected(), -1);  // a disabled item is not highlighted
  e.click(r2);
  e.click(sep);
  CHECK(e.root.modal() == m);  // neither runs nor closes
  CHECK(e.ran.empty());
  // A right press on an item does not run it.
  e.send(mouseEv(InputType::MouseDown, r1.x + 4, r1.y + 2, MouseButton::Right));
  CHECK(e.root.modal() == m);
  e.click(r1);
  REQUIRE_EQ(e.ran.size(), static_cast<size_t>(1));
  CHECK_EQ(e.ran[0], std::string("Copy"));
  CHECK(e.root.modal() == nullptr);
  // A press outside closes without running anything.
  e.ran.clear();
  m = e.open(sample(e), {50, 40, 0, 0}, false);
  e.send(mouseEv(InputType::MouseDown, 380, 280));
  CHECK(e.root.modal() == nullptr);
  CHECK(e.ran.empty());
}

TEST(ctxmenu_placement_clamps_to_the_window_and_flips_above) {
  WidgetEnv e;
  ContextMenu* m = e.open(sample(e), {395, 295, 0, 0}, false);
  RectF r = m->menuRect();
  CHECK(r.x >= 0 && r.x + r.w <= 400.0f);
  CHECK(r.y >= 0 && r.y + r.h <= 295.0f);  // no room below: it sits above the point
  CHECK(r.w >= 96.0f);
  m->close();
  m = e.open(sample(e), {-10, -10, 0, 0}, false);
  r = m->menuRect();
  CHECK(r.x >= 0 && r.y >= 0);
  m->close();
  m = e.open(sample(e), {30, 50, 0, 14}, true);  // an anchor with height (a caret): below it
  r = m->menuRect();
  CHECK_EQ(r.x, 30.0f);
  CHECK_EQ(r.y, 64.0f);
  // Shortcuts are right aligned and widen the menu.
  const RectF narrow = m->menuRect();
  m->close();
  std::vector<MenuItem> v;
  v.push_back(e.item("A"));
  m = e.open(std::move(v), {30, 50, 0, 14}, true);
  CHECK(m->menuRect().w < narrow.w + 1.0f);
}

TEST(ctxmenu_submenus_open_with_right_and_close_with_left) {
  WidgetEnv e;
  std::vector<MenuItem> v;
  MenuItem sub;
  sub.label = "Go To";
  sub.sub.push_back(e.item("Definition"));
  sub.sub.push_back(e.item("Declaration"));
  v.push_back(e.item("First"));
  v.push_back(sub);
  ContextMenu* m = e.open(std::move(v), {20, 20, 0, 0}, true);
  e.send(keyEv(Key::Down));
  CHECK_EQ(m->selected(), 1);
  CHECK_EQ(m->depth(), static_cast<size_t>(1));
  e.send(keyEv(Key::Right));
  REQUIRE_EQ(m->depth(), static_cast<size_t>(2));
  CHECK_EQ(m->selected(1), 0);
  const RectF top = m->menuRect(0), s = m->menuRect(1);
  CHECK(s.x >= top.x + top.w - 4);  // opens to the right of the parent
  e.send(keyEv(Key::Down));
  e.send(keyEv(Key::Left));
  CHECK_EQ(m->depth(), static_cast<size_t>(1));
  e.send(keyEv(Key::Right));
  e.send(keyEv(Key::Escape));  // closes the submenu only
  CHECK_EQ(m->depth(), static_cast<size_t>(1));
  CHECK(e.root.modal() == m);
  e.send(keyEv(Key::Right));
  e.send(keyEv(Key::Enter));
  REQUIRE_EQ(e.ran.size(), static_cast<size_t>(1));
  CHECK_EQ(e.ran[0], std::string("Definition"));
  CHECK(e.root.modal() == nullptr);
  // The mouse: hovering the parent opens the submenu, a click on its item runs it.
  e.ran.clear();
  std::vector<MenuItem> w;
  MenuItem sub2 = sub;
  w.push_back(e.item("First"));
  w.push_back(sub2);
  m = e.open(std::move(w), {20, 20, 0, 0}, false);
  const RectF p = m->itemRect(0, 1);
  e.send(mouseEv(InputType::MouseMove, p.x + 4, p.y + 2));
  REQUIRE_EQ(m->depth(), static_cast<size_t>(2));
  e.click(m->itemRect(1, 1));
  REQUIRE_EQ(e.ran.size(), static_cast<size_t>(1));
  CHECK_EQ(e.ran[0], std::string("Declaration"));
}

TEST(ctxmenu_submenu_flips_left_at_the_window_edge) {
  WidgetEnv e;
  std::vector<MenuItem> v;
  MenuItem sub;
  sub.label = "More";
  sub.sub.push_back(e.item("A very long submenu entry that needs room"));
  v.push_back(sub);
  ContextMenu* m = e.open(std::move(v), {300, 20, 0, 0}, true);
  e.send(keyEv(Key::Right));
  REQUIRE_EQ(m->depth(), static_cast<size_t>(2));
  const RectF top = m->menuRect(0), s = m->menuRect(1);
  CHECK(s.x + s.w <= 400.0f);
  CHECK(s.x < top.x);
}

// --- through EditorApp ----------------------------------------------------------------------------------------------

namespace {

struct Env {
  fs::path dir;
  std::unique_ptr<Editor> ed;
  std::unique_ptr<gfx::TextRenderer> text;
  NullRenderer nr;
  std::unique_ptr<Workbench> bench;
  std::unique_ptr<EditorApp> app;
  std::string clip;
  int64_t now = 1000;

  explicit Env(const std::string& aText = "foo bar foo\nbaz foo\nlonger line\nx\n", bool withVcs = false) {
    static int n = 0;
    dir =
        uitest::tempRoot() / ("tf_w8f_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                              "_" + std::to_string(n++));
    fs::create_directories(dir);
    std::ofstream(dir / "a.cpp", std::ios::binary) << aText;
    std::ofstream(dir / "b.cpp", std::ios::binary) << "b0\nb1\n";
    if (withVcs) {
      auto git = [&](std::vector<std::string> args) {
        ExecOptions o;
        o.exe = "git";
        o.args = std::move(args);
        o.cwd = dir;
        o.timeoutMs = 30000;
        return tf::runCommand(o);
      };
      git({"init", "-q"});
      git({"config", "user.name", "Tenfold Test"});
      git({"config", "user.email", "test@example.invalid"});
      git({"config", "commit.gpgsign", "false"});
      git({"config", "core.autocrlf", "false"});
      git({"add", "-A"});
      git({"commit", "-q", "-m", "base"});
    }
    text = mockText();
    Editor::Options o;
    o.semantic.useClangd = false;
    o.vcs.debounceMs = 20;
    ed = std::make_unique<Editor>(dir, o);
    bench = std::make_unique<Workbench>(*text, *ed);
    app = std::make_unique<EditorApp>(*bench, *ed, bench.get());
    ClipboardBackend cb;
    cb.get = [this] { return clip; };
    cb.set = [this](const std::string& s) {
      clip = s;
      return true;
    };
    setClipboardBackend(cb);
    bench->openFile("a.cpp");
    bench->sync();
    frame();
  }
  ~Env() {
    setClipboardBackend({});
    app.reset();
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
  bool send(const InputEvent& e) {
    now += 10;
    const bool r = app->handleEvent(e, now);
    frame();
    return r;
  }
  bool key(Key k, uint8_t mods = 0) { return send(keyEv(k, mods)); }
  void rightClick(float x, float y) {
    send(mouseEv(InputType::MouseDown, x, y, MouseButton::Right));
    send(mouseEv(InputType::MouseUp, x, y, MouseButton::Right));
  }
  void click(const RectF& r) {
    send(mouseEv(InputType::MouseDown, r.x + r.w / 2, r.y + r.h / 2));
    send(mouseEv(InputType::MouseUp, r.x + r.w / 2, r.y + r.h / 2));
  }
  TextView* view() {  // the active tab's view, whoever has the focus (an open menu has it)
    if (auto* tv = dynamic_cast<TextView*>(bench->focused())) return tv;
    return panel() ? dynamic_cast<TextView*>(panel()->activeContent()) : nullptr;
  }
  std::pair<float, float> centreOf(Position p) {
    const RectF r = view()->caretRect(p);
    return {r.x + 2, r.y + r.h / 2};
  }
  ContextMenu* menu() { return app->contextMenu(); }
  int indexOf(const std::string& label) {
    ContextMenu* m = menu();
    if (!m) return -1;
    for (size_t i = 0; i < m->items().size(); ++i)
      if (m->items()[i].label == label) return static_cast<int>(i);
    return -1;
  }
  bool has(const std::string& label) { return indexOf(label) >= 0; }
  const MenuItem& item(const std::string& label) { return menu()->items()[static_cast<size_t>(indexOf(label))]; }
  void clickItem(const std::string& label) {
    REQUIRE(menu() != nullptr);
    const int i = indexOf(label);
    REQUIRE(i >= 0);
    click(menu()->itemRect(0, i));
  }
  Panel* panel() { return bench->activePanel(); }
  size_t tabCount() { return panel()->tabs().tabs().size(); }
  int tabId(const std::string& name) {
    for (const auto& t : panel()->tabs().tabs())
      if (t.title == name) return t.id;
    return -1;
  }
};

}  // namespace

TEST(ctxmenu_show_context_menu_is_a_registered_10x_command_with_the_default_keys) {
  Env env;
  const ResolvedCommand r = resolveCommandName("ShowContextMenu");
  CHECK(r.known10x && r.implemented);
  CHECK_EQ(r.id, std::string("App.ShowContextMenu"));
  CHECK(env.app->commands().find("App.ShowContextMenu") != nullptr);
  int found = 0;
  for (const KeyBinding& b : env.app->keys().bindings())
    if (b.command == "App.ShowContextMenu") ++found;
  CHECK_EQ(found, 2);  // Shift F10 and AppsKey
  for (const char* n : {"CloseAllOtherTabs", "CloseAllButPinnedTabs", "PinTab", "TogglePinTab", "CopyActiveFilePath"})
    CHECK(resolveCommandName(n).implemented);
}

TEST(ctxmenu_shift_f10_opens_the_editor_menu_at_the_caret) {
  Env env;
  env.ed->setSelections({{{2, 3}, {2, 3}}});
  env.frame();
  REQUIRE(env.menu() == nullptr);
  CHECK(env.key(Key::F10, ModShift));
  REQUIRE(env.menu() != nullptr);
  const RectF caret = env.view()->caretRect({2, 3});
  const RectF mr = env.menu()->menuRect();
  CHECK_EQ(mr.x, caret.x);
  CHECK_EQ(mr.y, caret.y + caret.h);
  CHECK_EQ(env.menu()->selected(), 0);  // from the keyboard the first item is selected
  for (const char* l :
       {"Cut", "Copy", "Paste", "Go To Definition", "Find All References", "Rename...", "Format Selection",
        "Toggle Comment", "Toggle Breakpoint", "Run To Cursor", "Open Containing Folder", "Copy Full Path"})
    CHECK(env.has(l));
  // The key bound to the command is shown in 10x notation, right of the label.
  CHECK_EQ(env.item("Cut").shortcut, std::string("Control X"));
  CHECK_EQ(env.item("Copy").shortcut, std::string("Control C"));
  CHECK_EQ(env.item("Toggle Comment").shortcut, std::string("Control Shift /"));
  // No change under the caret: that entry is left out. The workspace explorer exists, so its entry is offered.
  CHECK(!env.has("Revert Change"));
  CHECK(env.has("Show in Workspace Tree"));
  // Pressing the key again dismisses the menu and the editor has its focus back.
  env.key(Key::F10, ModShift);
  CHECK(env.menu() == nullptr);
  CHECK(env.bench->focused() == env.view());
  // AppsKey does the same.
  env.key(Key::Menu);
  REQUIRE(env.menu() != nullptr);
  env.key(Key::Escape);
  CHECK(env.menu() == nullptr);
  CHECK(env.bench->focused() == env.view());
}

TEST(ctxmenu_enter_runs_the_command_and_disabled_items_are_greyed) {
  Env env;
  env.ed->setSelections({{{0, 0}, {0, 0}}});
  env.key(Key::F10, ModShift);
  REQUIRE(env.menu() != nullptr);
  // Select All by its first letter, then Enter: the command ran through the registry.
  env.send(textEv("s"));
  while (env.menu()->items()[static_cast<size_t>(env.menu()->selected())].label != "Select All") env.send(textEv("s"));
  env.key(Key::Enter);
  CHECK(env.menu() == nullptr);
  const Selection s = env.ed->selections()[0];
  CHECK(s.min() == (Position{0, 0}));
  CHECK(s.max().line >= 3);
  // The empty part of the tab strip has no menu.
  env.bench->closeTab(env.panel()->activeId());
  env.bench->sync();
  env.frame();
  env.app->openContextMenuAt(500, 5);  // the empty tab strip: no menu
  CHECK(env.menu() == nullptr);
}

TEST(ctxmenu_right_click_in_the_editor_moves_the_caret_unless_inside_the_selection) {
  Env env;
  env.ed->setSelections({{{0, 0}, {0, 3}}});
  env.frame();
  const auto [x1, y1] = env.centreOf({2, 4});
  env.rightClick(x1, y1);
  REQUIRE(env.menu() != nullptr);
  CHECK_EQ(env.ed->cursor().pos.line, 2);  // outside the selection: the caret went there
  CHECK(env.ed->selections()[0].empty());
  const RectF mr = env.menu()->menuRect();
  CHECK_EQ(mr.x, x1);  // at the mouse
  CHECK_EQ(mr.y, y1);
  CHECK_EQ(env.menu()->selected(), -1);
  CHECK(env.app->handleEvent(keyEv(Key::Escape), env.now + 1));
  CHECK(env.menu() == nullptr);
  // Inside a selection the selection stays, and Copy takes it.
  env.ed->setSelections({{{0, 0}, {0, 7}}});
  env.frame();
  const auto [x2, y2] = env.centreOf({0, 3});
  env.rightClick(x2, y2);
  REQUIRE(env.menu() != nullptr);
  CHECK(!env.ed->selections()[0].empty());
  env.clickItem("Copy");
  CHECK(env.menu() == nullptr);
  CHECK_EQ(env.clip, std::string("foo bar"));
  CHECK(env.bench->focused() == env.view());
}

TEST(ctxmenu_menu_near_the_window_corner_stays_inside_the_window) {
  Env env;
  env.rightClick(960, 560);
  REQUIRE(env.menu() != nullptr);
  const RectF r = env.menu()->menuRect();
  CHECK(r.x >= 0 && r.x + r.w <= 1000.0f && r.y >= 0 && r.y + r.h <= 600.0f);
}

TEST(ctxmenu_copy_full_path_item_runs_CopyActiveFilePath) {
  Env env;
  env.key(Key::F10, ModShift);
  env.clickItem("Copy Full Path");
  CHECK(env.menu() == nullptr);
  CHECK_EQ(fs::path(env.clip).filename().string(), std::string("a.cpp"));
  CHECK(fs::exists(env.clip));
}

TEST(ctxmenu_tab_menu_activates_the_tab_and_offers_close_pin_and_path_items) {
  Env env;
  env.bench->openFile("b.cpp");
  env.bench->sync();
  env.frame();
  CHECK_EQ(env.tabCount(), static_cast<size_t>(2));
  const int a = env.tabId("a.cpp"), b = env.tabId("b.cpp");
  REQUIRE(a >= 0 && b >= 0);
  CHECK_EQ(env.panel()->activeId(), b);
  const RectF ra = env.panel()->tabs().tabRect(a);
  env.rightClick(ra.x + 10, ra.y + 5);
  REQUIRE(env.menu() != nullptr);
  CHECK_EQ(env.panel()->activeId(), a);  // the right-clicked tab became the active one
  for (const char* l :
       {"Close", "Close All But This", "Close All But Pinned", "Pin Tab", "Copy Full Path", "Open Containing Folder"})
    CHECK(env.has(l));
  CHECK(env.has("Show in Workspace Tree"));
  // Pin it.
  env.clickItem("Pin Tab");
  CHECK(env.panel()->tabs().tabs()[0].pinned);
  CHECK_EQ(env.panel()->tabs().tabs()[0].id, a);
  const RectF ra2 = env.panel()->tabs().tabRect(a);
  env.rightClick(ra2.x + 10, ra2.y + 5);
  REQUIRE(env.menu() != nullptr);
  CHECK(env.has("Unpin Tab"));
  CHECK(!env.has("Pin Tab"));
  env.key(Key::Escape);
  // Close All But Pinned closes b, keeps a.
  const RectF rb = env.panel()->tabs().tabRect(b);
  env.rightClick(rb.x + 10, rb.y + 5);
  env.clickItem("Close All But Pinned");
  CHECK_EQ(env.tabCount(), static_cast<size_t>(1));
  CHECK_EQ(env.panel()->activeId(), a);
}

TEST(ctxmenu_tab_menu_close_all_but_this_and_close) {
  Env env;
  env.bench->openFile("b.cpp");
  env.bench->sync();
  const int a = env.tabId("a.cpp"), b = env.tabId("b.cpp");
  RectF ra = env.panel()->tabs().tabRect(a);
  env.rightClick(ra.x + 10, ra.y + 5);
  env.clickItem("Close All But This");
  CHECK_EQ(env.tabCount(), static_cast<size_t>(1));
  CHECK_EQ(env.tabId("a.cpp"), a);
  CHECK_EQ(env.tabId("b.cpp"), -1);
  (void)b;
  ra = env.panel()->tabs().tabRect(a);
  env.rightClick(ra.x + 10, ra.y + 5);
  env.clickItem("Close");
  CHECK_EQ(env.tabCount(), static_cast<size_t>(0));
}

TEST(ctxmenu_close_others_keeps_modified_tabs_open) {
  Env env;
  env.bench->openFile("b.cpp");
  env.bench->sync();
  env.frame();
  env.ed->document(env.dir / "b.cpp")->insert({0, 0}, "x");  // b.cpp is modified
  env.bench->sync();
  const int a = env.tabId("a.cpp");
  const RectF ra = env.panel()->tabs().tabRect(a);
  env.rightClick(ra.x + 10, ra.y + 5);
  env.clickItem("Close All But This");
  CHECK_EQ(env.tabCount(), static_cast<size_t>(2));  // nothing was lost
  CHECK(env.app->message().find("modified") != std::string::npos);
}

TEST(ctxmenu_status_bar_segments_have_menus) {
  Env env;
  const RectF pos = env.bench->status().segmentRect("pos");
  REQUIRE(pos.w > 0);
  env.rightClick(pos.x + 2, pos.y + 2);
  REQUIRE(env.menu() != nullptr);
  CHECK(env.has("Go To Line..."));
  CHECK_EQ(env.item("Go To Line...").shortcut, std::string("Control G"));
  env.clickItem("Go To Line...");
  CHECK(env.menu() == nullptr);
  CHECK(env.bench->modal() != nullptr);  // the Go To Line input
  env.key(Key::Escape);
  // The line ending segment offers both endings.
  const RectF eol = env.bench->status().segmentRect("eol");
  REQUIRE(eol.w > 0);
  env.rightClick(eol.x + 2, eol.y + 2);
  REQUIRE(env.menu() != nullptr);
  CHECK(env.has("Windows (CR LF)") && env.has("Unix (LF)") && env.has("Macintosh (CR)"));
  env.clickItem("Unix (LF)");
  CHECK(env.ed->currentDocument()->buffer().lineEnding() == LineEnding::LF);
  // A segment without a menu (the message area when empty / the workspace name) shows none.
  env.rightClick(1, 599);
  CHECK(env.menu() == nullptr);
}

TEST(ctxmenu_providers_extend_the_menus_for_widgets_of_the_application) {
  Env env;
  // A stand-in for the explorer tree: a widget that supplies its own menu through the hook.
  struct Probe : Widget {};
  auto probe = std::make_unique<Probe>();
  Probe* raw = probe.get();
  env.bench->addChild(std::move(probe));
  raw->setTopLayer(true);
  raw->layout({0, 0, 40, 40});
  int ran = 0;
  bool sawKeyboard = false;
  env.app->addContextMenuProvider([&](Widget& target, float, float, bool keyboard, EditorApp::ContextMenuRequest& out) {
    if (&target != raw) return false;
    sawKeyboard = keyboard;
    MenuItem m;
    m.label = "Open";
    m.action = [&] { ++ran; };
    out.items.push_back(m);
    out.items.push_back(MenuItem::sep());
    for (MenuItem& c : env.app->buildCommandMenu({{"Build.BuildCurrentFile", "Build Current File"}}))
      out.items.push_back(c);
    return true;
  });
  env.rightClick(10, 10);
  REQUIRE(env.menu() != nullptr);
  CHECK(!sawKeyboard);
  env.clickItem("Open");
  CHECK_EQ(ran, 1);
  env.rightClick(10, 10);
  REQUIRE(env.menu() != nullptr);
  CHECK(env.has("Build Current File"));
  env.key(Key::Escape);
  // With the probe focused, Shift F10 asks the provider too.
  raw->setFocus();
  env.key(Key::F10, ModShift);
  REQUIRE(env.menu() != nullptr);
  CHECK(sawKeyboard);
  env.key(Key::Escape);
}

TEST(ctxmenu_right_click_elsewhere_still_reaches_the_widgets_and_pickers_swallow_it) {
  Env env;
  env.app->runCommand("Editor.GotoLine");
  REQUIRE(env.bench->modal() != nullptr);
  env.rightClick(300, 300);  // a picker is open: no context menu
  CHECK(env.menu() == nullptr);
  env.app->closePickers();
}

TEST(ctxmenu_vcs_change_entries_only_on_a_changed_line) {
  SKIP_WITHOUT_GIT();
  Env env("one\ntwo\nthree\nfour\nfive\n", true);
  const fs::path file = env.dir / "a.cpp";
  auto until = [&](const std::function<bool()>& f) {
    for (int i = 0; i < 3000 && !f(); ++i) {
      env.frame();
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return f();
  };
  REQUIRE(until([&] { return env.ed->vcs().hasBase(file); }));
  Document* d = env.ed->document(file);
  d->replace({{1, 0}, {1, 3}}, "TWO");
  REQUIRE(
      until([&] { return env.ed->vcs().hunks(file).size() == 1 && env.ed->vcs().hunksVersion(file) == d->version(); }));
  const auto [x1, y1] = env.centreOf({1, 1});
  env.rightClick(x1, y1);
  REQUIRE(env.menu() != nullptr);
  CHECK(env.has("Revert Change"));
  env.key(Key::Escape);
  const auto [x2, y2] = env.centreOf({3, 1});
  env.rightClick(x2, y2);
  REQUIRE(env.menu() != nullptr);
  CHECK(!env.has("Revert Change"));
  env.key(Key::Escape);
  env.rightClick(x1, y1);
  env.clickItem("Revert Change");
  CHECK_EQ(d->buffer().line(1), std::string("two"));
}

// --- golden ---------------------------------------------------------------------------------------------------------

namespace {
std::string monoFont() {
  for (const char* p : {"/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
                        "/usr/share/fonts/dejavu/DejaVuSansMono.ttf", "/usr/share/fonts/TTF/DejaVuSansMono.ttf"})
    if (std::ifstream(p).good()) return p;
  return {};
}
}  // namespace

TEST(ctxmenu_golden_vs_dark_editor_menu) {
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
  CHECK(vs.menuBackground.a > 0 && vs.menuSelection.a > 0);
  const fs::path tmp = uitest::tempRoot() / "tf_w8f_golden";  // the folder name is in the status bar: fixed
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
    int64_t now = 1000;
    gfx::GlRenderer& r = *fx->renderer;
    r.setOffscreen(true);
    wb.render(r, 1024, 640);
    ed.moveCursor({3, 8});
    app.tick(now += 10);
    wb.render(r, 1024, 640);
    const RectF c = static_cast<TextView*>(wb.focused())->caretRect({3, 8});
    app.handleEvent(mouseEv(InputType::MouseDown, c.x + 1, c.y + 2, MouseButton::Right), now += 10);
    app.handleEvent(mouseEv(InputType::MouseUp, c.x + 1, c.y + 2, MouseButton::Right), now += 10);
    REQUIRE(app.contextMenu() != nullptr);
    app.contextMenu()->setSelected(4);  // Go To Definition highlighted
    const int w = 1024, h = 640;
    wb.render(r, w, h);
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4);
    REQUIRE(r.readPixels(w, h, px.data()));
    tr->releaseTexture(r);
    const std::string path = TENFOLD_SOURCE_DIR "/tests/golden/w8f_context_menu_vs_dark.png";
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
          gfx::writePngFile("w8f_context_menu_vs_dark.actual.png", px.data(), w, h);
          std::cerr << "    " << bad << " pixels differ; wrote w8f_context_menu_vs_dark.actual.png\n";
        }
        CHECK(bad <= golden.size() / 4 / 500);
      }
    }
    app.closePickers();
  }
  fs::remove_all(tmp, rm);
}

TEST(ctxmenu_right_click_in_another_panel_makes_it_active) {
  Env env;
  Panel* first = env.bench->activePanel();
  Panel* second = env.bench->splitActive(Splitter::Orientation::Horizontal);
  env.bench->setActivePanel(first);
  env.frame();
  REQUIRE(second != nullptr);
  CHECK(env.bench->activePanel() == first);
  TextView* v = dynamic_cast<TextView*>(second->activeContent());
  REQUIRE(v != nullptr);
  const RectF b = v->bounds();
  env.rightClick(b.x + b.w / 2, b.y + 40);
  REQUIRE(env.menu() != nullptr);
  CHECK(env.bench->activePanel() == second);
  env.key(Key::Escape);
  CHECK(env.bench->focused() == v);
}

// --- the workspace explorer tree's menu -----------------------------------------------------------------------------

namespace {

struct TreeEnv {
  fs::path dir;
  std::unique_ptr<Editor> ed;
  std::unique_ptr<gfx::TextRenderer> text;
  NullRenderer nr;
  std::unique_ptr<Workbench> bench;
  std::unique_ptr<EditorApp> app;
  std::string clip;
  std::vector<std::string> shown;  // paths the file manager was asked for
  int64_t now = 1000;

  TreeEnv() {
    static int n = 0;
    dir = uitest::tempRoot() / ("tf_w8f_treemenu_" + std::to_string(n++) + "_" +
                                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir);
    fs::copy(fs::path(TENFOLD_SOURCE_DIR) / "tests/fixtures/vs/Explorer", dir, fs::copy_options::recursive);
    text = mockText();
    Editor::Options o;
    o.semantic.useClangd = false;
    o.semantic.persistFastIndex = false;
    o.watchForcePolling = true;
    o.watchPollMs = 60000;
    ed = std::make_unique<Editor>(dir, o);
    ed->start();
    bench = std::make_unique<Workbench>(*text, *ed);
    app = std::make_unique<EditorApp>(*bench, *ed, bench.get());
    ClipboardBackend cb;
    cb.get = [this] { return clip; };
    cb.set = [this](const std::string& s) {
      clip = s;
      return true;
    };
    setClipboardBackend(cb);
    OsLauncher l;
    l.showInFolder = [this](const fs::path& p, std::string*) {
      shown.push_back(p.generic_string());
      return true;
    };
    app->setLauncher(l);
    app->runCommand("View.ToggleWorkspaceExplorer");
    tree().expandAll(true);
    frame();
  }
  ~TreeEnv() {
    setClipboardBackend({});
    app.reset();
    bench.reset();
    ed.reset();
    std::error_code ec;
    fs::remove_all(dir, ec);
  }
  TreeView& tree() { return *app->explorer()->view(); }
  void frame() {
    ed->pump();
    app->tick(now);
    bench->render(nr, 1000, 600);
  }
  bool send(const InputEvent& e) {
    now += 10;
    const bool r = app->handleEvent(e, now);
    frame();
    return r;
  }
  int rowNamed(const std::string& name) {
    for (size_t i = 0; i < tree().rows().size(); ++i)
      if (tree().model().node(tree().rows()[i].node).name == name) return static_cast<int>(i);
    return -1;
  }
  void rightClickRow(int row) {
    const RectF r = tree().rowRect(row);
    send(mouseEv(InputType::MouseDown, r.x + r.w * 0.6f, r.y + r.h / 2, MouseButton::Right));
    send(mouseEv(InputType::MouseUp, r.x + r.w * 0.6f, r.y + r.h / 2, MouseButton::Right));
  }
  ContextMenu* menu() { return app->contextMenu(); }
  bool has(const std::string& l) { return find(l) >= 0; }
  int find(const std::string& l) {
    if (!menu()) return -1;
    for (size_t i = 0; i < menu()->items().size(); ++i)
      if (menu()->items()[i].label == l) return static_cast<int>(i);
    return -1;
  }
  void clickItem(const std::string& l) {
    const int i = find(l);
    REQUIRE(i >= 0);
    const RectF r = menu()->itemRect(0, i);
    send(mouseEv(InputType::MouseDown, r.x + r.w / 2, r.y + r.h / 2));
    send(mouseEv(InputType::MouseUp, r.x + r.w / 2, r.y + r.h / 2));
  }
  std::string selectedName() { return tree().model().node(tree().selectedNode()).name; }
};

}  // namespace

TEST(ctxmenu_tree_right_click_on_a_file_selects_it_and_offers_file_items) {
  TreeEnv env;
  const int row = env.rowNamed("main.cpp");
  REQUIRE(row >= 0);
  env.rightClickRow(row);
  REQUIRE(env.menu() != nullptr);
  CHECK_EQ(env.selectedName(), std::string("main.cpp"));
  for (const char* l : {"Open", "Collapse All", "Open Containing Folder", "Copy Full Path", "Build Current File"})
    CHECK(env.has(l));
  CHECK(!env.has("Expand") && !env.has("Collapse"));
  env.clickItem("Copy Full Path");
  CHECK_EQ(fs::path(env.clip).filename().string(), std::string("main.cpp"));  // the node's file, not the open one
  env.rightClickRow(env.rowNamed("main.cpp"));
  env.clickItem("Open Containing Folder");
  REQUIRE_EQ(env.shown.size(), static_cast<size_t>(1));
  CHECK(env.shown[0].find("main.cpp") != std::string::npos);
  env.rightClickRow(env.rowNamed("main.cpp"));
  env.clickItem("Open");
  REQUIRE(env.ed->currentDocument() != nullptr);
  CHECK_EQ(env.ed->currentDocument()->path().filename().string(), std::string("main.cpp"));
}

TEST(ctxmenu_tree_project_and_folder_nodes_have_no_file_items) {
  TreeEnv env;
  const int row = env.rowNamed("App");
  REQUIRE(row >= 0);
  env.rightClickRow(row);
  REQUIRE(env.menu() != nullptr);
  CHECK_EQ(env.selectedName(), std::string("App"));
  CHECK(env.has("Collapse") && env.has("Collapse All") && env.has("Copy Full Path") &&
        env.has("Open Containing Folder"));
  CHECK(!env.has("Open") && !env.has("Build Current File"));
  CHECK(!env.has("Settings..."));  // no workspace settings UI to open (OPEN_QUESTIONS)
  env.clickItem("Collapse");
  CHECK(!env.tree().isExpanded(env.tree().selectedNode()));
  env.rightClickRow(env.rowNamed("App"));
  CHECK(env.has("Expand"));
  env.clickItem("Collapse All");
  CHECK(env.tree().rows().size() <= 4);  // the root open, nothing below
  // A right click below the last row shows no menu.
  const RectF l = env.tree().listRect();
  env.send(mouseEv(InputType::MouseDown, l.x + 20, l.y + l.h - 2, MouseButton::Right));
  CHECK(env.menu() == nullptr);
}

TEST(ctxmenu_tree_shift_f10_opens_the_menu_of_the_selected_node_at_its_row) {
  TreeEnv env;
  const int row = env.rowNamed("main.cpp");
  REQUIRE(row >= 0);
  env.tree().select(env.tree().rows()[static_cast<size_t>(row)].node);
  env.tree().setFocus();
  env.frame();
  env.send(keyEv(Key::F10, ModShift));
  REQUIRE(env.menu() != nullptr);
  CHECK_EQ(env.menu()->selected(), 0);
  CHECK_EQ(env.menu()->items()[0].label, std::string("Open"));
  const RectF r = env.tree().rowRect(env.tree().selectedRow());
  CHECK(env.menu()->menuRect().y >= r.y + r.h - 1);
  env.send(keyEv(Key::Escape));
  CHECK(env.menu() == nullptr);
  env.send(keyEv(Key::Menu));
  REQUIRE(env.menu() != nullptr);
  env.send(keyEv(Key::Enter));  // the first item: Open
  CHECK_EQ(env.ed->currentDocument()->path().filename().string(), std::string("main.cpp"));
}

// VS-TERM (M4): "Open in Terminal" on an explorer node opens a terminal in that folder (a file: its folder).
TEST(ctxmenu_tree_open_in_terminal_uses_the_node_s_folder) {
#ifndef _WIN32
  if (!Pty::available()) return;
  TreeEnv env;
  env.app->terminalPanel()->wake = [] {};  // no SDL window in these tests
  Settings st;
  st.loadTexts("terminal.command = /bin/sh -c \"echo CWD=$(pwd -P); sleep 60\"\n", "");
  env.app->applySettings(st);
  auto screenOf = [&](TerminalView* v) {
    std::string out;
    for (int r = 0; r < v->session()->screen().rows(); ++r) out += v->visibleRowText(r) + "\n";
    return out;
  };
  auto cwdOf = [&](int id) -> std::string {
    TerminalView* v = env.app->terminalPanel()->view(id);
    std::string sc;
    for (int i = 0; i < 5000 && sc.find("CWD=") == std::string::npos; ++i) {
      env.frame();
      sc = screenOf(v);
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    const size_t p = sc.find("CWD=");
    if (p == std::string::npos) return "(no CWD line)";
    return sc.substr(p + 4, sc.find('\n', p) - p - 4);
  };
  // A file node: the terminal starts in the file's folder, not the workspace root.
  int row = env.rowNamed("main.cpp");
  REQUIRE(row >= 0);
  const fs::path file = pathFromUtf8(env.tree().model().node(env.tree().rows()[static_cast<size_t>(row)].node).path);
  env.rightClickRow(row);
  REQUIRE(env.has("Open in Terminal"));
  env.clickItem("Open in Terminal");
  auto ids = env.app->terminalPanel()->terminals();
  REQUIRE_EQ(ids.size(), static_cast<size_t>(1));
  CHECK_EQ(cwdOf(ids.back()), fs::canonical(file.parent_path()).string());
  CHECK(fs::canonical(file.parent_path()) != fs::canonical(env.dir));
  // A folder node: its own folder.
  row = env.rowNamed("App");
  REQUIRE(row >= 0);
  fs::path node = pathFromUtf8(env.tree().model().node(env.tree().rows()[static_cast<size_t>(row)].node).path);
  std::error_code ec;
  const fs::path expect = fs::is_directory(node, ec) ? node : node.parent_path();
  env.rightClickRow(row);
  env.clickItem("Open in Terminal");
  ids = env.app->terminalPanel()->terminals();
  REQUIRE_EQ(ids.size(), static_cast<size_t>(2));
  CHECK_EQ(cwdOf(ids.back()), fs::canonical(expect).string());
  // NewTerminal still starts in the workspace root.
  env.app->runCommand("Terminal.New");
  ids = env.app->terminalPanel()->terminals();
  REQUIRE_EQ(ids.size(), static_cast<size_t>(3));
  CHECK_EQ(cwdOf(ids.back()), fs::canonical(env.dir).string());
#endif
}
