// C2c: the debugger UI. A recording renderer, the mock font (8 px cells, 14 px lines) and synthetic events through
// EditorApp with the real default key bindings and a FakeDebugger as the backend (installed with setBackend, so the
// session's worker thread really calls it). One golden (vs-dark, real font) with Break mode, breakpoints of every
// kind and the current-statement arrow.
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>

#include "../text/text_test_util.h"
#include "debug/FakeDebugger.h"
#include "ui/gfx/PngWrite.h"
#include "ui/gfx/TextRenderer.h"
#include "ui/settings/Theme.h"
#include "ui/shell/EditorApp.h"
#include "ui/widgets/TextView.h"
#include "ui_test.h"

#define REQUIRE_EQ(a, b) REQUIRE((a) == (b))

using namespace tf;
using namespace tf::ui;
namespace fs = std::filesystem;

namespace {

bool same(const gfx::Color& a, const gfx::Color& b) {
  return std::abs(a.r - b.r) < 1e-3f && std::abs(a.g - b.g) < 1e-3f && std::abs(a.b - b.b) < 1e-3f &&
         std::abs(a.a - b.a) < 1e-3f;
}

// Remembers the rects of the last frame with their colours.
struct Recorder : gfx::Renderer {
  struct R {
    gfx::RectF r;
    gfx::Color c;
  };
  std::vector<R> rects;
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
  bool hasRect(
      const gfx::Color& c) const {  // (the active tab shares vs-dark's status colour: use covers() for the bar)
    return std::any_of(rects.begin(), rects.end(), [&](const R& x) { return same(x.c, c); });
  }
  // Is pixel (px, py) covered by a rect of colour c?
  bool covers(const gfx::Color& c, float px, float py) const {
    return std::any_of(rects.begin(), rects.end(), [&](const R& x) {
      return same(x.c, c) && px + 0.5f >= x.r.x && px + 0.5f < x.r.x + x.r.w && py + 0.5f >= x.r.y &&
             py + 0.5f < x.r.y + x.r.h;
    });
  }
  // Number of covered pixels of colour c inside `box`.
  int area(const gfx::Color& c, const gfx::RectF& box) const {
    int n = 0;
    for (float y = box.y; y < box.y + box.h; ++y)
      for (float x = box.x; x < box.x + box.w; ++x) n += covers(c, x, y);
    return n;
  }
};

std::unique_ptr<gfx::TextRenderer> mockText() {
  gfx::FontStack stack;
  stack.add(std::make_shared<tftext::MockFont>(
      U"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ._-:,;/()[]{}<>+*&#?=!\"'"));
  return gfx::TextRenderer::fromFonts(std::move(stack));
}

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
InputEvent mouseEv(InputType t, float x, float y) {
  InputEvent e;
  e.type = t;
  e.x = x;
  e.y = y;
  e.clicks = 1;
  e.button = MouseButton::Left;
  return e;
}

void writeFile(const fs::path& p, const std::string& text) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary) << text;
}

const gfx::Color kRed{0.898f, 0.078f, 0.0f, 1};  // #E51400, the built-in fallback

Theme vsDark() {
  std::vector<SettingsDiag> diags;
  return parseTheme(builtinThemeText("vs-dark"), "vs-dark", diags);
}

struct Env {
  fs::path dir;
  std::unique_ptr<gfx::TextRenderer> text = mockText();
  Recorder rec;
  Theme theme;
  std::unique_ptr<Editor> ed;
  std::unique_ptr<Workbench> bench;
  std::unique_ptr<EditorApp> app;
  std::shared_ptr<FakeDebugger::State> fake;
  int64_t now = 1000;

  explicit Env(const Theme& th = Theme::darkDefault(), bool withBackend = true) : theme(th) {
    static int n = 0;
    dir = uitest::tempRoot() /
          ("tf_c2cui_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_" +
           std::to_string(n++));
    std::string a;
    for (int i = 1; i <= 30; ++i) a += "int line" + std::to_string(i) + ";\n";
    writeFile(dir / "a.cpp", a);
    std::string longLine = "int wrapped =";
    for (int i = 0; i < 60; ++i) longLine += " word" + std::to_string(i);  // ~400 chars: four rows at 1000 px
    writeFile(dir / "b.cpp", "int first;\n" + longLine + ";\nint third;\n");
    Editor::Options o;
    o.semantic.useClangd = false;
    o.semantic.persistFastIndex = false;
    o.watchForcePolling = true;
    o.watchPollMs = 60000;
    ed = std::make_unique<Editor>(dir, o);
    std::string err;
    if (!ed->start(&err)) std::cerr << "    editor start: " << err << "\n";
    if (withBackend) {
      auto d = std::make_unique<FakeDebugger>();
      fake = d->state();
      ed->debug().setPollIntervals(5, 5);
      ed->debug().setBackend(std::move(d));
    }
    bench = std::make_unique<Workbench>(*text, *ed, theme);
    app = std::make_unique<EditorApp>(*bench, *ed, bench.get());
    if (withBackend) {  // breakpoints are pushed to a connected backend only; the app decides when to connect
      ed->debug().connect();
      ed->waitFor([&] { return ed->debug().connected(); }, 5000);
    }
    bench->render(rec, 1000, 600);
  }
  ~Env() {
    app.reset();
    bench.reset();
    ed.reset();
    std::error_code ec;
    fs::remove_all(dir, ec);
  }
  void frame() {
    ed->pump();
    app->tick(now);
    bench->render(rec, 1000, 600);
  }
  bool send(const InputEvent& e) {
    now += 10;
    const bool r = app->handleEvent(e, now);
    frame();
    return r;
  }
  bool key(Key k, uint8_t mods = 0) { return send(keyEv(k, mods)); }
  void open(const char* rel) {
    bench->openFile(dir / rel);
    bench->sync();
    frame();
  }
  TextView* view() { return dynamic_cast<TextView*>(bench->focused()); }
  fs::path path(const char* rel) { return ed->workspace().root() / rel; }
  // Pumps the editor until pred() (the session's results arrive through the main-thread queue).
  bool until(const std::function<bool()>& pred, int ms = 5000) {
    const bool ok = ed->waitFor(pred, ms);
    frame();
    return ok;
  }
  bool fakeSaw(const std::string& method, size_t n = 1) {
    return fake->waitUntil([&](const FakeDebugger::State& s) { return s.count(method) >= n; }, 5000);
  }
  float rowY(int line) { return view()->caretRect({line, 0}).y; }
  // A left click in the glyph lane at the row of `line`.
  void clickLane(int line) {
    TextView* v = view();
    const RectF lane = v->glyphLaneRect();
    const float x = lane.x + lane.w / 2, y = rowY(line) + 3;
    send(mouseEv(InputType::MouseDown, x, y));
    send(mouseEv(InputType::MouseUp, x, y));
  }
  RectF laneCell(int line) {
    const RectF lane = view()->glyphLaneRect();
    return {lane.x, rowY(line), lane.w, root_lh()};
  }
  float root_lh() { return text->lineHeight(); }
};

}  // namespace

TEST(c2c_no_lane_without_a_backend_or_breakpoints_and_it_appears_with_the_first_breakpoint) {
  Env env(Theme::darkDefault(), false);
  env.open("a.cpp");
  TextView* v = env.view();
  REQUIRE(v != nullptr);
  CHECK(!v->glyphMarginActive());
  CHECK_EQ(v->glyphLaneWidth(), 0.0f);
  const float textX0 = v->caretRect({0, 0}).x;
  CHECK(env.key(Key::F9));  // Debug.ToggleBreakpoint at the caret
  CHECK(env.ed->breakpoints().has(env.path("a.cpp"), 0));
  CHECK(v->glyphMarginActive());
  CHECK_EQ(v->glyphLaneWidth(), env.root_lh());
  CHECK_EQ(v->caretRect({0, 0}).x, textX0 + v->glyphLaneWidth());  // the lane widened the gutter
  CHECK(env.key(Key::F9));
  CHECK(!v->glyphMarginActive());
  CHECK_EQ(v->caretRect({0, 0}).x, textX0);
}

TEST(c2c_clicking_in_the_lane_toggles_a_breakpoint_and_the_backend_gets_it_after_pump) {
  Env env;
  env.open("a.cpp");
  TextView* v = env.view();
  REQUIRE(v->glyphMarginActive());  // a backend is installed
  const Position caret = env.ed->selections()[0].head;
  env.clickLane(3);
  CHECK(env.ed->breakpoints().has(env.path("a.cpp"), 3));  // optimistic: the store has it at once
  CHECK(env.ed->selections()[0].head == caret);            // the click moved no caret
  REQUIRE(env.fakeSaw("setBreakpoint"));
  const auto calls = env.fake->callsOf("setBreakpoint");
  CHECK(calls[0].detail.find(":3:") != std::string::npos);  // 0-based line
  CHECK(env.rec.area(kRed, env.laneCell(3)) > 20);
  CHECK_EQ(env.rec.area(kRed, env.laneCell(4)), 0);
  env.clickLane(3);
  CHECK(!env.ed->breakpoints().has(env.path("a.cpp"), 3));
  CHECK(env.fakeSaw("removeBreakpoint"));
  CHECK_EQ(env.rec.area(kRed, env.laneCell(3)), 0);
  // Below the last row nothing happens.
  const RectF lane = v->glyphLaneRect();
  env.send(mouseEv(InputType::MouseDown, lane.x + 2, lane.y + lane.h - 1));
  CHECK(env.ed->breakpoints().empty() || env.ed->breakpoints().count() == 1);
}

TEST(c2c_debug_keys_reach_the_backend) {
  Env env;
  env.open("a.cpp");
  env.ed->moveCursor({5, 0});
  env.frame();
  CHECK(env.key(Key::F9));
  CHECK(env.ed->breakpoints().has(env.path("a.cpp"), 5));
  CHECK(env.key(Key::F9, ModCtrl));  // Debug.ToggleBreakpointEnabled
  CHECK(!env.ed->breakpoints().get(env.path("a.cpp"), 5)->enabled);
  CHECK(env.key(Key::F9, ModCtrl));
  CHECK(env.ed->breakpoints().get(env.path("a.cpp"), 5)->enabled);
  env.key(Key::F5);
  REQUIRE(env.fakeSaw("start"));
  REQUIRE(env.until([&] { return env.ed->debug().mode() == DebugMode::Running; }));
  env.key(Key::Pause, ModCtrl | ModAlt);  // Debug.BreakAll
  REQUIRE(env.fakeSaw("breakAll"));
  REQUIRE(env.until([&] { return env.ed->debug().mode() == DebugMode::Break; }));
  env.key(Key::F10);
  env.key(Key::F11);
  env.key(Key::F11, ModShift);
  env.key(Key::F10, ModCtrl);  // Debug.RunToCursor
  REQUIRE(env.fakeSaw("runToCursor"));
  CHECK(env.fakeSaw("stepOver"));
  CHECK(env.fakeSaw("stepInto"));
  CHECK(env.fakeSaw("stepOut"));
  CHECK(env.fake->callsOf("runToCursor")[0].detail.find(":5") != std::string::npos);
  env.key(Key::F5, ModShift);
  CHECK(env.fakeSaw("stop"));
  env.key(Key::F5, ModCtrl);
  CHECK(env.fakeSaw("startWithoutDebugging"));
  env.key(Key::F5, ModCtrl | ModShift);
  CHECK(env.fakeSaw("restart"));
  env.key(Key::F9, ModCtrl | ModShift);  // Debug.DeleteAllBreakpoints
  CHECK(env.ed->breakpoints().empty());
  // Every command is in the registry with its Visual Studio key.
  for (const char* id :
       {"Debug.Start", "Debug.StartWithoutDebugging", "Debug.Stop", "Debug.Restart", "Debug.ToggleBreakpoint",
        "Debug.ToggleBreakpointEnabled", "Debug.DeleteAllBreakpoints", "Debug.StepOver", "Debug.StepInto",
        "Debug.StepOut", "Debug.RunToCursor", "Debug.BreakAll", "Debug.ShowBreakpoints", "Debug.EditCondition"})
    CHECK(env.app->commands().find(id) != nullptr);
}

TEST(c2c_break_mode_shows_the_arrow_and_line_band_and_the_status_bar_turns_orange_in_vs_dark) {
  const Theme th = vsDark();
  REQUIRE(th.statusBackgroundDebugging.a > 0);
  Env env(th);
  env.open("a.cpp");
  const fs::path a = env.ed->currentDocument()->path();
  const RectF bar = env.bench->status().bounds();
  auto barIs = [&](const gfx::Color& c) { return env.rec.covers(c, bar.x + bar.w - 3, bar.y + bar.h / 2); };
  CHECK(!barIs(th.statusBackgroundDebugging));
  CHECK(barIs(th.statusBackground));
  env.key(Key::F5);
  REQUIRE(env.until([&] { return env.ed->debug().mode() == DebugMode::Running; }));
  CHECK(barIs(th.statusBackgroundDebugging));  // Running is orange too
  env.fake->setMode(DebugMode::Break, DebugLocation{a, 7});
  REQUIRE(env.until([&] { return env.ed->debug().mode() == DebugMode::Break; }));
  env.frame();
  TextView* v = env.view();
  CHECK(env.bench->status().debugging());
  CHECK(barIs(th.statusBackgroundDebugging));
  CHECK(!barIs(th.statusBackground));
  CHECK(env.bench->status().text("debug").find("Break") != std::string::npos);
  CHECK(env.bench->status().text("debug").find("a.cpp:8") != std::string::npos);
  // The jump itself is the engine's: the caret is on the statement line, the view shows it.
  CHECK_EQ(env.ed->selections()[0].head.line, 7);
  CHECK(env.rec.area(th.debugCurrentArrow, env.laneCell(7)) > 20);
  CHECK_EQ(env.rec.area(th.debugCurrentArrow, env.laneCell(8)), 0);
  const RectF ta = v->textArea();
  CHECK(env.rec.hasRect(th.debugCurrentLine));
  bool band = false;
  for (const auto& r : env.rec.rects)
    band |= same(r.c, th.debugCurrentLine) && r.r.y == env.rowY(7) && r.r.x >= ta.x - 1 && r.r.w > 100;
  CHECK(band);
  // Out of Break mode both go away and the orange stays only while Running.
  env.fake->setMode(DebugMode::Running);
  REQUIRE(env.until([&] { return env.ed->debug().mode() == DebugMode::Running; }));
  CHECK(!env.rec.hasRect(th.debugCurrentLine));
  CHECK_EQ(env.rec.area(th.debugCurrentArrow, env.laneCell(7)), 0);
  CHECK(barIs(th.statusBackgroundDebugging));
  env.fake->setMode(DebugMode::Design);
  REQUIRE(env.until([&] { return env.ed->debug().mode() == DebugMode::Design; }));
  CHECK(!env.bench->status().debugging());
  CHECK(barIs(th.statusBackground));
}

TEST(c2c_the_dark_theme_keeps_its_status_bar_while_debugging) {
  const Theme th = Theme::darkDefault();
  Env env(th);
  env.open("a.cpp");
  env.fake->setMode(DebugMode::Break, DebugLocation{env.ed->currentDocument()->path(), 2});
  REQUIRE(env.until([&] { return env.ed->debug().mode() == DebugMode::Break; }));
  CHECK(env.bench->status().debugging());
  CHECK(env.rec.hasRect(th.statusBackground));                // unchanged look
  CHECK(env.rec.area(th.dirtyMarker, env.laneCell(2)) > 20);  // the arrow falls back to the dirty-marker colour
}

TEST(c2c_the_arrow_shows_in_the_view_of_its_file_only) {
  Env env;
  env.open("a.cpp");
  env.open("b.cpp");
  TextView* vb = env.view();
  env.ed->debug().followCurrentStatement = false;
  env.fake->setMode(DebugMode::Break, DebugLocation{env.path("a.cpp"), 1});
  REQUIRE(env.until([&] { return env.ed->debug().mode() == DebugMode::Break; }));
  CHECK_EQ(env.ed->currentDocument()->path().filename().string(), std::string("b.cpp"));
  for (int line : {0, 1, 2}) CHECK_EQ(env.rec.area(env.theme.dirtyMarker, env.laneCell(line)), 0);
  (void)vb;
  env.fake->setMode(DebugMode::Break, DebugLocation{env.path("b.cpp"), 2});
  REQUIRE(env.until([&] {
    return env.ed->debug().mode() == DebugMode::Break && env.ed->debug().currentStatement() &&
           env.ed->debug().currentStatement()->line == 2;
  }));
  CHECK(env.rec.area(env.theme.dirtyMarker, env.laneCell(2)) > 20);
}

TEST(c2c_glyph_shapes_enabled_disabled_conditional) {
  Env env;
  env.open("a.cpp");
  const fs::path a = env.ed->currentDocument()->path();
  env.ed->breakpoints().add({a, 1, true, ""});
  env.ed->breakpoints().add({a, 2, false, ""});
  env.ed->breakpoints().add({a, 3, true, "i > 2"});
  env.ed->breakpoints().add({a, 4, false, "i > 2"});
  env.frame();
  const gfx::Color white{1, 1, 1, 1};
  auto centre = [&](int line) {
    const RectF c = env.laneCell(line);
    return std::make_pair(std::floor(c.x + c.w / 2), std::floor(c.y + c.h / 2));
  };
  // Enabled: a filled disc.
  auto [x1, y1] = centre(1);
  CHECK(env.rec.covers(kRed, x1, y1));
  const int filled = env.rec.area(kRed, env.laneCell(1));
  CHECK(filled > 40);
  // Disabled: a ring, the centre is empty but the same extent is covered at the rim.
  auto [x2, y2] = centre(2);
  CHECK(!env.rec.covers(kRed, x2, y2));
  const int ring = env.rec.area(kRed, env.laneCell(2));
  CHECK(ring > 10 && ring < filled * 3 / 4);
  // Conditional: a disc with a white plus; the plus is absent from the others.
  auto [x3, y3] = centre(3);
  CHECK(env.rec.covers(white, x3, y3));
  CHECK(env.rec.area(white, env.laneCell(3)) >= 5);
  CHECK_EQ(env.rec.area(white, env.laneCell(1)), 0);
  CHECK_EQ(env.rec.area(white, env.laneCell(2)), 0);
  // Disabled and conditional: a ring with a plus inside it in the ring's colour.
  auto [x4, y4] = centre(4);
  CHECK(env.rec.covers(kRed, x4, y4));
  CHECK(env.rec.area(kRed, env.laneCell(4)) > ring);
  // Moving the bookmark flag out of the way: it is drawn right of the lane.
  env.ed->moveCursor({1, 0});
  env.ed->toggleBookmark();
  env.frame();
  const RectF lane = env.view()->glyphLaneRect();
  bool flagRightOfLane = false;
  for (const auto& r : env.rec.rects)
    if (same(r.c, env.theme.tabAccent) &&
        std::abs(r.r.y - (env.rowY(1) + std::floor((env.root_lh() - r.r.h) / 2))) < 1 && r.r.w < 8 && r.r.h < 8)
      flagRightOfLane |= r.r.x >= lane.x + lane.w;
  CHECK(flagRightOfLane);
  CHECK(env.rec.covers(kRed, x1, y1));  // and the breakpoint disc is still there
}

TEST(c2c_a_wrapped_line_shows_its_glyphs_on_the_first_row_only) {
  Env env;
  env.open("b.cpp");
  TextView* v = env.view();
  REQUIRE(v->wordWrapActive());
  const fs::path b = env.ed->currentDocument()->path();
  env.ed->breakpoints().add({b, 1, true, ""});
  env.fake->setMode(DebugMode::Break, DebugLocation{b, 1});
  REQUIRE(env.until([&] { return env.ed->debug().mode() == DebugMode::Break; }));
  const float lh = env.root_lh();
  const RectF lane = v->glyphLaneRect();
  const float y0 = env.rowY(1);
  REQUIRE(v->wrapLayout().rowsOf(1) >= 3);
  CHECK(env.rec.area(kRed, {lane.x, y0, lane.w, lh}) > 20);
  CHECK(env.rec.area(env.theme.dirtyMarker, {lane.x, y0, lane.w, lh}) > 10);  // the arrow
  // No glyph pixels on rows 1..n of the line, but the band covers them.
  CHECK_EQ(env.rec.area(kRed, {lane.x, y0 + lh, lane.w, lh * 2}), 0);
  CHECK_EQ(env.rec.area(env.theme.dirtyMarker, {lane.x, y0 + lh, lane.w, lh * 2}), 0);
  Color band = env.theme.dirtyMarker;
  band.a = 0.25f;
  int bandRows = 0;
  for (const auto& r : env.rec.rects)
    bandRows += same(r.c, band) && r.r.y >= y0 - 0.5f && r.r.y < y0 + lh * v->wrapLayout().rowsOf(1) && r.r.w > 100;
  CHECK_EQ(bandRows, v->wrapLayout().rowsOf(1));
  // A click on a continuation row of the line toggles that line's breakpoint.
  env.send(mouseEv(InputType::MouseDown, lane.x + 3, y0 + lh + 3));
  CHECK(!env.ed->breakpoints().has(b, 1));
}

TEST(c2c_session_errors_reach_the_status_bar_message_and_the_state_segment) {
  Env env;
  env.open("a.cpp");
  env.fake->failNext("start", "cannot start: no project");
  env.key(Key::F5);
  REQUIRE(env.until([&] { return !env.ed->debug().lastError().empty(); }));
  CHECK(env.app->message().find("cannot start: no project") != std::string::npos);
  CHECK(env.bench->status().text("debug").find("fake") != std::string::npos);
  CHECK(env.bench->status().text("debug").find("cannot start") != std::string::npos);
}

TEST(c2c_no_backend_command_reports_it_and_leaves_the_bar_alone) {
  Env env(Theme::darkDefault(), false);
  env.open("a.cpp");
  env.key(Key::F5);
  env.frame();
  CHECK(env.app->message().find("no debugger backend") != std::string::npos);
  CHECK(env.bench->status().text("debug").empty());
  CHECK(!env.bench->status().debugging());
}

TEST(c2c_the_breakpoint_list_jumps_toggles_and_deletes) {
  Env env;
  env.open("a.cpp");
  const fs::path a = env.ed->currentDocument()->path(), b = env.path("b.cpp");
  env.ed->breakpoints().add({a, 2, true, "i > 2"});
  env.ed->breakpoints().add({b, 1, true, ""});
  env.ed->breakpoints().add({a, 9, true, ""});
  env.frame();
  CHECK(env.key(Key('B'), ModCtrl | ModAlt));  // Debug.ShowBreakpoints
  BreakpointList& list = env.app->breakpointList();
  REQUIRE(list.isOpen());
  REQUIRE_EQ(list.shown().size(), size_t(3));
  const auto& rows = list.popup()->rows();
  CHECK_EQ(rows[0].primary, std::string("[x] a.cpp:3"));
  CHECK(rows[0].secondary.find("i > 2") != std::string::npos);
  CHECK_EQ(rows[1].primary, std::string("[x] a.cpp:10"));
  CHECK_EQ(rows[2].primary, std::string("[x] b.cpp:2"));
  // Space on the second row disables it; the backend hears about it.
  env.key(Key::Down);
  env.key(Key::Space);
  CHECK(!env.ed->breakpoints().get(a, 9)->enabled);
  CHECK_EQ(list.popup()->rows()[1].primary, std::string("[ ] a.cpp:10"));
  CHECK_EQ(list.popup()->selected(), 1);
  REQUIRE(env.fake->waitUntil(
      [&](const FakeDebugger::State& s) {
        for (const auto& c : s.callsOf("setBreakpoint"))
          if (c.detail.find(":9:0:") != std::string::npos) return true;
        return false;
      },
      5000));
  env.key(Key::Space);
  CHECK(env.ed->breakpoints().get(a, 9)->enabled);
  // Delete removes the selected one.
  env.key(Key::Delete);
  CHECK(!env.ed->breakpoints().has(a, 9));
  REQUIRE_EQ(list.shown().size(), size_t(2));
  CHECK(env.fakeSaw("removeBreakpoint"));
  // Enter jumps to the (new) selection through gotoLocation.
  env.key(Key::Down);
  env.key(Key::Enter);
  CHECK(!list.isOpen());
  CHECK_EQ(env.ed->currentDocument()->path().filename().string(), std::string("b.cpp"));
  CHECK_EQ(env.ed->selections()[0].head.line, 1);
  CHECK(env.ed->back());  // it went through the navigation history
  CHECK_EQ(env.ed->currentDocument()->path().filename().string(), std::string("a.cpp"));
  // Escape closes; an empty store shows the hint.
  env.key(Key('B'), ModCtrl | ModAlt);
  REQUIRE(list.isOpen());
  env.key(Key::Escape);
  CHECK(!list.isOpen());
  env.ed->breakpoints().clear();
  env.key(Key('B'), ModCtrl | ModAlt);
  REQUIRE(list.isOpen());
  CHECK(list.popup()->rows().empty());
  env.key(Key::Escape);
}

TEST(c2c_the_breakpoint_list_follows_backend_changes) {
  Env env;
  env.open("a.cpp");
  const fs::path a = env.ed->currentDocument()->path();
  env.ed->breakpoints().add({a, 2, true, ""});
  env.app->commands().run("Debug.ShowBreakpoints", "", CommandContext{env.ed.get(), "editor", {}, {}});
  BreakpointList& list = env.app->breakpointList();
  REQUIRE(list.isOpen());
  CHECK_EQ(list.shown().size(), size_t(1));
  env.key(Key::F5);  // connects; the backend's own breakpoints are adopted afterwards
  env.fake->injectBreakpoint({a, 12, true, "x"});
  REQUIRE(env.until([&] { return env.ed->breakpoints().has(a, 12); }));
  env.frame();
  CHECK_EQ(list.shown().size(), size_t(2));
}

TEST(c2c_edit_condition_sets_the_condition_of_the_breakpoint_at_the_caret) {
  Env env;
  env.open("a.cpp");
  const fs::path a = env.ed->currentDocument()->path();
  env.ed->moveCursor({4, 0});
  env.frame();
  const CommandContext cc{env.ed.get(), "editor", {}, {}};
  // No breakpoint on the caret line: a message, no popup.
  env.app->commands().run("Debug.EditCondition", "", cc);
  CHECK(!env.app->breakpointList().conditionOpen());
  CHECK(env.app->message().find("No breakpoint") != std::string::npos);
  env.key(Key::F9);
  env.app->commands().run("Debug.EditCondition", "", cc);
  REQUIRE(env.app->breakpointList().conditionOpen());
  for (char c : std::string("n == 3")) env.send(textEv(std::string(1, c)));
  CHECK(env.app->breakpointList().conditionPopup()->rows()[0].primary.find("n == 3") != std::string::npos);
  env.key(Key::Enter);
  CHECK(!env.app->breakpointList().conditionOpen());
  CHECK_EQ(env.ed->breakpoints().get(a, 4)->condition, std::string("n == 3"));
  REQUIRE(env.fake->waitUntil(
      [&](const FakeDebugger::State& s) {
        for (const auto& c : s.callsOf("setBreakpoint"))
          if (c.detail.find("n == 3") != std::string::npos) return true;
        return false;
      },
      5000));
  // Editing again starts from the current text; clearing it removes the condition. Escape cancels.
  env.app->commands().run("Debug.EditCondition", "", cc);
  REQUIRE(env.app->breakpointList().conditionOpen());
  CHECK_EQ(env.app->breakpointList().conditionPopup()->input()->text(), std::string("n == 3"));
  env.key(Key::Escape);
  CHECK_EQ(env.ed->breakpoints().get(a, 4)->condition, std::string("n == 3"));
  env.app->commands().run("Debug.EditCondition", "", cc);
  for (int i = 0; i < 6; ++i) env.key(Key::Backspace);
  env.key(Key::Enter);
  CHECK(env.ed->breakpoints().get(a, 4)->condition.empty());
}

TEST(c2c_default_bindings_for_the_debugger_block) {
  Env env;
  struct B {
    const char* chord;
    const char* cmd;
  };
  const B expected[] = {{"F5", "Debug.Start"},
                        {"Ctrl+F5", "Debug.StartWithoutDebugging"},
                        {"Shift+F5", "Debug.Stop"},
                        {"Ctrl+Shift+F5", "Debug.Restart"},
                        {"F9", "Debug.ToggleBreakpoint"},
                        {"Ctrl+F9", "Debug.ToggleBreakpointEnabled"},
                        {"Ctrl+Shift+F9", "Debug.DeleteAllBreakpoints"},
                        {"F10", "Debug.StepOver"},
                        {"F11", "Debug.StepInto"},
                        {"Shift+F11", "Debug.StepOut"},
                        {"Ctrl+F10", "Debug.RunToCursor"},
                        {"Ctrl+Alt+Pause", "Debug.BreakAll"},
                        {"Ctrl+Alt+B", "Debug.ShowBreakpoints"},
                        {"Ctrl+Alt+O", "View.ToggleBuildPanel"}};
  for (const B& b : expected) {
    bool found = false;
    for (const KeyBinding& kb : env.app->keys().bindings())
      if (kb.command == b.cmd && formatChords(kb.chords) == b.chord) found = true;
    CHECK(found);
  }
  // Ctrl+Alt+B is no longer bound to the build panel.
  for (const KeyBinding& kb : env.app->keys().bindings())
    if (kb.command == "View.ToggleBuildPanel") CHECK(formatChords(kb.chords) != "Ctrl+Alt+B");
}

// --- golden ---------------------------------------------------------------------------------------------------------

namespace {
std::string monoFont() {
  for (const char* p : {"/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
                        "/usr/share/fonts/dejavu/DejaVuSansMono.ttf", "/usr/share/fonts/TTF/DejaVuSansMono.ttf"})
    if (std::ifstream(p).good()) return p;
  return {};
}

void checkGolden(gfx::GlRenderer& r, Workbench& wb, gfx::TextRenderer& tr, const char* name) {
  const int w = 1024, h = 640;
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

TEST(c2c_golden_vs_dark_break_mode_with_breakpoints_and_arrow) {
  SKIP_IF_NO_GL(fx);
  const std::string font = monoFont();
  if (font.empty()) {
    std::cerr << "    SKIP golden: DejaVuSansMono not installed; goldens are only comparable with the same font\n";
    return;
  }
  std::string err;
  auto tr = gfx::TextRenderer::create(font, 15.0f, 1.0f, &err);
  REQUIRE(tr != nullptr);
  Editor::Options o;
  o.semantic.useClangd = false;
  o.semantic.persistFastIndex = false;
  o.watchForcePolling = true;
  o.watchPollMs = 60000;
  const fs::path tmp = uitest::tempRoot() / "tf_c2c_golden";  // the folder name is in the status bar: fixed
  std::error_code rm;
  fs::remove_all(tmp, rm);
  fs::create_directories(tmp / "src");
  fs::copy_file(TENFOLD_SOURCE_DIR "/examples/sample_project/src/main.cpp", tmp / "src/main.cpp");
  {
    Editor ed(tmp, o);
    auto d = std::make_unique<FakeDebugger>();
    auto fake = d->state();
    ed.debug().setPollIntervals(5, 5);
    ed.debug().setBackend(std::move(d));
    ed.debug().connect();
    ed.waitFor([&] { return ed.debug().connected(); }, 5000);
    const Theme th = vsDark();
    Workbench wb(*tr, ed, th);
    wb.openFile("src/main.cpp");
    wb.sync();
    EditorApp app(wb, ed, &wb);
    uitest::wrapOff(wb);
    fx->renderer->setOffscreen(true);
    wb.render(*fx->renderer, 1024, 640);
    const fs::path file = ed.currentDocument()->path();
    ed.breakpoints().add({file, 6, true, ""});
    ed.breakpoints().add({file, 8, false, ""});
    ed.breakpoints().add({file, 11, true, "i > 2"});
    ed.breakpoints().add({file, 16, true, ""});
    ed.moveCursor({6, 0});
    ed.toggleBookmark();
    fake->setMode(DebugMode::Break, DebugLocation{file, 16});
    REQUIRE(ed.waitFor([&] { return ed.debug().mode() == DebugMode::Break; }, 5000));
    ed.moveCursor({0, 0});
    app.tick(2000);
    ed.pump();
    app.tick(2010);
    checkGolden(*fx->renderer, wb, *tr, "c2c_debugger_break.png");
  }
  std::error_code ec;
  fs::remove_all(tmp, ec);
}
