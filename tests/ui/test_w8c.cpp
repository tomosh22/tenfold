// W8c: the 10x parser commands (RenameSymbol / SubmitRenameSymbol, Autocomplete, ShowFunctionArgsInfo, ShowSymbolInfo,
// FindFunction, GotoSymbolDefinitionUnderMouse, FormatSelection) driven through EditorApp with the real default key
// bindings. A recording renderer and the mock font (8 px cells); the popups' backends are faked where the test is about
// the UI, and a real clangd is used (skipped without it) for rename across files and range formatting.
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#include "../text/text_test_util.h"
#include "core/Platform.h"
#include "ui/features/ParserCommands.h"
#include "ui/gfx/TextRenderer.h"
#include "ui/shell/EditorApp.h"
#include "ui/widgets/ListPopup.h"
#include "ui/widgets/Panel.h"
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
InputEvent mouseEv(InputType t, MouseButton b, float x, float y) {
  InputEvent e;
  e.type = t;
  e.button = b;
  e.x = x;
  e.y = y;
  e.clicks = 1;
  return e;
}

void writeFile(const fs::path& p, const std::string& text) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary) << text;
}
std::string slurp(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
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
  std::vector<CompletionPopup::Done> completions;
  std::vector<HoverTooltip::Done> hovers;
  std::vector<SignatureHelp::Done> sigs;

  struct RenameCall {
    Position at;
    std::string name;
    RenameInput::Done done;
  };
  std::vector<RenameCall> renames;

  explicit Env(const std::function<void(const fs::path&)>& populate, bool clangd = false) {
    static int n = 0;
    dir =
        uitest::tempRoot() / ("tf_w8c_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                              "_" + std::to_string(n++));
    fs::create_directories(dir);
    populate(dir);
    Editor::Options o;
    o.semantic.useClangd = clangd;
    o.semantic.persistFastIndex = false;
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
  }
  ~Env() {
    app.reset();
    bench.reset();
    ed.reset();
    std::error_code ec;
    fs::remove_all(dir, ec);
  }
  void fakeUi() {
    app->completion().setCompleteFn([this](CompletionPopup::Done d) { completions.push_back(std::move(d)); });
    app->completion().setApproximateFn([] { return false; });
    app->hover().setHoverFn([this](Document&, Position, HoverTooltip::Done d) { hovers.push_back(std::move(d)); });
    app->hover().setApproximateFn([] { return false; });
    app->signatureHelp().setSigFn([this](SignatureHelp::Done d) { sigs.push_back(std::move(d)); });
    app->renameInput().setAvailableFn([] { return true; });
    app->renameInput().setRenameFn([this](Position at, const std::string& name, RenameInput::Done d) {
      renames.push_back({at, name, std::move(d)});
    });
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
  void type(const std::string& s) {
    for (char c : s) send(textEv(std::string(1, c)));
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
  // Waits (pumping) until `pred` or the timeout.
  bool until(const std::function<bool()>& pred, int ms = 20000) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    for (;;) {
      frame();
      if (pred()) return true;
      if (std::chrono::steady_clock::now() >= end) return false;
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
  }
  bool clangdReady() {
    return until([&] { return ed->semantic().status().clangdInitialized; }, 60000);
  }
  std::string curFile() { return ed->currentDocument()->path().filename().string(); }
  std::string line(int n = 0) { return ed->currentDocument()->buffer().line(n); }
  std::string msg() { return app->message(); }
  TextView* view() { return dynamic_cast<TextView*>(bench->focused()); }
  ListPopup* popup() { return dynamic_cast<ListPopup*>(bench->modal()); }
};

const auto kEmpty = [](const fs::path&) {};

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// Pure helpers
// ---------------------------------------------------------------------------------------------------------------

TEST(w8c_brace_scope_finds_the_innermost_block_and_skips_comments_and_strings) {
  TextBuffer b(
      "int f() {\n"       // 0
      "  if (x) {\n"      // 1
      "    s = \"}\";\n"  // 2
      "    // }\n"        // 3
      "    g();\n"        // 4
      "  }\n"             // 5
      "  /* { */ h();\n"  // 6
      "}\n"               // 7
      "int y;\n");        // 8
  auto r = enclosingBraceScope(b, {4, 6});
  REQUIRE(r.has_value());
  CHECK_EQ(r->start.line, 1);
  CHECK_EQ(r->end.line, 5);
  r = enclosingBraceScope(b, {6, 2});  // after the if block: the function body
  REQUIRE(r.has_value());
  CHECK_EQ(r->start.line, 0);
  CHECK_EQ(r->end.line, 7);
  CHECK(!enclosingBraceScope(b, {8, 1}).has_value());
  CHECK(!enclosingBraceScope(b, {0, 0}).has_value());  // before the `{`
  const Range line = formatScopeRange(b, {8, 1});      // outside braces: the caret line
  CHECK_EQ(line.start.line, 8);
  CHECK_EQ(line.end.line, 8);
  CHECK_EQ(formatScopeRange(b, {2, 5}).start.line, 1);
}

TEST(w8c_function_definition_filter) {
  TextBuffer b("int f(int a);\nint g(int a) { return a; }\nstruct S { void m(); void n() {} };\n");
  SymbolLocation s;
  s.kind = "function";
  s.confidence = Confidence::Authoritative;
  s.extent = {{0, 0}, {0, 13}};
  CHECK(!isFunctionDefinition(s, b));
  s.extent = {{1, 0}, {1, 26}};
  CHECK(isFunctionDefinition(s, b));
  s.kind = "method";
  s.extent = {{2, 11}, {2, 20}};
  CHECK(!isFunctionDefinition(s, b));
  s.extent = {{2, 21}, {2, 32}};
  CHECK(isFunctionDefinition(s, b));
  s.kind = "variable";
  CHECK(!isFunctionDefinition(s, b));
  s.kind = "function";  // fast tier: its own flag
  s.confidence = Confidence::Approximate;
  s.definition = false;
  CHECK(!isFunctionDefinition(s, b));
  s.definition = true;
  CHECK(isFunctionDefinition(s, b));
}

// ---------------------------------------------------------------------------------------------------------------
// Autocomplete, signature help, symbol info (fake backends)
// ---------------------------------------------------------------------------------------------------------------

TEST(w8c_control_space_opens_the_completion_list_at_the_caret) {
  Env env([](const fs::path& d) { writeFile(d / "a.cpp", "int alpha;\nint beta = al;\n"); });
  env.fakeUi();
  env.open("a.cpp");
  env.caret(1, 0);  // an empty word: nothing would open by typing
  CHECK(env.key(Key::Space, ModCtrl));
  REQUIRE_EQ(env.completions.size(), 1u);
  LspCompletionItem a, b;
  a.label = a.insertText = "alpha";
  b.label = b.insertText = "beta";
  env.completions[0]({a, b}, "");
  env.frame();
  CHECK(env.app->completion().isOpen());
  CHECK_EQ(env.app->completion().shownLabels().size(), 2u);
  CHECK(env.key(Key::Down));
  CHECK_EQ(env.app->completion().selected(), 1);
  CHECK(env.key(Key::Enter));  // accepts beta; no newline is inserted
  CHECK_EQ(env.line(1), std::string("betaint beta = al;"));
  CHECK_EQ(env.ed->currentDocument()->buffer().lineCount(), 3);
  CHECK(!env.app->completion().isOpen());

  // With a typed prefix the list opens for the word before the caret; an empty answer says so.
  env.caret(1, 17);
  CHECK(env.key(Key::Space, ModCtrl));
  REQUIRE_EQ(env.completions.size(), 2u);
  env.completions[1]({}, "");
  env.frame();
  CHECK(!env.app->completion().isOpen());
  CHECK_EQ(env.msg(), std::string("No completions here"));
  // Escape closes an open list without touching the text.
  env.caret(1, 17);
  env.key(Key::Space, ModCtrl);
  env.completions[2]({a}, "");
  env.frame();
  CHECK(env.app->completion().isOpen());
  env.key(Key::Escape);
  CHECK(!env.app->completion().isOpen());
}

TEST(w8c_control_shift_space_shows_the_signature_of_the_call_around_the_caret) {
  Env env([](const fs::path& d) { writeFile(d / "a.cpp", "int f(int a, int b);\nint g() { return f(1, 2); }\n"); });
  env.fakeUi();
  env.open("a.cpp");
  env.caret(0, 3);  // not in a call
  CHECK(env.key(Key::Space, ModCtrl | ModShift));
  CHECK(env.sigs.empty());
  CHECK_EQ(env.msg(), std::string("The caret is not inside a function call"));
  env.caret(1, 22);  // after `f(1, `
  CHECK(env.key(Key::Space, ModCtrl | ModShift));
  REQUIRE_EQ(env.sigs.size(), 1u);
  LspSignatureHelp h;
  h.signatures.push_back({"int f(int a, int b)", "", {"int a", "int b"}});
  env.sigs[0](h, "");
  env.frame();
  CHECK(env.app->signatureHelp().isOpen());
  CHECK_EQ(env.app->signatureHelp().activeParameter(), 1);
  const auto& lines = env.app->signatureHelp().box()->lines();
  REQUIRE(!lines.empty());
  std::string shown;
  for (const PopupSpan& s : lines[0].spans) shown += s.text;
  CHECK_EQ(shown, std::string("int f(int a, int b)"));
  env.key(Key::Escape);
  CHECK(!env.app->signatureHelp().isOpen());
  // clangd missing: the answer is an error, which is reported rather than swallowed.
  env.caret(1, 22);
  env.key(Key::Space, ModCtrl | ModShift);
  env.sigs[1]({}, "clangd not running");
  env.frame();
  CHECK(!env.app->signatureHelp().isOpen());
  CHECK_EQ(env.msg(), std::string("Function arguments: clangd not running"));
}

TEST(w8c_control_k_control_i_shows_the_symbol_info_at_the_caret_in_a_popup) {
  Env env([](const fs::path& d) { writeFile(d / "a.cpp", "int f(int a);\nint g() { return f(1); }\n"); });
  env.fakeUi();
  env.open("a.cpp");
  env.caret(1, 18);  // on `f`
  CHECK(env.key(Key('K'), ModCtrl));
  CHECK(env.key(Key('I'), ModCtrl));
  REQUIRE_EQ(env.hovers.size(), 1u);
  CHECK(!env.app->hover().shown());
  env.hovers[0]("```cpp\nint f(int a)\n```\nDoes f.", "");
  env.frame();
  REQUIRE(env.app->hover().shown());
  std::string all;
  for (const PopupLine& l : env.app->hover().box()->lines())
    for (const PopupSpan& s : l.spans) all += s.text + "\n";
  CHECK(all.find("int f(int a)") != std::string::npos);
  CHECK(all.find("Does f.") != std::string::npos);
  CHECK(all.find("```") == std::string::npos);
  env.key(Key::Right);  // any key dismisses it
  CHECK(!env.app->hover().shown());
  // Nothing from the backend: the status bar says so.
  env.caret(1, 18);
  env.key(Key('K'), ModCtrl);
  env.key(Key('I'), ModCtrl);
  env.hovers[1]("", "");
  env.frame();
  CHECK_EQ(env.msg(), std::string("No symbol information here"));
  // Blank space: no symbol.
  env.caret(1, 9);
  env.key(Key('K'), ModCtrl);
  env.key(Key('I'), ModCtrl);
  CHECK_EQ(env.hovers.size(), 2u);
  CHECK_EQ(env.msg(), std::string("No symbol at the caret"));
}

// ---------------------------------------------------------------------------------------------------------------
// Rename
// ---------------------------------------------------------------------------------------------------------------

TEST(w8c_rename_box_prefills_enter_submits_escape_cancels_and_enter_never_types) {
  Env env([](const fs::path& d) { writeFile(d / "a.cpp", "int alpha = 1;\nint b = alpha;\n"); });
  env.fakeUi();
  env.open("a.cpp");
  const std::string before = env.ed->currentDocument()->buffer().text();
  env.caret(1, 11);  // inside `alpha`
  CHECK(env.key(Key('R'), ModCtrl));
  REQUIRE(env.app->renameInput().isOpen());
  CHECK_EQ(env.app->renameInput().input()->text(), std::string("alpha"));
  CHECK(env.bench->focused() == env.app->renameInput().input());
  CHECK(env.app->renameInput().input()->hasSelection());  // typing replaces the old name
  env.type("gamma");
  CHECK_EQ(env.app->renameInput().input()->text(), std::string("gamma"));
  CHECK_EQ(env.ed->currentDocument()->buffer().text(), before);  // the typing went to the box
  CHECK(!env.app->completion().isOpen());
  env.key(Key::Up);  // the autocomplete keys mean nothing here and say nothing
  CHECK(env.msg().find("not implemented") == std::string::npos);
  CHECK(env.key(Key::Enter));  // SubmitRenameSymbol (the popup context), not a newline
  CHECK(!env.app->renameInput().isOpen());
  REQUIRE_EQ(env.renames.size(), 1u);
  CHECK_EQ(env.renames[0].name, std::string("gamma"));
  CHECK(env.renames[0].at == (Position{1, 8}));  // the start of the word under the caret
  CHECK_EQ(env.ed->currentDocument()->buffer().text(), before);
  CHECK(env.view() != nullptr);  // the keys went back to the editor view
  Editor::RenameResult res;
  res.edits = 2;
  res.updated = {env.dir / "a.cpp"};
  env.renames[0].done(res, "");
  CHECK_EQ(env.msg(), std::string("Renamed alpha to gamma: 2 edits in 1 file"));

  // Escape cancels; so does a click elsewhere.
  env.caret(0, 5);
  env.key(Key('R'), ModCtrl);
  REQUIRE(env.app->renameInput().isOpen());
  env.key(Key::Escape);
  CHECK(!env.app->renameInput().isOpen());
  CHECK_EQ(env.renames.size(), 1u);
  CHECK(env.view() != nullptr);
  env.key(Key('R'), ModCtrl);
  REQUIRE(env.app->renameInput().isOpen());
  env.send(mouseEv(InputType::MouseDown, MouseButton::Left, 900, 500));
  CHECK(!env.app->renameInput().isOpen());

  // An unchanged name or an invalid identifier does not call the backend.
  env.caret(0, 5);
  env.key(Key('R'), ModCtrl);
  env.key(Key::Enter);
  CHECK(!env.app->renameInput().isOpen());
  CHECK_EQ(env.renames.size(), 1u);
  env.caret(0, 5);
  env.key(Key('R'), ModCtrl);
  env.type("9x");
  env.key(Key::Enter);
  CHECK(env.app->renameInput().isOpen());  // stays open for a correction
  CHECK(env.msg().find("not a valid identifier") != std::string::npos);
  CHECK_EQ(env.renames.size(), 1u);
  env.key(Key::Escape);
  // No symbol at the caret.
  env.caret(0, 10);
  env.key(Key('R'), ModCtrl);
  CHECK(!env.app->renameInput().isOpen());
  CHECK_EQ(env.msg(), std::string("No symbol at the caret to rename"));
}

// W9-FIX-B rename safety: an edit set that misses spellings of the old name is not applied; the box comes back and a
// second Enter forces the rename.
TEST(w8c_rename_that_misses_uses_asks_before_applying) {
  Env env([](const fs::path& d) { writeFile(d / "a.cpp", "int alpha = 1;\nint b = alpha;\n"); });
  env.fakeUi();
  std::vector<Env::RenameCall> forced;
  env.app->renameInput().setForceRenameFn(
      [&](Position at, const std::string& name, RenameInput::Done d) { forced.push_back({at, name, std::move(d)}); });
  env.open("a.cpp");
  env.caret(1, 11);
  env.key(Key('R'), ModCtrl);
  REQUIRE(env.app->renameInput().isOpen());
  env.type("gamma");
  env.key(Key::Enter);
  REQUIRE_EQ(env.renames.size(), 1u);
  Editor::RenameResult held;
  held.needsConfirmation = true;
  held.missedTotal = 2;
  held.missed = {Location{env.dir / "a.tpp", {3, 4}}, Location{env.dir / "b.inl", {0, 1}}};
  env.renames[0].done(held, "");
  env.frame();
  CHECK(env.msg().find("NOT applied") != std::string::npos);
  CHECK(env.msg().find("a.tpp:4") != std::string::npos);
  REQUIRE(env.app->renameInput().isOpen());  // asking again
  CHECK_EQ(env.app->renameInput().input()->text(), std::string("gamma"));
  CHECK(forced.empty());
  env.key(Key::Enter);  // confirmed
  CHECK(!env.app->renameInput().isOpen());
  CHECK_EQ(env.renames.size(), 1u);
  REQUIRE_EQ(forced.size(), 1u);
  CHECK_EQ(forced[0].name, std::string("gamma"));
  Editor::RenameResult res;
  res.edits = 2;
  res.updated = {env.dir / "a.cpp"};
  res.missedTotal = 2;
  forced[0].done(res, "");
  CHECK(env.msg().find("WARNING: 2 other spelling") != std::string::npos);

  // Escape on the second box abandons the rename; a changed name starts over unforced.
  env.caret(0, 5);
  env.key(Key('R'), ModCtrl);
  env.type("delta");
  env.key(Key::Enter);
  REQUIRE_EQ(env.renames.size(), 2u);
  env.renames[1].done(held, "");
  env.frame();
  REQUIRE(env.app->renameInput().isOpen());
  env.key(Key::Escape);
  CHECK(!env.app->renameInput().isOpen());
  CHECK_EQ(forced.size(), 1u);
}

TEST(w8c_rename_without_clangd_says_so_and_has_no_fast_tier_fallback) {
  Env env([](const fs::path& d) { writeFile(d / "a.cpp", "int alpha = 1;\n"); });  // clangd disabled
  env.open("a.cpp");
  env.caret(0, 5);
  CHECK(env.key(Key('R'), ModCtrl));
  CHECK(!env.app->renameInput().isOpen());
  CHECK(env.msg().find("needs clangd") != std::string::npos);
  CHECK(env.msg().find("approximate renames would be wrong") != std::string::npos);
  CHECK_EQ(env.ed->currentDocument()->buffer().text(), std::string("int alpha = 1;\n"));
  // The engine agrees: no fabricated edits.
  bool called = false;
  env.ed->renameSymbol({0, 4}, "beta", [&](const Editor::RenameResult& r, const std::string& err) {
    called = true;
    CHECK_EQ(err, std::string("clangd not running"));
    CHECK_EQ(r.edits, 0u);
  });
  env.until([&] { return called; }, 2000);
  CHECK(called);
  CHECK(!env.ed->currentDocument()->dirty());
  // FormatSelection is clangd-only as well.
  env.key(Key('K'), ModCtrl);
  env.key(Key('F'), ModCtrl);
  CHECK(env.msg().find("Format needs clangd") != std::string::npos);
}

TEST(w8c_rename_across_files_with_clangd_through_control_r_and_enter) {
  SKIP_WITHOUT_CLANGD();
  const std::string header = "#pragma once\nint computeTotal(int a, int b);\n";
  const std::string impl = "#include \"util.h\"\nint computeTotal(int a, int b) { return a + b; }\n";
  const std::string mainSrc = "#include \"util.h\"\nint main() { return computeTotal(1, 2); }\n";
  Env env(
      [&](const fs::path& d) {
        writeFile(d / "src/util.h", header);
        writeFile(d / "src/util.cpp", impl);
        writeFile(d / "src/main.cpp", mainSrc);
      },
      /*clangd=*/true);
  REQUIRE(env.clangdReady());
  env.open("src/main.cpp");
  env.open("src/util.cpp");
  env.open("src/util.h");
  for (const char* f : {"src/main.cpp", "src/util.cpp", "src/util.h"})
    env.ed->semantic().waitForFile(env.dir / f, 60000);
  env.caret(1, 8);  // on computeTotal in the header
  CHECK(env.key(Key('R'), ModCtrl));
  REQUIRE(env.app->renameInput().isOpen());
  CHECK_EQ(env.app->renameInput().input()->text(), std::string("computeTotal"));
  env.type("computeSum");
  CHECK(env.key(Key::Enter));
  REQUIRE(env.until([&] { return env.msg().rfind("Renamed", 0) == 0 || env.msg().rfind("Rename failed", 0) == 0; }));
  CHECK_EQ(env.msg().rfind("Renamed", 0), 0u);
  Document* h = env.ed->document(env.dir / "src/util.h");
  Document* c = env.ed->document(env.dir / "src/util.cpp");
  REQUIRE(h != nullptr);
  REQUIRE(c != nullptr);
  CHECK_EQ(h->buffer().line(1), std::string("int computeSum(int a, int b);"));
  CHECK_EQ(c->buffer().line(1), std::string("int computeSum(int a, int b) { return a + b; }"));
  Document* m = env.ed->document(env.dir / "src/main.cpp");
  REQUIRE(m != nullptr);
  CHECK_EQ(m->buffer().line(1), std::string("int main() { return computeSum(1, 2); }"));
  CHECK(h->dirty());  // open documents are edited, not written
  CHECK(c->dirty());
  CHECK(m->dirty());
  CHECK_EQ(slurp(env.dir / "src/util.h"), header);
  CHECK_EQ(env.msg(), std::string("Renamed computeTotal to computeSum: 3 edits in 3 files"));
  // One undo step per open file.
  CHECK(h->undo());
  CHECK_EQ(h->buffer().text(), header);
  CHECK(!h->canUndo());
  CHECK(c->undo());
  CHECK_EQ(c->buffer().text(), impl);
  CHECK(m->undo());
  CHECK_EQ(m->buffer().text(), mainSrc);
}

// ---------------------------------------------------------------------------------------------------------------
// FindFunction
// ---------------------------------------------------------------------------------------------------------------

TEST(w8c_alt_m_lists_the_functions_defined_in_this_file_and_enter_jumps_with_history) {
  Env env([](const fs::path& d) {
    writeFile(d / "a.cpp",
              "int helper(int a);\n"               // 0 declaration only
              "int helper(int a) { return a; }\n"  // 1
              "int unusedGlobal = 3;\n"            // 2
              "\n"                                 // 3
              "int compute(int x) {\n"             // 4
              "  return helper(x);\n"              // 5
              "}\n" +                              // 6
                  std::string(15, '\n') +          // 7-21: far enough for the history not to merge the jump
                  "void tail() {}\n");             // 22
    writeFile(d / "b.cpp", "int fromOtherFile() { return 1; }\n");
  });
  env.open("a.cpp");
  env.caret(5, 3);
  const size_t base = env.ed->navigation().entries().size();
  CHECK(env.key(Key('M'), ModAlt));
  ListPopup* p = env.popup();
  REQUIRE(p != nullptr);
  env.until([&] { return p->rows().size() >= 3; }, 5000);
  std::vector<std::string> names;
  for (const ListRow& r : p->rows()) names.push_back(r.primary);
  CHECK(names == (std::vector<std::string>{"helper", "compute", "tail"}));  // definitions only, source order
  CHECK_EQ(p->rows()[0].detail, std::string(":2"));                         // the definition, not the declaration
  CHECK(p->rows()[0].dim);                                      // no clangd: the fast tier is shown greyed...
  CHECK(p->status().find("approximate") != std::string::npos);  // ...and says so
  // Typing filters.
  env.type("tai");
  REQUIRE_EQ(p->rows().size(), 1u);
  CHECK_EQ(p->rows()[0].primary, std::string("tail"));
  CHECK(env.key(Key::Enter));
  CHECK(env.popup() == nullptr);
  CHECK_EQ(env.ed->cursor().pos.line, 22);
  CHECK_EQ(env.curFile(), std::string("a.cpp"));
  CHECK(env.ed->navigation().entries().size() > base);  // a navigation jump
  CHECK(env.ed->back());
  CHECK_EQ(env.ed->cursor().pos.line, 5);
}

TEST(w8c_find_function_is_upgraded_by_clangd_and_keeps_the_selection) {
  SKIP_WITHOUT_CLANGD();
  Env env(
      [](const fs::path& d) {
        writeFile(d / "a.cpp",
                  "int helper(int a);\n"
                  "int helper(int a) { return a; }\n"
                  "struct S {\n"
                  "  void decl();\n"
                  "  int inl() { return 1; }\n"
                  "};\n"
                  "void S::decl() {}\n"
                  "int main() { return helper(1); }\n");
      },
      /*clangd=*/true);
  REQUIRE(env.clangdReady());
  env.open("a.cpp");
  env.ed->semantic().waitForFile(env.dir / "a.cpp", 60000);
  env.key(Key('M'), ModAlt);
  ListPopup* p = env.popup();
  REQUIRE(p != nullptr);
  REQUIRE(env.until([&] { return p && !p->rows().empty() && !p->rows()[0].dim; }, 20000));
  std::vector<std::string> names;
  for (const ListRow& r : p->rows()) names.push_back(r.secondary.empty() ? r.primary : r.secondary + "::" + r.primary);
  CHECK(std::find(names.begin(), names.end(), "helper") != names.end());
  CHECK(std::find(names.begin(), names.end(), "main") != names.end());
  CHECK(std::find(names.begin(), names.end(), "S::inl") != names.end());
  CHECK(std::find(names.begin(), names.end(), "S::decl") != names.end());  // the out-of-line definition only
  CHECK_EQ(std::count(names.begin(), names.end(), "S::decl"), 1);
  size_t helpers = 0;
  for (const ListRow& r : p->rows()) helpers += r.primary == "helper";
  CHECK_EQ(helpers, 1u);       // the declaration on line 1 is not listed
  CHECK(p->status().empty());  // authoritative: no badge
  env.key(Key::Escape);
}

// ---------------------------------------------------------------------------------------------------------------
// GotoSymbolDefinitionUnderMouse
// ---------------------------------------------------------------------------------------------------------------

TEST(w8c_middle_click_on_an_identifier_jumps_to_its_definition_and_not_to_the_caret_symbol) {
  Env env([](const fs::path& d) {
    writeFile(d / "main.cpp", "int main() { return helper(1) + other(2); }\n");
    writeFile(d / "util.cpp", "int helper(int a) { return a; }\n\nint other(int a) { return a + 1; }\n");
  });
  env.open("main.cpp");
  env.caret(0, 0);  // the caret is not on either call
  TextView* v = env.view();
  REQUIRE(v != nullptr);
  const size_t base = env.ed->navigation().entries().size();
  const RectF r = v->caretRect({0, 33});  // inside `other`
  env.send(mouseEv(InputType::MouseMove, MouseButton::Left, r.x + 3, r.y + 5));
  CHECK(env.send(mouseEv(InputType::MouseDown, MouseButton::Middle, r.x + 3, r.y + 5)));
  env.send(mouseEv(InputType::MouseUp, MouseButton::Middle, r.x + 3, r.y + 5));
  env.until([&] { return env.curFile() == "util.cpp"; }, 5000);
  CHECK_EQ(env.curFile(), std::string("util.cpp"));
  CHECK_EQ(env.ed->cursor().pos.line, 2);  // `other`, the symbol under the mouse
  CHECK(env.ed->navigation().entries().size() > base);
  CHECK(env.ed->back());
  CHECK_EQ(env.curFile(), std::string("main.cpp"));
  CHECK_EQ(env.ed->cursor().pos.line, 0);
  CHECK_EQ(env.ed->cursor().pos.column, 0);
}

TEST(w8c_middle_click_on_a_tab_still_closes_it_and_blank_text_does_not_jump) {
  Env env([](const fs::path& d) {
    writeFile(d / "main.cpp", "int main() { return helper(1); }\n");
    writeFile(d / "util.cpp", "int helper(int a) { return a; }\n");
  });
  env.open("main.cpp");
  env.open("util.cpp");
  Panel* panel = env.bench->activePanel();
  REQUIRE(panel != nullptr);
  const int mainTab = [&] {
    for (const auto& t : panel->tabs().tabs())
      if (env.bench->tabPath(t.id).filename() == "main.cpp") return t.id;
    return -1;
  }();
  REQUIRE(mainTab >= 0);
  // Blank space at the end of a line of the active view: the click goes to the widgets, nothing jumps.
  env.open("main.cpp");
  TextView* v = env.view();
  REQUIRE(v != nullptr);
  const RectF end = v->caretRect({0, 32});
  const size_t base = env.ed->navigation().entries().size();
  env.send(mouseEv(InputType::MouseDown, MouseButton::Middle, end.x + 100, end.y + 5));
  env.send(mouseEv(InputType::MouseUp, MouseButton::Middle, end.x + 100, end.y + 5));
  CHECK_EQ(env.curFile(), std::string("main.cpp"));
  CHECK_EQ(env.ed->navigation().entries().size(), base);
  // The tab strip: middle click closes the tab.
  const RectF tab = panel->tabs().tabRect(mainTab);
  const float x = tab.x + tab.w / 2, y = tab.y + tab.h / 2;
  CHECK(env.send(mouseEv(InputType::MouseDown, MouseButton::Middle, x, y)));
  env.send(mouseEv(InputType::MouseUp, MouseButton::Middle, x, y));
  bool stillOpen = false;
  for (const auto& t : panel->tabs().tabs()) stillOpen |= env.bench->tabPath(t.id).filename() == "main.cpp";
  CHECK(!stillOpen);
  CHECK_EQ(env.ed->navigation().entries().size(), base);
}

// ---------------------------------------------------------------------------------------------------------------
// FormatSelection
// ---------------------------------------------------------------------------------------------------------------

TEST(w8c_format_selection_changes_only_the_selected_lines_with_clangd) {
  SKIP_WITHOUT_CLANGD();
  const std::string src =
      "int   add(int a,int b){return a+b;}\n"   // 0 untouched
      "int   sub(int a,int b){return a-b;}\n"   // 1 selected
      "int   mul(int a,int b){return a*b;}\n"   // 2 selected
      "int   dvd(int a,int b){return a/b;}\n";  // 3 untouched
  Env env(
      [&](const fs::path& d) {
        writeFile(d / ".clang-format", "BasedOnStyle: LLVM\nColumnLimit: 80\n");
        writeFile(d / "a.cpp", src);
      },
      /*clangd=*/true);
  REQUIRE(env.clangdReady());
  env.open("a.cpp");
  env.ed->semantic().waitForFile(env.dir / "a.cpp", 60000);
  Document* d = env.ed->currentDocument();
  env.ed->setSelections({Selection{{1, 0}, {2, 36}}});
  env.frame();
  CHECK(env.key(Key('K'), ModCtrl));
  CHECK(env.key(Key('F'), ModCtrl));
  REQUIRE(env.until([&] { return d->buffer().line(1) != "int   sub(int a,int b){return a-b;}"; }, 20000));
  CHECK_EQ(d->buffer().line(0), std::string("int   add(int a,int b){return a+b;}"));
  CHECK(d->buffer().line(1).find("int sub(int a, int b)") != std::string::npos);
  CHECK(d->buffer().text().find("int mul(int a, int b)") != std::string::npos);
  CHECK(d->buffer().text().find("int   dvd(int a,int b){return a/b;}") != std::string::npos);
  CHECK(env.msg().find("Formatted the selection") != std::string::npos);
  CHECK(d->undo());  // one undo step
  CHECK_EQ(d->buffer().text(), src);
  CHECK(!d->canUndo());

  // No selection: the scope around the caret (here the function body block), not the whole file.
  const std::string src2 =
      "int   one(int a,int b){return a+b;}\n"
      "void f() {\n"
      "  int   x=1;\n"
      "  if(x){x=2;}\n"
      "}\n"
      "int   two(int a,int b){return a+b;}\n";
  d->replace({{0, 0}, d->buffer().endPosition()}, src2);
  env.frame();
  env.caret(2, 4);
  env.key(Key('K'), ModCtrl);
  env.key(Key('F'), ModCtrl);
  REQUIRE(env.until([&] { return d->buffer().text().find("int x = 1;") != std::string::npos; }, 20000));
  CHECK_EQ(d->buffer().line(0), std::string("int   one(int a,int b){return a+b;}"));
  CHECK_EQ(d->buffer().line(d->buffer().lineCount() - 2), std::string("int   two(int a,int b){return a+b;}"));
  CHECK(env.msg().find("Formatted the scope") != std::string::npos);
}

// ---------------------------------------------------------------------------------------------------------------
// W12-CMD: AutocompleteUp / Down / PageUp / PageDown are key-map commands that share their keys with the caret
// ---------------------------------------------------------------------------------------------------------------

namespace {
void openList(Env& env, int items) {
  env.key(Key::Space, ModCtrl);
  std::vector<LspCompletionItem> v;
  for (int i = 0; i < items; ++i) {
    LspCompletionItem it;
    it.label = it.insertText = "item" + std::to_string(100 + i);
    v.push_back(it);
  }
  env.completions.back()(v, "");
  env.frame();
}
}  // namespace

TEST(w12_completion_keys_move_the_list_when_open_and_the_caret_when_closed) {
  Env env([](const fs::path& d) { writeFile(d / "a.cpp", "aaa\nbbb\nccc\nddd\n"); });
  env.fakeUi();
  env.open("a.cpp");
  env.caret(1, 1);
  // Closed: Up / Down / PageDown are the caret commands and say nothing.
  env.key(Key::Down);
  CHECK(env.ed->cursor().pos == (Position{2, 1}));
  env.key(Key::Up);
  CHECK(env.ed->cursor().pos == (Position{1, 1}));
  env.key(Key::PageDown);
  CHECK(env.ed->cursor().pos.line > 1);
  CHECK(env.msg().find("not implemented") == std::string::npos);
  env.caret(1, 0);
  openList(env, 20);
  REQUIRE(env.app->completion().isOpen());
  const Position at = env.ed->cursor().pos;
  env.key(Key::Down);
  env.key(Key::Down);
  CHECK_EQ(env.app->completion().selected(), 2);
  env.key(Key::Up);
  CHECK_EQ(env.app->completion().selected(), 1);
  env.key(Key::PageDown);
  const int paged = env.app->completion().selected();
  CHECK(paged > 2);
  env.key(Key::PageUp);
  CHECK_EQ(env.app->completion().selected(), 1);
  env.key(Key::PageUp);
  CHECK_EQ(env.app->completion().selected(), 0);
  for (int i = 0; i < 5; ++i) env.key(Key::PageDown);
  CHECK_EQ(env.app->completion().selected(), 19);  // clamped
  CHECK(env.ed->cursor().pos == at);               // the caret never moved
  CHECK(env.msg().find("not implemented") == std::string::npos);
  // Shift+Down is its own chord (SelectDown): it extends the selection and the list keeps its row.
  env.key(Key::Down, ModShift);
  CHECK(env.app->completion().selected() == 19 || !env.app->completion().isOpen());
  CHECK(env.ed->cursor().pos != at);
}

TEST(w12_remapping_autocomplete_down_moves_the_list_on_the_new_key_only) {
  Env env([](const fs::path& d) { writeFile(d / "a.cpp", "aaa\nbbb\nccc\n"); });
  env.fakeUi();
  env.open("a.cpp");
  std::vector<KeyBindingDiag> diags;
  parseKeyBindings("Down: MoveCursorDown\nControl J: AutocompleteDown\n", "user", env.app->keys(), diags);
  env.caret(1, 0);
  openList(env, 5);
  REQUIRE(env.app->completion().isOpen());
  env.key(Key('J'), ModCtrl);
  CHECK_EQ(env.app->completion().selected(), 1);
  env.key(Key('J'), ModCtrl);
  CHECK_EQ(env.app->completion().selected(), 2);
  // Plain Down is no longer a list key: it moves the caret (and with it the list closes or stays; the row is
  // unchanged).
  env.key(Key::Down);
  CHECK(env.ed->cursor().pos.line == 2);
  CHECK(!env.app->completion().isOpen() || env.app->completion().selected() == 2);
  // Closed list: Control J does not run the command and does not complain.
  env.app->completion().close();
  env.key(Key('J'), ModCtrl);
  CHECK(env.msg().find("not implemented") == std::string::npos);
}

TEST(w12_completion_keys_in_the_rename_box_and_a_picker_stay_with_their_widgets) {
  Env env([](const fs::path& d) { writeFile(d / "a.cpp", "int alpha;\nint beta;\n"); });
  env.fakeUi();
  env.open("a.cpp");
  env.caret(0, 5);
  env.key(Key('R'), ModCtrl);
  REQUIRE(env.app->renameInput().isOpen());
  for (Key k : {Key::Up, Key::Down, Key::PageUp, Key::PageDown}) env.key(k);
  CHECK(env.msg().find("not implemented") == std::string::npos);
  CHECK(env.app->renameInput().isOpen());
  CHECK(env.ed->cursor().pos == (Position{0, 5}));
  env.key(Key::Escape);
  CHECK(!env.app->renameInput().isOpen());
  env.key(Key('P'), ModCtrl);  // Control P: the file picker (modal)
  if (env.popup() || env.bench->modal()) {
    env.key(Key::Down);
    CHECK(env.msg().find("not implemented") == std::string::npos);
    CHECK(!env.app->completion().isOpen());
  }
}

TEST(w12_insert_next_prev_autocomplete_stay_unimplemented_without_a_source) {
  Env env([](const fs::path& d) { writeFile(d / "a.cpp", "abc\n"); });
  env.open("a.cpp");
  env.key(Key::F14);
  CHECK_EQ(env.msg(), std::string("not implemented yet: InsertNextAutocomplete"));
  env.key(Key::F14, ModShift);
  CHECK_EQ(env.msg(), std::string("not implemented yet: InsertPrevAutocomplete"));
}
