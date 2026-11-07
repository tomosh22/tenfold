// W11-GUI-LSP: call / type hierarchy panel, Quick Actions (Ctrl+.) with the light bulb, and inlay hints, driven through
// EditorApp with the real default key bindings. The engine is faked through the features' backends where the test is
// about the UI (a recording renderer and the mock font, 8 px cells); a real clangd (skipped without it) answers the
// end-to-end tests; two goldens (real font, vs-dark theme) show the hierarchy panel and the inlay hints.
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#include "../text/text_test_util.h"
#include "core/Platform.h"
#include "ui/features/ContextMenus.h"
#include "ui/gfx/PngWrite.h"
#include "ui/gfx/TextRenderer.h"
#include "ui/settings/Theme.h"
#include "ui/shell/EditorApp.h"
#include "ui/view/InlayMap.h"
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
      U"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ._-:,;/()[]{}<>+*&#?=!\"'"));
  return gfx::TextRenderer::fromFonts(std::move(stack));
}

InputEvent keyEv(Key k, uint8_t mods = 0, InputType t = InputType::KeyDown) {
  InputEvent e;
  e.type = t;
  e.key = k;
  e.mods = mods;
  return e;
}

void writeFile(const fs::path& p, const std::string& text) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary) << text;
}

bool clangdAvailable() { return !findExecutable("clangd").empty(); }
#define SKIP_WITHOUT_CLANGD()                               \
  if (!clangdAvailable()) {                                 \
    std::cerr << "    SKIPPED: clangd not found on PATH\n"; \
    return;                                                 \
  }

struct Env {
  fs::path dir;
  std::unique_ptr<gfx::TextRenderer> text = mockText();
  Recorder rec;
  std::unique_ptr<Editor> ed;
  std::unique_ptr<Workbench> bench;
  std::unique_ptr<EditorApp> app;
  int64_t now = 1000;

  explicit Env(const std::function<void(const fs::path&)>& populate, bool clangd = false) {
    static int n = 0;
    dir =
        uitest::tempRoot() / ("tf_w11_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                              "_" + std::to_string(n++));
    fs::create_directories(dir);
    populate(dir);
    Editor::Options o;
    o.semantic.useClangd = clangd;
    o.semantic.persistFastIndex = false;
    o.watchForcePolling = true;
    o.watchPollMs = 60000;
    o.vcs.enabled = false;
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
  void keyUp(Key k, uint8_t mods = 0) { send(keyEv(k, mods, InputType::KeyUp)); }
  void wait(int ms) {  // advances the app clock; one tick first so that the app sees what changed, one after
    frame();
    now += ms;
    frame();
  }
  void open(const char* rel) {
    bench->openFile(dir / rel);
    bench->sync();
    frame();
  }
  void caret(int line, int col) {
    ed->setSelections({caretAt({line, col})});
    frame();
  }
  bool until(const std::function<bool()>& pred, int ms = 20000) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    for (;;) {
      now += 10;
      frame();
      if (pred()) return true;
      if (std::chrono::steady_clock::now() >= end) return false;
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
  }
  bool clangdReady() {
    return until([&] { return ed->semantic().status().clangdInitialized; }, 60000);
  }
  std::string msg() { return app->message(); }
  TextView* view() {
    TextView* found = nullptr;
    std::function<void(Widget&)> walk = [&](Widget& w) {
      if (auto* v = dynamic_cast<TextView*>(&w))
        if (v->isCurrent() && v->visible() && !found) found = v;
      for (const auto& c : w.children()) walk(*c);
    };
    walk(*bench);
    return found;
  }
  ListPopup* popup() { return dynamic_cast<ListPopup*>(bench->modal()); }
  HierarchyPanel& hp() { return *app->hierarchyPanel(); }
};

const auto kEmpty = [](const fs::path&) {};

HierarchyItem item(const fs::path& file, const std::string& name, int line, int col = 0) {
  HierarchyItem it;
  it.name = name;
  it.kind = "function";
  it.loc = {file, {line, col}};
  it.range = it.selection = {{line, col}, {line, col + static_cast<int>(name.size())}};
  return it;
}

// The first node whose label starts with `prefix`, searching from node `from`.
int nodeByLabel(HierarchyPanel& p, const std::string& prefix, int from = 0) {
  const TreeModel& m = p.tree().model();
  for (size_t i = static_cast<size_t>(from); i < m.size(); ++i)
    if (m.node(static_cast<int>(i)).name.rfind(prefix, 0) == 0) return static_cast<int>(i);
  return -1;
}

// The child of node `parent` whose label starts with `prefix`.
int childByLabel(HierarchyPanel& p, int parent, const std::string& prefix) {
  const TreeModel& m = p.tree().model();
  for (int k : m.node(parent).kids)
    if (m.node(k).name.rfind(prefix, 0) == 0) return k;
  return -1;
}

// A fake call hierarchy: callers and callees answers are held until the test resolves them.
struct FakeCalls {
  fs::path file;
  int incomingAsked = 0, outgoingAsked = 0, prepareAsked = 0;
  std::vector<std::string> askedFor;
  std::vector<HierarchyPanel::Done<CallEdge>> incomingDone, outgoingDone;
  std::vector<HierarchyPanel::Done<HierarchyItem>> prepareDone;
  std::vector<std::string> jumped;
  HierarchyPanel::Backend backend() {
    HierarchyPanel::Backend b;
    b.prepareCall = [this](HierarchyPanel::Done<HierarchyItem> d) {
      ++prepareAsked;
      prepareDone.push_back(std::move(d));
    };
    b.prepareType = b.prepareCall;
    b.incoming = [this](const HierarchyItem& i, HierarchyPanel::Done<CallEdge> d) {
      ++incomingAsked;
      askedFor.push_back("in:" + i.name);
      incomingDone.push_back(std::move(d));
    };
    b.outgoing = [this](const HierarchyItem& i, HierarchyPanel::Done<CallEdge> d) {
      ++outgoingAsked;
      askedFor.push_back("out:" + i.name);
      outgoingDone.push_back(std::move(d));
    };
    b.supertypes = [this](const HierarchyItem& i, HierarchyPanel::Done<HierarchyItem> d) {
      askedFor.push_back("super:" + i.name);
      d({item(file, "Base", 0)}, "");
    };
    b.subtypes = [this](const HierarchyItem& i, HierarchyPanel::Done<HierarchyItem> d) {
      askedFor.push_back("sub:" + i.name);
      d({item(file, "Derived", 4), item(file, "Other", 8)}, "");
    };
    b.goTo = [this](const HierarchyItem& i, std::string*) {
      jumped.push_back(i.name);
      return true;
    };
    return b;
  }
};

CallEdge edge(const HierarchyItem& it, size_t calls = 1) {
  CallEdge e;
  e.item = it;
  e.ranges.assign(calls, it.selection);
  return e;
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// Key bindings, command names, settings, menus
// ---------------------------------------------------------------------------------------------------------------

TEST(w11_keys_are_visual_studios_and_free_in_the_10x_map) {
  Env env(kEmpty);
  auto bound = [&](const char* text) {
    std::vector<std::string> cmds;
    for (const KeyBinding& b : env.app->keys().bindings())
      if (formatChords(b.chords) == text) cmds.push_back(b.command);
    return cmds;
  };
  const auto qa = bound("Control .");
  REQUIRE_EQ(qa.size(), 1u);
  CHECK_EQ(qa[0], std::string("Editor.QuickActions"));
  const auto ch = bound("Control K, Control T");
  REQUIRE_EQ(ch.size(), 1u);
  CHECK_EQ(ch[0], std::string("View.CallHierarchy"));
  const auto ih = bound("Alt F1");
  REQUIRE_EQ(ih.size(), 1u);
  CHECK_EQ(ih[0], std::string("Editor.InlayHintsHold"));
  // Alt+Enter stays 10x's ToggleFullscreen; the type hierarchy has no default key.
  const auto fs1 = bound("Alt Enter");
  REQUIRE_EQ(fs1.size(), 1u);
  CHECK_EQ(fs1[0], std::string("View.ToggleFullscreen"));
  for (const KeyBinding& b : env.app->keys().bindings()) CHECK(b.command != "View.TypeHierarchy");
  for (const char* c : {"View.CallHierarchy", "View.TypeHierarchy", "Editor.QuickActions", "Editor.InlayHintsHold"})
    CHECK(env.app->commands().find(c) != nullptr);
}

TEST(w11_inlay_hints_setting_has_the_visual_studio_default) {
  Settings s;
  CHECK_EQ(s.getString("editor.inlayHints"), std::string("while-pressed"));
  CHECK(InlayHints::parseMode("off") == InlayHints::Mode::Off);
  CHECK(InlayHints::parseMode("on") == InlayHints::Mode::On);
  CHECK(InlayHints::parseMode("while-pressed") == InlayHints::Mode::WhilePressed);
  CHECK(InlayHints::parseMode("nonsense") == InlayHints::Mode::WhilePressed);
}

TEST(w11_editor_menu_offers_quick_actions_and_the_hierarchies) {
  Env env(kEmpty);
  const auto items = env.app->buildCommandMenu(editorMenuEntries());
  auto has = [&](const char* l) {
    return std::any_of(items.begin(), items.end(), [&](const MenuItem& m) { return m.label == l; });
  };
  CHECK(has("Quick Actions and Refactorings..."));
  CHECK(has("View Call Hierarchy"));
  CHECK(has("View Type Hierarchy"));
}

// ---------------------------------------------------------------------------------------------------------------
// InlayMap
// ---------------------------------------------------------------------------------------------------------------

TEST(w11_inlay_map_shift_and_unshift) {
  gfx::FontStack stack;
  stack.add(std::make_shared<tftext::MockFont>(U"abcdefghijklmnopqrstuvwxyz0123456789 ()_,:"));
  auto tr = gfx::TextRenderer::fromFonts(std::move(stack));
  InlayMap m;
  LspInlayHint p1, p2, t;
  p1.pos = {0, 4};
  p1.label = "a:";
  p1.kind = 2;
  p2.pos = {0, 7};
  p2.label = "bb:";
  p2.kind = 2;
  t.pos = {1, 5};
  t.label = ": int";
  t.kind = 1;
  m.set({p2, t, p1}, tr->textLayout());
  REQUIRE_EQ(m.items().size(), 3u);
  CHECK_EQ(m.items()[0].col, 4);  // sorted
  CHECK_EQ(m.items()[0].width(), 3);
  const int len = 10;
  CHECK_EQ(m.shiftCells(0, 0, len, len, 3), 0);
  CHECK_EQ(m.shiftCells(0, 0, len, len, 4), 3);        // a caret at the parameter's column is after its hint ...
  CHECK_EQ(m.shiftCells(0, 0, len, len, 4, true), 0);  // ... but a range ending there does not include it
  CHECK_EQ(m.shiftCells(0, 0, len, len, 8), 3 + 4);
  CHECK_EQ(m.shiftCells(1, 0, len, len, 5), 0);  // a type hint follows the text: the caret at its column is before it
  CHECK_EQ(m.shiftCells(1, 0, len, len, 6), 6);
  // x back to the natural x: cw = 8, natural(col) = 8 * col.
  auto nat = [](int col) { return 8.0f * static_cast<float>(col); };
  CHECK_EQ(m.unshift(0, 0, len, len, 8.0f * 3, 8.0f, nat), 24.0f);      // before the first hint
  CHECK_EQ(m.unshift(0, 0, len, len, 8.0f * 4 + 4, 8.0f, nat), 32.0f);  // inside the hint box: its column
  CHECK_EQ(m.unshift(0, 0, len, len, 8.0f * 7, 8.0f, nat), 8.0f * 4);   // after "a: " the text resumes at column 4
  CHECK_EQ(m.unshift(0, 0, len, len, 8.0f * 14, 8.0f, nat), 8.0f * 7);  // past both hints (3 + 4 cells)
  // Editing a line drops its hints and moves the ones below.
  Document::Change c;
  c.removed = {{0, 4}, {0, 4}};
  c.newText = "x\ny";
  c.lineDelta = 1;
  m.onChange(c);
  REQUIRE_EQ(m.items().size(), 1u);
  CHECK_EQ(m.items()[0].line, 2);
}

// ---------------------------------------------------------------------------------------------------------------
// Hierarchy panel
// ---------------------------------------------------------------------------------------------------------------

TEST(w11_call_hierarchy_opens_the_panel_and_expands_lazily) {
  Env env([](const fs::path& d) { writeFile(d / "a.cpp", "void foo();\nvoid bar() { foo(); }\n"); });
  env.open("a.cpp");
  FakeCalls fake;
  fake.file = env.dir / "a.cpp";
  env.hp().setBackend(fake.backend());
  env.app->quickActions().setAllowedFn([] { return true; });
  // Without clangd the command says so instead of asking.
  env.caret(0, 6);
  CHECK(env.key(Key('K'), ModCtrl));
  CHECK(env.key(Key('T'), ModCtrl));
  CHECK(env.bench->bottomVisible());
  CHECK_EQ(env.bench->bottomPanelName(), std::string("Hierarchy"));
  CHECK_EQ(fake.prepareAsked, 0);
  REQUIRE_EQ(env.hp().rowLabels().size(), 1u);
  CHECK(env.hp().rowLabels()[0].find("Needs clangd") != std::string::npos);
  // With the backend available (clangd pretended running): the panel asks, shows Loading..., then the tree.
  env.hp().setUnavailableFn(nullptr);
  env.hp().open(HierarchyPanel::Mode::Call);
  CHECK_EQ(fake.prepareAsked, 1);
  CHECK(env.hp().busy());
  CHECK_EQ(env.hp().status(), std::string("Loading..."));
  fake.prepareDone[0]({item(fake.file, "foo", 0, 5)}, "");
  env.frame();
  CHECK(env.hp().busy());  // the callers are on their way
  CHECK_EQ(env.hp().title(), std::string("Call Hierarchy: foo"));
  auto rows = env.hp().rowLabels();
  REQUIRE_EQ(rows.size(), 4u);
  CHECK_EQ(rows[0], std::string("foo   a.cpp:1"));
  CHECK_EQ(rows[1], std::string("Calls To 'foo'"));
  CHECK_EQ(rows[2], std::string("Loading..."));  // the callers are on their way
  CHECK_EQ(rows[3], std::string("Calls From 'foo'"));
  CHECK_EQ(fake.incomingAsked, 1);
  CHECK_EQ(fake.outgoingAsked, 0);  // only expanded nodes are asked
  fake.incomingDone[0]({edge(item(fake.file, "bar", 1), 2)}, "");
  env.frame();
  rows = env.hp().rowLabels();
  REQUIRE_EQ(rows.size(), 4u);
  CHECK_EQ(rows[2], std::string("bar   a.cpp:2  (2 calls)"));
  // Expanding "Calls From" asks once, however often it is toggled.
  const int from = childByLabel(env.hp(), 0, "Calls From");
  env.hp().tree().setExpanded(from, true);
  CHECK_EQ(fake.outgoingAsked, 1);
  env.hp().tree().setExpanded(from, false);
  env.hp().tree().setExpanded(from, true);
  CHECK_EQ(fake.outgoingAsked, 1);
  fake.outgoingDone[0]({}, "");
  env.frame();
  CHECK_EQ(env.hp().rowLabels().back(), std::string("No calls"));
  // A caller expands to its own Calls To / Calls From nodes (Visual Studio), which are asked when opened.
  const int bar = nodeByLabel(env.hp(), "bar");
  env.hp().tree().setExpanded(bar, true);
  CHECK_EQ(fake.incomingAsked, 1);
  const int barTo = childByLabel(env.hp(), bar, "Calls To 'bar'");
  REQUIRE(barTo > 0);
  CHECK(childByLabel(env.hp(), bar, "Calls From 'bar'") > 0);
  env.hp().tree().setExpanded(barTo, true);
  CHECK_EQ(fake.incomingAsked, 2);
  CHECK_EQ(fake.askedFor.back(), std::string("in:bar"));
  fake.incomingDone[1]({}, "");
  env.frame();
  CHECK(!env.hp().busy());
}

TEST(w11_hierarchy_enter_and_double_click_jump_and_arrows_only_expand) {
  Env env([](const fs::path& d) { writeFile(d / "a.cpp", "void foo();\nvoid bar() { foo(); }\n"); });
  env.open("a.cpp");
  FakeCalls fake;
  fake.file = env.dir / "a.cpp";
  env.hp().setBackend(fake.backend());
  env.hp().setUnavailableFn(nullptr);
  env.bench->showBottomPanel("Hierarchy");
  env.hp().open(HierarchyPanel::Mode::Call);
  fake.prepareDone[0]({item(fake.file, "foo", 0, 5)}, "");
  env.frame();
  fake.incomingDone[0]({edge(item(fake.file, "bar", 1))}, "");
  env.frame();
  env.hp().focusTree();
  const int bar = nodeByLabel(env.hp(), "bar");
  env.hp().tree().select(bar);
  CHECK(env.key(Key::Enter));
  REQUIRE_EQ(fake.jumped.size(), 1u);
  CHECK_EQ(fake.jumped[0], std::string("bar"));
  CHECK(!env.hp().tree().isExpanded(bar));  // Enter on an entry jumps, it does not expand
  env.hp().focusTree();                     // a jump gives the editor the keys back
  CHECK(env.key(Key::Right));
  CHECK(env.hp().tree().isExpanded(bar));
  CHECK_EQ(fake.jumped.size(), 1u);
  // The root jumps too; a group heading does not.
  env.hp().focusTree();
  env.hp().tree().select(0);
  env.key(Key::Enter);
  CHECK_EQ(fake.jumped.back(), std::string("foo"));
  const size_t n = fake.jumped.size();
  env.hp().focusTree();
  env.hp().tree().select(childByLabel(env.hp(), 0, "Calls To"));
  env.key(Key::Enter);  // toggles the group
  CHECK_EQ(fake.jumped.size(), n);
  // Escape closes the panel and gives the editor the keys back.
  env.hp().focusTree();
  env.key(Key::Escape);
  CHECK(!env.bench->bottomVisible());
}

TEST(w11_hierarchy_jump_uses_navigation_history_so_back_returns) {
  Env env([](const fs::path& d) {
    writeFile(d / "a.cpp", "void foo();\nvoid bar() { foo(); }\n");
    writeFile(d / "b.cpp", "// b\nvoid baz() {}\n");
  });
  env.open("a.cpp");
  env.caret(1, 5);
  FakeCalls fake;
  fake.file = env.dir / "b.cpp";
  HierarchyPanel::Backend b = fake.backend();
  b.goTo = HierarchyPanel::editorBackend(*env.ed).goTo;  // the real Editor::gotoHierarchyItem
  env.hp().setBackend(b);
  env.hp().setUnavailableFn(nullptr);
  env.bench->showBottomPanel("Hierarchy");
  env.hp().open(HierarchyPanel::Mode::Call);
  fake.prepareDone[0]({item(env.dir / "b.cpp", "baz", 1, 5)}, "");
  env.frame();
  env.hp().focusTree();
  env.hp().tree().select(0);
  CHECK(env.key(Key::Enter));
  env.frame();
  REQUIRE(env.ed->currentDocument() != nullptr);
  CHECK_EQ(env.ed->currentDocument()->path().filename().string(), std::string("b.cpp"));
  CHECK_EQ(env.ed->selections()[0].head.line, 1);
  CHECK_EQ(env.ed->selections()[0].head.column, 5);
  // Back (Control -) returns to where the hierarchy was opened from.
  env.bench->setActivePanel(env.bench->activePanel());
  CHECK(env.key(Key('-'), ModCtrl));
  CHECK_EQ(env.ed->currentDocument()->path().filename().string(), std::string("a.cpp"));
  CHECK_EQ(env.ed->selections()[0].head.line, 1);
  CHECK_EQ(env.ed->selections()[0].head.column, 5);
}

TEST(w11_a_newer_hierarchy_drops_the_answers_of_the_older_one) {
  Env env(kEmpty);
  FakeCalls fake;
  fake.file = env.dir / "x.cpp";
  env.hp().setBackend(fake.backend());
  env.hp().setUnavailableFn(nullptr);
  env.hp().open(HierarchyPanel::Mode::Call);
  env.hp().open(HierarchyPanel::Mode::Type);
  REQUIRE_EQ(fake.prepareDone.size(), 2u);
  fake.prepareDone[0]({item(fake.file, "old", 0)}, "");  // the stale one arrives first and is ignored
  CHECK(env.hp().busy());
  CHECK_EQ(env.hp().rowLabels()[0], std::string("Loading..."));
  fake.prepareDone[1]({item(fake.file, "Shape", 0)}, "");
  CHECK(!env.hp().busy());
  CHECK_EQ(env.hp().title(), std::string("Type Hierarchy: Shape"));
  // Late answers for nodes of the replaced tree do not touch the new one.
  env.hp().open(HierarchyPanel::Mode::Call);
  fake.prepareDone[2]({item(fake.file, "f", 0)}, "");
  CHECK_EQ(fake.incomingDone.size(), 1u);
  env.hp().open(HierarchyPanel::Mode::Call);
  fake.incomingDone[0]({edge(item(fake.file, "ghost", 1))}, "");
  for (const std::string& l : env.hp().rowLabels()) CHECK(l.find("ghost") == std::string::npos);
}

TEST(w11_type_hierarchy_shows_base_and_derived_types) {
  Env env(kEmpty);
  FakeCalls fake;
  fake.file = env.dir / "t.cpp";
  env.hp().setBackend(fake.backend());
  env.hp().setUnavailableFn(nullptr);
  env.hp().open(HierarchyPanel::Mode::Type);
  fake.prepareDone[0]({item(fake.file, "Mid", 2)}, "");
  env.frame();
  auto rows = env.hp().rowLabels();
  REQUIRE(rows.size() >= 4u);
  CHECK_EQ(rows[1], std::string("Base Types"));
  CHECK_EQ(rows[2], std::string("Base   t.cpp:1"));  // the first group opened by itself
  CHECK_EQ(rows[3], std::string("Derived Types"));
  env.hp().tree().setExpanded(childByLabel(env.hp(), 0, "Derived Types"), true);
  rows = env.hp().rowLabels();
  CHECK_EQ(rows.size(), 6u);
  CHECK_EQ(rows[4], std::string("Derived   t.cpp:5"));
  CHECK_EQ(rows[5], std::string("Other   t.cpp:9"));
  const int derived = childByLabel(env.hp(), childByLabel(env.hp(), 0, "Derived Types"), "Derived   ");
  env.hp().tree().setExpanded(derived, true);  // each entry has both nodes again
  const int below = childByLabel(env.hp(), derived, "Derived Types");
  REQUIRE(below > 0);
  env.hp().tree().setExpanded(below, true);
  CHECK_EQ(fake.askedFor.back(), std::string("sub:Derived"));  // the next level
}

TEST(w11_hierarchy_errors_and_empty_answers_are_shown) {
  Env env(kEmpty);
  FakeCalls fake;
  fake.file = env.dir / "t.cpp";
  env.hp().setBackend(fake.backend());
  env.hp().setUnavailableFn(nullptr);
  env.hp().open(HierarchyPanel::Mode::Call);
  fake.prepareDone[0]({}, "");
  CHECK(env.hp().rowLabels()[0].find("No symbol") != std::string::npos);
  env.hp().open(HierarchyPanel::Mode::Call);
  fake.prepareDone[1]({}, "clangd not running");
  CHECK_EQ(env.hp().rowLabels()[0], std::string("clangd not running"));
  env.hp().open(HierarchyPanel::Mode::Call);
  fake.prepareDone[2]({item(fake.file, "f", 0)}, "");
  fake.incomingDone[0]({}, "request timed out");
  CHECK_EQ(env.hp().rowLabels()[2], std::string("request timed out"));
  CHECK(!env.hp().busy());
}

// ---------------------------------------------------------------------------------------------------------------
// Quick Actions and the light bulb
// ---------------------------------------------------------------------------------------------------------------

namespace {

CodeAction insertAction(const fs::path& file, const std::string& title, const std::string& text, bool fix = true) {
  CodeAction a;
  a.title = title;
  a.kind = fix ? "quickfix" : "refactor.extract";
  a.hasEdit = true;
  a.edit[file] = {{Range{{0, 0}, {0, 0}}, text}};
  return a;
}

struct FakeActions {
  int asked = 0;
  std::vector<Range> ranges;
  std::vector<CodeAction> actions;
  int onlyLine = -1;  // answer only for ranges on this line (-1: always)
  QuickActions::Backend backend(Editor& ed) {
    QuickActions::Backend b = QuickActions::editorBackend(ed);  // apply is the real one
    b.fetch = [this](Range r, QuickActions::Done<CodeAction> d) {
      ++asked;
      ranges.push_back(r);
      d(onlyLine < 0 || r.start.line == onlyLine ? actions : std::vector<CodeAction>{}, "");
    };
    return b;
  }
};

}  // namespace

TEST(w11_quick_actions_list_applies_one_action_as_one_undo_step) {
  Env env([](const fs::path& d) { writeFile(d / "a.cpp", "std::vector<int> v;\nint x;\n"); });
  env.open("a.cpp");
  FakeActions fake;
  fake.actions = {insertAction(env.dir / "a.cpp", "Insert #include <vector>", "#include <vector>\n"),
                  insertAction(env.dir / "a.cpp", "Extract variable", "// extracted\n", false)};
  env.app->quickActions().setBackend(fake.backend(*env.ed));
  env.app->quickActions().setAllowedFn([] { return true; });
  env.caret(0, 3);
  CHECK(env.key(Key('.'), ModCtrl));
  REQUIRE(env.popup() != nullptr);
  CHECK(env.popup() == env.app->quickActions().popup());
  REQUIRE_EQ(env.popup()->rows().size(), 2u);
  CHECK_EQ(env.popup()->rows()[0].primary, std::string("Insert #include <vector>"));
  CHECK_EQ(env.popup()->rows()[0].badge, std::string("fix"));
  CHECK_EQ(env.popup()->rows()[1].badge, std::string("refactor"));
  CHECK(env.popup()->anchored());  // at the caret
  // The request was for the caret's line without its blanks.
  REQUIRE(!fake.ranges.empty());
  CHECK_EQ(fake.ranges.back().start.line, 0);
  CHECK_EQ(fake.ranges.back().end.column, 19);
  // Down, Up, Enter apply the first action.
  env.key(Key::Down);
  env.key(Key::Up);
  CHECK(env.key(Key::Enter));
  Document* doc = env.ed->currentDocument();
  CHECK_EQ(doc->buffer().line(0), std::string("#include <vector>"));
  CHECK_EQ(doc->buffer().line(1), std::string("std::vector<int> v;"));
  CHECK(env.until([&] { return env.msg().rfind("Applied", 0) == 0; }, 2000));
  // One undo restores the original text.
  CHECK(env.key(Key('Z'), ModCtrl));
  CHECK_EQ(doc->buffer().text(), std::string("std::vector<int> v;\nint x;\n"));
  CHECK(!doc->canUndo());
  CHECK(env.popup() == nullptr);
}

TEST(w11_quick_actions_escape_closes_and_nothing_is_shown_without_actions_or_clangd) {
  Env env([](const fs::path& d) { writeFile(d / "a.cpp", "int x;\n"); });
  env.open("a.cpp");
  // No clangd (useClangd = false): a message, no popup, no request.
  env.caret(0, 1);
  CHECK(env.key(Key('.'), ModCtrl));
  CHECK(env.popup() == nullptr);
  CHECK(env.msg().find("No quick actions") != std::string::npos);
  FakeActions fake;
  env.app->quickActions().setBackend(fake.backend(*env.ed));
  env.app->quickActions().setAllowedFn([] { return true; });
  fake.actions = {};
  CHECK(env.app->quickActions().open());  // asked, nothing there
  env.frame();
  CHECK(env.popup() == nullptr);
  CHECK(env.msg().find("No quick actions") != std::string::npos);
  fake.actions = {insertAction(env.dir / "a.cpp", "Fix", "// fix\n")};
  CHECK(env.app->quickActions().open());
  REQUIRE(env.popup() != nullptr);
  CHECK(env.key(Key::Escape));
  CHECK(env.popup() == nullptr);
  CHECK_EQ(env.ed->currentDocument()->buffer().line(0), std::string("int x;"));
}

TEST(w11_light_bulb_follows_the_caret_line_and_a_click_opens_the_list) {
  Env env([](const fs::path& d) { writeFile(d / "a.cpp", "int a;\nint b;\nint c;\n"); });
  env.open("a.cpp");
  FakeActions fake;
  fake.onlyLine = 1;  // only line 1 has a fix
  fake.actions = {insertAction(env.dir / "a.cpp", "Fix b", "// fix\n")};
  env.app->quickActions().setBackend(fake.backend(*env.ed));
  env.app->quickActions().setAllowedFn([] { return true; });
  TextView* v = env.view();
  REQUIRE(v != nullptr);
  env.caret(0, 0);
  env.wait(100);
  CHECK_EQ(v->lightBulbLine(), -1);
  CHECK_EQ(fake.asked, 0);  // debounced: the caret has not rested yet
  env.wait(400);
  CHECK_EQ(fake.asked, 1);
  CHECK_EQ(v->lightBulbLine(), -1);  // line 0 has no fix
  env.caret(1, 2);
  env.wait(400);
  CHECK_EQ(v->lightBulbLine(), 1);
  CHECK(v->lightBulbRect().w > 0);
  // Moving off the line removes it at once; coming back asks again.
  env.caret(2, 0);
  CHECK_EQ(v->lightBulbLine(), -1);
  env.wait(400);
  CHECK_EQ(v->lightBulbLine(), -1);
  env.caret(1, 0);
  env.wait(400);
  CHECK_EQ(v->lightBulbLine(), 1);
  // A click on the bulb opens the Quick Actions list.
  const RectF r = v->lightBulbRect();
  InputEvent click;
  click.type = InputType::MouseDown;
  click.button = MouseButton::Left;
  click.x = r.x + r.w / 2;
  click.y = r.y + r.h / 2;
  click.clicks = 1;
  env.send(click);
  REQUIRE(env.popup() != nullptr);
  env.key(Key::Escape);
  // Editing the line keeps the bulb while the new answer is pending, a dead server removes it.
  env.app->quickActions().setAllowedFn([] { return false; });
  env.ed->setSelections({caretAt({1, 6})});
  env.ed->typeText("x");
  env.wait(400);
  CHECK_EQ(v->lightBulbLine(), -1);
}

TEST(w11_light_bulb_only_for_quick_fixes_not_for_refactorings) {
  Env env([](const fs::path& d) { writeFile(d / "a.cpp", "int a;\n"); });
  env.open("a.cpp");
  FakeActions fake;
  fake.actions = {insertAction(env.dir / "a.cpp", "Extract", "x", false)};
  env.app->quickActions().setBackend(fake.backend(*env.ed));
  env.app->quickActions().setAllowedFn([] { return true; });
  env.caret(0, 0);
  env.wait(500);
  CHECK_EQ(fake.asked, 1);
  CHECK_EQ(env.view()->lightBulbLine(), -1);
  CHECK(env.app->quickActions().open());  // Ctrl+. still lists it
  CHECK(env.popup() != nullptr);
}

// ---------------------------------------------------------------------------------------------------------------
// Inlay hints
// ---------------------------------------------------------------------------------------------------------------

namespace {

struct FakeHints {
  int asked = 0;
  std::vector<LspInlayHint> hints;
  InlayHints::Backend backend() {
    InlayHints::Backend b;
    b.fetch = [this](Range, InlayHints::Done<LspInlayHint> d) {
      ++asked;
      d(hints, "");
    };
    return b;
  }
};

LspInlayHint hint(int line, int col, const std::string& label, int kind) {
  LspInlayHint h;
  h.pos = {line, col};
  h.label = label;
  h.kind = kind;
  return h;
}

}  // namespace

TEST(w11_inlay_hints_while_pressed_show_only_while_alt_f1_is_held) {
  Env env([](const fs::path& d) { writeFile(d / "a.cpp", "f(1, 2);\n"); });
  env.open("a.cpp");
  FakeHints fake;
  fake.hints = {hint(0, 2, "a:", 2), hint(0, 5, "b:", 2)};
  env.app->inlayHints().setBackend(fake.backend());
  env.app->inlayHints().setAllowedFn([] { return true; });
  TextView* v = env.view();
  REQUIRE(v != nullptr);
  env.wait(500);
  CHECK(!v->inlayHintsShown());  // the default: nothing until the key is held
  CHECK_EQ(fake.asked, 0);
  CHECK(env.key(Key::F1, ModAlt));
  CHECK(v->inlayHintsShown());
  env.wait(300);
  CHECK_EQ(fake.asked, 1);
  CHECK_EQ(v->inlayMap().items().size(), 2u);
  env.keyUp(Key::F1, ModAlt);
  CHECK(!v->inlayHintsShown());
  CHECK(v->inlayMap().empty());
  // Alt released first (the key is still down): the hints go too.
  env.key(Key::F1, ModAlt);
  env.wait(300);
  CHECK(v->inlayHintsShown());
  env.keyUp(Key::Unknown, 0);
  CHECK(!v->inlayHintsShown());
}

TEST(w11_inlay_hints_modes_off_and_on) {
  Env env([](const fs::path& d) { writeFile(d / "a.cpp", "f(1, 2);\n"); });
  env.open("a.cpp");
  FakeHints fake;
  fake.hints = {hint(0, 2, "a:", 2)};
  env.app->inlayHints().setBackend(fake.backend());
  env.app->inlayHints().setAllowedFn([] { return true; });
  TextView* v = env.view();
  env.app->inlayHints().setMode(InlayHints::Mode::Off);
  env.key(Key::F1, ModAlt);
  env.wait(400);
  CHECK(!v->inlayHintsShown());  // Alt+F1 does nothing when off
  CHECK(v->inlayMap().empty());
  CHECK(!env.app->inlayHints().shown());
  env.keyUp(Key::F1, ModAlt);
  env.app->inlayHints().setMode(InlayHints::Mode::On);
  env.wait(400);
  CHECK(v->inlayHintsShown());
  CHECK_EQ(v->inlayMap().items().size(), 1u);
  const int asked = fake.asked;
  env.wait(1000);
  CHECK_EQ(fake.asked, asked);  // not asked again while nothing changed
  // Typing on another line moves the version: asked again.
  env.ed->setSelections({caretAt({0, 8})});
  env.ed->typeText("\n");
  env.wait(400);
  env.wait(400);  // the new line changed what is visible: settles one more debounce
  CHECK(fake.asked > asked);
  env.app->inlayHints().setMode(InlayHints::Mode::Off);
  env.wait(10);
  CHECK(!v->inlayHintsShown());
  CHECK(v->inlayMap().empty());
}

TEST(w11_inlay_hints_are_virtual_text_the_caret_skips) {
  Env env([](const fs::path& d) { writeFile(d / "a.cpp", "f(1, 2);\n"); });
  env.open("a.cpp");
  TextView* v = env.view();
  REQUIRE(v != nullptr);
  v->settings().wordWrap = false;
  env.frame();
  FakeHints fake;
  fake.hints = {hint(0, 2, "a:", 2), hint(0, 5, "bb:", 2)};
  env.app->inlayHints().setBackend(fake.backend());
  env.app->inlayHints().setAllowedFn([] { return true; });
  env.app->inlayHints().setMode(InlayHints::Mode::On);
  env.wait(400);
  REQUIRE_EQ(v->inlayMap().items().size(), 2u);
  const float cw = env.text->cellWidth();
  const float x0 = v->caretRect({0, 0}).x;
  auto cells = [&](int col) { return (v->caretRect({0, col}).x - x0) / cw; };
  CHECK_EQ(cells(1), 1.0f);
  CHECK_EQ(cells(2), 2.0f + 3.0f);  // at the parameter's column the caret is after its "a:" box (2 cells + 1 padding)
  CHECK_EQ(cells(3), 3.0f + 3.0f);
  CHECK_EQ(cells(5), 5.0f + 3.0f + 4.0f);
  CHECK_EQ(cells(8), 8.0f + 3.0f + 4.0f);
  // Right arrow walks the document columns; each press lands on a real column, never inside a hint, and the status
  // bar column is the document's.
  env.caret(0, 1);
  std::vector<int> cols;
  for (int i = 0; i < 4; ++i) {
    env.key(Key::Right);
    cols.push_back(env.ed->selections()[0].head.column);
  }
  CHECK(cols == (std::vector<int>{2, 3, 4, 5}));
  CHECK_EQ(v->positionText(), std::string("Ln 1, Col 6"));
  // A click inside a hint puts the caret at its column; a click after it lands on the text it covers.
  const float y = v->caretRect({0, 0}).y + 2;
  auto clickAt = [&](float x) {
    InputEvent e;
    e.type = InputType::MouseDown;
    e.button = MouseButton::Left;
    e.x = x;
    e.y = y;
    e.clicks = 1;
    env.send(e);
    return env.ed->selections()[0].head.column;
  };
  CHECK_EQ(clickAt(x0 + cw * 3.5f), 2);  // inside "a:" (cells 2..5)
  CHECK_EQ(clickAt(x0 + cw * 5.2f), 2);
  CHECK_EQ(clickAt(x0 + cw * 6.3f), 3);  // the comma: column 3
  CHECK_EQ(clickAt(x0 + cw * 14.3f), 7);
  // Selection rectangles follow the shifted text; the document is untouched.
  CHECK_EQ(env.ed->currentDocument()->buffer().line(0), std::string("f(1, 2);"));
  // Hiding the hints puts everything back.
  env.app->inlayHints().setMode(InlayHints::Mode::Off);
  env.wait(10);
  CHECK_EQ(v->caretRect({0, 2}).x, x0 + 2 * cw);
}

TEST(w11_inlay_hints_are_not_asked_for_files_outside_every_translation_unit) {
  Env env([](const fs::path& d) { writeFile(d / "a.cpp", "f(1);\n"); });
  env.open("a.cpp");
  FakeHints fake;
  fake.hints = {hint(0, 2, "a:", 2)};
  env.app->inlayHints().setBackend(fake.backend());
  // The app's own rule: no clangd running here, so nothing is asked and nothing is shown.
  env.app->inlayHints().setMode(InlayHints::Mode::On);
  env.wait(500);
  CHECK_EQ(fake.asked, 0);
  CHECK(env.view()->inlayMap().empty());
}

// ---------------------------------------------------------------------------------------------------------------
// With a real clangd
// ---------------------------------------------------------------------------------------------------------------

TEST(w11_clangd_call_hierarchy_through_ctrl_k_ctrl_t_and_back) {
  SKIP_WITHOUT_CLANGD();
  Env env(
      [](const fs::path& d) {
        writeFile(d / "lib.cpp", "int leaf() { return 1; }\nint mid() { return leaf() + 1; }\n");
        writeFile(d / "main.cpp", "int mid();\nint main() { return mid(); }\n");
      },
      /*clangd=*/true);
  REQUIRE(env.clangdReady());
  env.open("lib.cpp");
  env.ed->semantic().waitForFile(env.dir / "lib.cpp", 60000);
  env.open("main.cpp");
  env.ed->semantic().waitForFile(env.dir / "main.cpp", 60000);
  env.open("lib.cpp");
  env.caret(1, 5);  // on mid
  CHECK(env.key(Key('K'), ModCtrl));
  CHECK(env.key(Key('T'), ModCtrl));
  REQUIRE(env.until([&] { return env.hp().title() == "Call Hierarchy: mid" && !env.hp().busy(); }));
  // "Calls To 'mid'" opened by itself and lists main (which lives in main.cpp).
  REQUIRE(env.until([&] {
    const auto r = env.hp().rowLabels();
    return r.size() >= 3 && r[2].rfind("main", 0) == 0;
  }));
  CHECK(env.hp().rowLabels()[2].find("main.cpp:2") != std::string::npos);
  // "Calls From 'mid'" is asked when expanded and lists leaf.
  env.hp().tree().setExpanded(childByLabel(env.hp(), 0, "Calls From"), true);
  REQUIRE(env.until([&] { return nodeByLabel(env.hp(), "leaf") >= 0; }));
  // Enter on main jumps there; Back returns to lib.cpp where we were.
  const int mainNode = nodeByLabel(env.hp(), "main");
  env.hp().focusTree();
  env.hp().tree().select(mainNode);
  CHECK(env.key(Key::Enter));
  env.frame();
  CHECK_EQ(env.ed->currentDocument()->path().filename().string(), std::string("main.cpp"));
  CHECK_EQ(env.ed->selections()[0].head.line, 1);
  env.bench->setActivePanel(env.bench->activePanel());
  CHECK(env.key(Key('-'), ModCtrl));
  CHECK_EQ(env.ed->currentDocument()->path().filename().string(), std::string("lib.cpp"));
  CHECK_EQ(env.ed->selections()[0].head.line, 1);
}

TEST(w11_clangd_type_hierarchy_from_the_command_palette_command) {
  SKIP_WITHOUT_CLANGD();
  Env env(
      [](const fs::path& d) {
        writeFile(d / "t.cpp", "struct Base { virtual ~Base() {} };\nstruct Mid : Base {};\nstruct Leaf : Mid {};\n");
      },
      true);
  REQUIRE(env.clangdReady());
  env.open("t.cpp");
  env.ed->semantic().waitForFile(env.dir / "t.cpp", 60000);
  env.caret(1, 8);  // on Mid
  env.app->runCommand("View.TypeHierarchy");
  REQUIRE(env.until([&] { return env.hp().title() == "Type Hierarchy: Mid" && !env.hp().busy(); }));
  REQUIRE(env.until([&] {
    const auto r = env.hp().rowLabels();
    return r.size() >= 3 && r[2].rfind("Base", 0) == 0;
  }));
  env.hp().tree().setExpanded(childByLabel(env.hp(), 0, "Derived Types"), true);
  REQUIRE(env.until([&] { return nodeByLabel(env.hp(), "Leaf") >= 0; }));
}

TEST(w11_clangd_quick_fix_applies_with_ctrl_dot_and_is_one_undo_step) {
  SKIP_WITHOUT_CLANGD();
  // clangd reports the missing semicolon on the next token (line 1) and offers the fix-it "insert ';'" (edit on line
  // 0).
  const std::string src = "int a = 1\nint b = 2;\n";
  Env env([&](const fs::path& d) { writeFile(d / "a.cpp", src); }, true);
  REQUIRE(env.clangdReady());
  env.open("a.cpp");
  env.ed->semantic().waitForFile(env.dir / "a.cpp", 60000);
  REQUIRE(env.until([&] { return !env.ed->diagnostics(env.dir / "a.cpp").empty(); }, 60000));
  TextView* v = env.view();
  // The light bulb appears on the line with the error once the caret rests on it, and not on the other line.
  env.caret(0, 2);
  env.wait(600);
  CHECK_EQ(v->lightBulbLine(), -1);
  env.caret(1, 1);
  REQUIRE(env.until([&] { return v->lightBulbLine() == 1; }, 20000));
  CHECK(v->lightBulbIsError());  // an error with a fix: Visual Studio's red "error light bulb"
  CHECK(env.key(Key('.'), ModCtrl));
  REQUIRE(env.until([&] { return env.popup() != nullptr; }, 20000));
  REQUIRE(!env.popup()->rows().empty());
  CHECK(env.popup()->rows()[0].primary.find("insert") != std::string::npos);
  CHECK_EQ(env.popup()->rows()[0].badge, std::string("fix"));
  CHECK(env.key(Key::Enter));
  Document* doc = env.ed->currentDocument();
  REQUIRE(env.until([&] { return doc->buffer().line(0) == "int a = 1;"; }, 10000));
  // One undo step restores the text; redo applies it again and the error (and its light bulb) goes away.
  CHECK(env.key(Key('Z'), ModCtrl));
  CHECK_EQ(doc->buffer().text(), src);
  CHECK(!doc->canUndo());
  CHECK(env.key(Key('Y'), ModCtrl));
  CHECK_EQ(doc->buffer().line(0), std::string("int a = 1;"));
  REQUIRE(env.until([&] { return env.ed->diagnostics(env.dir / "a.cpp").empty(); }, 30000));
  env.caret(0, 0);
  env.caret(1, 1);
  REQUIRE(env.until([&] { return v->lightBulbLine() == -1; }, 10000));
}

TEST(w11_clangd_inlay_hints_name_the_parameters_while_alt_f1_is_held) {
  SKIP_WITHOUT_CLANGD();
  Env env(
      [](const fs::path& d) {
        writeFile(d / "a.cpp", "void area(int width, int height) {}\nvoid g() { area(3, 4); }\n");
      },
      true);
  REQUIRE(env.clangdReady());
  env.open("a.cpp");
  env.ed->semantic().waitForFile(env.dir / "a.cpp", 60000);
  TextView* v = env.view();
  env.key(Key::F1, ModAlt);
  REQUIRE(env.until([&] { return v->inlayMap().items().size() >= 2; }, 30000));
  std::vector<std::string> labels;
  for (const auto& it : v->inlayMap().items()) labels.push_back(it.text);
  CHECK(std::any_of(labels.begin(), labels.end(),
                    [](const std::string& s) { return s.find("width") != std::string::npos; }));
  CHECK(std::any_of(labels.begin(), labels.end(),
                    [](const std::string& s) { return s.find("height") != std::string::npos; }));
  env.keyUp(Key::F1, ModAlt);
  CHECK(v->inlayMap().empty());
}

// ---------------------------------------------------------------------------------------------------------------
// Goldens (real font, vs-dark)
// ---------------------------------------------------------------------------------------------------------------

namespace {

std::string dejavuMono() {
  for (const char* p : {"/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
                        "/usr/share/fonts/dejavu/DejaVuSansMono.ttf", "/usr/share/fonts/TTF/DejaVuSansMono.ttf"})
    if (std::ifstream(p).good()) return p;
  return {};
}

void compareGolden(gfx::GlRenderer& r, Workbench& wb, gfx::TextRenderer& tr, const char* name, int w, int h) {
  r.setOffscreen(true);
  wb.render(r, w, h);
  std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4);
  REQUIRE(r.readPixels(w, h, px.data()));
  tr.releaseTexture(r);
  const std::string path = std::string(TENFOLD_SOURCE_DIR "/tests/golden/") + name + ".png";
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

struct GoldenEnv {
  fs::path dir;
  std::unique_ptr<gfx::TextRenderer> tr;
  std::unique_ptr<Editor> ed;
  std::unique_ptr<Workbench> wb;
  std::unique_ptr<EditorApp> app;
  int64_t now = 1000;
  GoldenEnv(const std::string& font, const std::string& src, const char* name) {
    dir = uitest::tempRoot() / name;  // the workspace name shows in the status bar: fixed for the golden
    std::error_code rec;
    fs::remove_all(dir, rec);
    fs::create_directories(dir);
    writeFile(dir / "shapes.cpp", src);
    std::string err;
    tr = gfx::TextRenderer::create(font, 15.0f, 1.0f, &err);
    Editor::Options o;
    o.semantic.useClangd = false;
    o.semantic.persistFastIndex = false;
    o.watchForcePolling = true;
    o.watchPollMs = 60000;
    o.vcs.enabled = false;
    ed = std::make_unique<Editor>(dir, o);
    ed->start(&err);
    std::vector<SettingsDiag> diags;
    const Theme vs = loadTheme("vs-dark", {}, {}, diags);
    wb = std::make_unique<Workbench>(*tr, *ed, vs);
    wb->openFile(dir / "shapes.cpp");
    wb->sync();
    app = std::make_unique<EditorApp>(*wb, *ed, wb.get());
  }
  ~GoldenEnv() {
    app.reset();
    wb.reset();
    ed.reset();
    std::error_code ec;
    fs::remove_all(dir, ec);
  }
  void frame() {
    ed->pump();
    app->tick(now);
  }
};

const char* kShapes =
    "struct Shape {\n"
    "  virtual double area(int scale, bool exact) const = 0;\n"
    "};\n"
    "\n"
    "double total(const Shape& s, int n) {\n"
    "  double sum = s.area(n, true);\n"
    "  auto half = sum / 2;\n"
    "  return half + s.area(2, false);\n"
    "}\n";

}  // namespace

TEST(w11_golden_call_hierarchy_panel_vs_dark) {
  SKIP_IF_NO_GL(fx);
  const std::string font = dejavuMono();
  if (font.empty()) {
    std::cerr << "    SKIP golden: DejaVuSansMono not installed; goldens are only comparable with the same font\n";
    return;
  }
  GoldenEnv g(font, kShapes, "tf_w11_hierarchy");
  REQUIRE(g.tr != nullptr);
  const fs::path f = g.dir / "shapes.cpp";
  FakeCalls fake;
  fake.file = f;
  HierarchyPanel& hp = *g.app->hierarchyPanel();
  hp.setBackend(fake.backend());
  hp.setUnavailableFn(nullptr);
  g.wb->setBottomHeight(0.45f);
  g.wb->showBottomPanel("Hierarchy");
  g.ed->setSelections({caretAt({4, 8})});
  hp.open(HierarchyPanel::Mode::Call);
  fake.prepareDone[0]({item(f, "total", 4, 7)}, "");
  g.frame();
  fake.incomingDone[0](
      {edge(item(g.dir / "main.cpp", "main", 11, 4), 2), edge(item(g.dir / "render.cpp", "Canvas::draw", 40, 6))}, "");
  g.frame();
  hp.tree().setExpanded(childByLabel(hp, 0, "Calls From"), true);
  fake.outgoingDone[0]({edge(item(f, "Shape::area", 1, 17), 2)}, "");
  g.frame();
  hp.tree().select(nodeByLabel(hp, "main"));
  g.app->tick(g.now);
  compareGolden(*fx->renderer, *g.wb, *g.tr, "w11_call_hierarchy_vs_dark", 1024, 640);
}

TEST(w11_golden_inlay_hints_vs_dark) {
  SKIP_IF_NO_GL(fx);
  const std::string font = dejavuMono();
  if (font.empty()) {
    std::cerr << "    SKIP golden: DejaVuSansMono not installed; goldens are only comparable with the same font\n";
    return;
  }
  GoldenEnv g(font, kShapes, "tf_w11_hints");
  REQUIRE(g.tr != nullptr);
  FakeHints fake;
  fake.hints = {hint(5, 22, "scale:", 2), hint(5, 25, "exact:", 2), hint(6, 11, ": double", 1),
                hint(7, 23, "scale:", 2), hint(7, 26, "exact:", 2)};
  g.app->inlayHints().setBackend(fake.backend());
  g.app->inlayHints().setAllowedFn([] { return true; });
  g.app->inlayHints().setMode(InlayHints::Mode::On);
  g.wb->render(*fx->renderer, 760, 300);
  g.frame();
  g.now += 400;
  g.frame();
  g.ed->setSelections({caretAt({5, 24})});
  g.frame();
  g.wb->render(*fx->renderer, 760, 300);
  compareGolden(*fx->renderer, *g.wb, *g.tr, "w11_inlay_hints_vs_dark", 760, 300);
}
