// B5a: InputLine, ListPopup (+ modal/top layer in RootWidget), symbol search, open-file search and the multi-result
// navigation picker. No GL except the golden: a recording renderer, the mock font (8 px cells, 14 px lines) and
// synthetic events driven through EditorApp with the real default key bindings.
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>

#include "../text/text_test_util.h"
#include "semantic/FastIndex.h"
#include "ui/features/FileSearch.h"
#include "ui/gfx/PngWrite.h"
#include "ui/gfx/TextRenderer.h"
#include "ui/platform/Clipboard.h"
#include "ui/settings/Settings.h"
#include "ui/shell/EditorApp.h"
#include "ui/widgets/InputLine.h"
#include "ui/widgets/ListPopup.h"
#include "ui_test.h"

using namespace tf;
using namespace tf::ui;
namespace fs = std::filesystem;

namespace {

// Logs the draw calls in order: 'R' rect, 'G' glyph run.
struct Recorder : gfx::Renderer {
  std::string log;
  int frames = 0;
  std::vector<gfx::RectF> rects;
  void beginFrame(int, int, gfx::Color) override {
    log.clear();
    rects.clear();
    ++frames;
  }
  void drawRect(const gfx::RectF& r, gfx::Color) override {
    if (log.empty() || log.back() != 'R') log += 'R';
    rects.push_back(r);
  }
  void drawLine(float, float, float, float, float, gfx::Color) override {}
  void drawGlyphs(gfx::TextureId, const gfx::GlyphQuad*, int, gfx::Color) override {
    if (log.empty() || log.back() != 'G') log += 'G';
  }
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
      U"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ._-:,;/()[]{}<>+*&#?=ƒ…éö"));
  return gfx::TextRenderer::fromFonts(std::move(stack));
}

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
InputEvent textEv(const std::string& s) {
  InputEvent e;
  e.type = InputType::Text;
  e.text = s;
  return e;
}
InputEvent mouseEv(InputType t, float x, float y, int clicks = 1) {
  InputEvent e;
  e.type = t;
  e.x = x;
  e.y = y;
  e.clicks = clicks;
  return e;
}
InputEvent click(float x, float y, int clicks = 1) { return mouseEv(InputType::MouseDown, x, y, clicks); }

// ---- widget-level fixture: a bare root with a focusable InputLine "below" the popups ----
struct WidgetEnv {
  std::unique_ptr<gfx::TextRenderer> text = mockText();
  Recorder rec;
  RootWidget root{*text};
  InputLine* base = nullptr;
  WidgetEnv() {
    base = static_cast<InputLine*>(root.addChild(std::make_unique<InputLine>()));
    base->setText("base");
    base->setFocus();
    render();
  }
  void render() { root.render(rec, 800, 600); }
  bool send(const InputEvent& e) {
    const bool r = root.dispatch(e);
    render();
    return r;
  }
  void type(const std::string& s) { send(textEv(s)); }
};

std::vector<ListRow> numberedRows(int n) {
  std::vector<ListRow> rows;
  for (int i = 0; i < n; ++i) {
    ListRow r;
    r.key = "k" + std::to_string(i);
    r.primary = "row" + std::to_string(i);
    rows.push_back(r);
  }
  return rows;
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// InputLine
// ---------------------------------------------------------------------------------------------------------------

TEST(input_line_editing_utf8_words_selection_clipboard) {
  FakeClipboard cb;
  WidgetEnv env;
  InputLine& in = *env.base;
  in.setText("");
  std::vector<std::string> changes;
  in.onChanged([&](const std::string& s) { changes.push_back(s); });
  env.type("h\xC3\xA9llo w\xC3\xB6rld");  // héllo wörld
  CHECK_EQ(in.text(), std::string("h\xC3\xA9llo w\xC3\xB6rld"));
  CHECK_EQ(changes.size(), size_t(1));
  // Left/Right step over whole code points; Backspace deletes one code point, never half of one.
  env.send(keyEv(Key::Left));
  env.send(keyEv(Key::Left));  // between 'r' and 'l'
  env.send(keyEv(Key::Backspace));
  CHECK_EQ(in.text(), std::string("h\xC3\xA9llo w\xC3\xB6ld"));
  env.send(keyEv(Key::Left));  // before the two-byte o-umlaut
  env.send(keyEv(Key::Delete));
  CHECK_EQ(in.text(), std::string("h\xC3\xA9llo wld"));
  env.send(keyEv(Key::Left));
  env.send(keyEv(Key::Backspace));  // the blank
  CHECK_EQ(in.text(), std::string("h\xC3\xA9llowld"));
  in.setText("h\xC3\xA9llo ld");
  // Home / End / Delete.
  env.send(keyEv(Key::Home));
  CHECK_EQ(in.caret(), size_t(0));
  env.send(keyEv(Key::Delete));
  CHECK_EQ(in.text(), std::string("\xC3\xA9llo ld"));
  env.send(keyEv(Key::Right));
  CHECK_EQ(in.caret(), size_t(2));  // one code point = two bytes
  env.send(keyEv(Key::End));
  CHECK_EQ(in.caret(), in.text().size());
  // Ctrl+Backspace deletes the previous word, then the blank and the word before.
  env.send(keyEv(Key::Backspace, ModCtrl));
  CHECK_EQ(in.text(), std::string("\xC3\xA9llo "));
  env.send(keyEv(Key::Backspace, ModCtrl));
  CHECK_EQ(in.text(), std::string(""));
  // Ctrl+Left/Right move by words; Ctrl+Delete deletes to the end of the word.
  in.setText("foo::bar baz");
  env.send(keyEv(Key::Left, ModCtrl));
  CHECK_EQ(in.caret(), size_t(9));
  env.send(keyEv(Key::Left, ModCtrl));
  CHECK_EQ(in.caret(), size_t(5));
  env.send(keyEv(Key::Delete, ModCtrl));
  CHECK_EQ(in.text(), std::string("foo:: baz"));
  // Selection: Shift+Home selects, typing replaces it, Ctrl+A/C/X/V use the clipboard.
  env.send(keyEv(Key::Home, ModShift));
  CHECK_EQ(in.selectedText(), std::string("foo::"));
  env.type("X");
  CHECK_EQ(in.text(), std::string("X baz"));
  env.send(keyEv(Key('A'), ModCtrl));
  env.send(keyEv(Key('C'), ModCtrl));
  CHECK_EQ(cb.value, std::string("X baz"));
  env.send(keyEv(Key('X'), ModCtrl));
  CHECK_EQ(in.text(), std::string(""));
  cb.value = "line1\nline2\r\n";
  env.send(keyEv(Key('V'), ModCtrl));
  CHECK_EQ(in.text(), std::string("line1 line2  "));  // line breaks become blanks: the field stays one line
  // Keys the field does not own bubble (the popup needs them).
  CHECK(!env.send(keyEv(Key::Up)));
  CHECK(!env.send(keyEv(Key::Enter)));
  CHECK(!env.send(keyEv(Key::Escape)));
}

TEST(input_line_mouse_caret_drag_and_word_select) {
  WidgetEnv env;
  InputLine& in = *env.base;
  in.setText("alpha beta");
  in.layout({0, 0, 400, 22});
  const float pad = env.root.rootTheme().padding;
  // 8 px cells: a click in the middle of the 3rd cell lands before/after 'p' depending on the half.
  env.send(click(pad + 8 * 3 + 1, 10));
  CHECK_EQ(in.caret(), size_t(3));
  env.send(mouseEv(InputType::MouseMove, pad + 8 * 7 + 1, 10));  // drag (captured)
  CHECK_EQ(in.selectedText(), std::string("ha b"));
  env.send(mouseEv(InputType::MouseUp, pad + 8 * 7, 10));
  env.send(click(pad + 8 * 7, 10, 2));  // double click: the word under the pointer
  CHECK_EQ(in.selectedText(), std::string("beta"));
  env.send(click(pad + 8, 10, 3));
  CHECK_EQ(in.selectedText(), std::string("alpha beta"));
}

// ---------------------------------------------------------------------------------------------------------------
// ListPopup + modal
// ---------------------------------------------------------------------------------------------------------------

TEST(list_popup_keyboard_navigation_scroll_accept_escape) {
  WidgetEnv env;
  auto own = std::make_unique<ListPopup>(true);
  own->setMaxVisibleRows(5);
  std::vector<std::string> queries;
  std::vector<int> accepted, selections;
  bool closed = false;
  own->onQuery([&](const std::string& q) { queries.push_back(q); });
  own->onAccept([&](int r) { accepted.push_back(r); });
  own->onSelection([&](int r) { selections.push_back(r); });
  own->onClosed([&] { closed = true; });
  ListPopup* p = ListPopup::open(env.root, std::move(own));
  p->setRows(numberedRows(30));
  env.render();
  CHECK(env.root.modal() == p);
  CHECK(env.root.focused() == p->input());
  CHECK_EQ(p->selected(), 0);

  // Typing goes to the input and fires onQuery; the base field behind the popup gets nothing.
  env.type("ab");
  CHECK_EQ(p->input()->text(), std::string("ab"));
  CHECK_EQ(env.base->text(), std::string("base"));
  CHECK_EQ(queries.size(), size_t(1));

  env.send(keyEv(Key::Down));
  env.send(keyEv(Key::Down));
  CHECK_EQ(p->selected(), 2);
  env.send(keyEv(Key::Up));
  CHECK_EQ(p->selected(), 1);
  env.send(keyEv(Key::Up));
  env.send(keyEv(Key::Up));  // clamps at the first row
  CHECK_EQ(p->selected(), 0);
  env.send(keyEv(Key::PageDown));
  CHECK_EQ(p->selected(), 5);
  CHECK_EQ(p->firstRow(), 1);  // the selection scrolled into view
  env.send(keyEv(Key::PageDown));
  CHECK_EQ(p->selected(), 10);
  CHECK_EQ(p->firstRow(), 6);
  env.send(keyEv(Key::PageUp));
  CHECK_EQ(p->selected(), 5);
  env.send(keyEv(Key::End, ModCtrl));
  CHECK_EQ(p->selected(), 29);
  CHECK_EQ(p->firstRow(), 25);
  env.send(keyEv(Key::Home, ModCtrl));
  CHECK_EQ(p->selected(), 0);
  CHECK_EQ(p->firstRow(), 0);
  // Plain Home/End belong to the input's caret while there is an input.
  env.send(keyEv(Key::End));
  CHECK_EQ(p->selected(), 0);
  CHECK_EQ(p->input()->caret(), size_t(2));

  // Wheel scrolls without moving the selection.
  InputEvent w;
  w.type = InputType::Wheel;
  w.x = p->popupRect().x + 20;
  w.y = p->listRect().y + 5;
  w.dy = -2;  // down
  env.send(w);
  CHECK_EQ(p->firstRow(), 6);
  CHECK_EQ(p->selected(), 0);

  // Hover highlights the row under the pointer; a click on it accepts and closes.
  const RectF r3 = p->rowRect(8);
  env.send(mouseEv(InputType::MouseMove, r3.x + 10, r3.y + 3));
  CHECK_EQ(p->hoverRow(), 8);
  env.send(click(r3.x + 10, r3.y + 3));
  CHECK_EQ(accepted.size(), size_t(1));
  CHECK_EQ(accepted[0], 8);
  CHECK(closed);
  CHECK(env.root.modal() == nullptr);
  CHECK(env.root.focused() == env.base);  // the previous focus is back
  env.type("!");
  CHECK_EQ(env.base->text(), std::string("base!"));
}

TEST(list_popup_escape_outside_click_enter_and_modality) {
  WidgetEnv env;
  for (int how = 0; how < 3; ++how) {
    auto own = std::make_unique<ListPopup>(false);  // input-less: Home/End navigate the list
    int accepted = -1;
    bool closed = false;
    own->onAccept([&](int r) { accepted = r; });
    own->onClosed([&] { closed = true; });
    ListPopup* p = ListPopup::open(env.root, std::move(own));
    p->setRows(numberedRows(4), 1);
    env.render();
    CHECK(env.root.focused() == p);
    env.send(keyEv(Key::End));
    CHECK_EQ(p->selected(), 3);
    env.send(keyEv(Key::Home));
    CHECK_EQ(p->selected(), 0);
    env.send(keyEv(Key::Down));
    if (how == 0) env.send(keyEv(Key::Escape));
    else if (how == 1) env.send(click(2, 2));  // outside the popup box
    else env.send(keyEv(Key::Enter));
    CHECK(closed);
    CHECK(env.root.modal() == nullptr);
    CHECK_EQ(accepted, how == 2 ? 1 : -1);  // Escape and outside click never accept
  }
  // A click outside does not reach the widget below (the modal swallows it) and keys never leak past it.
  auto own = std::make_unique<ListPopup>(false);
  ListPopup* p = ListPopup::open(env.root, std::move(own));
  p->setRows(numberedRows(2));
  env.base->setFocus();  // someone steals the focus: the next key still goes to the modal
  env.send(keyEv(Key::Down));
  CHECK_EQ(p->selected(), 1);
  env.type("z");
  CHECK_EQ(env.base->text(), std::string("base"));
  p->close();
}

TEST(list_popup_paints_above_text_on_a_second_layer) {
  WidgetEnv env;
  env.render();
  const std::string baseLog = env.rec.log;  // rects, then glyphs (the "base" text), then overlay rects (caret)
  CHECK(baseLog.find('G') != std::string::npos);
  auto own = std::make_unique<ListPopup>(true);
  ListPopup* p = ListPopup::open(env.root, std::move(own));
  std::vector<ListRow> rows = numberedRows(6);
  rows[1].primary = "match";
  rows[1].match = fuzzyMatchPositions("mtc", rows[1].primary);
  rows[1].secondary = "scope::x";
  rows[1].detail = "src/a.cpp:3";
  rows[1].icon = "\xC6\x92";
  p->setRows(rows);
  p->setStatus("verifying\xE2\x80\xA6");
  env.render();
  // The popup's rects come after the base layer's glyphs, and it has glyph runs of its own after them.
  const size_t firstG = env.rec.log.find('G');
  REQUIRE(firstG != std::string::npos);
  const size_t rectAfter = env.rec.log.find('R', firstG);
  REQUIRE(rectAfter != std::string::npos);
  CHECK(env.rec.log.find('G', rectAfter) != std::string::npos);
  CHECK(env.rec.log.size() <= 8);  // low draw-call count: a handful of batches for the whole frame
  CHECK_EQ(fuzzyMatchPositions("mtc", "match").size(), size_t(3));
  CHECK_EQ(fuzzyMatchPositions("mtc", "match")[0], 0);
  CHECK_EQ(fuzzyMatchPositions("mtc", "match")[1], 2);
  CHECK(fuzzyMatchPositions("xyz", "match").empty());
  p->close();
  env.render();
  CHECK(env.rec.log == baseLog);
}

// ---------------------------------------------------------------------------------------------------------------
// App-level fixture: a tiny workspace, Editor (fast-only), Workbench, EditorApp with the real default bindings
// ---------------------------------------------------------------------------------------------------------------

namespace {

// Editor::waitFor only wakes up for posted callbacks, and the index finishing is not one: poll instead.
bool waitForIndex(Editor& ed, int timeoutMs = 10000) {
  for (int i = 0; i < timeoutMs / 5; ++i) {
    ed.pump();
    if (ed.semantic().status().fastIndexDone) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return false;
}

void writeFile(const fs::path& p, const std::string& text) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary) << text;
}

struct AppEnv {
  fs::path dir;
  std::unique_ptr<gfx::TextRenderer> text = mockText();
  Recorder rec;
  std::unique_ptr<Editor> ed;
  std::unique_ptr<Workbench> bench;
  std::unique_ptr<EditorApp> app;
  int64_t now = 1000;
  using Fake = std::function<void(const std::string&, Editor::NavResult)>;
  std::vector<std::pair<std::string, Editor::NavResult>> pendingQueries;  // fake symbol backend

  AppEnv() {
    static int n = 0;
    dir =
        uitest::tempRoot() / ("tf_pick_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                              "_" + std::to_string(n++));
    writeFile(dir / "src/main.cpp", "int main() { return dup(); }\n");
    writeFile(dir / "src/alpha.cpp", "int dup() { return 1; }\nstruct AlphaShape {};\nint alphaOnly() { return 3; }\n");
    writeFile(dir / "src/beta.cpp",
              "// beta\nint dup() { return 2; }\nclass BetaShape {};\nint shapeCount() { return 0; }\n");
    writeFile(dir / "include/alpha.h", "struct AlphaShape;\n");
    writeFile(dir / "docs/readme.txt", "hello\n");
    writeFile(dir / "docs/alpha_notes.txt", "notes\n");
    Editor::Options o;
    o.semantic.useClangd = false;
    o.semantic.persistFastIndex = false;
    o.watchForcePolling = true;
    o.watchPollMs = 60000;
    ed = std::make_unique<Editor>(dir, o);
    std::string err;
    if (!ed->start(&err)) std::cerr << "    editor start: " << err << "\n";
    waitForIndex(*ed);
    bench = std::make_unique<Workbench>(*text, *ed);
    app = std::make_unique<EditorApp>(*bench, *ed, bench.get());
    bench->render(rec, 1000, 600);
  }
  ~AppEnv() {
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
  void type(const std::string& s) { send(textEv(s)); }
  void advance(int ms) {
    now += ms;
    frame();
  }
  void fakeSymbols() {
    app->symbolSearch().setSearchFn(
        [this](const std::string& q, Editor::NavResult cb) { pendingQueries.push_back({q, cb}); });
  }
  void open(const char* rel) {
    bench->openFile(rel);
    bench->sync();
  }
  std::string curFile() { return ed->currentDocument()->path().filename().string(); }
  ListPopup* popup() { return dynamic_cast<ListPopup*>(bench->modal()); }
};

SymbolLocation sym(const AppEnv& env, const char* file, int line, const char* name, const char* kind, Confidence c,
                   const char* container = "") {
  SymbolLocation s;
  s.loc.file = env.dir / file;
  s.loc.pos = {line, 4};
  s.name = name;
  s.kind = kind;
  s.container = container;
  s.confidence = c;
  return s;
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// Symbol search
// ---------------------------------------------------------------------------------------------------------------

TEST(symbol_search_approximate_badge_then_authoritative_keeps_selection) {
  AppEnv env;
  env.fakeSymbols();
  env.open("src/main.cpp");
  CHECK(env.key(Key(','), ModCtrl));  // default binding Search.SymbolSearch
  ListPopup* p = env.popup();
  REQUIRE(p != nullptr);
  CHECK_EQ(std::string(env.app->message()).find("not available"), std::string::npos);
  CHECK(env.app->symbolSearch().isOpen());
  CHECK(env.pendingQueries.empty());  // an empty query searches nothing

  env.type("sh");
  REQUIRE(env.pendingQueries.size() == 1);
  CHECK_EQ(env.pendingQueries[0].first, std::string("sh"));
  const std::vector<SymbolLocation> approx = {
      sym(env, "src/alpha.cpp", 1, "AlphaShape", "struct", Confidence::Approximate),
      sym(env, "src/beta.cpp", 3, "shapeCount", "function", Confidence::Approximate, "ns"),
      sym(env, "src/beta.cpp", 2, "BetaShape", "class", Confidence::Approximate)};
  env.pendingQueries[0].second(Confidence::Approximate, approx, "");
  env.frame();
  REQUIRE(p->rows().size() == 3);
  CHECK(p->status().find("verifying") != std::string::npos);  // never approximate-as-final
  for (const ListRow& r : p->rows()) CHECK(r.dim);
  CHECK_EQ(p->rows()[1].secondary, std::string("ns"));
  CHECK_EQ(p->rows()[1].detail, std::string("src/beta.cpp:4"));
  CHECK_EQ(p->rows()[1].icon, std::string("\xC6\x92"));
  CHECK_EQ(p->rows()[2].icon, std::string("C"));
  CHECK_EQ(p->rows()[0].match.size(), size_t(2));  // "sh" highlighted inside AlphaShape

  env.key(Key::Down);
  env.key(Key::Down);
  REQUIRE(p->selectedRow() != nullptr);
  CHECK_EQ(p->selectedRow()->primary, std::string("BetaShape"));

  // The authoritative answer re-orders and adds a symbol: the badge goes, the rows are no longer greyed, and the
  // selection follows BetaShape to its new position.
  std::vector<SymbolLocation> auth = {
      sym(env, "src/alpha.cpp", 1, "AlphaShape", "struct", Confidence::Authoritative),
      sym(env, "src/beta.cpp", 5, "ShapeNew", "class", Confidence::Authoritative),
      sym(env, "src/beta.cpp", 3, "shapeCount", "function", Confidence::Authoritative, "ns"),
      sym(env, "src/beta.cpp", 2, "BetaShape", "class", Confidence::Authoritative)};
  env.pendingQueries[0].second(Confidence::Authoritative, auth, "");
  env.frame();
  REQUIRE(p->rows().size() == 4);
  CHECK(p->status().empty());
  for (const ListRow& r : p->rows()) CHECK(!r.dim);
  REQUIRE(p->selectedRow() != nullptr);
  CHECK_EQ(p->selectedRow()->primary, std::string("BetaShape"));
  CHECK_EQ(p->selected(), 3);

  // If the symbol is gone from the authoritative list the selection falls back to the first row.
  env.type("a");  // query "sha": a new request
  env.advance(40);
  REQUIRE(env.pendingQueries.size() == 2);
  env.pendingQueries[1].second(Confidence::Authoritative, {auth[0], auth[1]}, "");
  env.frame();
  CHECK_EQ(p->selected(), 0);
}

TEST(symbol_search_stale_responses_and_debounce) {
  AppEnv env;
  env.fakeSymbols();
  env.open("src/main.cpp");
  env.key(Key(','), ModCtrl);
  ListPopup* p = env.popup();
  REQUIRE(p != nullptr);
  SymbolSearch& ss = env.app->symbolSearch();
  // The first key after a pause goes out at once; keys typed within the debounce window are coalesced into one
  // request that is sent by tick() when it is due (<= 25 ms after the previous one).
  env.type("a");
  CHECK_EQ(ss.queriesIssued(), 1);
  env.type("b");
  env.type("c");
  CHECK_EQ(ss.queriesIssued(), 1);
  const int wait = env.app->tick(env.now);
  CHECK(wait >= 1 && wait <= 25);  // the app is told when to wake up
  env.advance(30);
  CHECK_EQ(ss.queriesIssued(), 2);
  REQUIRE(env.pendingQueries.size() == 2);
  CHECK_EQ(env.pendingQueries[1].first, std::string("abc"));

  // A late answer for "a" must not replace the (newer) request's rows; the answer for "abc" wins.
  env.pendingQueries[1].second(Confidence::Authoritative,
                               {sym(env, "src/alpha.cpp", 1, "abcNew", "struct", Confidence::Authoritative)}, "");
  env.pendingQueries[0].second(Confidence::Authoritative,
                               {sym(env, "src/alpha.cpp", 0, "aOld", "function", Confidence::Authoritative),
                                sym(env, "src/alpha.cpp", 2, "aOld2", "function", Confidence::Authoritative)},
                               "");
  env.frame();
  REQUIRE(p->rows().size() == 1);
  CHECK_EQ(p->rows()[0].primary, std::string("abcNew"));
  // Clearing the query clears the list at once, and an answer arriving afterwards is dropped.
  env.key(Key('A'), ModCtrl);
  env.key(Key::Backspace);
  CHECK(p->rows().empty());
  env.pendingQueries[1].second(Confidence::Authoritative,
                               {sym(env, "src/alpha.cpp", 0, "late", "function", Confidence::Authoritative)}, "");
  env.frame();
  CHECK(p->rows().empty());
  // Closing the picker drops answers in flight as well.
  env.type("q");
  env.advance(40);
  const size_t n = env.pendingQueries.size();
  env.key(Key::Escape);
  CHECK(env.popup() == nullptr);
  env.pendingQueries[n - 1].second(Confidence::Authoritative,
                                   {sym(env, "src/alpha.cpp", 0, "late", "function", Confidence::Authoritative)}, "");
  env.frame();
  CHECK(env.popup() == nullptr);
}

TEST(symbol_search_enter_jumps_through_gotolocation_and_alt_left_returns) {
  AppEnv env;
  env.open("src/main.cpp");
  const size_t base = env.ed->navigation().entries().size();
  env.key(Key(','), ModCtrl);
  ListPopup* p = env.popup();
  REQUIRE(p != nullptr);
  CHECK(!p->previewEnabled() == false);  // picker.preview defaults to true
  env.type("AlphaShape");
  CHECK(env.ed->waitFor([&] { return !p->rows().empty(); }, 5000));  // real findSymbols, fast tier only
  env.frame();
  REQUIRE(!p->rows().empty());
  CHECK_EQ(p->rows()[0].primary, std::string("AlphaShape"));
  CHECK_EQ(p->rows()[0].icon, std::string("S"));
  CHECK(p->rows()[0].dim);  // fast-only engine: approximate, and the badge says so
  CHECK(p->status().find("approximate") != std::string::npos);
  CHECK(p->rows()[0].detail.find("alpha") != std::string::npos);
  // Preview of the target line: the open Document is used when there is one, else the file on disk.
  CHECK(p->previewContent().title.find(":2") != std::string::npos);
  bool found = false;
  for (const std::string& l : p->previewContent().lines)
    found = found || l.find("struct AlphaShape") != std::string::npos;
  CHECK(found);
  CHECK(p->previewContent().highlight >= 0);
  CHECK(p->previewContent().lines[static_cast<size_t>(p->previewContent().highlight)].find("AlphaShape") !=
        std::string::npos);

  CHECK(env.key(Key::Enter));
  CHECK(env.popup() == nullptr);
  CHECK(env.ed->navigation().entries().size() > base);  // gotoLocation recorded the jump
  CHECK_EQ(env.curFile(), std::string("alpha.cpp"));
  CHECK_EQ(env.bench->tabPath(env.bench->activePanel()->activeId()).filename().string(), std::string("alpha.cpp"));
  CHECK_EQ(env.ed->cursor().pos.line, 1);
  CHECK(env.key(Key::Left, ModAlt));
  CHECK_EQ(env.curFile(), std::string("main.cpp"));
  CHECK_EQ(env.bench->tabPath(env.bench->activePanel()->activeId()).filename().string(), std::string("main.cpp"));
}

TEST(symbol_search_preview_setting_off_and_preview_reads_unsaved_text) {
  AppEnv env;
  env.open("src/alpha.cpp");
  env.ed->setSelections({Selection{{0, 0}, {0, 0}}});
  env.ed->insertText("// edited\n");  // unsaved change: line numbers move down by one
  Settings st;
  st.loadTexts("picker.preview = false\n", "");
  env.app->applySettings(st);
  env.key(Key(','), ModCtrl);
  ListPopup* p = env.popup();
  REQUIRE(p != nullptr);
  CHECK(!p->previewEnabled());
  env.key(Key::Escape);
  st.loadTexts("picker.preview = true\n", "");
  env.app->applySettings(st);
  env.key(Key(','), ModCtrl);
  p = env.popup();
  REQUIRE(p != nullptr);
  CHECK(p->previewEnabled());
  env.fakeSymbols();
  env.type("x");
  env.pendingQueries.back().second(Confidence::Authoritative,
                                   {sym(env, "src/alpha.cpp", 1, "dup", "function", Confidence::Authoritative)}, "");
  env.frame();
  REQUIRE(!p->previewContent().lines.empty());
  CHECK_EQ(p->previewContent().lines[0], std::string("// edited"));  // from the Document, not from disk
}

// ---------------------------------------------------------------------------------------------------------------
// Open-file search
// ---------------------------------------------------------------------------------------------------------------

TEST(file_search_ranking_filename_beats_path_and_recent_first) {
  const std::vector<std::string> files = {"docs/alpha_notes.txt", "include/alpha.h", "src/alpha.cpp",
                                          "src/beta.cpp",         "src/main.cpp",    "src/zalpha/other.cpp",
                                          "alpha/readme.txt"};
  // Exact filename first, then the filename prefix, then a path fragment ("alpha/readme.txt", "zalpha/other.cpp").
  auto r = FileSearch::rank(files, {}, "alpha.cpp");
  REQUIRE(!r.empty());
  CHECK_EQ(files[r[0]], std::string("src/alpha.cpp"));
  r = FileSearch::rank(files, {}, "alpha");
  REQUIRE(r.size() >= 5);
  const std::string top = files[r[0]];
  CHECK(top == "include/alpha.h" || top == "src/alpha.cpp");  // filename starts with the query
  size_t posNotes = 99, posFragment = 99;
  for (size_t i = 0; i < r.size(); ++i) {
    if (files[r[i]] == "docs/alpha_notes.txt") posNotes = i;
    if (files[r[i]] == "src/zalpha/other.cpp") posFragment = i;
  }
  CHECK(posNotes < posFragment);  // filename prefix beats a directory-only match
  CHECK(files[r[2]] != "src/zalpha/other.cpp");
  // Several tokens: all must match.
  r = FileSearch::rank(files, {}, "src alpha");
  for (size_t i : r) CHECK(files[i].find("src/") != std::string::npos);
  CHECK(FileSearch::rank(files, {}, "qqq").empty());
  // No query: recent files first, in MRU order, then the rest.
  r = FileSearch::rank(files, {"src/main.cpp", "docs/alpha_notes.txt"}, "");
  REQUIRE(r.size() == files.size());
  CHECK_EQ(files[r[0]], std::string("src/main.cpp"));
  CHECK_EQ(files[r[1]], std::string("docs/alpha_notes.txt"));
  // A recent file wins ties with an otherwise equal match.
  r = FileSearch::rank({"a/util.cpp", "b/util.cpp"}, {"b/util.cpp"}, "util");
  CHECK_EQ(r[0], size_t(1));
}

TEST(file_search_picker_mru_open_is_not_a_navigation_jump) {
  AppEnv env;
  env.open("src/alpha.cpp");
  env.open("src/beta.cpp");
  env.open("docs/readme.txt");
  env.open("src/main.cpp");
  const size_t base = env.ed->navigation().entries().size();
  CHECK(env.key(Key('P'), ModCtrl));  // default binding File.OpenFile
  ListPopup* p = env.popup();
  REQUIRE(p != nullptr);
  REQUIRE(env.app->fileSearch() != nullptr);
  CHECK(env.app->fileSearch()->isOpen());
  REQUIRE(p->rows().size() >= 6);
  // Empty query: the most recently used files lead.
  CHECK_EQ(p->rows()[0].key, std::string("src/main.cpp"));
  CHECK_EQ(p->rows()[1].key, std::string("docs/readme.txt"));
  CHECK_EQ(p->rows()[2].key, std::string("src/beta.cpp"));
  CHECK_EQ(p->rows()[3].key, std::string("src/alpha.cpp"));
  CHECK_EQ(p->rows()[0].badge, std::string("recent"));
  CHECK_EQ(p->rows()[4].badge, std::string(""));
  CHECK_EQ(p->rows()[1].primary, std::string("readme.txt"));
  CHECK_EQ(p->rows()[1].secondary, std::string("docs"));

  env.type("alpha");
  REQUIRE(!p->rows().empty());
  CHECK(p->rows()[0].key == "include/alpha.h" || p->rows()[0].key == "src/alpha.cpp");
  env.type(".h");
  CHECK_EQ(p->rows()[0].key, std::string("include/alpha.h"));
  CHECK(!p->rows()[0].match.empty());
  CHECK(env.key(Key::Enter));
  CHECK(env.popup() == nullptr);
  CHECK_EQ(env.curFile(), std::string("alpha.h"));
  CHECK_EQ(env.bench->tabPath(env.bench->activePanel()->activeId()).filename().string(), std::string("alpha.h"));
  CHECK_EQ(env.ed->navigation().entries().size(), base);  // openFile is a tab operation, not a jump
  CHECK_EQ(env.bench->recentFiles()[0].filename().string(), std::string("alpha.h"));

  // The MRU list is persisted in layout.json.
  const std::string json = env.bench->layoutJson();
  CHECK(json.find("\"recent\"") != std::string::npos);
  Workbench other(*env.text, *env.ed);
  CHECK(other.restoreLayoutFromJson(json));
  REQUIRE(!other.recentFiles().empty());
  CHECK_EQ(other.recentFiles()[0].filename().string(), std::string("alpha.h"));
  CHECK_EQ(other.recentFiles().size(), env.bench->recentFiles().size());
}

// ---------------------------------------------------------------------------------------------------------------
// Multi-result navigation picker
// ---------------------------------------------------------------------------------------------------------------

TEST(nav_result_picker_f12_with_several_definitions_lists_them_and_enter_jumps) {
  AppEnv env;
  env.open("src/main.cpp");
  env.ed->moveCursor({0, 22});  // on "dup" in "return dup();"
  const size_t base = env.ed->navigation().entries().size();
  CHECK(env.key(Key::F12));  // Editor.GotoDefinition: two definitions of dup() in the fast index
  CHECK(env.ed->waitFor([&] { return env.app->navResults().isOpen(); }, 5000));
  env.frame();
  ListPopup* p = env.popup();
  REQUIRE(p != nullptr);
  CHECK(env.app->message().find("not available") == std::string::npos);
  REQUIRE(p->rows().size() == 2);
  CHECK(p->status().find("approximate") != std::string::npos || p->status().find("verifying") != std::string::npos);
  CHECK(p->rows()[0].dim);
  CHECK(p->rows()[0].primary.find("src/") == 0);
  CHECK(p->rows()[0].primary.find(".cpp:") != std::string::npos);
  CHECK(p->rows()[0].secondary.find("dup") != std::string::npos);  // the source line of the result
  CHECK_EQ(env.ed->navigation().entries().size(), base);           // nothing jumped yet
  env.key(Key::Down);
  const std::string chosen = p->selectedRow()->primary;  // "src/beta.cpp:2" or ".../alpha.cpp:1"
  CHECK(env.key(Key::Enter));
  CHECK(env.popup() == nullptr);
  CHECK(env.ed->navigation().entries().size() > base);
  const std::string file = chosen.substr(0, chosen.find(':'));
  CHECK_EQ(env.ed->currentDocument()->path().generic_string(), (env.dir / file).generic_string());
  CHECK(env.key(Key::Left, ModAlt));
  CHECK_EQ(env.curFile(), std::string("main.cpp"));
}

TEST(nav_result_picker_swaps_approximate_for_authoritative_keeping_selection) {
  AppEnv env;
  env.open("src/main.cpp");
  NavResultPicker& np = env.app->navResults();
  auto s = [&](const char* f, int line, Confidence c) { return sym(env, f, line, "dup", "function", c); };
  np.show("Find All References", Confidence::Approximate,
          {s("src/alpha.cpp", 0, Confidence::Approximate), s("src/beta.cpp", 1, Confidence::Approximate)}, "", 1);
  env.frame();
  ListPopup* p = env.popup();
  REQUIRE(p != nullptr);
  CHECK(p->status().find("verifying") != std::string::npos);
  CHECK(p->rows()[0].dim);
  env.key(Key::Down);
  CHECK_EQ(p->selected(), 1);
  np.show("Find All References", Confidence::Authoritative,
          {s("src/main.cpp", 0, Confidence::Authoritative), s("src/alpha.cpp", 0, Confidence::Authoritative),
           s("src/beta.cpp", 1, Confidence::Authoritative)},
          "", 1);
  env.frame();
  CHECK(env.popup() == p);  // the same popup is updated in place
  CHECK_EQ(p->rows().size(), size_t(3));
  CHECK(p->status().empty());
  CHECK(!p->rows()[0].dim);
  CHECK_EQ(p->selected(), 2);  // still src/beta.cpp:2
  CHECK_EQ(p->rows()[2].primary, std::string("src/beta.cpp:2"));
  // Fewer results than the minimum (the Editor jumped by itself): the picker goes away.
  np.show("Find All References", Confidence::Authoritative, {s("src/beta.cpp", 1, Confidence::Authoritative)}, "", 2);
  CHECK(env.popup() == nullptr);
}

// ---------------------------------------------------------------------------------------------------------------
// Performance (informational): TENFOLD_PERF_GODOT=/path/to/godot tenfold-ui-tests pickers_perf
// ---------------------------------------------------------------------------------------------------------------

TEST(pickers_perf_godot) {
  const char* root = std::getenv("TENFOLD_PERF_GODOT");
  if (!root) {
    std::cerr << "    SKIP perf: set TENFOLD_PERF_GODOT=<godot checkout>\n";
    return;
  }
  using Clock = std::chrono::steady_clock;
  auto ms = [](Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
  };
  Editor::Options o;
  o.semantic.useClangd = false;
  Editor ed(root, o);
  std::string err;
  REQUIRE(ed.start(&err));
  CHECK(waitForIndex(ed, 120000));
  // File filtering per keystroke.
  const std::vector<std::string> files = FileSearch::collect(ed.workspace());
  std::cerr << "    PERF files: " << files.size() << " paths\n";
  const std::vector<std::string> recent = {files[0], files[files.size() / 2]};
  for (const char* q : {"n", "no", "nod", "node3d", "node_3d.cpp", "scene gd", "editor/ plug", "zzzz"}) {
    double best = 1e9, sum = 0;
    size_t hits = 0;
    for (int i = 0; i < 5; ++i) {
      const auto t0 = Clock::now();
      hits = FileSearch::rank(files, recent, q).size();
      const double d = ms(t0, Clock::now());
      best = std::min(best, d);
      sum += d;
    }
    std::cerr << "    PERF file filter \"" << q << "\": best " << best << " ms, mean " << sum / 5 << " ms (" << hits
              << " shown)\n";
  }
  // Symbol search, fast tier, through Editor::findSymbols (time until the callback is pumped).
  for (const char* q : {"n", "nod", "Node3D", "get_transform", "set_pos", "RenderingServer", "zzzzz"}) {
    double worst = 0, sum = 0;
    size_t n = 0;
    for (int i = 0; i < 5; ++i) {
      bool got = false;
      const auto t0 = Clock::now();
      ed.findSymbols(q, [&](Confidence, const std::vector<SymbolLocation>& l, const std::string&) {
        got = true;
        n = l.size();
      });
      ed.waitFor([&] { return got; }, 5000);
      const double d = ms(t0, Clock::now());
      worst = std::max(worst, d);
      sum += d;
    }
    std::cerr << "    PERF symbol search \"" << q << "\": worst " << worst << " ms, mean " << sum / 5 << " ms (" << n
              << " results)\n";
  }
}

// ---------------------------------------------------------------------------------------------------------------
// Golden: the symbol picker open over the editor
// ---------------------------------------------------------------------------------------------------------------

TEST(pickers_golden_symbol_search_over_editor) {
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
  Editor::Options o;
  o.semantic.useClangd = false;
  o.semantic.persistFastIndex = false;
  o.watchForcePolling = true;
  o.watchPollMs = 60000;
  Editor ed(TENFOLD_SOURCE_DIR "/examples/sample_project", o);
  REQUIRE(ed.start(&err));
  waitForIndex(ed);
  Workbench wb(*tr, ed);
  wb.openFile("src/geometry.cpp");
  wb.openFile("src/main.cpp");
  wb.sync();
  EditorApp app(wb, ed, &wb);
  const fs::path root = ed.workspace().root();
  app.symbolSearch().setSearchFn([&](const std::string&, Editor::NavResult cb) {
    auto mk = [&](const char* file, int line, const char* name, const char* kind, const char* scope) {
      SymbolLocation s;
      s.loc.file = root / file;
      s.loc.pos = {line, 0};
      s.name = name;
      s.kind = kind;
      s.container = scope;
      s.confidence = Confidence::Approximate;
      return s;
    };
    cb(Confidence::Approximate,
       {mk("src/geometry.cpp", 5, "Circle::area", "method", "geo"),
        mk("src/geometry.cpp", 8, "make_default_shape", "function", "geo"),
        mk("include/geometry.h", 9, "Shape", "class", "geo"), mk("include/geometry.h", 3, "geo", "namespace", ""),
        mk("src/main.cpp", 6, "main", "function", "")},
       "");
  });
  int64_t now = 1000;
  app.handleEvent(keyEv(Key(','), ModCtrl), now);
  app.handleEvent(textEv("sha"), now += 10);
  ed.pump();
  app.tick(now += 10);
  ListPopup* p = dynamic_cast<ListPopup*>(wb.modal());
  REQUIRE(p != nullptr);
  p->setSelected(2);
  gfx::GlRenderer& r = *fx->renderer;
  const int w = 1024, h = 640;
  r.setOffscreen(true);
  wb.render(r, w, h);
  std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4);
  REQUIRE(r.readPixels(w, h, px.data()));
  tr->releaseTexture(r);

  const std::string path = TENFOLD_SOURCE_DIR "/tests/golden/b5_symbol_picker.png";
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
    gfx::writePngFile("b5_symbol_picker.actual.png", px.data(), w, h);
    std::cerr << "    " << bad << " pixels differ; wrote b5_symbol_picker.actual.png\n";
  }
  CHECK(bad <= golden.size() / 4 / 500);
}
