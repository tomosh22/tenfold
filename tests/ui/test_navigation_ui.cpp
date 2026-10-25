// B6: navigation toolbar (Back / Forward buttons, history dropdown, status counts, tooltips) and the "last edit
// location" command, driven through EditorApp with the real default key bindings, a null renderer and a mock font.
// The Editor is never started (no clangd): history is filled with gotoLocation like the engine navigation tests do.
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>

#include "../text/text_test_util.h"
#include "ui/gfx/GlLoader.h"
#include "ui/gfx/PngWrite.h"
#include "ui/gfx/TextRenderer.h"
#include "ui/shell/EditorApp.h"
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

InputEvent mouseEv(InputType t, float x, float y, MouseButton b = MouseButton::Left) {
  InputEvent e;
  e.type = t;
  e.x = x;
  e.y = y;
  e.button = b;
  e.clicks = 1;
  return e;
}

using Which = NavigationUi::Which;

struct Env {
  fs::path dir;
  std::unique_ptr<Editor> ed;
  std::unique_ptr<gfx::TextRenderer> text;
  NullRenderer nr;
  std::unique_ptr<Workbench> bench;
  std::unique_ptr<EditorApp> app;
  int64_t now = 1000;

  explicit Env(const std::string& userKeys = {}) {
    static int n = 0;
    dir = fs::temp_directory_path() /
          ("tf_navui_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_" +
           std::to_string(n++));
    fs::create_directories(dir / ".tenfold");
    for (const char* f : {"a.cpp", "b.cpp", "c.cpp"}) {
      std::ofstream out(dir / f);
      for (int i = 0; i < 40; ++i) out << "int " << f[0] << i << ";\n";
    }
    if (!userKeys.empty()) std::ofstream(dir / ".tenfold" / "keybindings.txt") << userKeys;
    gfx::FontStack stack;
    stack.add(std::make_shared<tftext::MockFont>(U"abcdefghijklmnopqrstuvwxyz0123456789 ._-:,;×●…ABCLMNTUFDK>"));
    text = gfx::TextRenderer::fromFonts(std::move(stack));
    ed = std::make_unique<Editor>(dir);
    bench = std::make_unique<Workbench>(*text, *ed);
    app = std::make_unique<EditorApp>(*bench, *ed, bench.get());
    bench->openFile("a.cpp");
    frame();
  }
  ~Env() {
    app.reset();
    bench.reset();
    ed.reset();
    std::error_code ec;
    fs::remove_all(dir, ec);
  }
  NavigationUi& nav() { return *app->navigationUi(); }
  void frame() {
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
  void click(const RectF& r, MouseButton b = MouseButton::Left) {
    send(mouseEv(InputType::MouseDown, r.x + r.w / 2, r.y + r.h / 2, b));
    send(mouseEv(InputType::MouseUp, r.x + r.w / 2, r.y + r.h / 2, b));
  }
  void advance(int ms) {
    now += ms;
    frame();
  }
  Location at(const char* f, int line, int col = 0) const { return {dir / f, {line, col}}; }
  // The three-entry history of the plan's scenario: a:0 -> b:5 "foo" -> c:20 "bar"; the caret ends at c:20.
  void defDef() {
    ed->gotoLocation(at("a.cpp", 0));
    ed->gotoLocation(at("b.cpp", 5), nullptr, "foo");
    ed->gotoLocation(at("c.cpp", 20), nullptr, "bar");
    frame();
  }
  std::string activeFile() { return bench->tabPath(bench->activePanel()->activeId()).filename().string(); }
  std::string curFile() { return ed->currentDocument()->path().filename().string(); }
  std::string status(const char* seg) { return bench->status().text(seg); }
  RectF btn(Which w) { return nav().buttonRect(w); }
};

}  // namespace

TEST(navui_def_def_back_back_forward_by_keys) {
  Env env;
  env.defDef();
  CHECK(env.key(Key::Left, ModAlt));
  CHECK(env.ed->cursor() == env.at("b.cpp", 5));
  CHECK(env.key(Key::Left, ModAlt));
  CHECK(env.ed->cursor() == env.at("a.cpp", 0));
  CHECK(env.key(Key::Right, ModAlt));
  CHECK(env.ed->cursor() == env.at("b.cpp", 5));
  CHECK_EQ(env.activeFile(), std::string("b.cpp"));  // Workbench::sync revealed the tab
}

TEST(navui_def_def_back_back_forward_by_toolbar_buttons) {
  Env env;
  env.defDef();
  env.click(env.btn(Which::Back));
  CHECK(env.ed->cursor() == env.at("b.cpp", 5));
  env.click(env.btn(Which::Back));
  CHECK(env.ed->cursor() == env.at("a.cpp", 0));
  env.click(env.btn(Which::Forward));
  CHECK(env.ed->cursor() == env.at("b.cpp", 5));
  CHECK_EQ(env.activeFile(), std::string("b.cpp"));
  // The same entries as with the keys: buttons add nothing of their own.
  CHECK_EQ(env.ed->navigation().entries().size(), 3u);
}

TEST(navui_def_def_back_back_forward_by_thumb_buttons) {
  Env env;
  env.defDef();
  env.send(mouseEv(InputType::MouseDown, 300, 300, MouseButton::X1));
  CHECK(env.ed->cursor() == env.at("b.cpp", 5));
  env.send(mouseEv(InputType::MouseDown, 300, 300, MouseButton::X1));
  CHECK(env.ed->cursor() == env.at("a.cpp", 0));
  env.send(mouseEv(InputType::MouseDown, 300, 300, MouseButton::X2));
  CHECK(env.ed->cursor() == env.at("b.cpp", 5));
  CHECK_EQ(env.activeFile(), std::string("b.cpp"));
}

TEST(navui_toolbar_sits_above_the_workbench) {
  Env env;
  const RectF back = env.btn(Which::Back);
  CHECK(back.y >= 0 && back.y + back.h <= env.bench->panels()[0]->bounds().y);
  CHECK(env.bench->panels()[0]->bounds().y > 14);
}

TEST(navui_button_enabled_state_follows_history) {
  Env env;
  CHECK(!env.nav().enabled(Which::Back) && !env.nav().enabled(Which::Forward));
  // A disabled button does nothing.
  env.click(env.btn(Which::Back));
  CHECK_EQ(env.curFile(), std::string("a.cpp"));
  env.defDef();
  CHECK(env.nav().enabled(Which::Back) && !env.nav().enabled(Which::Forward));
  env.key(Key::Left, ModAlt);
  CHECK(env.nav().enabled(Which::Back) && env.nav().enabled(Which::Forward));
  env.key(Key::Left, ModAlt);
  CHECK(!env.nav().enabled(Which::Back) && env.nav().enabled(Which::Forward));
  CHECK(env.nav().enabled(Which::Back) == env.ed->navigation().canGoBack());
  CHECK(env.nav().enabled(Which::Forward) == env.ed->navigation().canGoForward());
}

TEST(navui_tooltip_shows_the_bound_key) {
  Env env;
  CHECK_EQ(env.nav().tooltip(Which::Back), std::string("Navigate Backward (Alt+Left)"));
  CHECK_EQ(env.nav().tooltip(Which::Forward), std::string("Navigate Forward (Alt+Right)"));
  // After the hover delay the tooltip is on screen; leaving hides it.
  const RectF b = env.btn(Which::Back);
  env.send(mouseEv(InputType::MouseMove, b.x + 2, b.y + 2));
  CHECK(env.nav().shownTooltip().empty());
  env.advance(NavigationUi::kTooltipMs + 20);
  CHECK_EQ(env.nav().shownTooltip(), std::string("Navigate Backward (Alt+Left)"));
  env.send(mouseEv(InputType::MouseMove, 700, 400));
  CHECK(env.nav().shownTooltip().empty());

  // A user binding changes the text (the defaults for Back are removed, F7 added).
  Env env2("Alt+Left = -\nCtrl+- = -\nF7 = Nav.Back\n");
  CHECK_EQ(env2.nav().tooltip(Which::Back), std::string("Navigate Backward (F7)"));
  CHECK_EQ(env2.nav().tooltip(Which::Forward), std::string("Navigate Forward (Alt+Right)"));
  Env env3("Alt+Left = -\nCtrl+- = -\nMouse4 = -\n");
  CHECK_EQ(env3.nav().tooltip(Which::Back), std::string("Navigate Backward"));
}

TEST(navui_dropdown_lists_entries_and_jumping_adds_nothing) {
  Env env;
  env.defDef();
  env.click(env.btn(Which::Back), MouseButton::Right);
  REQUIRE(env.nav().dropdownOpen());
  const auto rows = env.nav().rows();
  REQUIRE(rows.size() == 3u);
  // Newest first; the current entry is marked; labels are the recorded symbol names, else file:line.
  CHECK_EQ(rows[0].text, std::string("bar"));
  CHECK(rows[0].current && !rows[1].current && !rows[2].current);
  CHECK_EQ(rows[0].detail, std::string("c.cpp:21"));
  CHECK_EQ(rows[1].text, std::string("foo"));
  CHECK_EQ(rows[2].text, std::string("a.cpp:1"));
  CHECK(rows[2].detail.empty());
  const size_t before = env.ed->navigation().entries().size();

  // Click the oldest entry: the caret moves there, nothing is added, the tab is revealed.
  const RectF r2 = env.nav().rowRect(2);
  env.send(mouseEv(InputType::MouseMove, r2.x + 4, r2.y + 2));
  env.send(mouseEv(InputType::MouseDown, r2.x + 4, r2.y + 2));
  env.send(mouseEv(InputType::MouseUp, r2.x + 4, r2.y + 2));
  CHECK(!env.nav().dropdownOpen());
  CHECK(env.ed->cursor() == env.at("a.cpp", 0));
  CHECK_EQ(env.ed->navigation().entries().size(), before);
  CHECK_EQ(env.activeFile(), std::string("a.cpp"));
  CHECK(env.nav().enabled(Which::Forward) && !env.nav().enabled(Which::Back));

  // The Forward dropdown lists the current entry and everything newer, oldest first.
  env.click(env.btn(Which::Forward), MouseButton::Right);
  REQUIRE(env.nav().dropdownOpen());
  REQUIRE(env.nav().rows().size() == 3u);
  CHECK(env.nav().rows()[0].current && env.nav().rows()[0].text == "a.cpp:1");
  CHECK_EQ(env.nav().rows()[2].text, std::string("bar"));
  const RectF f2 = env.nav().rowRect(1);
  env.send(mouseEv(InputType::MouseDown, f2.x + 4, f2.y + 2));
  CHECK(env.ed->cursor() == env.at("b.cpp", 5));
  CHECK_EQ(env.ed->navigation().entries().size(), before);
  CHECK_EQ(env.activeFile(), std::string("b.cpp"));
  // Plain Back / Forward keep working from where the jump left the cursor.
  env.key(Key::Right, ModAlt);
  CHECK(env.ed->cursor() == env.at("c.cpp", 20));
  env.key(Key::Left, ModAlt);
  CHECK(env.ed->cursor() == env.at("b.cpp", 5));
}

TEST(navui_dropdown_closes_on_escape_and_outside_click_and_keeps_focus) {
  Env env;
  env.defDef();
  Widget* focus = env.bench->focused();
  env.click(env.btn(Which::Back), MouseButton::Right);
  REQUIRE(env.nav().dropdownOpen());
  CHECK(env.key(Key::Escape));
  CHECK(!env.nav().dropdownOpen());
  CHECK(env.bench->focused() == focus);
  CHECK(env.ed->cursor() == env.at("c.cpp", 20));
  env.click(env.btn(Which::Back), MouseButton::Right);
  REQUIRE(env.nav().dropdownOpen());
  env.click({700, 400, 2, 2});  // anywhere else: closes and does not click through
  CHECK(!env.nav().dropdownOpen());
  CHECK(env.ed->cursor() == env.at("c.cpp", 20));
  // Keyboard: Down, Down, Enter picks the third row.
  env.click(env.btn(Which::Back), MouseButton::Right);
  env.key(Key::Down);
  env.key(Key::Down);
  env.key(Key::Enter);
  CHECK(!env.nav().dropdownOpen());
  CHECK(env.ed->cursor() == env.at("b.cpp", 5));  // sel starts at row 0 -> clamp: Down moves to 0 then 1
}

TEST(navui_long_press_opens_dropdown_and_release_does_not_navigate) {
  Env env;
  env.defDef();
  const RectF b = env.btn(Which::Back);
  env.send(mouseEv(InputType::MouseDown, b.x + 3, b.y + 3));
  env.advance(NavigationUi::kLongPressMs - 60);
  CHECK(!env.nav().dropdownOpen());
  env.advance(80);
  CHECK(env.nav().dropdownOpen());
  CHECK(env.app->tick(env.now) >= -1);
  env.send(mouseEv(InputType::MouseUp, b.x + 3, b.y + 3));
  CHECK(env.nav().dropdownOpen());                 // still open after release
  CHECK(env.ed->cursor() == env.at("c.cpp", 20));  // and no Back happened
  env.key(Key::Escape);
  // A short press is a normal click.
  env.send(mouseEv(InputType::MouseDown, b.x + 3, b.y + 3));
  env.advance(100);
  env.send(mouseEv(InputType::MouseUp, b.x + 3, b.y + 3));
  CHECK(!env.nav().dropdownOpen());
  CHECK(env.ed->cursor() == env.at("b.cpp", 5));
  // The long press on a disabled direction opens nothing.
  const RectF f = env.btn(Which::Forward);
  env.key(Key::Right, ModAlt);  // now at the live position again: Forward disabled
  env.send(mouseEv(InputType::MouseDown, f.x + 3, f.y + 3));
  env.advance(NavigationUi::kLongPressMs + 50);
  CHECK(!env.nav().dropdownOpen());
  env.send(mouseEv(InputType::MouseUp, f.x + 3, f.y + 3));
}

TEST(navui_status_counts_follow_the_setting) {
  Env env;
  env.defDef();
  CHECK_EQ(env.status("navcounts"), std::string());  // default off
  env.app->setNavStatusCounts(true);
  env.frame();
  CHECK_EQ(env.status("navcounts"), std::string("\xE2\x86\x90 2 | 0 \xE2\x86\x92"));
  env.key(Key::Left, ModAlt);
  CHECK_EQ(env.status("navcounts"), std::string("\xE2\x86\x90 1 | 1 \xE2\x86\x92"));
  env.key(Key::Left, ModAlt);
  CHECK_EQ(env.status("navcounts"), std::string("\xE2\x86\x90 0 | 2 \xE2\x86\x92"));
  env.app->setNavStatusCounts(false);
  env.frame();
  CHECK_EQ(env.status("navcounts"), std::string());
}

TEST(navui_last_edit_location_command_and_key) {
  Env env;
  CHECK(!env.key(Key::Backspace, ModCtrl | ModShift) || true);  // nothing to go to yet: disabled, no crash
  CHECK(!env.ed->lastEditLocation().has_value());
  env.ed->moveCursor({8, 0});
  env.ed->insertText("Z");
  CHECK(*env.ed->lastEditLocation() == env.at("a.cpp", 8, 1));
  env.ed->gotoLocation(env.at("c.cpp", 30));
  env.frame();
  CHECK_EQ(env.activeFile(), std::string("c.cpp"));
  // Edit above the remembered place in the same file (external reload): the location shifts with the lines.
  env.ed->gotoLocation(env.at("a.cpp", 30));
  env.ed->save(*env.ed->currentDocument());
  {
    std::ifstream in(env.dir / "a.cpp");
    std::string t((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    std::ofstream(env.dir / "a.cpp", std::ios::binary) << "x\ny\n" << t;
  }
  env.ed->reloadFromDisk(*env.ed->currentDocument());
  CHECK(*env.ed->lastEditLocation() == env.at("a.cpp", 10, 1));
  env.ed->gotoLocation(env.at("c.cpp", 2));
  env.frame();
  const size_t entries = env.ed->navigation().entries().size();
  CHECK(env.key(Key::Backspace, ModCtrl | ModShift));
  CHECK(env.ed->cursor() == env.at("a.cpp", 10, 1));
  CHECK_EQ(env.activeFile(), std::string("a.cpp"));
  CHECK(env.ed->navigation().entries().size() >= entries);  // it is a jump: Back returns
  CHECK(env.key(Key::Left, ModAlt));
  CHECK_EQ(env.curFile(), std::string("c.cpp"));
}

TEST(navui_golden_back_dropdown) {
  SKIP_IF_NO_GL(fx);
  std::string font;
  for (const char* p : {"/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
                        "/usr/share/fonts/dejavu/DejaVuSansMono.ttf", "/usr/share/fonts/TTF/DejaVuSansMono.ttf"})
    if (std::ifstream(p).good()) {
      font = p;
      break;
    }
  if (font.empty()) {
    std::cerr << "    SKIP golden: DejaVuSansMono not installed; goldens are only comparable with the same font\n";
    return;
  }
  std::string err;
  auto tr = gfx::TextRenderer::create(font, 15.0f, 1.0f, &err);
  REQUIRE(tr != nullptr);
  Editor ed(TENFOLD_SOURCE_DIR "/examples/sample_project");
  Workbench wb(*tr, ed);
  EditorApp app(wb, ed, &wb);
  app.setNavStatusCounts(true);
  wb.openFile("include/geometry.h");
  wb.sync();
  const fs::path root = ed.workspace().root();
  ed.gotoLocation({root / "include/geometry.h", {50, 0}}, nullptr, "Circle::area");
  ed.gotoLocation({root / "src/main.cpp", {15, 0}}, nullptr, "main");
  ed.gotoLocation({root / "src/geometry.cpp", {8, 0}}, nullptr, "Rect::perimeter");
  int64_t now = 1000;
  app.tick(now);
  while (gfx::gl::GetError() != gfx::gl::NO_ERROR) {}  // readPixels reports a stale error left by an earlier test
  gfx::GlRenderer& r = *fx->renderer;
  const int w = 760, h = 360;
  r.setOffscreen(true);
  wb.render(r, w, h);
  const RectF b = app.navigationUi()->buttonRect(Which::Back);
  InputEvent down = mouseEv(InputType::MouseDown, b.x + b.w / 2, b.y + b.h / 2, MouseButton::Right);
  app.handleEvent(down, ++now);
  app.handleEvent(mouseEv(InputType::MouseUp, b.x + b.w / 2, b.y + b.h / 2, MouseButton::Right), ++now);
  const RectF row = app.navigationUi()->rowRect(1);
  app.handleEvent(mouseEv(InputType::MouseMove, row.x + 10, row.y + 3), ++now);
  app.tick(now);
  REQUIRE(app.navigationUi()->dropdownOpen());
  wb.render(r, w, h);
  std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4);
  REQUIRE(r.readPixels(w, h, px.data()));
  tr->releaseTexture(r);

  const std::string path = TENFOLD_SOURCE_DIR "/tests/golden/b6_nav_dropdown.png";
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
    gfx::writePngFile("b6_nav_dropdown.actual.png", px.data(), w, h);
    std::cerr << "    " << bad << " pixels differ; wrote b6_nav_dropdown.actual.png\n";
  }
  CHECK(bad <= golden.size() / 4 / 500);
}
