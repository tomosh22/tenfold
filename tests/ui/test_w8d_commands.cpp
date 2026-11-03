// W8d: the 10x multi-cursor and navigation commands through the UI. Every command is run by pressing its 10x default
// key (the real embedded key bindings), against a recording renderer and the mock font (8 px cells, 14 px lines).
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>

#include "../text/text_test_util.h"
#include "ui/commands/CommandNames.h"
#include "ui/gfx/TextRenderer.h"
#include "ui/settings/KeyBindings.h"
#include "ui/settings/Settings.h"
#include "ui/shell/EditorApp.h"
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

InputEvent clickEv(float x, float y, MouseButton b, uint8_t mods = 0, InputType t = InputType::MouseDown) {
  InputEvent e;
  e.type = t;
  e.button = b;
  e.mods = mods;
  e.x = x;
  e.y = y;
  e.clicks = 1;
  return e;
}

constexpr uint8_t kCS = ModCtrl | ModShift;
constexpr uint8_t kCSA = ModCtrl | ModShift | ModAlt;
constexpr uint8_t kSA = ModShift | ModAlt;

struct Env {
  fs::path dir;
  std::unique_ptr<Editor> ed;
  std::unique_ptr<gfx::TextRenderer> text;
  NullRenderer nr;
  std::unique_ptr<Workbench> bench;
  std::unique_ptr<EditorApp> app;
  int64_t now = 1000;

  explicit Env(const std::string& aText = "foo bar foo\nbaz foo\nlonger line\nx\n") {
    static int n = 0;
    dir =
        uitest::tempRoot() / ("tf_w8d_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                              "_" + std::to_string(n++));
    fs::create_directories(dir);
    std::ofstream(dir / "a.cpp", std::ios::binary) << aText;
    std::string b;
    for (int i = 0; i < 60; ++i) b += "b" + std::to_string(i) + "\n";
    std::ofstream(dir / "b.cpp", std::ios::binary) << b;
    gfx::FontStack stack;
    stack.add(std::make_shared<tftext::MockFont>(
        U"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ._-:,;/()[]{}<>+*&#?=!\"'\\|"));
    text = gfx::TextRenderer::fromFonts(std::move(stack));
    Editor::Options o;
    o.semantic.useClangd = false;
    ed = std::make_unique<Editor>(dir, o);
    bench = std::make_unique<Workbench>(*text, *ed);
    app = std::make_unique<EditorApp>(*bench, *ed, bench.get());
    bench->openFile("a.cpp");
    bench->sync();
    frame();
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
    bench->render(nr, 1000, 600);
  }
  bool key(Key k, uint8_t mods = 0) {
    now += 10;
    const bool r = app->handleEvent(keyEv(k, mods), now);
    frame();
    return r;
  }
  void type(const std::string& t) {
    InputEvent e;
    e.type = InputType::Text;
    e.text = t;
    send(e);
  }
  bool send(const InputEvent& e) {
    now += 10;
    const bool r = app->handleEvent(e, now);
    frame();
    return r;
  }
  // A jump to a file line, the way the UI does it (through gotoLocation), then the tabs follow.
  void jump(const char* file, int line, int col = 0) {
    ed->gotoLocation({dir / file, {line, col}});
    bench->sync();
    frame();
  }
  TextView* view() { return dynamic_cast<TextView*>(bench->focused()); }
  Document* doc() { return ed->currentDocument(); }
  std::string name() { return doc()->path().filename().string(); }
  std::string txt() { return doc()->buffer().text(); }
  Selection sel(size_t i = 0) { return ed->selections()[i]; }
  void set(std::vector<Selection> s) { ed->setSelections(std::move(s)); }
  std::pair<float, float> centreOf(Position p) {
    const RectF r = view()->caretRect(p);
    return {r.x + 2, r.y + r.h / 2};
  }
  size_t tabCount() {
    size_t n = 0;
    for (Panel* p : bench->panels()) n += p->tabs().tabs().size();
    return n;
  }
};

Selection C(int l, int c) { return {{l, c}, {l, c}}; }
Selection S(int l1, int c1, int l2, int c2) { return {{l1, c1}, {l2, c2}}; }

}  // namespace

TEST(w8d_names_resolve_and_defaults_bind_to_them) {
  const char* names[] = {
      "UndoSelectNextMultiCursor",    "SelectAllOccurrences", "AlignMultiCursor",        "MultiCursorMoveLeft",
      "MultiCursorMoveRight",         "AddCursorAtMousePos",  "PrevLocationCurrentFile", "NextLocationCurrentFile",
      "RestoreRecentlyClosedFile",    "PrevBookmarkInFile",   "NextBookmarkInFile",      "RemoveAllBookmarksFromFile",
      "RemoveAllBreakpointsFromFile", "GotoNextResult",       "GotoPrevResult",          "GotoNextBuildWarningOrError",
      "GotoPrevBuildWarningOrError"};
  Env env;
  for (const char* n : names) {
    const ResolvedCommand r = resolveCommandName(n);
    CHECK(r.known10x);
    CHECK(r.implemented);
    const Command* c = env.app->commands().find(r.id);
    if (!c) std::cerr << "    no command for " << n << " -> " << r.id << "\n";
    CHECK(c != nullptr);
    CHECK(c && static_cast<bool>(c->handler));  // bound, not "unbound"
  }
  KeyMap map;
  std::vector<KeyBindingDiag> diags;
  parseKeyBindings(defaultKeyBindingsText(), "defaults", map, diags);
  CHECK_EQ(diags.size(), size_t(0));
}

TEST(w8d_undo_select_next_keys) {
  Env env;
  env.set({C(0, 1)});
  env.key(Key('D'), ModCtrl);  // SelectNextMultiCursor: the word
  env.key(Key('D'), ModCtrl);
  env.key(Key('D'), ModCtrl);
  CHECK_EQ(env.ed->selections().size(), size_t(3));
  env.key(Key('D'), kCS);  // UndoSelectNextMultiCursor
  CHECK_EQ(env.ed->selections().size(), size_t(2));
  env.key(Key('D'), kCS);
  env.key(Key('D'), kCS);
  CHECK(env.ed->selections().size() == 1 && env.sel() == C(0, 1));
  env.key(Key('D'), kCS);  // nothing left: silent
  CHECK(env.app->message().empty());
}

TEST(w8d_select_all_occurrences_keys_use_the_word_or_the_find_bar) {
  Env env;
  env.set({C(0, 1)});
  env.key(Key('J'), kCSA);
  CHECK_EQ(env.ed->selections().size(), size_t(3));  // foo x3
  CHECK(env.app->message().find("3 occurrences") != std::string::npos);
  env.set({C(0, 5)});  // on "bar"
  env.key(Key::F3, ModAlt);
  CHECK_EQ(env.ed->selections().size(), size_t(1));
  // The find bar's text and options win while the bar is open.
  env.app->findReplace().open(false);
  env.app->findReplace().setOptions(false, false, false);
  env.app->findReplace().setQuery("ba");
  env.frame();
  env.key(Key::F3, ModAlt);
  CHECK_EQ(env.ed->selections().size(), size_t(2));  // bar, baz
  CHECK_EQ(env.doc()->buffer().textInRange(env.sel().range()), std::string("ba"));
}

TEST(w8d_align_and_selection_end_keys) {
  Env env("a = 1\nlonger = 2\nxy = 3\n");
  env.set({C(0, 1), C(1, 6), C(2, 2)});
  env.key(Key('A'), ModAlt);  // AlignMultiCursor
  CHECK_EQ(env.txt(), std::string("a      = 1\nlonger = 2\nxy     = 3\n"));
  env.key(Key('Z'), ModCtrl);
  CHECK_EQ(env.txt(), std::string("a = 1\nlonger = 2\nxy = 3\n"));
  env.set({S(0, 0, 0, 1), S(1, 0, 1, 1)});
  env.key(Key::Right, kSA);  // MultiCursorMoveRight: "Decrease selection end"
  CHECK(env.sel(0) == C(0, 0));
  CHECK(env.sel(1) == C(1, 0));
  env.key(Key::Left, kSA);  // MultiCursorMoveLeft: "Increase selection end"
  env.key(Key::Left, kSA);
  CHECK(env.sel(0) == S(0, 0, 0, 2));
  CHECK(env.sel(1) == S(1, 0, 1, 2));
  CHECK_EQ(env.txt(), std::string("a = 1\nlonger = 2\nxy = 3\n"));
}

TEST(w8d_ctrl_middle_click_adds_a_caret_and_plain_middle_click_does_not) {
  Env env;
  env.set({C(0, 1)});
  const auto at = env.centreOf({1, 2});
  env.send(clickEv(at.first, at.second, MouseButton::Middle));  // plain: no caret, not "not implemented" either
  CHECK_EQ(env.ed->selections().size(), size_t(1));
  CHECK(env.app->message().find("not implemented") == std::string::npos);
  CHECK(env.send(clickEv(at.first, at.second, MouseButton::Middle, ModCtrl)));
  REQUIRE_EQ(env.ed->selections().size(), size_t(2));
  CHECK(env.sel(0) == C(1, 2));  // the new caret is the primary
  CHECK(env.sel(1) == C(0, 1));
  env.send(clickEv(at.first, at.second, MouseButton::Middle, ModCtrl, InputType::MouseUp));
  env.type("X");
  CHECK_EQ(env.txt().substr(0, 12), std::string("fXoo bar foo"));
  CHECK_EQ(env.doc()->buffer().line(1), std::string("baXz foo"));
  // a second one further down keeps both
  const auto at2 = env.centreOf({2, 4});
  env.send(clickEv(at2.first, at2.second, MouseButton::Middle, ModCtrl));
  CHECK_EQ(env.ed->selections().size(), size_t(3));
}

TEST(w8d_middle_click_still_closes_tabs_with_or_without_control) {
  Env env;
  env.bench->openFile("b.cpp");
  env.bench->sync();
  env.frame();
  REQUIRE_EQ(env.tabCount(), size_t(2));
  Panel* p = env.bench->activePanel();
  const int first = p->tabs().tabs()[0].id;
  RectF r = p->tabs().tabRect(first);
  env.send(clickEv(r.x + r.w / 2 - 10, r.y + r.h / 2, MouseButton::Middle));
  env.send(clickEv(r.x + r.w / 2 - 10, r.y + r.h / 2, MouseButton::Middle, 0, InputType::MouseUp));
  CHECK_EQ(env.tabCount(), size_t(1));
  CHECK_EQ(env.name(), std::string("b.cpp"));
  // Control + middle over the tab strip is not an editor-view command either: the click reaches the tab strip.
  env.bench->openFile("a.cpp");
  env.bench->sync();
  env.frame();
  REQUIRE_EQ(env.tabCount(), size_t(2));
  p = env.bench->activePanel();
  const int second = p->tabs().tabs()[1].id;
  r = p->tabs().tabRect(second);
  env.send(clickEv(r.x + r.w / 2 - 10, r.y + r.h / 2, MouseButton::Middle, ModCtrl));
  env.send(clickEv(r.x + r.w / 2 - 10, r.y + r.h / 2, MouseButton::Middle, ModCtrl, InputType::MouseUp));
  CHECK_EQ(env.tabCount(), size_t(1));
}

TEST(w8d_location_in_current_file_keys) {
  Env env;
  env.jump("a.cpp", 0);
  env.jump("b.cpp", 20);
  env.jump("b.cpp", 40);
  env.jump("a.cpp", 3);
  env.jump("b.cpp", 55);
  CHECK_EQ(env.name(), std::string("b.cpp"));
  env.key(Key('='), ModCtrl);  // PrevLocationCurrentFile
  CHECK_EQ(env.name(), std::string("b.cpp"));
  CHECK_EQ(env.ed->cursor().pos.line, 40);
  env.key(Key('='), ModCtrl);
  CHECK_EQ(env.ed->cursor().pos.line, 20);
  env.key(Key('='), ModCtrl);  // no older entry of b.cpp
  CHECK_EQ(env.ed->cursor().pos.line, 20);
  env.key(Key('='), kCS);  // NextLocationCurrentFile
  CHECK_EQ(env.ed->cursor().pos.line, 40);
  env.key(Key('='), kCS);
  CHECK_EQ(env.ed->cursor().pos.line, 55);
  CHECK_EQ(env.name(), std::string("b.cpp"));
  env.key(Key('-'), ModCtrl);  // plain PrevLocation walks across files again
  CHECK_EQ(env.name(), std::string("a.cpp"));
}

TEST(w8d_restore_recently_closed_file_with_its_caret) {
  Env env;
  env.jump("b.cpp", 33, 1);
  env.set({C(33, 1)});
  env.bench->sync();
  Panel* p = env.bench->activePanel();
  env.bench->requestCloseTab(p->activeId());  // b.cpp closes; a.cpp is the document again
  env.frame();
  CHECK_EQ(env.name(), std::string("a.cpp"));
  CHECK(env.ed->document(env.dir / "b.cpp") == nullptr);
  env.set({C(2, 4)});
  env.bench->sync();
  env.bench->requestCloseTab(env.bench->activePanel()->activeId());  // a.cpp closes too
  env.frame();
  CHECK_EQ(env.tabCount(), size_t(0));
  env.key(Key('T'), kCS);  // RestoreRecentlyClosedFile: the most recent first
  CHECK_EQ(env.name(), std::string("a.cpp"));
  CHECK(env.sel() == C(2, 4));
  CHECK_EQ(env.tabCount(), size_t(1));
  env.key(Key('T'), kCS);  // and then the one before it
  CHECK_EQ(env.name(), std::string("b.cpp"));
  CHECK(env.sel() == C(33, 1));
  CHECK_EQ(env.tabCount(), size_t(2));
  env.key(Key('T'), kCS);  // nothing left
  CHECK(env.app->message().find("No recently closed") != std::string::npos);
  CHECK_EQ(env.tabCount(), size_t(2));
  // The restore is a jump: Back returns to where it came from.
  CHECK(env.ed->navigation().canGoBack());
}

TEST(w8d_restore_skips_files_that_are_open_again_or_gone) {
  Env env;
  env.bench->openFile("b.cpp");
  env.bench->sync();
  env.bench->requestCloseTab(env.bench->activePanel()->activeId());  // b.cpp closed
  env.frame();
  env.bench->openFile("b.cpp");  // opened again by hand
  env.bench->sync();
  env.frame();
  const size_t tabs = env.tabCount();
  env.key(Key('T'), kCS);  // the only closed file is open: nothing to restore
  CHECK_EQ(env.tabCount(), tabs);
  CHECK(env.app->message().find("No recently closed") != std::string::npos);
  env.bench->requestCloseTab(env.bench->activePanel()->activeId());
  env.frame();
  std::error_code ec;
  fs::remove(env.dir / "b.cpp", ec);  // gone from disk
  env.key(Key('T'), kCS);
  CHECK_EQ(env.tabCount(), size_t(1));
}

TEST(w8d_bookmark_in_file_and_remove_keys) {
  Env env;
  env.jump("a.cpp", 0);
  env.set({C(1, 0)});
  env.key(Key::F2, ModCtrl);  // ToggleBookmark
  env.set({C(3, 0)});
  env.key(Key::F2, ModCtrl);
  env.jump("b.cpp", 5);
  env.key(Key::F2, ModCtrl);
  env.jump("a.cpp", 0);
  env.key(Key::F2, ModAlt);  // NextBookmarkInFile
  CHECK_EQ(env.ed->cursor().pos.line, 1);
  env.key(Key::F2, ModAlt);
  CHECK_EQ(env.ed->cursor().pos.line, 3);
  env.key(Key::F2, ModAlt);  // wraps inside a.cpp
  CHECK_EQ(env.ed->cursor().pos.line, 1);
  CHECK_EQ(env.name(), std::string("a.cpp"));
  env.key(Key::F2, kSA);  // PrevBookmarkInFile
  CHECK_EQ(env.ed->cursor().pos.line, 3);
  env.key(Key::F2, kCS);  // RemoveAllBookmarksFromFile
  CHECK(env.ed->bookmarks().lines(env.dir / "a.cpp").empty());
  CHECK_EQ(env.ed->bookmarks().lines(env.dir / "b.cpp").size(), size_t(1));
  env.key(Key::F2, ModAlt);  // nothing in this file now: the cursor stays
  CHECK_EQ(env.ed->cursor().pos.line, 3);
}

TEST(w8d_remove_all_breakpoints_from_file_key) {
  Env env;
  env.jump("a.cpp", 0);
  env.set({C(1, 0)});
  env.key(Key::F9);  // ToggleBreakpoint
  env.set({C(2, 0)});
  env.key(Key::F9);
  env.jump("b.cpp", 4);
  env.key(Key::F9);
  env.jump("a.cpp", 0);
  CHECK_EQ(env.ed->breakpoints().count(), size_t(3));
  env.key(Key::F9, kCS);  // RemoveAllBreakpointsFromFile
  CHECK(env.ed->breakpoints().lines(env.dir / "a.cpp").empty());
  CHECK_EQ(env.ed->breakpoints().lines(env.dir / "b.cpp").size(), size_t(1));
  CHECK(env.app->message().find("not implemented") == std::string::npos);
}
