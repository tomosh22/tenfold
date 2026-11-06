// W8f terminal panel: NewTerminal / ShowTerminal / ClaudeCodeTerminal / CursorTerminal through the real default key
// bindings, a terminal in an editor panel's tab, the program's output on the VtScreen, typing, paste, selection and
// clipboard, scrollback, resize, colours, closing and the process tree, the exit line, and a 50 MB burst. A mock font
// (8 px cells, 14 px lines) and a recording renderer, except one golden (GL + DejaVuSansMono, skipped without them).
// The programs are POSIX `sh -c` scripts; the few Windows variants use `cmd /c`.
#include <algorithm>
#include <chrono>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>

#ifndef _WIN32
#include <signal.h>
#include <sys/types.h>
#endif

#include "../text/text_test_util.h"
#include "term/Pty.h"
#include "ui/commands/CommandNames.h"
#include "ui/gfx/PngWrite.h"
#include "ui/gfx/TextRenderer.h"
#include "ui/platform/Clipboard.h"
#include "ui/settings/Theme.h"
#include "ui/shell/EditorApp.h"
#include "ui/widgets/TerminalView.h"
#include "ui_test.h"

using namespace tf;
using namespace tf::ui;
namespace fs = std::filesystem;

namespace {

bool same(const gfx::Color& a, const gfx::Color& b) {
  return std::abs(a.r - b.r) < 1e-3f && std::abs(a.g - b.g) < 1e-3f && std::abs(a.b - b.b) < 1e-3f;
}

// Remembers the colours of the rects and of the glyph batches of the last frame.
struct Recorder : gfx::Renderer {
  std::vector<gfx::Color> rects, glyphs;
  void beginFrame(int, int, gfx::Color) override {
    rects.clear();
    glyphs.clear();
  }
  void drawRect(const gfx::RectF&, gfx::Color c) override { rects.push_back(c); }
  void drawLine(float, float, float, float, float, gfx::Color) override {}
  void drawGlyphs(gfx::TextureId, const gfx::GlyphQuad*, int, gfx::Color c) override { glyphs.push_back(c); }
  void pushScissor(const gfx::RectF&) override {}
  void popScissor() override {}
  gfx::TextureId createTexture(int, int, gfx::TextureFormat) override { return 1; }
  void updateTexture(gfx::TextureId, int, int, int, int, const void*, int) override {}
  void destroyTexture(gfx::TextureId) override {}
  void endFrame() override {}
  bool readPixels(int, int, uint8_t*) override { return false; }
  bool hasRect(const gfx::Color& c) const {
    return std::any_of(rects.begin(), rects.end(), [&](const gfx::Color& x) { return same(x, c); });
  }
  bool hasGlyphs(const gfx::Color& c) const {
    return std::any_of(glyphs.begin(), glyphs.end(), [&](const gfx::Color& x) { return same(x, c); });
  }
};

std::unique_ptr<gfx::TextRenderer> mockText() {
  gfx::FontStack stack;
  stack.add(std::make_shared<tftext::MockFont>(
      U"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ._-:,;/()[]{}<>+*&#?=!\"'^~"));
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
InputEvent mouseEv(InputType t, float x, float y, int clicks = 1, uint8_t mods = 0) {
  InputEvent e;
  e.type = t;
  e.x = x;
  e.y = y;
  e.clicks = clicks;
  e.mods = mods;
  return e;
}
InputEvent wheelEv(float dy, float x, float y) {
  InputEvent e;
  e.type = InputType::Wheel;
  e.dy = dy;
  e.x = x;
  e.y = y;
  return e;
}

constexpr Key kQuote = Key('\'');  // NewTerminal is `Control '`

// The setting that makes NewTerminal run `script` (POSIX: sh -c; Windows: cmd /c). No double quotes in the script.
std::string cmdSetting(const std::string& script) {
#ifdef _WIN32
  return "terminal.command = cmd /c \"" + script + "\"\n";
#else
  return "terminal.command = /bin/sh -c \"" + script + "\"\n";
#endif
}

struct FakeClipboard {
  std::string text;
  FakeClipboard() {
    setClipboardBackend({[this] { return text; },
                         [this](const std::string& s) {
                           text = s;
                           return true;
                         }});
  }
  ~FakeClipboard() { setClipboardBackend({}); }
};

struct Env {
  fs::path dir;
  std::unique_ptr<gfx::TextRenderer> text = mockText();
  Recorder rec;
  std::unique_ptr<Editor> ed;
  std::unique_ptr<Workbench> bench;
  std::unique_ptr<EditorApp> app;
  int64_t now = 1000;
  int width = 1000, height = 600;

  explicit Env(const Theme& theme = Theme::darkDefault()) {
    static int n = 0;
    dir = uitest::tempRoot() /
          ("tf_w8fterm_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_" +
           std::to_string(n++));
    fs::create_directories(dir);
    std::ofstream(dir / "a.cpp") << "int a;\nint b;\n";
    Editor::Options o;
    o.semantic.useClangd = false;
    o.semantic.persistFastIndex = false;
    o.watchForcePolling = true;
    o.watchPollMs = 60000;
    ed = std::make_unique<Editor>(dir, o);
    std::string err;
    if (!ed->start(&err)) std::cerr << "    editor start: " << err << "\n";
    bench = std::make_unique<Workbench>(*text, *ed, theme);
    app = std::make_unique<EditorApp>(*bench, *ed, bench.get());
    app->terminalPanel()->wake = [] {};  // no SDL window in these tests
    bench->openFile(dir / "a.cpp");
    bench->sync();
    bench->render(rec, width, height);
  }
  ~Env() {
    app.reset();
    bench.reset();
    ed.reset();
    std::error_code ec;
    fs::remove_all(dir, ec);
  }
  void settings(const std::string& userText) {
    Settings s;
    s.loadTexts(userText, "");
    app->applySettings(s);
  }
  void frame() {
    ed->pump();
    app->tick(now);
    bench->render(rec, width, height);
  }
  bool send(const InputEvent& e) {
    now += 10;
    const bool r = app->handleEvent(e, now);
    frame();
    return r;
  }
  bool key(Key k, uint8_t mods = 0) { return send(keyEv(k, mods)); }
  void type(const std::string& s) { send(textEv(s)); }
  template <class Pred>
  bool waitFor(Pred p, int ms = 20000) {
    for (int i = 0; i < ms / 2; ++i) {
      frame();
      if (p()) return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
  }
  TerminalPanel& terms() { return *app->terminalPanel(); }
  TerminalView* tv(int id = -1) {
    if (id < 0) {
      Panel* p = bench->activePanel();
      return p ? dynamic_cast<TerminalView*>(p->activeContent()) : nullptr;
    }
    return terms().view(id);
  }
  static std::string screenOf(TerminalView* v) {
    std::string s;
    if (!v) return s;
    for (int r = 0; r < v->session()->screen().rows(); ++r) s += v->visibleRowText(r) + "\n";
    return s;
  }
  std::string screen(int id = -1) { return screenOf(tv(id)); }
  bool has(const std::string& needle, int id = -1) { return screen(id).find(needle) != std::string::npos; }
  bool waitHas(const std::string& needle, int ms = 20000, int id = -1) {
    return waitFor([&] { return has(needle, id); }, ms);
  }
  int newTerminal() {
    const size_t before = terms().terminals().size();
    key(kQuote, ModCtrl);
    const auto ids = terms().terminals();
    return ids.size() == before + 1 ? ids.back() : -1;
  }
};

#ifndef _WIN32
// A process still running (a zombie waiting for init to reap it does not count).
bool alive(long pid) {
  std::ifstream f("/proc/" + std::to_string(pid) + "/stat");
  if (!f) return false;
  std::string line;
  std::getline(f, line);
  const size_t rp = line.rfind(')');
  return rp != std::string::npos && rp + 2 < line.size() && line[rp + 2] != 'Z';
}
bool exists(long pid) { return ::kill(static_cast<pid_t>(pid), 0) == 0; }
long pidAfter(const std::string& screen, const std::string& tag) {
  const size_t p = screen.find(tag);
  return p == std::string::npos ? -1 : std::atol(screen.c_str() + p + tag.size());
}
#endif

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// Commands, keys, hosting
// ---------------------------------------------------------------------------------------------------------------

TEST(term_default_keys_run_the_terminal_commands_and_the_10x_names_resolve) {
  for (const char* n : {"NewTerminal", "ShowTerminal", "ClaudeCodeTerminal", "CursorTerminal"}) {
    const ResolvedCommand rc = resolveCommandName(n);
    CHECK(rc.known10x && rc.implemented);
  }
  CHECK_EQ(resolveCommandName("NewTerminal").id, std::string("Terminal.New"));
  CHECK_EQ(resolveCommandName("ShowTerminal").id, std::string("Terminal.Show"));
  CHECK_EQ(resolveCommandName("ClaudeCodeTerminal").id, std::string("Terminal.NewClaude"));
  CHECK_EQ(resolveCommandName("CursorTerminal").id, std::string("Terminal.NewCursor"));
  Env env;
  for (const char* id : {"Terminal.New", "Terminal.Show", "Terminal.NewClaude", "Terminal.NewCursor"}) {
    const Command* c = env.app->commands().find(id);
    REQUIRE(c != nullptr);
    CHECK(static_cast<bool>(c->handler));
  }
  // 10x's default keys: Control ' and Control Shift I.
  bool nt = false, st = false;
  for (const KeyBinding& b : env.app->keys().bindings()) {
    if (b.command == "NewTerminal" || b.command == "Terminal.New") nt = formatChords(b.chords) == "Control '";
    if (b.command == "ShowTerminal" || b.command == "Terminal.Show") st = formatChords(b.chords) == "Control Shift I";
  }
  CHECK(nt && st);
}

TEST(term_new_terminal_runs_the_program_in_a_tab_of_the_active_panel_and_shows_its_output) {
  if (!Pty::available()) return;
  Env env;
  env.settings(cmdSetting(
#ifdef _WIN32
      "echo hello term& ping -n 60 127.0.0.1 >nul"
#else
      "printf 'hello term\\n'; sleep 60"
#endif
      ));
  CHECK_EQ(env.terms().terminals().size(), size_t(0));
  Panel* p = env.bench->activePanel();
  const size_t tabs = p->tabs().tabs().size();
  const int id = env.newTerminal();  // Control '
  REQUIRE(id >= 0);
  CHECK_EQ(p->tabs().tabs().size(), tabs + 1);
  CHECK_EQ(p->activeId(), id);
  CHECK(env.bench->isWidgetTab(id));
  CHECK(env.tv(id)->hasFocus());
  CHECK(env.waitHas("hello term"));
  // The grid fits the panel; the program was started with that size.
  const VtScreen& s = env.tv()->session()->screen();
  CHECK(s.cols() > 40 && s.rows() > 10);
  // A second NewTerminal always creates another one.
  CHECK(env.newTerminal() >= 0);
  CHECK_EQ(env.terms().terminals().size(), size_t(2));
}

TEST(term_working_directory_is_the_workspace_root) {
#ifndef _WIN32
  if (!Pty::available()) return;
  Env env;
  env.settings(cmdSetting("pwd; sleep 60"));
  REQUIRE(env.newTerminal() >= 0);
  CHECK(env.waitHas(env.dir.generic_string()));
  CHECK_EQ(env.terms().workingDirectory().generic_string(), env.dir.generic_string());
#endif
}

TEST(term_typing_reaches_the_program_and_snaps_the_view_to_the_bottom) {
#ifndef _WIN32
  if (!Pty::available()) return;
  Env env;
  env.settings(cmdSetting("cat"));
  REQUIRE(env.newTerminal() >= 0);
  env.type("abc");
  env.key(Key::Enter);
  // The tty echoes the typed line, `cat` prints it again.
  CHECK(env.waitFor([&] {
    int n = 0;
    for (int r = 0; r < env.tv()->session()->screen().rows(); ++r)
      if (env.tv()->visibleRowText(r) == "abc") ++n;
    return n >= 2;
  }));
  // Application cursor keys: arrow encoding follows the mode (checked in TermKeys tests); here Backspace reaches cat.
  env.type("x");
  env.key(Key::Backspace);
  env.key(Key::Enter);
  CHECK(env.waitFor([&] { return env.tv()->session()->screen().cursorRow() >= 3; }));
#endif
}

TEST(term_show_terminal_creates_then_cycles_and_refocuses) {
#ifndef _WIN32
  if (!Pty::available()) return;
  Env env;
  env.settings(cmdSetting("sleep 60"));
  CHECK_EQ(env.terms().terminals().size(), size_t(0));
  env.key(Key('I'), ModCtrl | ModShift);  // ShowTerminal with none: creates one
  REQUIRE(env.terms().terminals().size() == 1);
  const int a = env.terms().terminals()[0];
  CHECK_EQ(env.bench->activePanel()->activeId(), a);
  env.key(Key('I'), ModCtrl | ModShift);  // one terminal, already focused: stays
  CHECK_EQ(env.terms().terminals().size(), size_t(1));
  CHECK_EQ(env.bench->activePanel()->activeId(), a);
  const int b = env.newTerminal();
  REQUIRE(b >= 0);
  CHECK_EQ(env.bench->activePanel()->activeId(), b);
  env.key(Key('I'), ModCtrl | ModShift);  // cycles b -> a
  CHECK_EQ(env.bench->activePanel()->activeId(), a);
  CHECK(env.tv(a)->hasFocus());
  env.key(Key('I'), ModCtrl | ModShift);  // a -> b (wraps)
  CHECK_EQ(env.bench->activePanel()->activeId(), b);
  // From an editor tab it shows the terminal shown last, without creating another.
  const int doc = env.bench->panelOf(b)->tabs().tabs().front().id;
  env.bench->panelOf(b)->setActive(doc);
  env.bench->setActivePanel(env.bench->panelOf(b));
  env.frame();
  CHECK(!env.bench->isWidgetTab(env.bench->activePanel()->activeId()));
  env.key(Key('I'), ModCtrl | ModShift);
  CHECK_EQ(env.bench->activePanel()->activeId(), b);
  CHECK_EQ(env.terms().terminals().size(), size_t(2));
#endif
}

TEST(term_claude_and_cursor_terminals_run_their_own_commands) {
#ifndef _WIN32
  if (!Pty::available()) return;
  Env env;
  env.settings(
      "terminal.claudeCommand = /bin/sh -c \"echo i-am-claude; sleep 60\"\n"
      "terminal.cursorCommand = cmd /c \"echo i-am-cursor; sleep 60\"\n"  // 10x's `cmd /c` prefix is dropped off
                                                                          // Windows
  );
  CHECK(env.terms().commandFor(TerminalPanel::Kind::Cursor) == std::vector<std::string>{"echo i-am-cursor; sleep 60"});
  env.app->runCommand("ClaudeCodeTerminal");
  REQUIRE(env.terms().terminals().size() == 1);
  CHECK(env.waitHas("i-am-claude"));
  CHECK_EQ(env.bench->panelOf(env.terms().terminals()[0])->tabs().tabs().back().title, std::string("Claude Code"));
  env.app->runCommand("CursorTerminal");  // runs `echo i-am-cursor; sleep 60` as a program name: fails to start
  CHECK(env.app->message().find("Terminal:") == 0);
  CHECK_EQ(env.terms().terminals().size(), size_t(1));
#endif
}

TEST(term_command_translation_off_windows_and_defaults) {
  Env env;
  Settings s;  // the shipped defaults: 10x's own
  env.app->applySettings(s);
  CHECK_EQ(s.getString("terminal.command"),
           std::string(""));  // VS-TERM: empty = pwsh/powershell on Windows, else $SHELL
  CHECK(!s.getBool("terminal.ctrlCCopiesSelection"));
  CHECK_EQ(s.getString("terminal.claudeCommand"), std::string("claude"));
  CHECK_EQ(s.getString("terminal.cursorCommand"), std::string("cmd /c agent"));
  CHECK(s.getFont("terminal.font").family.empty());
  CHECK_EQ(s.getFloat("terminal.fontSize"), 0.0);
  const auto claude = env.terms().commandFor(TerminalPanel::Kind::Claude);
  CHECK_EQ(claude.size(), size_t(1));
  CHECK_EQ(claude[0], std::string("claude"));
  const auto shell = env.terms().commandFor(TerminalPanel::Kind::Shell);
  const auto cursor = env.terms().commandFor(TerminalPanel::Kind::Cursor);
#ifdef _WIN32
  CHECK(shell[0] == "pwsh.exe" || shell[0] == "powershell.exe");
  CHECK_EQ(cursor.size(), size_t(3));
#else
  CHECK(shell == std::vector<std::string>{Pty::defaultShell()});  // $SHELL, else /bin/sh
  // An explicitly set 10x TerminalCommand stays authoritative; 10x's own `cmd.exe` still means the shell here.
  env.settings("terminal.command = /bin/sh -l\n");
  CHECK((env.terms().commandFor(TerminalPanel::Kind::Shell) == std::vector<std::string>{"/bin/sh", "-l"}));
  env.settings("terminal.command = cmd.exe\n");
  CHECK(env.terms().commandFor(TerminalPanel::Kind::Shell) == shell);
  CHECK(cursor == std::vector<std::string>{"agent"});
#endif
}

TEST(term_start_failure_is_reported_and_leaves_no_tab) {
  if (!Pty::available()) return;
  Env env;
  env.settings("terminal.command = /nonexistent/definitely-not-here\n");
  Panel* p = env.bench->activePanel();
  const size_t tabs = p->tabs().tabs().size();
  std::string err;
  CHECK_EQ(env.terms().create(TerminalPanel::Kind::Shell, &err), -1);
  CHECK(!err.empty());
  CHECK_EQ(p->tabs().tabs().size(), tabs);
  env.key(kQuote, ModCtrl);
  CHECK(env.app->message().find("Terminal:") == 0);
}

// ---------------------------------------------------------------------------------------------------------------
// Closing, exit
// ---------------------------------------------------------------------------------------------------------------

TEST(term_closing_the_tab_kills_the_whole_process_tree) {
#ifdef _WIN32
  Env env;
  env.settings(cmdSetting("echo tree-up& ping -n 120 127.0.0.1 >nul"));
  REQUIRE(env.newTerminal() >= 0);
  CHECK(env.waitHas("tree-up"));
  env.key(Key::F4, ModCtrl);  // Workbench.CloseTab
  CHECK_EQ(env.terms().terminals().size(), size_t(0));
#else
  if (!Pty::available()) return;
  Env env;
  // The shell starts a background child and waits for it: two processes in the session.
  env.settings(cmdSetting("sleep 300 & echo CHILD=$!; echo SHELL=$$; wait"));
  const int id = env.newTerminal();
  REQUIRE(id >= 0);
  REQUIRE(env.waitHas("SHELL="));
  const std::string scr = env.screen();
  const long child = pidAfter(scr, "CHILD="), shell = pidAfter(scr, "SHELL=");
  REQUIRE(child > 0 && shell > 0);
  CHECK(alive(child) && alive(shell));
  const size_t tabsBefore = env.bench->panelOf(id)->tabs().tabs().size();
  Panel* p = env.bench->panelOf(id);
  env.key(Key::F4, ModCtrl);  // Workbench.CloseTab (Control F4)
  CHECK_EQ(p->tabs().tabs().size(), tabsBefore - 1);
  CHECK_EQ(env.terms().terminals().size(), size_t(0));
  CHECK(!exists(shell));  // reaped by the Pty: not even a zombie
  for (int i = 0; i < 100 && alive(child); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
  CHECK(!alive(child));
  // The editor tab is back in front and has the keys.
  CHECK(!env.bench->isWidgetTab(env.bench->activePanel()->activeId()));
#endif
}

TEST(term_closing_with_the_tab_x_and_destroying_the_workbench_both_kill) {
#ifndef _WIN32
  if (!Pty::available()) return;
  long shell = -1;
  {
    Env env;
    env.settings(cmdSetting("echo SHELL=$$; sleep 300"));
    const int id = env.newTerminal();
    REQUIRE(id >= 0);
    REQUIRE(env.waitHas("SHELL="));
    shell = pidAfter(env.screen(), "SHELL=");
    Panel* p = env.bench->panelOf(id);
    // The x of the terminal's tab.
    const RectF x = p->tabs().closeRect(id);
    env.send(mouseEv(InputType::MouseDown, x.x + x.w / 2, x.y + x.h / 2));
    env.send(mouseEv(InputType::MouseUp, x.x + x.w / 2, x.y + x.h / 2));
    CHECK_EQ(env.terms().terminals().size(), size_t(0));
    CHECK(!exists(shell));
    // And a second one dies with the window.
    env.settings(cmdSetting("echo SHELL=$$; sleep 300"));
    REQUIRE(env.newTerminal() >= 0);
    REQUIRE(env.waitHas("SHELL="));
    shell = pidAfter(env.screen(), "SHELL=");
    CHECK(alive(shell));
  }
  CHECK(!exists(shell));
#endif
}

TEST(term_program_exit_keeps_the_final_screen_with_an_exit_line_and_does_not_close) {
#ifndef _WIN32
  if (!Pty::available()) return;
  Env env;
  env.settings(cmdSetting("echo last words; exit 3"));
  const int id = env.newTerminal();
  REQUIRE(id >= 0);
  CHECK(env.waitHas("[process exited with code 3]"));
  CHECK(env.has("last words"));
  TerminalSession* s = env.tv(id)->session();
  CHECK(s->exited());
  CHECK_EQ(s->exitCode(), 3);
#endif
}

TEST(term_close_on_exit_setting_closes_the_tab_by_itself) {
#ifndef _WIN32
  if (!Pty::available()) return;
  Env env;
  env.settings(cmdSetting("sleep 1; echo bye; exit 0") + "terminal.closeOnExit = true\n");
  const int id = env.newTerminal();
  REQUIRE(id >= 0);
  CHECK(env.bench->panelOf(id) != nullptr);  // still there while the program runs
  CHECK(env.waitFor([&] { return env.terms().terminals().empty(); }));
  CHECK(env.bench->panelOf(id) == nullptr);
#endif
}

TEST(term_typing_after_exit_is_ignored_and_ctrl_c_copy_still_works) {
#ifndef _WIN32
  if (!Pty::available()) return;
  Env env;
  FakeClipboard cb;
  env.settings(cmdSetting("echo gone; exit 1"));
  REQUIRE(env.newTerminal() >= 0);
  REQUIRE(env.waitHas("[process exited with code 1]"));
  env.type("zzz");  // must not crash or reach a dead pty
  env.key(Key('C'), ModCtrl);
  CHECK(env.tv()->session()->exited());
#endif
}

// ---------------------------------------------------------------------------------------------------------------
// Keys: what the program gets, what the app keeps
// ---------------------------------------------------------------------------------------------------------------

TEST(term_keys_go_to_the_program_except_the_app_strokes) {
#ifndef _WIN32
  if (!Pty::available()) return;
  Env env;
  env.settings(cmdSetting("cat -v"));
  const int id = env.newTerminal();
  REQUIRE(id >= 0);
  const int first = env.bench->panelOf(id)->tabs().tabs().front().id;
  // Ctrl+K starts a chord in the editor (Ctrl+K, Ctrl+C ...); in the terminal it is the program's.
  env.key(Key('K'), ModCtrl);
  CHECK(!env.app->message().empty() ? env.app->message().find("Control K") == std::string::npos : true);
  env.key(Key::Enter);
  CHECK(env.waitHas("^K"));
  // Ctrl+F (find in the editor) and Alt+Z (word wrap) are not the app's here either.
  env.key(Key('F'), ModCtrl);
  env.key(Key('Z'), ModAlt);
  env.key(Key::Enter);
  CHECK(env.waitHas("^F"));
  CHECK(env.waitHas("^[z"));
  // Ctrl+PageUp is the app's (PrevPanelTab): the terminal tab gives way to the editor tab and keeps its process.
  env.key(Key::PageUp, ModCtrl);
  CHECK_EQ(env.bench->activePanel()->activeId(), first);
  CHECK(env.terms().view(id)->session()->running());
  // ... and back with Ctrl+PageDown; the terminal has the keys again.
  env.key(Key::PageDown, ModCtrl);
  CHECK_EQ(env.bench->activePanel()->activeId(), id);
  CHECK(env.tv(id)->hasFocus());
  // The terminal commands themselves work from the terminal: Control ' makes another one.
  CHECK(env.newTerminal() >= 0);
#endif
}

namespace {
// Selects "beta gamma" on the "alpha beta gamma" line of the terminal with the mouse.
void selectBetaGamma(Env& env, TerminalView* v) {
  int row = -1;
  for (int r = 0; r < v->session()->screen().rows(); ++r)
    if (v->visibleRowText(r) == "alpha beta gamma") row = r;
  REQUIRE(row >= 0);
  const RectF a = v->cellRect(row, 6), b = v->cellRect(row, 15);
  env.send(mouseEv(InputType::MouseDown, a.x + 2, a.y + 3));
  env.send(mouseEv(InputType::MouseMove, b.x + 2, b.y + 3));
  env.send(mouseEv(InputType::MouseUp, b.x + 2, b.y + 3));
  CHECK(v->hasSelection());
  CHECK_EQ(v->selectedText(), std::string("beta gamma"));
}
}  // namespace

// VS-TERM (docs/VS_BEHAVIOUR.md sec. 12, M1): Ctrl+C is always ^C, with or without a selection.
TEST(term_ctrl_c_always_sends_0x03_even_with_a_selection) {
#ifndef _WIN32
  if (!Pty::available()) return;
  Env env;
  FakeClipboard cb;
  cb.text = "untouched";
  // Raw mode: the tty delivers ^C as a byte (cat -v shows it) instead of turning it into SIGINT.
  env.settings(cmdSetting("stty raw -echo; echo alpha beta gamma; cat -v"));
  REQUIRE(env.newTerminal() >= 0);
  REQUIRE(env.waitHas("alpha beta gamma"));
  TerminalView* v = env.tv();
  env.key(Key('C'), ModCtrl);  // no selection
  REQUIRE(env.waitHas("^C"));
  selectBetaGamma(env, v);
  env.key(Key('C'), ModCtrl);  // selection: still the program's, nothing copied
  CHECK(env.waitFor([&] {
    const std::string sc = env.screen();
    return sc.find("^C^C") != std::string::npos;
  }));
  CHECK_EQ(cb.text, std::string("untouched"));
  CHECK(v->hasSelection());
#endif
}

TEST(term_ctrl_c_interrupts_a_running_program_with_a_selection_too) {
#ifndef _WIN32
  if (!Pty::available()) return;
  Env env;
  FakeClipboard cb;
  env.settings(cmdSetting("trap 'echo GOT-INT; exit 7' INT; echo alpha beta gamma; while :; do sleep 0.05; done"));
  REQUIRE(env.newTerminal() >= 0);
  REQUIRE(env.waitHas("alpha beta gamma"));
  selectBetaGamma(env, env.tv());
  env.key(Key('C'), ModCtrl);
  CHECK(env.waitHas("GOT-INT"));
  CHECK(env.waitHas("[process exited with code 7]"));
  CHECK(cb.text.empty());
#endif
}

TEST(term_ctrl_v_sends_its_control_character_and_does_not_paste) {
#ifndef _WIN32
  if (!Pty::available()) return;
  Env env;
  FakeClipboard cb;
  cb.text = "clip";
  env.settings(cmdSetting("stty raw -echo; echo ready; cat -v"));
  REQUIRE(env.newTerminal() >= 0);
  REQUIRE(env.waitHas("ready"));
  env.key(Key('V'), ModCtrl);
  CHECK(env.waitHas("^V"));
  CHECK(!env.has("clip"));
#endif
}

TEST(term_ctrl_shift_c_and_ctrl_insert_copy_without_sending_anything) {
#ifndef _WIN32
  if (!Pty::available()) return;
  Env env;
  FakeClipboard cb;
  env.settings(cmdSetting("stty raw -echo; echo alpha beta gamma; cat -v"));
  REQUIRE(env.newTerminal() >= 0);
  REQUIRE(env.waitHas("alpha beta gamma"));
  TerminalView* v = env.tv();
  selectBetaGamma(env, v);
  env.key(Key('C'), ModCtrl | ModShift);
  CHECK_EQ(cb.text, std::string("beta gamma"));
  CHECK(v->hasSelection());  // copying keeps the selection
  cb.text.clear();
  env.key(Key::Insert, ModCtrl);
  CHECK_EQ(cb.text, std::string("beta gamma"));
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  env.frame();
  CHECK(!env.has("^C") && !env.has("^["));  // nothing reached the program
#endif
}

TEST(term_ctrl_shift_v_and_shift_insert_paste_bracketed) {
#ifndef _WIN32
  if (!Pty::available()) return;
  Env env;
  FakeClipboard cb;
  env.settings(cmdSetting("printf '\\033[?2004h'; cat -v"));
  REQUIRE(env.newTerminal() >= 0);
  REQUIRE(env.waitFor([&] { return env.tv()->session()->screen().bracketedPaste(); }));
  auto count = [&](const std::string& needle) {
    const std::string sc = env.screen();
    size_t n = 0;
    for (size_t p = sc.find(needle); p != std::string::npos; p = sc.find(needle, p + 1)) ++n;
    return n;
  };
  cb.text = "pasted";
  env.key(Key('V'), ModCtrl | ModShift);
  CHECK(env.waitFor([&] { return count("^[[200~pasted^[[201~") == 1; }));
  env.key(Key::Insert, ModShift);
  CHECK(env.waitFor([&] { return count("^[[200~pasted^[[201~") == 2; }));
#endif
}

TEST(term_right_click_pastes_and_never_copies_a_selection) {
#ifndef _WIN32
  if (!Pty::available()) return;
  Env env;
  FakeClipboard cb;
  env.settings(cmdSetting("stty raw -echo; echo alpha beta gamma; cat -v"));
  REQUIRE(env.newTerminal() >= 0);
  REQUIRE(env.waitHas("alpha beta gamma"));
  TerminalView* v = env.tv();
  cb.text = "RC";
  const RectF r = v->bounds();
  auto rightClick = [&] {
    InputEvent d = mouseEv(InputType::MouseDown, r.x + 40, r.y + r.h - 12);
    d.button = MouseButton::Right;
    env.send(d);
    d.type = InputType::MouseUp;
    env.send(d);
  };
  rightClick();  // no selection
  CHECK(env.waitFor([&] { return env.screen().find("RC") != std::string::npos; }));
  selectBetaGamma(env, v);
  cb.text = "ZZ";
  rightClick();  // selection: pastes the clipboard, the clipboard keeps its text
  CHECK(env.waitFor([&] { return env.screen().find("ZZ") != std::string::npos; }));
  CHECK_EQ(cb.text, std::string("ZZ"));
#endif
}

TEST(term_ctrl_c_copies_selection_setting_restores_windows_terminal_behaviour) {
#ifndef _WIN32
  if (!Pty::available()) return;
  Env env;
  FakeClipboard cb;
  env.settings("terminal.ctrlCCopiesSelection = true\n" +
               cmdSetting("trap 'echo GOT-INT; exit 7' INT; echo alpha beta gamma; while :; do sleep 0.05; done"));
  REQUIRE(env.newTerminal() >= 0);
  REQUIRE(env.waitHas("alpha beta gamma"));
  TerminalView* v = env.tv();
  selectBetaGamma(env, v);
  env.key(Key('C'), ModCtrl);  // copies and clears, does not interrupt
  CHECK_EQ(cb.text, std::string("beta gamma"));
  CHECK(!v->hasSelection());
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  env.frame();
  CHECK(!env.has("GOT-INT"));
  env.key(Key('C'), ModCtrl);  // no selection: ^C
  CHECK(env.waitHas("GOT-INT"));
  // The setting is picked up by a terminal that already runs.
  Env env2;
  env2.settings(cmdSetting("stty raw -echo; echo alpha beta gamma; cat -v"));
  REQUIRE(env2.newTerminal() >= 0);
  CHECK(!env2.tv()->ctrlCCopiesSelection);
  env2.settings("terminal.ctrlCCopiesSelection = true\n");
  CHECK(env2.tv()->ctrlCCopiesSelection);
#endif
}

TEST(term_copy_paste_keys_are_rebindable_through_the_key_map) {
  Env env;
  FakeClipboard cb;
  // The defaults: terminal-context bindings; Ctrl+C and Ctrl+V are not bound there.
  auto find = [&](const char* text) -> const KeyBinding* {
    for (const KeyBinding& b : env.app->keys().bindings())
      if (b.context == "terminal" && formatChords(b.chords) == text) return &b;
    return nullptr;
  };
  for (const char* k : {"Control Shift C", "Control Insert", "Control Shift V", "Shift Insert"}) {
    const KeyBinding* b = find(k);
    REQUIRE(b != nullptr);
    CHECK(resolveCommandName(b->command).id.rfind("Terminal.", 0) == 0);
  }
  CHECK(find("Control C") == nullptr && find("Control V") == nullptr);
  CHECK(env.app->commands().find("Terminal.Copy") != nullptr);
  CHECK(env.app->commands().find("Terminal.Paste") != nullptr);
  CHECK_EQ(resolveCommandName("Terminal.Copy").context, std::string("terminal"));
#ifndef _WIN32
  if (!Pty::available()) return;
  env.settings(cmdSetting("stty raw -echo; echo alpha beta gamma; cat -v"));
  REQUIRE(env.newTerminal() >= 0);
  REQUIRE(env.waitHas("alpha beta gamma"));
  // A user binding: Alt+Y pastes.
  KeyBinding kb;
  kb.chords = {chordFromKey(Key('Y'), ModAlt)};
  kb.context = "terminal";
  kb.command = "Terminal.Paste";
  env.app->keys().add(kb);
  cb.text = "viaalty";
  env.key(Key('Y'), ModAlt);
  CHECK(env.waitHas("viaalty"));
  // Removing the default makes Ctrl+Shift+V a key for the program (no paste).
  env.app->keys().remove({chordFromKey(Key('V'), ModCtrl | ModShift)}, "terminal");
  cb.text = "nopaste";
  env.key(Key('V'), ModCtrl | ModShift);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  env.frame();
  CHECK(!env.has("nopaste"));
#endif
}

TEST(term_double_and_triple_click_select_a_word_and_a_line) {
#ifndef _WIN32
  if (!Pty::available()) return;
  Env env;
  FakeClipboard cb;
  env.settings(cmdSetting("echo one two/three four; sleep 60"));
  REQUIRE(env.newTerminal() >= 0);
  REQUIRE(env.waitHas("one two/three four"));
  TerminalView* v = env.tv();
  int row = -1;
  for (int r = 0; r < v->session()->screen().rows(); ++r)
    if (v->visibleRowText(r) == "one two/three four") row = r;
  REQUIRE(row >= 0);
  const RectF c = v->cellRect(row, 5);  // inside "two/three"
  env.send(mouseEv(InputType::MouseDown, c.x + 2, c.y + 3, 2));
  env.send(mouseEv(InputType::MouseUp, c.x + 2, c.y + 3, 2));
  CHECK_EQ(v->selectedText(), std::string("two/three"));
  env.send(mouseEv(InputType::MouseDown, c.x + 2, c.y + 3, 3));
  env.send(mouseEv(InputType::MouseUp, c.x + 2, c.y + 3, 3));
  CHECK_EQ(v->selectedText(), std::string("one two/three four"));
  // A plain click clears it.
  env.send(mouseEv(InputType::MouseDown, c.x + 2, c.y + 3, 1));
  env.send(mouseEv(InputType::MouseUp, c.x + 2, c.y + 3, 1));
  CHECK(!v->hasSelection());
#endif
}

TEST(term_paste_is_bracketed_when_the_program_asked_and_never_blocks_the_ui) {
#ifndef _WIN32
  if (!Pty::available()) return;
  Env env;
  FakeClipboard cb;
  env.settings(cmdSetting("printf '\\033[?2004h'; cat -v"));
  REQUIRE(env.newTerminal() >= 0);
  REQUIRE(env.waitFor([&] { return env.tv()->session()->screen().bracketedPaste(); }));
  cb.text = "pasted";
  env.key(Key('V'), ModCtrl | ModShift);  // Ctrl+Shift+V pastes
  CHECK(env.waitHas("^[[200~pasted^[[201~"));
  env.key(Key::Insert, ModShift);  // so does Shift+Insert
  CHECK(env.waitFor([&] {
    const std::string s = env.screen();
    return s.find("^[[200~pasted^[[201~^[[200~pasted^[[201~") != std::string::npos ||
           std::count(s.begin(), s.end(), '~') >= 8;
  }));
#endif
}

TEST(term_a_huge_paste_into_a_program_that_does_not_read_returns_at_once_and_closing_still_works) {
#ifndef _WIN32
  if (!Pty::available()) return;
  Env env;
  env.settings(cmdSetting("echo deaf; sleep 300"));
  const int id = env.newTerminal();
  REQUIRE(id >= 0);
  REQUIRE(env.waitHas("deaf"));
  const std::string big(8 << 20, 'x');
  const auto t0 = std::chrono::steady_clock::now();
  env.tv()->pasteText(big);
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
  CHECK(ms < 3000);  // queued, not written inline (writing 8 MB into a deaf program takes far longer)
  const auto t1 = std::chrono::steady_clock::now();
  env.key(Key::F4, ModCtrl);  // closing kills the program the writer thread is stuck on
  const auto closeMs =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t1).count();
  std::printf("    paste %lld ms, close %lld ms\n", (long long)ms, (long long)closeMs);
  CHECK(closeMs < 5000);
  CHECK_EQ(env.terms().terminals().size(), size_t(0));
#endif
}

TEST(term_mouse_reports_follow_the_program_s_mode_and_shift_selects_instead) {
#ifndef _WIN32
  if (!Pty::available()) return;
  Env env;
  env.settings(cmdSetting("printf '\\033[?1000h\\033[?1006h'; cat -v"));
  REQUIRE(env.newTerminal() >= 0);
  TerminalView* v = env.tv();
  REQUIRE(env.waitFor([&] { return v->session()->screen().mouseTracking() == 1000; }));
  const RectF c = v->cellRect(4, 9);  // row 5, column 10 (1-based)
  env.send(mouseEv(InputType::MouseDown, c.x + 2, c.y + 3));
  env.send(mouseEv(InputType::MouseUp, c.x + 2, c.y + 3));
  env.key(Key::Enter);
  CHECK(env.waitHas("^[[<0;10;5M^[[<0;10;5m"));
  CHECK(!v->hasSelection());
  // A wheel notch reports button 64.
  env.send(wheelEv(1, c.x + 2, c.y + 3));
  env.key(Key::Enter);
  CHECK(env.waitHas("^[[<64;10;5M"));
  // Unit: the legacy encoding and modifiers.
  TermMouse m;
  m.kind = TermMouse::Press;
  m.button = 2;
  m.col = 0;
  m.row = 0;
  m.ctrl = true;
  CHECK_EQ(encodeTermMouse(m, false), std::string("\x1b[M") + char(32 + 2 + 16) + char(33) + char(33));
  CHECK_EQ(encodeTermMouse(m, true), std::string("\x1b[<18;1;1M"));
  m.kind = TermMouse::Release;
  CHECK_EQ(encodeTermMouse(m, true), std::string("\x1b[<18;1;1m"));
  m.col = 300;
  CHECK(encodeTermMouse(m, false).empty());
#endif
}

TEST(term_focus_reporting_tells_the_program_when_the_terminal_gains_and_loses_the_focus) {
#ifndef _WIN32
  if (!Pty::available()) return;
  Env env;
  env.settings(cmdSetting("printf '\\033[?1004h'; cat -v"));
  const int id = env.newTerminal();
  REQUIRE(id >= 0);
  TerminalView* v = env.tv();
  REQUIRE(env.waitFor([&] { return v->session()->screen().focusReporting(); }));
  const int doc = env.bench->panelOf(id)->tabs().tabs().front().id;
  env.bench->panelOf(id)->setActive(doc);
  env.bench->setActivePanel(env.bench->panelOf(id));  // focus moves to the editor view
  env.frame();
  CHECK(!v->hasFocus());
  env.bench->panelOf(id)->setActive(id);
  env.bench->setActivePanel(env.bench->panelOf(id));
  env.frame();
  CHECK(v->hasFocus());
  env.key(Key::Enter);
  CHECK(env.waitHas("^[[O^[[I"));
  // The window losing the focus counts as well.
  InputEvent lost;
  lost.type = InputType::FocusLost;
  env.send(lost);
  env.key(Key::Enter);
  env.key(Key::Enter);
  CHECK(env.waitFor([&] {
    const std::string sc = env.screen();
    size_t n = 0, pos = 0;
    while ((pos = sc.find("^[[O", pos)) != std::string::npos) ++n, pos += 4;
    return n >= 2;
  }));
#endif
}

// ---------------------------------------------------------------------------------------------------------------
// Size, scrollback, colours
// ---------------------------------------------------------------------------------------------------------------

TEST(term_resizing_the_panel_resizes_the_screen_and_the_pty) {
#ifndef _WIN32
  if (!Pty::available()) return;
  Env env;
  env.settings(cmdSetting("while read x; do stty size; done"));
  REQUIRE(env.newTerminal() >= 0);
  TerminalView* v = env.tv();
  auto sizeLine = [&](int rows, int cols) { return std::to_string(rows) + " " + std::to_string(cols); };
  env.key(Key::Enter);
  const VtScreen& s = v->session()->screen();
  CHECK(env.waitHas(sizeLine(s.rows(), s.cols())));
  const int rows0 = s.rows(), cols0 = s.cols();
  env.width = 700;
  env.height = 400;
  env.frame();
  CHECK(s.rows() < rows0 && s.cols() < cols0);
  // The view's grid is what the panel holds with this font: 8x14 cells, a small margin and the scrollbar column.
  const auto [gr, gc] = v->gridFor(v->bounds());
  CHECK_EQ(s.rows(), gr);
  CHECK_EQ(s.cols(), gc);
  env.key(Key::Enter);
  CHECK(env.waitHas(sizeLine(s.rows(), s.cols())));
#endif
}

TEST(term_scrollback_wheel_shift_page_keys_and_snap_to_bottom) {
#ifndef _WIN32
  if (!Pty::available()) return;
  Env env;
  env.settings(cmdSetting("i=1; while [ $i -le 300 ]; do echo line-$i; i=$((i+1)); done; cat"));
  REQUIRE(env.newTerminal() >= 0);
  TerminalView* v = env.tv();
  REQUIRE(env.waitHas("line-300"));
  const VtScreen& s = v->session()->screen();
  CHECK(s.scrollbackSize() > 200);
  CHECK_EQ(v->scrollOffset(), 0);
  const std::string bottom = Env::screenOf(v);
  const RectF r = v->bounds();
  env.send(wheelEv(1, r.x + 50, r.y + 50));  // up: wheelLines (3) lines
  CHECK_EQ(v->scrollOffset(), 3);
  CHECK(Env::screenOf(v) != bottom);
  env.send(wheelEv(-1, r.x + 50, r.y + 50));
  CHECK_EQ(v->scrollOffset(), 0);
  env.key(Key::PageUp, ModShift);
  CHECK_EQ(v->scrollOffset(), s.rows() - 1);
  env.key(Key::PageUp, ModShift);
  CHECK_EQ(v->scrollOffset(), 2 * (s.rows() - 1));
  env.key(Key::PageDown, ModShift);
  CHECK_EQ(v->scrollOffset(), s.rows() - 1);
  // New output keeps the place while scrolled back.
  const int off = v->scrollOffset();
  const std::string shown = Env::screenOf(v);
  env.type("more\n");
  CHECK_EQ(v->scrollOffset(), 0);  // typing snapped to the bottom
  // The tty echoes "more" and `cat` prints its copy a moment later: wait for both, or the copy arrives while the
  // keep-place part below counts lines and shifts the held view by one (seen under load).
  const auto occurrences = [&](const std::string& needle) {
    const std::string scr = Env::screenOf(v);
    size_t n = 0;
    for (size_t p = scr.find(needle); p != std::string::npos; p = scr.find(needle, p + 1)) ++n;
    return n;
  };
  CHECK(env.waitFor([&] { return occurrences("more") >= 2; }));
  (void)off;
  (void)shown;
  // Keep-place: scroll up, then let output arrive.
  env.send(wheelEv(2, r.x + 50, r.y + 50));
  const std::string held = Env::screenOf(v);
  const uint64_t fed0 = v->session()->bytesFed();
  v->session()->write("x\n");  // echoed by the tty and printed by cat: scrolls the live screen
  CHECK(env.waitFor([&] { return v->session()->bytesFed() > fed0; }));
  // Both lines (the echo, then cat's copy) must have arrived: the held view moved up by exactly two. Waiting for the
  // offset instead of a fixed number of frames; the frames after it check that nothing more arrives.
  CHECK(env.waitFor([&] { return v->scrollOffset() >= 2 * 3 + 2; }));
  for (int i = 0; i < 50; ++i) {
    env.frame();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  CHECK_EQ(v->scrollOffset(), 2 * 3 + 2);  // the held lines moved up with the two new ones: echo and cat's copy
  CHECK_EQ(Env::screenOf(v), held);
  // Any key snaps back.
  env.key(Key::Enter);
  CHECK_EQ(v->scrollOffset(), 0);
  // The scrollbar thumb drags.
  env.send(wheelEv(5, r.x + 50, r.y + 50));
  CHECK(v->scrollOffset() > 0);
  env.send(mouseEv(InputType::MouseDown, r.x + r.w - 3, r.y + r.h - 10));  // bottom of the track
  env.send(mouseEv(InputType::MouseUp, r.x + r.w - 3, r.y + r.h - 10));
  CHECK_EQ(v->scrollOffset(), 0);
#endif
}

TEST(term_sgr_colours_render_with_the_theme_palette) {
#ifndef _WIN32
  if (!Pty::available()) return;
  Theme t;
  t.terminalBackground = {0.10f, 0.11f, 0.12f, 1};
  t.terminalForeground = {0.70f, 0.71f, 0.72f, 1};
  t.terminalAnsi1 = {0.91f, 0.21f, 0.11f, 1};   // red text
  t.terminalAnsi4 = {0.13f, 0.43f, 0.93f, 1};   // blue background
  t.terminalAnsi10 = {0.31f, 0.81f, 0.41f, 1};  // bright green
  Env env(t);
  env.settings(cmdSetting(
      "printf '\\033[31mred\\033[0m \\033[44mblue\\033[0m \\033[92mbright\\033[0m \\033[38;5;196mcube\\033[0m "
      "\\033[38;2;10;20;30mtrue\\033[0m \\033[1;4mbu\\033[0m \\033[7minv\\033[0m\\n'; sleep 60"));
  REQUIRE(env.newTerminal() >= 0);
  REQUIRE(env.waitHas("true"));
  // The screen model carries the attributes ...
  TerminalView* v = env.tv();
  const VtScreen& s = v->session()->screen();
  int row = -1;
  for (int r = 0; r < s.rows(); ++r)
    if (v->visibleRowText(r).find("red blue") == 0) row = r;
  REQUIRE(row >= 0);
  CHECK_EQ(s.cell(row, 0).fg, vtIndexed(1));
  CHECK_EQ(s.cell(row, 4).bg, vtIndexed(4));
  CHECK_EQ(s.cell(row, 9).fg, vtIndexed(10));
  CHECK_EQ(s.cell(row, 16).fg, vtIndexed(196));
  CHECK_EQ(s.cell(row, 21).fg, vtRgb(10, 20, 30));
  CHECK(s.cell(row, 26).attrs & kVtBold);
  CHECK(s.cell(row, 26).attrs & kVtUnderline);
  CHECK(s.cell(row, 29).attrs & kVtInverse);
  // ... and the frame uses the theme: the terminal background, the blue cell background, glyph batches in the palette
  // colours (red, bright green) and in the 256-colour / truecolour values.
  CHECK(env.rec.hasRect(t.terminalBackground));
  CHECK(env.rec.hasRect(t.terminalAnsi4));
  CHECK(env.rec.hasGlyphs(t.terminalAnsi1));
  CHECK(env.rec.hasGlyphs(t.terminalAnsi10));
  CHECK(env.rec.hasGlyphs({1.0f, 0.0f, 0.0f, 1}));  // 38;5;196 = #FF0000
  CHECK(env.rec.hasGlyphs({10 / 255.f, 20 / 255.f, 30 / 255.f, 1}));
  CHECK(env.rec.hasGlyphs(t.terminalForeground));
  CHECK(env.rec.hasRect(t.terminalForeground));  // the inverse cells and the block cursor
#endif
}

TEST(term_theme_colours_fall_back_and_vs_dark_defines_the_terminal) {
  Theme def;
  CHECK(same(def.terminalBg(), def.panelBackground));
  CHECK(same(def.terminalFg(), def.text));
  def.editorBackground = {0.2f, 0.2f, 0.2f, 1};
  CHECK(same(def.terminalBg(), def.editorBackground));
  CHECK(same(def.terminalAnsi(1), def.terminalAnsi1));
  CHECK(same(def.terminalAnsi(15), def.terminalAnsi15));
  std::vector<SettingsDiag> d;
  const Theme vs = loadTheme("vs-dark", {}, {}, d);
  CHECK(d.empty());
  CHECK(same(vs.terminalBg(), {30 / 255.f, 30 / 255.f, 30 / 255.f, 1}));
  CHECK(same(vs.terminalFg(), {204 / 255.f, 204 / 255.f, 204 / 255.f, 1}));
  CHECK(same(vs.terminalAnsi(1), {197 / 255.f, 15 / 255.f, 31 / 255.f, 1}));
  CHECK(same(vs.terminalAnsi(12), {59 / 255.f, 120 / 255.f, 255 / 255.f, 1}));
}

// ---------------------------------------------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------------------------------------------

TEST(term_tabs_are_not_saved_in_the_layout_and_move_between_panels) {
#ifndef _WIN32
  if (!Pty::available()) return;
  Env env;
  env.settings(cmdSetting("echo moving; sleep 60"));
  const int id = env.newTerminal();
  REQUIRE(id >= 0);
  REQUIRE(env.waitHas("moving"));
  const std::string json = env.bench->layoutJson();
  CHECK(json.find("\"path\": \"\"") == std::string::npos);
  CHECK(json.find("a.cpp") != std::string::npos);
  // Split: the terminal moves into the new panel with its process and its screen.
  Panel* p0 = env.bench->activePanel();
  Panel* p1 = env.bench->splitActive(Splitter::Orientation::Horizontal);
  REQUIRE(p1 != p0);
  env.frame();
  CHECK(env.bench->moveTab(id, p1, -1));
  env.frame();
  CHECK(env.bench->panelOf(id) == p1);
  CHECK(env.terms().view(id)->session()->running());
  CHECK(env.has("moving", id));
  // The new panel is smaller than the old; the terminal followed.
  CHECK(env.terms().view(id)->session()->screen().cols() < 120);
#endif
}

// ---------------------------------------------------------------------------------------------------------------
// Session unit tests
// ---------------------------------------------------------------------------------------------------------------

TEST(term_session_feeds_on_the_posting_thread_and_ends_with_one_exit_line) {
#ifndef _WIN32
  if (!Pty::available()) return;
  std::mutex mu;
  std::deque<std::function<void()>> q;
  TerminalSession::Hooks h;
  h.post = [&](std::function<void()> f) {
    std::lock_guard<std::mutex> lk(mu);
    q.push_back(std::move(f));
  };
  auto pump = [&] {
    std::deque<std::function<void()>> b;
    {
      std::lock_guard<std::mutex> lk(mu);
      b.swap(q);
    }
    for (auto& f : b) f();
  };
  int exits = 0, outputs = 0;
  TerminalSession s(h, 10, 40, 100);
  s.onExit = [&](int) { ++exits; };
  s.onOutput = [&] { ++outputs; };
  PtyOptions o;
  o.program = "/bin/sh";
  o.args = {"-c", "printf 'one\\ntwo\\n'; exit 5"};
  std::string err;
  REQUIRE(s.start(o, &err));
  for (int i = 0; i < 5000 && !s.exited(); ++i) {
    pump();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  REQUIRE(s.exited());
  pump();
  CHECK_EQ(exits, 1);
  CHECK(outputs >= 1);
  CHECK_EQ(s.exitCode(), 5);
  CHECK_EQ(s.screen().rowText(0), std::string("one"));
  CHECK_EQ(s.screen().rowText(1), std::string("two"));
  CHECK_EQ(s.screen().rowText(2), std::string("[process exited with code 5]"));
  // Output queued for a session that died is dropped, not delivered into freed memory.
#endif
}

TEST(term_session_destroyed_with_tasks_still_queued_is_safe) {
#ifndef _WIN32
  if (!Pty::available()) return;
  std::mutex mu;
  std::deque<std::function<void()>> q;
  TerminalSession::Hooks h;
  h.post = [&](std::function<void()> f) {
    std::lock_guard<std::mutex> lk(mu);
    q.push_back(std::move(f));
  };
  {
    TerminalSession s(h, 10, 40, 100);
    PtyOptions o;
    o.program = "/bin/sh";
    o.args = {"-c", "yes | head -c 3000000; sleep 30"};
    std::string err;
    REQUIRE(s.start(o, &err));
    std::this_thread::sleep_for(
        std::chrono::milliseconds(100));  // the reader is blocked on the high-water mark or busy
  }  // destroyed without a single pump
  std::deque<std::function<void()>> b;
  {
    std::lock_guard<std::mutex> lk(mu);
    b.swap(q);
  }
  for (auto& f : b) f();  // finds the session gone
  CHECK(true);
#endif
}

// ---------------------------------------------------------------------------------------------------------------
// Performance: a 50 MB burst of coloured output
// ---------------------------------------------------------------------------------------------------------------

TEST(term_perf_50mb_burst_keeps_frames_short) {
#ifndef _WIN32
  if (!Pty::available()) return;
  Env env;
  const fs::path script = env.dir / "burst.sh";
  std::ofstream(script)
      << "yes \"$(printf '\\033[32mgreen\\033[0m \\033[1;31mred bold\\033[0m plain text 0123456789 "
         "abcdefghij klmnopqrstuvwxyz ........')\" | head -c 52428800\nprintf '\\033[0m\\nBURST-END\\n'\nsleep 60\n";
  env.settings("terminal.command = /bin/sh " + script.generic_string() + "\n");
  REQUIRE(env.newTerminal() >= 0);
  TerminalView* v = env.tv();
  using clk = std::chrono::steady_clock;
  std::vector<double> frames;
  const auto t0 = clk::now();
  bool done = false;
  while (!done && clk::now() - t0 < std::chrono::seconds(180)) {
    const auto f0 = clk::now();
    env.frame();
    frames.push_back(std::chrono::duration<double, std::milli>(clk::now() - f0).count());
    done = env.has("BURST-END");
    if (!v->session()->outputPending()) std::this_thread::sleep_for(std::chrono::microseconds(200));
  }
  const double secs = std::chrono::duration<double>(clk::now() - t0).count();
  REQUIRE(done);
  const double mb = static_cast<double>(v->session()->bytesFed()) / (1 << 20);
  std::sort(frames.begin(), frames.end());
  const double worst = frames.back(), p99 = frames[frames.size() * 99 / 100];
  std::printf("    PERF terminal burst: %.1f MB in %.2f s = %.1f MB/s; %zu frames, p99 %.1f ms, worst %.1f ms\n", mb,
              secs, mb / secs, frames.size(), p99, worst);
  CHECK(mb >= 50.0);
  PERF_CHECK(worst < 100.0, "worst frame during a 50 MB burst under 100 ms");
  PERF_CHECK(p99 < 40.0, "99th percentile frame under 40 ms");
  PERF_CHECK(mb / secs > 15.0, "burst throughput over 15 MB/s");
#endif
}

// ---------------------------------------------------------------------------------------------------------------
// Golden (GL + DejaVuSansMono)
// ---------------------------------------------------------------------------------------------------------------

TEST(term_golden_vs_dark_terminal_with_coloured_output) {
#ifdef _WIN32
  return;  // the program and the tab title differ on Windows; the golden is Linux/macOS only
#else
  SKIP_IF_NO_GL(fx);
  if (!Pty::available()) return;
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
  std::vector<SettingsDiag> diags;
  const Theme vs = loadTheme("vs-dark", {}, {}, diags);
  CHECK(diags.empty());
  Editor::Options o;
  o.semantic.useClangd = false;
  o.semantic.persistFastIndex = false;
  o.watchForcePolling = true;
  o.watchPollMs = 60000;
  o.vcs.enabled = false;
  Editor ed(TENFOLD_SOURCE_DIR "/examples/sample_project", o);
  REQUIRE(ed.start(&err));
  Workbench wb(*tr, ed, vs);
  wb.openFile("src/main.cpp");
  wb.sync();
  uitest::wrapOff(wb);
  EditorApp app(wb, ed, &wb);
  app.terminalPanel()->wake = [] {};
  wb.splitActive(Splitter::Orientation::Horizontal);  // the terminal goes into the new, right-hand panel
  gfx::GlRenderer& r = *fx->renderer;
  r.setOffscreen(true);
  wb.render(r, 1024, 640);
  const fs::path script = fs::temp_directory_path() / "tf_w8f_golden.sh";
  std::ofstream(script) << R"SH(printf '\033]0;sh\007'
printf '\033[1;32muser@tenfold\033[0m:\033[1;34m~/sample_project\033[0m$ ls --color\n'
printf '\033[1;34msrc\033[0m  \033[1;34minclude\033[0m  \033[0;32mbuild.sh\033[0m  README.md\n'
printf '\033[1;32muser@tenfold\033[0m:\033[1;34m~/sample_project\033[0m$ palette\n'
i=0; while [ $i -lt 8 ]; do printf '\033[4%dm  \033[0m' $i; i=$((i+1)); done; printf '\n'
i=0; while [ $i -lt 8 ]; do printf '\033[10%dm  \033[0m' $i; i=$((i+1)); done; printf '\n'
printf '\033[31merror\033[0m \033[33mwarning\033[0m \033[36mnote\033[0m \033[1;4mbold underline\033[0m \033[7minverse\033[0m \033[2mdim\033[0m \033[38;2;255;128;0mtruecolour\033[0m\n'
printf '\033[1;32muser@tenfold\033[0m:\033[1;34m~/sample_project\033[0m$ \nEND\n'
sleep 60
)SH";
  Settings st;
  st.loadTexts("terminal.command = /bin/sh " + script.generic_string() + "\n", "");
  app.applySettings(st);
  app.terminalPanel()->create(TerminalPanel::Kind::Shell, &err);
  auto frame = [&] {
    ed.pump();
    app.tick(1000);
    wb.render(r, 1024, 640);
  };
  TerminalView* v = app.terminalPanel()->view(app.terminalPanel()->terminals().at(0));
  REQUIRE(v != nullptr);
  bool ok = false;
  for (int i = 0; i < 5000 && !ok; ++i) {
    frame();
    for (int row = 0; row < v->session()->screen().rows(); ++row)
      if (v->visibleRowText(row) == "END") ok = true;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  REQUIRE(ok);
  for (int i = 0; i < 3; ++i) frame();
  const int w = 1024, h = 640;
  wb.render(r, w, h);
  std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4);
  REQUIRE(r.readPixels(w, h, px.data()));
  tr->releaseTexture(r);
  const std::string path = TENFOLD_SOURCE_DIR "/tests/golden/w8f_terminal.png";
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
    gfx::writePngFile("w8f_terminal.actual.png", px.data(), w, h);
    std::cerr << "    " << bad << " pixels differ; wrote w8f_terminal.actual.png\n";
  }
  CHECK(bad <= golden.size() / 4 / 500);
#endif
}
