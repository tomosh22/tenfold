// C1-UI: build panel in the bottom dock, F4 stepping, clickable output, build squiggles / markers / hover, the
// "build" status segment and the dock's layout.json persistence. No GL except one golden: a recording renderer, the
// mock font (8 px cells, 14 px lines) and synthetic events through EditorApp with the real default key bindings. The
// builds are real: a fake command (POSIX `sh -c`, Windows `cmd /c`) prints GCC- and MSVC-style messages that point
// into a temp workspace.
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#include "../text/text_test_util.h"
#include "ui/gfx/PngWrite.h"
#include "ui/gfx/TextRenderer.h"
#include "ui/shell/EditorApp.h"
#include "ui/widgets/TextView.h"
#include "ui_test.h"

#define REQUIRE_EQ(a, b) REQUIRE((a) == (b))

using namespace tf;
using namespace tf::ui;
namespace fs = std::filesystem;

namespace {

bool same(const gfx::Color& a, const gfx::Color& b) {
  return std::abs(a.r - b.r) < 1e-4f && std::abs(a.g - b.g) < 1e-4f && std::abs(a.b - b.b) < 1e-4f &&
         std::abs(a.a - b.a) < 1e-4f;
}

// Remembers the colours of the rects and lines of the last frame.
struct Recorder : gfx::Renderer {
  std::vector<gfx::Color> rects, lines;
  void beginFrame(int, int, gfx::Color) override {
    rects.clear();
    lines.clear();
  }
  void drawRect(const gfx::RectF&, gfx::Color c) override { rects.push_back(c); }
  void drawLine(float, float, float, float, float, gfx::Color c) override { lines.push_back(c); }
  void drawGlyphs(gfx::TextureId, const gfx::GlyphQuad*, int, gfx::Color) override {}
  void pushScissor(const gfx::RectF&) override {}
  void popScissor() override {}
  gfx::TextureId createTexture(int, int, gfx::TextureFormat) override { return 1; }
  void updateTexture(gfx::TextureId, int, int, int, int, const void*, int) override {}
  void destroyTexture(gfx::TextureId) override {}
  void endFrame() override {}
  bool readPixels(int, int, uint8_t*) override { return false; }
  bool hasLine(const gfx::Color& c) const {
    return std::any_of(lines.begin(), lines.end(), [&](const gfx::Color& x) { return same(x, c); });
  }
  bool hasRect(const gfx::Color& c) const {
    return std::any_of(rects.begin(), rects.end(), [&](const gfx::Color& x) { return same(x, c); });
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
InputEvent mouseEv(InputType t, float x, float y) {
  InputEvent e;
  e.type = t;
  e.x = x;
  e.y = y;
  e.clicks = 1;
  return e;
}

void writeFile(const fs::path& p, const std::string& text) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary) << text;
}

// A shell command line printing `lines` and exiting with `code` (no single quotes in the lines).
std::string emit(const std::vector<std::string>& lines, int code) {
  std::string c;
#ifdef _WIN32
  for (const std::string& l : lines) c += "echo " + l + "& ";
  return c + "exit " + std::to_string(code);
#else
  c = "printf '%s\\n'";
  for (const std::string& l : lines) c += " '" + l + "'";
  return c + "; exit " + std::to_string(code);
#endif
}

std::string sleeper(const std::string& marker) {
#ifdef _WIN32
  return "echo " + marker + "& ping -n 120 127.0.0.1 >nul";
#else
  return "echo " + marker + "; sleep 120 & wait";
#endif
}

struct Env {
  fs::path dir;
  std::unique_ptr<gfx::TextRenderer> text = mockText();
  Recorder rec;
  std::unique_ptr<Editor> ed;
  std::unique_ptr<Workbench> bench;
  std::unique_ptr<EditorApp> app;
  int64_t now = 1000;
  std::vector<HoverTooltip::Done> hovers;

  Env() {
    static int n = 0;
    dir = fs::temp_directory_path() /
          ("tf_c1ui_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_" +
           std::to_string(n++));
    writeFile(dir / "a.cpp", "int a;\nint bad_name;\nint c;\n");
    writeFile(dir / "b.cpp", "int x;\nint y;\nint z;\nint w = foo;\n");
    Editor::Options o;
    o.semantic.useClangd = false;
    o.semantic.persistFastIndex = false;
    o.watchForcePolling = true;
    o.watchPollMs = 60000;
    ed = std::make_unique<Editor>(dir, o);
    std::string err;
    if (!ed->start(&err)) std::cerr << "    editor start: " << err << "\n";
    bench = std::make_unique<Workbench>(*text, *ed);
    app = std::make_unique<EditorApp>(*bench, *ed, bench.get());
    app->hover().setHoverFn([this](Document&, Position, HoverTooltip::Done d) { hovers.push_back(std::move(d)); });
    app->hover().setApproximateFn([] { return false; });
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
  void advance(int ms) {
    now += ms;
    frame();
  }
  void open(const char* rel) {
    bench->openFile(dir / rel);
    bench->sync();
    frame();
  }
  BuildPanel& panel() { return *app->buildPanel(); }
  std::string a() const { return (dir / "a.cpp").generic_string(); }
  std::string b() const { return (dir / "b.cpp").generic_string(); }
  std::string curFile() { return ed->currentDocument()->path().filename().string(); }
  bool waitState(BuildPanel::State s, int ms = 20000) {
    for (int i = 0; i < ms / 5; ++i) {
      frame();
      if (panel().state() == s) return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
  }
  bool waitFinished(int ms = 20000) {
    for (int i = 0; i < ms / 5; ++i) {
      frame();
      if (panel().state() != BuildPanel::State::Running && panel().state() != BuildPanel::State::Idle) return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
  }
  bool run(const std::string& shell) {
    BuildOverrides o;
    o.command = shell;
    bench->showBottomPanel("Build");
    std::string err;
    if (!panel().start(o, &err)) std::cerr << "    build start: " << err << "\n";
    return waitFinished();
  }
  // The standard failing build: a GCC error in a.cpp, noise, an MSVC warning in b.cpp and a location-less error.
  bool runFailing() {
    return run(emit(
        {a() + ":2:5: error: bad name", "noise", b() + "(4,9): warning C4189: meh unused", "clang: error: link failed"},
        1));
  }
  int lineWith(const std::string& needle) {
    const auto& ls = panel().lines();
    for (size_t i = 1; i < ls.size(); ++i)  // line 0 is the command line
      if (ls[i].text.find(needle) != std::string::npos) return static_cast<int>(i);
    return -1;
  }
  void clickLine(int i) {
    const RectF r = panel().lineRect(i);
    send(mouseEv(InputType::MouseDown, r.x + 20, r.y + r.h / 2));
    send(mouseEv(InputType::MouseUp, r.x + 20, r.y + r.h / 2));
  }
};

}  // namespace

TEST(build_ui_dock_hidden_by_default_and_shown_by_build_and_toggle) {
  Env env;
  CHECK(!env.bench->bottomVisible());
  CHECK(env.app->buildPanel() != nullptr);
  CHECK(env.bench->bottomRect().w == 0);
  const RectF treeBefore = env.bench->panels()[0]->bounds();
  CHECK(env.key(Key('B'), ModCtrl | ModAlt));  // View.ToggleBuildPanel
  CHECK(env.bench->bottomVisible());
  CHECK_EQ(env.bench->bottomPanelName(), std::string("Build"));
  CHECK(env.bench->panels()[0]->bounds().h < treeBefore.h);
  CHECK(env.bench->bottomRect().h > 100);
  CHECK(env.key(Key('B'), ModCtrl | ModAlt));
  CHECK(!env.bench->bottomVisible());
  CHECK(env.bench->panels()[0]->bounds().h == treeBefore.h);
  // Clicking the status bar segment toggles too (once there is text on it).
  env.run(emit({"fine"}, 0));
  env.bench->hideBottomPanel();
  env.frame();
  const RectF seg = env.bench->status().segmentRect("build");
  REQUIRE(seg.w > 0);
  env.send(mouseEv(InputType::MouseDown, seg.x + 3, seg.y + seg.h / 2));
  CHECK(env.bench->bottomVisible());
}

TEST(build_ui_clean_strips_ansi_and_control_characters) {
  CHECK_EQ(BuildPanel::clean("\x1b[1m\x1b[31merror:\x1b[0m bad"), std::string("error: bad"));
  CHECK_EQ(BuildPanel::clean("\x1b[01m\x1b[Ka.cpp:1:2:\x1b[m x"), std::string("a.cpp:1:2: x"));
  CHECK_EQ(BuildPanel::clean("\x1b]0;title\x07text"), std::string("text"));
  CHECK_EQ(BuildPanel::clean("a\tb"), std::string("a    b"));
  CHECK_EQ(BuildPanel::clean("done\r"), std::string("done"));
  CHECK_EQ(BuildPanel::clean("10%\r50%\r100%"), std::string("100%"));
  CHECK_EQ(BuildPanel::clean("x\x01y"), std::string("xy"));
  CHECK(BuildPanel::clean(std::string(10000, 'x')).size() < 4200);
}

TEST(build_ui_panel_streams_lines_in_order_parses_messages_and_reports_failure) {
  Env env;
  const bool wasHidden = !env.bench->bottomVisible();
  CHECK(wasHidden);
  REQUIRE(env.runFailing());
  CHECK(env.bench->bottomVisible());  // a build shows the panel
  const auto& ls = env.panel().lines();
  REQUIRE(ls.size() >= 5);
  CHECK(ls[0].text.rfind("> ", 0) == 0);  // the command line first
  CHECK(ls[1].text.find("error: bad name") != std::string::npos);
  CHECK_EQ(ls[2].text, std::string("noise"));
  CHECK(ls[3].text.find("warning C4189") != std::string::npos);
  CHECK_EQ(ls[4].text, std::string("clang: error: link failed"));
  CHECK(ls[1].message >= 0 && ls[2].message < 0 && ls[3].message >= 0);
  CHECK(env.panel().state() == BuildPanel::State::Failed);
  CHECK_EQ(env.panel().statusText(), std::string("Failed: 2 errors, 1 warning"));
  CHECK_EQ(env.bench->status().text("build"), std::string("build: Failed: 2 errors, 1 warning"));

  // Success: "Succeeded in N.N s"; ANSI colours in the output are stripped.
  REQUIRE(env.run(emit({"\x1b[32mall good\x1b[0m"}, 0)));
  CHECK(env.panel().state() == BuildPanel::State::Succeeded);
  const std::string st = env.panel().statusText();
  CHECK(st.rfind("Succeeded in ", 0) == 0 && st.size() > 5 && st.substr(st.size() - 2) == " s");
  CHECK(env.lineWith("all good") > 0);
  CHECK(env.lineWith("\x1b") < 0);
  CHECK(env.panel().lines().size() == 2);  // the new build replaced the old output
}

TEST(build_ui_build_command_without_build_system_fails_with_a_message) {
  Env env;
  CHECK(env.key(Key('B'), ModCtrl | ModShift));  // Build.Build: nothing to build in the temp workspace
  CHECK(env.bench->bottomVisible());
  CHECK(env.panel().state() == BuildPanel::State::Failed);
  CHECK(env.panel().statusText().rfind("Failed to start", 0) == 0);
  CHECK(env.app->message().find("Build failed to start") != std::string::npos);
}

TEST(build_ui_f4_steps_through_messages_selects_lines_and_records_history) {
  Env env;
  env.open("a.cpp");
  REQUIRE(env.runFailing());
  const int errLine = env.lineWith("error: bad name"), warnLine = env.lineWith("warning C4189");
  REQUIRE(errLine > 0 && warnLine > 0);
  CHECK(env.key(Key::F4));
  CHECK_EQ(env.curFile(), std::string("a.cpp"));
  CHECK_EQ(env.ed->cursor().pos.line, 1);
  CHECK_EQ(env.ed->cursor().pos.column, 4);
  CHECK_EQ(env.panel().selectedLine(), errLine);
  CHECK(env.app->message().find("bad name") != std::string::npos);
  CHECK(env.key(Key::F4));
  CHECK_EQ(env.curFile(), std::string("b.cpp"));
  CHECK_EQ(env.ed->cursor().pos.line, 3);
  CHECK_EQ(env.ed->cursor().pos.column, 8);
  CHECK_EQ(env.panel().selectedLine(), warnLine);
  CHECK(env.key(Key::F4));  // wraps past the location-less error
  CHECK_EQ(env.curFile(), std::string("a.cpp"));
  CHECK(env.key(Key::F4, ModShift));  // previous wraps back
  CHECK_EQ(env.curFile(), std::string("b.cpp"));
  CHECK_EQ(env.panel().selectedLine(), warnLine);
  CHECK(env.key(Key::Left, ModAlt));  // every jump is in the navigation history (constraint 1.3)
  CHECK_EQ(env.curFile(), std::string("a.cpp"));
  CHECK_EQ(env.ed->cursor().pos.line, 1);
  // Nothing to step through after a clean build.
  REQUIRE(env.run(emit({"fine"}, 0)));
  CHECK(env.key(Key::F4));
  CHECK(env.app->message().find("No build errors") != std::string::npos);
}

TEST(build_ui_clicking_a_line_jumps_and_alt_left_returns) {
  Env env;
  env.open("a.cpp");
  REQUIRE(env.runFailing());
  const int warnLine = env.lineWith("warning C4189");
  REQUIRE(warnLine > 0);
  env.clickLine(env.lineWith("noise"));  // plain output: selects, does not jump
  CHECK_EQ(env.curFile(), std::string("a.cpp"));
  env.clickLine(warnLine);
  CHECK_EQ(env.curFile(), std::string("b.cpp"));
  CHECK_EQ(env.ed->cursor().pos.line, 3);
  CHECK_EQ(env.ed->cursor().pos.column, 8);
  CHECK_EQ(env.panel().selectedLine(), warnLine);
  CHECK(env.key(Key::Left, ModAlt));
  CHECK_EQ(env.curFile(), std::string("a.cpp"));
  CHECK_EQ(env.ed->cursor().pos.line, 0);
  // Enter on the selected line jumps as well (keys go to the focused panel after a click).
  env.clickLine(env.lineWith("error: bad name"));
  CHECK_EQ(env.ed->cursor().pos.line, 1);
  CHECK(env.panel().activate(warnLine));
  CHECK_EQ(env.curFile(), std::string("b.cpp"));
}

TEST(build_ui_cancel_shows_cancelled_via_key_and_button) {
  Env env;
  BuildOverrides o;
  o.command = sleeper("started");
  env.bench->showBottomPanel("Build");
  REQUIRE(env.panel().start(o));
  for (int i = 0; i < 4000 && env.panel().lines().size() < 2; ++i) {
    env.frame();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  REQUIRE(env.panel().lines().size() >= 2);
  CHECK(env.panel().state() == BuildPanel::State::Running);
  CHECK_EQ(env.panel().statusText(), std::string("Building\xE2\x80\xA6"));
  CHECK(env.key(Key::Pause, ModCtrl));  // Ctrl+Break
  REQUIRE(env.waitState(BuildPanel::State::Cancelled));
  CHECK_EQ(env.panel().statusText(), std::string("Cancelled"));
  CHECK_EQ(env.bench->status().text("build"), std::string("build: Cancelled"));
  // The Cancel button does the same.
  REQUIRE(env.panel().start(o));
  const RectF c = env.panel().cancelButtonRect();
  env.send(mouseEv(InputType::MouseDown, c.x + 3, c.y + c.h / 2));
  REQUIRE(env.waitState(BuildPanel::State::Cancelled));
}

#ifndef _WIN32
TEST(build_ui_output_follows_the_end_until_the_user_scrolls_up) {
  Env env;
  env.bench->showBottomPanel("Build");
  BuildOverrides o;
  o.command = "i=0; while [ $i -lt 100 ]; do echo l$i; i=$((i+1)); done; sleep 1; echo tail1; echo tail2";
  REQUIRE(env.panel().start(o));
  for (int i = 0; i < 4000 && env.panel().lines().size() < 100; ++i) {
    env.frame();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  REQUIRE(env.panel().lines().size() >= 100);
  CHECK(env.panel().following());
  const float rh = env.text->lineHeight();
  const RectF body = env.panel().bodyRect();
  const float end = env.panel().scrollY();
  CHECK(end > 0 &&
        env.panel().lineRect(static_cast<int>(env.panel().lines().size()) - 1).y + rh <= body.y + body.h + 1);
  InputEvent w;
  w.type = InputType::Wheel;
  w.x = body.x + 20;
  w.y = body.y + 20;
  w.dy = 5;  // up
  env.send(w);
  CHECK(!env.panel().following());
  const float held = env.panel().scrollY();
  CHECK(held < end);
  REQUIRE(env.waitFinished());
  CHECK(env.lineWith("tail2") > 100);
  CHECK(!env.panel().following());
  CHECK_EQ(env.panel().scrollY(), held);  // new output did not yank the view
  w.dy = -1000;                           // back to the end resumes following
  env.send(w);
  CHECK(env.panel().following());
}
#endif

TEST(build_ui_squiggles_use_the_build_colours_apart_from_clangd) {
  Env env;
  env.open("a.cpp");
  env.frame();
  const Theme th = env.bench->rootTheme();
  CHECK(!env.rec.hasLine(th.buildError) && !env.rec.hasRect(th.buildError));
  REQUIRE(env.runFailing());
  env.frame();
  CHECK(env.rec.hasLine(th.buildError));  // a.cpp:2:5
  CHECK(!env.rec.hasLine(th.buildWarning) && !env.rec.hasLine(th.diagnosticError));
  CHECK(env.rec.hasRect(th.buildError));  // scrollbar marker
  env.open("b.cpp");
  env.frame();
  CHECK(env.rec.hasLine(th.buildWarning));  // b.cpp(4,9)
  CHECK(env.rec.hasRect(th.buildWarning));
  // A clangd diagnostic on the same file draws in addition, in its own colour.
  env.open("a.cpp");
  Diagnostic d;
  d.file = env.dir / "a.cpp";
  d.range = {{0, 0}, {0, 3}};
  d.severity = 1;
  d.message = "clangd says no";
  env.ed->setDiagnostics(d.file, {d});
  env.frame();
  CHECK(env.rec.hasLine(th.buildError) && env.rec.hasLine(th.diagnosticError));
  // A new build clears the squiggles.
  REQUIRE(env.run(emit({"fine"}, 0)));
  env.frame();
  CHECK(!env.rec.hasLine(th.buildError));
  CHECK(env.rec.hasLine(th.diagnosticError));
}

TEST(build_ui_hover_over_a_build_squiggle_shows_the_message_labelled_build) {
  Env env;
  env.open("a.cpp");
  REQUIRE(env.runFailing());
  env.frame();
  TextView* v = dynamic_cast<TextView*>(env.bench->focused());
  REQUIRE(v != nullptr);
  const RectF r = v->caretRect({1, 6});  // inside "bad_name" (the message has a column: from 5 to the line end)
  env.send(mouseEv(InputType::MouseMove, r.x + 2, r.y + 3));
  env.advance(500);
  HoverTooltip& h = env.app->hover();
  REQUIRE(h.shown());
  const auto& lines = h.box()->lines();
  REQUIRE(!lines.empty());
  REQUIRE(lines[0].spans.size() >= 2);
  CHECK_EQ(lines[0].spans[0].text, std::string("build "));
  CHECK_EQ(lines[0].spans[1].text, std::string("bad name"));
  // clangd's own diagnostics keep the unlabelled form and come first.
  Diagnostic d;
  d.file = env.dir / "a.cpp";
  d.range = {{1, 4}, {1, 12}};
  d.severity = 2;
  d.message = "clangd note";
  env.ed->setDiagnostics(d.file, {d});
  h.dismiss();
  env.send(mouseEv(InputType::MouseMove, r.x + 2, r.y + 3));
  env.advance(500);
  REQUIRE(h.shown());
  CHECK_EQ(h.box()->lines()[0].spans[0].text, std::string("clangd note"));
  bool labelled = false;
  for (const PopupLine& l : h.box()->lines()) labelled |= !l.spans.empty() && l.spans[0].text == "build ";
  CHECK(labelled);
}

TEST(build_ui_dock_divider_resizes_and_layout_json_round_trips) {
  Env env;
  env.bench->showBottomPanel("Build");
  env.frame();
  const RectF dock = env.bench->bottomRect();
  const float h0 = env.bench->bottomHeight();
  CHECK(h0 > 0.2f && h0 < 0.4f);
  // Drag the divider (just above the dock) up by 100 px.
  const float y = dock.y - 2;
  env.send(mouseEv(InputType::MouseDown, 300, y));
  env.send(mouseEv(InputType::MouseMove, 300, y - 100));
  env.send(mouseEv(InputType::MouseUp, 300, y - 100));
  CHECK(env.bench->bottomRect().h > dock.h + 80);
  const float h1 = env.bench->bottomHeight();
  CHECK(h1 > h0);

  const std::string json = env.bench->layoutJson();
  CHECK(json.find("\"bottom\"") != std::string::npos);
  env.bench->hideBottomPanel();
  env.bench->setBottomHeight(0.15f);
  CHECK(!env.bench->bottomVisible());
  REQUIRE(env.bench->restoreLayoutFromJson(json));
  CHECK(env.bench->bottomVisible());
  CHECK_EQ(env.bench->bottomPanelName(), std::string("Build"));
  CHECK(std::abs(env.bench->bottomHeight() - h1) < 0.001f);

  // Hidden state and height survive too, and through the file on disk.
  env.bench->hideBottomPanel();
  REQUIRE(env.bench->saveLayout());
  env.bench->showBottomPanel("Build");
  REQUIRE(env.bench->restoreLayout());
  CHECK(!env.bench->bottomVisible());
  CHECK(std::abs(env.bench->bottomHeight() - h1) < 0.001f);
}

TEST(build_ui_old_layout_without_the_bottom_key_loads_and_restore_waits_for_the_panel) {
  Env env;
  env.bench->showBottomPanel("Build");
  const std::string old = R"({"version":1,"activePanel":0,"root":{"type":"panel","tabs":[],"active":-1}})";
  REQUIRE(env.bench->restoreLayoutFromJson(old));
  CHECK(!env.bench->bottomVisible());  // a layout without the key keeps the dock hidden
  // A fresh workbench restoring a visible dock shows it once the panel exists (EditorApp adds it after restore).
  auto text2 = mockText();
  Workbench wb(*text2, *env.ed);
  const std::string with =
      R"({"version":1,"activePanel":0,"root":{"type":"panel","tabs":[],"active":-1},"bottom":{"visible":true,"panel":"Build","height":0.35}})";
  REQUIRE(wb.restoreLayoutFromJson(with));
  CHECK(!wb.bottomVisible());
  wb.addBottomPanel("Build", std::make_unique<Widget>());
  CHECK(wb.bottomVisible());
  CHECK(std::abs(wb.bottomHeight() - 0.35f) < 0.001f);
  // Garbage in the key is ignored, not fatal.
  const std::string bad =
      R"({"version":1,"root":{"type":"panel","tabs":[]},"bottom":{"visible":"yes","panel":5,"height":"tall"}})";
  REQUIRE(wb.restoreLayoutFromJson(bad));
  CHECK(!wb.bottomVisible());
}

// ---------------------------------------------------------------------------------------------------------------
// Golden
// ---------------------------------------------------------------------------------------------------------------

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

TEST(c1ui_golden_build_panel_with_failed_build_and_squiggle) {
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
  Editor ed(TENFOLD_SOURCE_DIR "/examples/sample_project", o);
  REQUIRE(ed.start(&err));
  for (int i = 0; i < 2000 && !ed.semantic().status().fastIndexDone; ++i) {
    ed.pump();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  Workbench wb(*tr, ed);
  wb.openFile("src/main.cpp");
  wb.sync();
  EditorApp app(wb, ed, &wb);
  // Relative paths resolve against the workspace root (the build's working directory).
  BuildOverrides bo;
  bo.command = emit({"src/main.cpp:12:30: error: use of undeclared identifier shapes_total",
                     "src/main.cpp:19:20: warning: unused variable fa [-Wunused-variable]",
                     "src/geometry.cpp:6:16: note: declared here", "ninja: build stopped: subcommand failed."},
                    1);
  REQUIRE(app.buildPanel() != nullptr);
  wb.showBottomPanel("Build");
  REQUIRE(app.buildPanel()->start(bo, &err));
  for (int i = 0; i < 4000 && app.buildPanel()->state() == BuildPanel::State::Running; ++i) {
    ed.pump();
    app.tick(1000 + i);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  REQUIRE(app.buildPanel()->state() == BuildPanel::State::Failed);
  fx->renderer->setOffscreen(true);
  wb.render(*fx->renderer, 1024, 640);
  REQUIRE(app.buildPanel()->selectMessage(ed.buildMessages()[0]));
  app.tick(9000);
  checkGolden(*fx->renderer, wb, *tr, "c1ui_build_panel.png");
}
