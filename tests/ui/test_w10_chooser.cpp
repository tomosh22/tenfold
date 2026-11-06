// W10-CHOOSE: the go-to-definition chooser (NavResultPicker with labelled targets) driven through EditorApp with the
// real key bindings: it appears at the caret for an alias / a constructor call, selects the preferred target, Enter
// jumps through gotoLocation (Back returns), Escape cancels; a single target still jumps directly. The first tests feed
// the picker directly; the others use a real clangd (skipped without it).
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#include "../text/text_test_util.h"
#include "core/Platform.h"
#include "ui/features/NavResultPicker.h"
#include "ui/gfx/TextRenderer.h"
#include "ui/shell/EditorApp.h"
#include "ui/widgets/ListPopup.h"
#include "ui/widgets/TextView.h"
#include "ui_test.h"

#define REQUIRE_EQ(a, b) REQUIRE((a) == (b))

using namespace tf;
using namespace tf::ui;
namespace fs = std::filesystem;

namespace {

struct Recorder : gfx::Renderer {
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

std::unique_ptr<gfx::TextRenderer> mockText() {
  gfx::FontStack stack;
  stack.add(std::make_shared<tftext::MockFont>(
      U"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ._-:,;/()[]{}<>+*&#?=!\"'#→"));
  return gfx::TextRenderer::fromFonts(std::move(stack));
}

InputEvent keyEv(Key k, uint8_t mods = 0) {
  InputEvent e;
  e.type = InputType::KeyDown;
  e.key = k;
  e.mods = mods;
  return e;
}

void writeFile(const fs::path& p, const std::string& text) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary) << text;
}

const char* kHeader = R"(#pragma once
class Widget {
 public:
  Widget();
  explicit Widget(int size);
  int size() const { return size_; }
 private:
  int size_;
};
struct Gadget { int n = 1; };
typedef Widget WidgetAlias;
namespace ns { int f(int); }
using ns::f;
int plain_fn(int);
)";
const char* kMain = R"(#include "t.h"
Widget::Widget() : size_(0) {}
Widget::Widget(int size) : size_(size) {}
int ns::f(int x) { return x; }
int plain_fn(int x) { return x; }
// ................................................................................................ filler: the
// definitions above are far from the uses below, so a jump is a real history entry (nearby jumps coalesce)
//
//
//
//
//
//
//
//
//
//
//
//
//
//
//
//
//
//
//
//
//
//
int use() {
  Widget w1(3);
  Gadget g1;
  Gadget g2(g1);
  WidgetAlias wa;
  return w1.size() + f(1) + plain_fn(2) + g2.n + wa.size();
}
)";

struct Env {
  fs::path dir;
  std::unique_ptr<gfx::TextRenderer> text = mockText();
  Recorder rec;
  std::unique_ptr<Editor> ed;
  std::unique_ptr<Workbench> bench;
  std::unique_ptr<EditorApp> app;
  int64_t now = 1000;

  explicit Env(bool clangd, int budgetMs = 250) {
    static int n = 0;
    dir =
        uitest::tempRoot() / ("tf_w10_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                              "_" + std::to_string(n++));
    fs::create_directories(dir);
    writeFile(dir / "src/t.h", kHeader);
    writeFile(dir / "src/main.cpp", kMain);
    writeFile(dir / "compile_commands.json", "[{\"directory\":\"" + dir.generic_string() + "\",\"file\":\"" +
                                                 (dir / "src/main.cpp").generic_string() +
                                                 "\",\"arguments\":[\"clang++\",\"-std=c++20\",\"-c\",\"" +
                                                 (dir / "src/main.cpp").generic_string() + "\"]}]\n");
    Editor::Options o;
    o.semantic.useClangd = clangd;
    o.semantic.persistFastIndex = false;
    o.semantic.clangdBudgetMs = budgetMs;
    o.watchForcePolling = true;
    o.watchPollMs = 60000;
    ed = std::make_unique<Editor>(dir, o);
    std::string err;
    if (!ed->start(&err)) std::cerr << "    editor start: " << err << "\n";
    for (int i = 0; i < 2000; ++i) {
      ed->pump();
      if (ed->semantic().status().fastIndexDone) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    bench = std::make_unique<Workbench>(*text, *ed);
    app = std::make_unique<EditorApp>(*bench, *ed, bench.get());
    bench->render(rec, 1000, 600);
    bench->openFile(dir / "src/main.cpp");
    bench->sync();
    frame();
    if (clangd) {
      until([&] { return ed->semantic().status().clangdInitialized; }, 60000);
      ed->semantic().waitForFile(dir / "src/main.cpp", 60000);
      ed->semantic().waitForClangdIndex(60000);
    }
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
  bool key(Key k, uint8_t mods = 0) {
    now += 10;
    const bool r = app->handleEvent(keyEv(k, mods), now);
    frame();
    return r;
  }
  bool until(const std::function<bool()>& pred, int ms = 20000) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    for (;;) {
      frame();
      if (pred()) return true;
      if (std::chrono::steady_clock::now() >= end) return false;
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
  }
  // Caret on the first character of the `nth` `token` of source line `line` (0-based) of the current file.
  void caretAt(int line, const std::string& token, int nth = 0) {
    const std::string& l = ed->currentDocument()->buffer().line(line);
    size_t p = std::string::npos;
    for (int i = 0; i <= nth; ++i) p = l.find(token, p == std::string::npos ? 0 : p + 1);
    ed->moveCursor({line, static_cast<int>(p)});
    frame();
  }
  // 0-based line of the first line of the current document that contains `text`.
  int lineOf(const std::string& text) {
    const TextBuffer& b = ed->currentDocument()->buffer();
    for (int i = 0; i < b.lineCount(); ++i)
      if (b.line(i).find(text) != std::string::npos) return i;
    return -1;
  }
  std::string curFile() { return ed->currentDocument()->path().filename().string(); }
  TextView* view() { return dynamic_cast<TextView*>(bench->focused()); }
  ListPopup* popup() { return dynamic_cast<ListPopup*>(bench->modal()); }
};

SymbolLocation target(Env& env, const char* file, int line, const std::string& label, bool preferred = false,
                      Confidence c = Confidence::Authoritative) {
  SymbolLocation s;
  s.loc.file = env.dir / file;
  s.loc.pos = {line, 0};
  s.name = label;
  s.label = label;
  s.preferred = preferred;
  s.confidence = c;
  return s;
}

bool haveClangd() { return !findExecutable("clangd").empty(); }
#define SKIP_WITHOUT_CLANGD()                               \
  if (!haveClangd()) {                                      \
    std::cerr << "    SKIPPED: clangd not found on PATH\n"; \
    return;                                                 \
  }

}  // namespace

TEST(w10_chooser_rows_show_kind_code_and_location_and_select_the_preferred_target) {
  Env env(false);
  NavResultPicker& np = env.app->navResults();
  np.show("Go to Definition", Confidence::Authoritative,
          {target(env, "src/t.h", 1, "class Widget"), target(env, "src/main.cpp", 2, "constructor Widget(int)", true)},
          "", 2);
  env.frame();
  ListPopup* p = env.popup();
  REQUIRE(p != nullptr);
  REQUIRE_EQ(p->rows().size(), size_t(2));
  CHECK_EQ(p->rows()[0].primary, std::string("class Widget"));
  CHECK_EQ(p->rows()[1].primary, std::string("constructor Widget(int)"));
  CHECK_EQ(p->rows()[0].detail, std::string("src/t.h:2"));
  CHECK_EQ(p->rows()[1].detail, std::string("src/main.cpp:3"));
  CHECK(p->rows()[0].secondary.find("class Widget") != std::string::npos);  // the code line
  CHECK(p->rows()[1].secondary.find("Widget::Widget(int size)") != std::string::npos);
  CHECK_EQ(p->selected(), 1);  // the preferred target is selected, not the first row
  CHECK(p->status().empty());
  CHECK(!p->rows()[0].dim);
}

TEST(w10_chooser_sits_at_the_caret_and_enter_jumps_with_history_and_escape_cancels) {
  Env env(false);
  const int useLine = env.lineOf("Widget w1(3)");
  env.caretAt(useLine, "Widget");
  TextView* tv = env.view();
  REQUIRE(tv != nullptr);
  const RectF caret = tv->caretRect(env.ed->cursor().pos);
  NavResultPicker& np = env.app->navResults();
  np.setAnchor(caret);
  const size_t base = env.ed->navigation().entries().size();
  np.show("Go to Definition", Confidence::Authoritative,
          {target(env, "src/t.h", 1, "class Widget"), target(env, "src/main.cpp", 2, "constructor Widget(int)", true)},
          "", 2);
  env.frame();
  ListPopup* p = env.popup();
  REQUIRE(p != nullptr);
  CHECK(p->anchored());
  const RectF box = p->popupRect();
  CHECK(box.y >= caret.y + caret.h - 1 || box.y + box.h <= caret.y + 1);  // next to the caret, not over it
  CHECK(box.x <= caret.x + 1 && box.x + box.w >= caret.x);
  CHECK(box.h < 200);  // two rows, not the ten-row search popup
  // Keyboard only: Up selects the class, Enter jumps there; the jump is in the navigation history.
  CHECK(env.key(Key::Up));
  CHECK_EQ(p->selected(), 0);
  CHECK(env.key(Key::Enter));
  CHECK(env.popup() == nullptr);
  CHECK_EQ(env.curFile(), std::string("t.h"));
  CHECK_EQ(env.ed->cursor().pos.line, 1);
  CHECK(env.ed->navigation().entries().size() > base);
  CHECK(env.key(Key('-'), ModCtrl));  // Back
  CHECK_EQ(env.curFile(), std::string("main.cpp"));
  CHECK_EQ(env.ed->cursor().pos.line, useLine);
  // Escape cancels: nothing moves, nothing is recorded.
  const size_t n = env.ed->navigation().entries().size();
  np.show("Go to Definition", Confidence::Authoritative,
          {target(env, "src/t.h", 1, "class Widget"), target(env, "src/main.cpp", 2, "constructor Widget(int)", true)},
          "", 2);
  env.frame();
  REQUIRE(env.popup() != nullptr);
  CHECK(env.key(Key::Escape));
  CHECK(env.popup() == nullptr);
  CHECK_EQ(env.curFile(), std::string("main.cpp"));
  CHECK_EQ(env.ed->cursor().pos.line, useLine);
  CHECK_EQ(env.ed->navigation().entries().size(), n);
}

TEST(w10_chooser_is_updated_in_place_when_the_authoritative_list_arrives) {
  Env env(false);
  NavResultPicker& np = env.app->navResults();
  np.show("Go to Definition", Confidence::Authoritative,
          {target(env, "src/t.h", 1, "class Widget"), target(env, "src/main.cpp", 2, "constructor Widget(int)", true)},
          "", 2);
  env.frame();
  ListPopup* p = env.popup();
  REQUIRE(p != nullptr);
  env.key(Key::Up);
  CHECK_EQ(p->selected(), 0);
  // A second answer for the same run swaps the rows; the selection stays on the same target; one popup only.
  np.show("Go to Definition", Confidence::Authoritative,
          {target(env, "src/t.h", 1, "class Widget"), target(env, "src/main.cpp", 2, "constructor Widget(int)", true),
           target(env, "src/main.cpp", 1, "constructor Widget()")},
          "", 2);
  env.frame();
  CHECK(env.popup() == p);
  CHECK_EQ(p->rows().size(), size_t(3));
  CHECK_EQ(p->selected(), 0);
}

TEST(w10_f12_on_a_constructor_call_opens_the_chooser_with_the_constructor_selected) {
  SKIP_WITHOUT_CLANGD();
  Env env(true);
  const int useLine = env.lineOf("Widget w1(3)");
  env.caretAt(useLine, "Widget");
  const size_t base = env.ed->navigation().entries().size();
  CHECK(env.key(Key::F12));  // Editor.GotoDefinition, the 10x binding
  REQUIRE(env.until([&] { return env.popup() != nullptr; }));
  ListPopup* p = env.popup();
  REQUIRE_EQ(p->rows().size(), size_t(2));
  CHECK_EQ(p->rows()[0].primary, std::string("class Widget"));
  CHECK_EQ(p->rows()[1].primary, std::string("constructor Widget(int)"));
  CHECK_EQ(p->selected(), 1);
  CHECK(p->anchored());
  CHECK_EQ(env.curFile(), std::string("main.cpp"));  // not jumped
  CHECK_EQ(env.ed->navigation().entries().size(), base);
  CHECK(env.key(Key::Enter));  // the constructor: its definition in main.cpp
  CHECK(env.popup() == nullptr);
  CHECK_EQ(env.curFile(), std::string("main.cpp"));
  CHECK_EQ(env.ed->cursor().pos.line, 2);
  CHECK(env.ed->navigation().entries().size() > base);
  CHECK(env.key(Key('-'), ModCtrl));
  CHECK_EQ(env.ed->cursor().pos.line, useLine);
  // Again, this time choosing the class.
  CHECK(env.key(Key::F12));
  REQUIRE(env.until([&] { return env.popup() != nullptr; }));
  env.key(Key::Up);
  CHECK(env.key(Key::Enter));
  CHECK_EQ(env.curFile(), std::string("t.h"));
  CHECK_EQ(env.ed->cursor().pos.line, 1);
  CHECK(env.key(Key('-'), ModCtrl));
  CHECK_EQ(env.curFile(), std::string("main.cpp"));
}

TEST(w10_f12_through_a_using_declaration_and_an_alias_opens_the_chooser_with_the_entity_selected) {
  SKIP_WITHOUT_CLANGD();
  Env env(true);
  const int callLine = env.lineOf("return w1.size()");
  env.caretAt(callLine, "f(1)");
  CHECK(env.key(Key::F12));
  REQUIRE(env.until([&] { return env.popup() != nullptr; }));
  ListPopup* p = env.popup();
  REQUIRE_EQ(p->rows().size(), size_t(2));
  CHECK_EQ(p->rows()[0].primary, std::string("using-declaration"));
  CHECK(p->rows()[1].primary.find("function ns::f") == 0);
  CHECK_EQ(p->selected(), 1);
  CHECK(env.key(Key::Escape));  // cancelled: still in main.cpp at the call
  CHECK(env.popup() == nullptr);
  CHECK_EQ(env.curFile(), std::string("main.cpp"));
  CHECK_EQ(env.ed->cursor().pos.line, callLine);

  env.caretAt(env.lineOf("WidgetAlias wa"), "WidgetAlias");
  CHECK(env.key(Key::F12));
  REQUIRE(env.until([&] { return env.popup() != nullptr; }));
  p = env.popup();
  REQUIRE_EQ(p->rows().size(), size_t(2));
  CHECK(p->rows()[0].primary.find("typedef WidgetAlias") == 0);
  CHECK_EQ(p->rows()[1].primary, std::string("class Widget"));
  CHECK_EQ(p->selected(), 1);
  CHECK(env.key(Key::Enter));
  CHECK_EQ(env.curFile(), std::string("t.h"));
  CHECK_EQ(env.ed->cursor().pos.line, 1);
}

TEST(w10_single_target_jumps_directly_without_a_chooser_and_an_implicit_constructor_is_said) {
  SKIP_WITHOUT_CLANGD();
  Env env(true);
  const int callLine = env.lineOf("return w1.size()");
  env.caretAt(callLine, "plain_fn");
  CHECK(env.key(Key::F12));
  REQUIRE(env.until([&] { return env.curFile() == "main.cpp" && env.ed->cursor().pos.line == 4; }));
  CHECK(env.popup() == nullptr);
  CHECK(env.key(Key('-'), ModCtrl));
  CHECK_EQ(env.ed->cursor().pos.line, callLine);
  // Gadget g2(g1): no written constructor, one target: jumps to the class and says so.
  env.caretAt(env.lineOf("Gadget g2(g1)"), "Gadget");
  CHECK(env.key(Key::F12));
  REQUIRE(env.until([&] { return env.curFile() == "t.h"; }));
  CHECK(env.popup() == nullptr);
  CHECK(env.until([&] { return env.app->message().find("implicit constructor") != std::string::npos; }));
}

TEST(w10_a_held_back_approximate_answer_shows_one_chooser_when_clangd_answers) {
  SKIP_WITHOUT_CLANGD();
  Env env(true, 0);  // clangd's answer is always later than the fast tier's
  env.caretAt(env.lineOf("Widget w1(3)"), "Widget");
  const size_t base = env.ed->navigation().entries().size();
  CHECK(env.key(Key::F12));
  // The fast answer (the class alone) is not acted on: no jump, no popup of one row; then ONE chooser.
  REQUIRE(env.until([&] { return env.popup() != nullptr; }));
  CHECK_EQ(env.popup()->rows().size(), size_t(2));
  CHECK_EQ(env.curFile(), std::string("main.cpp"));
  CHECK_EQ(env.ed->navigation().entries().size(), base);
  ListPopup* p = env.popup();
  for (int i = 0; i < 20; ++i) {
    env.frame();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  CHECK(env.popup() == p);
  CHECK_EQ(p->selected(), 1);
}
