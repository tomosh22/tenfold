// W8a: the 10x editing commands through the UI. Every command is run by pressing its 10x default key (the real embedded
// key bindings), against a recording renderer and the mock font (8 px cells, 14 px lines).
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>

#include "../text/text_test_util.h"
#include "ui/commands/CommandNames.h"
#include "ui/gfx/TextRenderer.h"
#include "ui/platform/Clipboard.h"
#include "ui/settings/KeyBindings.h"
#include "ui/settings/Settings.h"
#include "ui/shell/EditorApp.h"
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

struct FakeClipboard {
  std::string value;
  FakeClipboard() {
    setClipboardBackend({[this] { return value; },
                         [this](const std::string& s) {
                           value = s;
                           return true;
                         }});
  }
  ~FakeClipboard() { setClipboardBackend({}); }
};

InputEvent keyEv(Key k, uint8_t mods = 0) {
  InputEvent e;
  e.type = InputType::KeyDown;
  e.key = k;
  e.mods = mods;
  return e;
}

constexpr uint8_t kCS = ModCtrl | ModShift;

struct Env {
  fs::path dir;
  std::unique_ptr<Editor> ed;
  std::unique_ptr<gfx::TextRenderer> text;
  NullRenderer nr;
  std::unique_ptr<Workbench> bench;
  std::unique_ptr<EditorApp> app;
  int64_t now = 1000;

  explicit Env(const std::string& aText = "int alpha = 1;\nint beta = alpha;\n// Alpha\nint gamma;\nalpha();\n") {
    static int n = 0;
    dir =
        uitest::tempRoot() / ("tf_w8a_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                              "_" + std::to_string(n++));
    fs::create_directories(dir);
    std::ofstream(dir / "a.cpp", std::ios::binary) << aText;
    std::ofstream(dir / "b.cpp", std::ios::binary) << "b0\nb1\n";
    gfx::FontStack stack;
    stack.add(std::make_shared<tftext::MockFont>(
        U"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ._-:,;/()[]{}<>+*&#?=!\"'\\|"));
    text = gfx::TextRenderer::fromFonts(std::move(stack));
    ed = std::make_unique<Editor>(dir);
    ed->editing().indentUnit = "    ";
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
  TextView* view() { return dynamic_cast<TextView*>(bench->focused()); }
  Document* doc() { return ed->currentDocument(); }
  std::string txt() { return doc()->buffer().text(); }
  Selection sel(size_t i = 0) { return ed->selections()[i]; }
  void set(std::vector<Selection> s) { ed->setSelections(std::move(s)); }
};

Selection C(int l, int c) { return {{l, c}, {l, c}}; }
Selection S(int l1, int c1, int l2, int c2) { return {{l1, c1}, {l2, c2}}; }

}  // namespace

TEST(w8a_names_resolve_and_defaults_bind_to_them) {
  const char* names[] = {"SaveAll",
                         "CutLine",
                         "MakeSelectionUppercase",
                         "MakeSelectionLowercase",
                         "Commentline",
                         "Uncommentline",
                         "ToggleComment",
                         "DeleteToEndOfLine",
                         "InsertLine",
                         "InsertLineAfter",
                         "SelectCurrentWord",
                         "SelectCurrentScope",
                         "SelectCurrentBracketScope",
                         "MoveToMatchingBracket",
                         "ToggleWhitespace",
                         "ScrollOneLineUp",
                         "ScrollOneLineDown",
                         "FindInFilePrevCurrentWord",
                         "FindInFileNextCurrentWord"};
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
  CHECK_EQ(resolveCommandName("ToggleComment").id, std::string("Editor.ToggleComment"));
  // The default key file names none of them as unimplemented.
  KeyMap map;
  std::vector<KeyBindingDiag> diags;
  parseKeyBindings(defaultKeyBindingsText(), "defaults", map, diags);
  CHECK_EQ(diags.size(), size_t(0));
}

TEST(w8a_case_keys) {
  Env env;
  env.set({S(0, 4, 0, 9)});
  env.key(Key('U'), kCS);
  CHECK_EQ(env.txt().substr(0, 14), std::string("int ALPHA = 1;"));
  CHECK(env.sel() == S(0, 4, 0, 9));
  env.key(Key('U'), ModCtrl);
  CHECK_EQ(env.txt().substr(0, 14), std::string("int alpha = 1;"));
  env.key(Key('Z'), ModCtrl);  // one undo step each
  CHECK_EQ(env.txt().substr(0, 14), std::string("int ALPHA = 1;"));
}

TEST(w8a_cut_line_key_fills_the_clipboard) {
  FakeClipboard clip;
  Env env;
  env.set({C(1, 3)});
  env.key(Key::Delete, ModShift);
  CHECK_EQ(clip.value, std::string("int beta = alpha;\n"));
  CHECK_EQ(env.doc()->buffer().line(1), std::string("// Alpha"));
  CHECK_EQ(env.doc()->buffer().lineCount(), 5);
  env.key(Key('Z'), ModCtrl);
  CHECK_EQ(env.doc()->buffer().line(1), std::string("int beta = alpha;"));
}

TEST(w8a_comment_uncomment_and_toggle_keys) {
  Env env("a\nb\nc\n");
  env.set({S(0, 0, 1, 1)});
  env.key(Key('K'), ModCtrl);  // Control K, Control C
  env.key(Key('C'), ModCtrl);
  CHECK_EQ(env.txt(), std::string("// a\n// b\nc\n"));
  env.key(Key('K'), ModCtrl);  // Control K, Control U
  env.key(Key('U'), ModCtrl);
  CHECK_EQ(env.txt(), std::string("a\nb\nc\n"));
  env.set({C(2, 0)});
  env.key(Key('/'), kCS);  // ToggleComment, no selection: a line comment
  CHECK_EQ(env.txt(), std::string("a\nb\n// c\n"));
  env.key(Key('/'), kCS);
  CHECK_EQ(env.txt(), std::string("a\nb\nc\n"));
  env.set({S(0, 0, 0, 1)});  // a selection with the default setting (true): line comments
  env.key(Key('/'), kCS);
  CHECK_EQ(env.txt(), std::string("// a\nb\nc\n"));
  env.key(Key('/'), kCS);
}

TEST(w8a_comment_block_setting_reaches_toggle_comment) {
  Env env("a b\nc\n");
  SettingsService svc({.workspaceRoot = env.dir, .userDir = {}});
  svc.settings().loadTexts("", "");
  env.app->attachSettings(svc);
  CHECK(env.ed->editing().commentBlockUsesLineComments);
  svc.settings().loadTexts("", "editor.commentBlockUsesLineComments = false\n");
  CHECK(!env.ed->editing().commentBlockUsesLineComments);
  env.set({S(0, 2, 0, 3)});
  env.key(Key('/'), kCS);
  CHECK_EQ(env.txt(), std::string("a /*b*/\nc\n"));
  env.key(Key('/'), kCS);
  CHECK_EQ(env.txt(), std::string("a b\nc\n"));
  env.set({C(1, 0)});  // no selection: still a line comment
  env.key(Key('/'), kCS);
  CHECK_EQ(env.txt(), std::string("a b\n// c\n"));
}

TEST(w8a_delete_to_end_of_line_and_insert_line_keys) {
  Env env("    abc def\nxyz\n");
  env.set({C(0, 7)});
  env.key(Key('L'), ModCtrl);
  CHECK_EQ(env.doc()->buffer().line(0), std::string("    abc"));
  env.key(Key::Enter, ModCtrl);  // InsertLine: above, same indentation, caret on the new line
  CHECK_EQ(env.txt(), std::string("    \n    abc\nxyz\n"));
  CHECK(env.sel().head == (Position{0, 4}));
  env.set({C(1, 2)});
  env.key(Key::Enter, kCS);  // InsertLineAfter
  CHECK_EQ(env.txt(), std::string("    \n    abc\n    \nxyz\n"));
  CHECK(env.sel().head == (Position{2, 4}));
}

TEST(w8a_selection_keys) {
  Env env("a {\n  b { c }\n  d\n}\nfoo_bar baz\n");
  env.set({C(4, 2)});
  env.key(Key('W'), ModCtrl);  // SelectCurrentWord
  CHECK(env.sel() == S(4, 0, 4, 7));
  env.set({C(1, 6)});
  env.key(Key('='), ModShift | ModAlt);  // SelectCurrentScope
  CHECK(env.sel() == S(1, 5, 1, 8));
  env.key(Key('='), ModShift | ModAlt);
  CHECK(env.sel() == S(1, 4, 1, 9));
  env.key(Key('='), ModShift | ModAlt);
  CHECK(env.sel() == S(0, 3, 3, 0));
  env.set({C(1, 6)});
  env.key(Key('M'), kCS);  // SelectCurrentBracketScope
  CHECK(env.sel() == S(1, 5, 1, 8));
  CHECK_EQ(env.txt(), std::string("a {\n  b { c }\n  d\n}\nfoo_bar baz\n"));
}

TEST(w8a_move_to_matching_bracket_key) {
  Env env("f(a, [b])\n");
  env.set({C(0, 1)});
  env.key(Key('\\'), kCS);
  CHECK(env.sel().head == (Position{0, 8}));
  env.key(Key('\\'), kCS);
  CHECK(env.sel().head == (Position{0, 1}));
  env.set({C(0, 0)});  // not at a bracket: the command fails and says so, the caret stays
  env.key(Key('\\'), kCS);
  CHECK(env.sel().head == (Position{0, 0}));
}

TEST(w8a_save_all_keys) {
  Env env;
  Document* a = env.doc();
  Document* b = env.ed->open("b.cpp");
  REQUIRE(a && b);
  for (int round = 0; round < 2; ++round) {
    env.ed->gotoLocation({a->path(), {0, 0}});
    env.ed->insertText("x");
    env.ed->gotoLocation({b->path(), {0, 0}});
    env.ed->insertText("y");
    CHECK(a->dirty());
    CHECK(b->dirty());
    if (round == 0) {  // Control S, Control S
      env.key(Key('S'), ModCtrl);
      env.key(Key('S'), ModCtrl);
    } else {
      env.key(Key('S'), kCS);
    }
    CHECK(!a->dirty());
    CHECK(!b->dirty());
  }
}

TEST(w8a_toggle_whitespace_key_lasts_for_the_session) {
  Env env;
  CHECK(!env.view()->settings().showWhitespace);
  env.key(Key('W'), kCS);
  CHECK(env.view()->settings().showWhitespace);
  SettingsService svc({.workspaceRoot = env.dir, .userDir = {}});
  svc.settings().loadTexts("", "editor.tabWidth = 2\n");  // an unrelated change keeps the toggle
  env.app->attachSettings(svc);
  CHECK(env.view()->settings().showWhitespace);
  env.key(Key('W'), kCS);
  CHECK(!env.view()->settings().showWhitespace);
  env.key(Key('W'), kCS);
  env.key(Key('W'), kCS);
  CHECK(!env.view()->settings().showWhitespace);                   // toggled off for the session
  svc.settings().loadTexts("", "editor.showWhitespace = true\n");  // the settings file speaks again
  CHECK(env.view()->settings().showWhitespace);
}

TEST(w8a_scroll_one_line_keeps_the_caret_until_it_would_leave) {
  std::string big;
  for (int i = 0; i < 200; ++i) big += "line " + std::to_string(i) + "\n";
  Env env(big);
  TextView* v = env.view();
  REQUIRE(v);
  env.set({C(5, 2)});
  env.frame();
  CHECK_EQ(v->viewport().targetY(), 0.0f);
  const float lh = 14;
  env.key(Key::Down, ModCtrl);
  CHECK_EQ(v->viewport().targetY(), lh);
  CHECK(env.sel().head == (Position{5, 2}));  // the caret stayed
  env.key(Key::Down, ModCtrl);
  env.key(Key::Down, ModCtrl);
  CHECK_EQ(v->viewport().targetY(), 3 * lh);
  CHECK(env.sel().head == (Position{5, 2}));
  for (int i = 0; i < 4; ++i) env.key(Key::Down, ModCtrl);  // 7 lines scrolled: line 5 left the view
  CHECK_EQ(v->viewport().targetY(), 7 * lh);
  CHECK_EQ(env.sel().head.line, 7);  // moved to the first visible line, same column
  CHECK_EQ(env.sel().head.column, 2);
  env.key(Key::Up, ModCtrl);
  CHECK_EQ(v->viewport().targetY(), 6 * lh);
  CHECK_EQ(env.sel().head.line, 7);
  CHECK_EQ(env.txt().size(), big.size());  // no edit
  // The other end: scrolling up pushes the caret off the bottom row.
  env.set({C(0, 0)});
  for (int i = 0; i < 6; ++i) env.key(Key::Up, ModCtrl);
  CHECK_EQ(v->viewport().targetY(), 0.0f);
  env.set({C(40, 0)});
  env.frame();
  const float top = v->viewport().targetY();
  env.key(Key::Up, ModCtrl);
  CHECK(v->viewport().targetY() < top || top == 0.0f);
  const int visible = v->visibleLineCount();
  for (int i = 0; i < 100 && env.sel().head.line > 0; ++i) env.key(Key::Up, ModCtrl);
  CHECK(env.sel().head.line <= visible);
}

TEST(w8a_find_current_word_keys) {
  Env env;
  env.set({C(0, 6)});  // inside the first "alpha"
  env.key(Key::F3, ModCtrl);
  CHECK(env.sel() == S(1, 11, 1, 16));  // case-sensitive whole word: "Alpha" in the comment is skipped
  CHECK_EQ(env.app->findReplace().query(), std::string("alpha"));
  env.key(Key::F3, ModCtrl);
  CHECK(env.sel() == S(4, 0, 4, 5));
  env.key(Key::F3, ModCtrl);  // wraps
  CHECK(env.sel() == S(0, 4, 0, 9));
  env.key(Key::F3, kCS);  // previous
  CHECK(env.sel() == S(4, 0, 4, 5));
  env.set({C(3, 0)});  // "int" has two other occurrences
  env.key(Key::F3, kCS);
  CHECK(env.sel() == S(1, 0, 1, 3));
  env.set({S(0, 4, 0, 7)});  // a selection is searched as text, not as a whole word
  env.key(Key::F3, ModCtrl);
  CHECK_EQ(env.app->findReplace().query(), std::string("alp"));
  CHECK(env.sel() == S(1, 11, 1, 14));
  env.set({C(3, 3)});  // between "int" and a blank: the word before the caret
  env.key(Key::F3, ModCtrl);
  CHECK_EQ(env.app->findReplace().query(), std::string("int"));
}
