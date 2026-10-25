// B5b: find in files, completion popup, hover tooltip, signature help, diagnostics list. No GL except the goldens: a
// recording renderer, the mock font (8 px cells, 14 px lines), fake backends injected into the features and synthetic
// events driven through EditorApp with the real default key bindings. One test runs the real Editor::findInWorkspace.
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#include "../text/text_test_util.h"
#include "ui/gfx/PngWrite.h"
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
InputEvent moveEv(float x, float y) {
  InputEvent e;
  e.type = InputType::MouseMove;
  e.x = x;
  e.y = y;
  return e;
}

void writeFile(const fs::path& p, const std::string& text) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary) << text;
}

bool waitForIndex(Editor& ed, int timeoutMs = 10000) {
  for (int i = 0; i < timeoutMs / 5; ++i) {
    ed.pump();
    if (ed.semantic().status().fastIndexDone) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return false;
}

LspCompletionItem item(const char* label, const char* detail = "", const char* doc = "", int kind = 3) {
  LspCompletionItem it;
  it.label = label;
  it.detail = detail;
  it.documentation = doc;
  it.insertText = label;
  it.filterText = label;
  it.sortText = label;
  it.kind = kind;
  return it;
}

struct Env {
  fs::path dir;
  std::unique_ptr<gfx::TextRenderer> text = mockText();
  Recorder rec;
  std::unique_ptr<Editor> ed;
  std::unique_ptr<Workbench> bench;
  std::unique_ptr<EditorApp> app;
  int64_t now = 1000;
  // Fakes: pending completion / hover / signature / search requests, answered by the test.
  std::vector<CompletionPopup::Done> completions;
  std::vector<HoverTooltip::Done> hovers;
  std::vector<Position> hoverAt;
  std::vector<SignatureHelp::Done> sigs;
  struct SearchCall {
    std::string query;
    SearchOptions opts;
    Editor::SearchBatch batch;
    Editor::SearchDone done;
    SearchHandle handle;
  };
  std::vector<SearchCall> searches;

  Env() {
    static int n = 0;
    dir = fs::temp_directory_path() /
          ("tf_b5b_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_" +
           std::to_string(n++));
    writeFile(dir / "src/main.cpp", "int main() { return dup(); }\n");
    writeFile(dir / "src/alpha.cpp",
              "int dup() { return 1; }\nstruct AlphaShape {};\nint alphaOnly() { return dup(); }\n");
    writeFile(dir / "src/beta.cpp", "// beta\nint dup() { return 2; }\nclass BetaShape {};\n");
    writeFile(dir / "scratch.cpp", "");
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
  ~Env() {
    app.reset();
    bench.reset();
    ed.reset();
    std::error_code ec;
    fs::remove_all(dir, ec);
  }
  void fakeAll() {
    app->completion().setCompleteFn([this](CompletionPopup::Done d) { completions.push_back(std::move(d)); });
    app->completion().setApproximateFn([] { return false; });
    app->hover().setHoverFn([this](Document&, Position p, HoverTooltip::Done d) {
      hoverAt.push_back(p);
      hovers.push_back(std::move(d));
    });
    app->hover().setApproximateFn([] { return false; });
    app->signatureHelp().setSigFn([this](SignatureHelp::Done d) { sigs.push_back(std::move(d)); });
    app->findInFiles().setSearchFn(
        [this](const std::string& q, SearchOptions o, Editor::SearchBatch b, Editor::SearchDone d) {
          searches.push_back({q, o, std::move(b), std::move(d), SearchHandle::create()});
          return searches.back().handle;
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
    for (size_t i = 0; i < s.size(); ++i) send(textEv(std::string(1, s[i])));  // one Text event per character
  }
  void advance(int ms) {
    now += ms;
    frame();
  }
  void open(const char* rel) {
    bench->openFile(rel);
    bench->sync();
    frame();
  }
  std::string curFile() { return ed->currentDocument()->path().filename().string(); }
  std::string line(int n = 0) { return ed->currentDocument()->buffer().line(n); }
  ListPopup* popup() { return dynamic_cast<ListPopup*>(bench->modal()); }
  TextView* view() { return dynamic_cast<TextView*>(bench->focused()); }
  void answer(size_t i, std::vector<LspCompletionItem> items, const std::string& err = "") {
    completions[i](items, err);
    frame();
  }
};

std::vector<std::string> primaries(ListPopup* p) {
  std::vector<std::string> out;
  for (const ListRow& r : p->rows()) out.push_back(r.primary);
  return out;
}

SearchMatch match(const Env& env, const char* rel, int line, int col, int len, const char* text) {
  SearchMatch m;
  m.file = env.dir / rel;
  m.line = line;
  m.column = col;
  m.length = len;
  m.text = text;
  return m;
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// Find in files
// ---------------------------------------------------------------------------------------------------------------

TEST(find_parse_options_and_globs) {
  const SearchOptions o = FindInFiles::parse("foo bar :: *.cpp, !third_party  src/**", true, true, false);
  CHECK_EQ(o.query, std::string("foo bar"));
  CHECK(o.caseSensitive && o.wholeWord && !o.regex);
  CHECK_EQ(o.includeGlobs.size(), size_t(2));
  CHECK_EQ(o.includeGlobs[0], std::string("*.cpp"));
  CHECK_EQ(o.includeGlobs[1], std::string("src/**"));
  CHECK_EQ(o.excludeGlobs.size(), size_t(1));
  CHECK_EQ(o.excludeGlobs[0], std::string("third_party"));
  CHECK_EQ(FindInFiles::parse("a::b", false, false, true).query, std::string("a::b"));  // no spaces: not a glob split
}

TEST(find_in_files_streams_groups_by_file_and_reports_counts) {
  Env env;
  env.fakeAll();
  env.open("src/main.cpp");
  CHECK(env.key(Key('F'), ModCtrl | ModShift));
  ListPopup* p = env.popup();
  REQUIRE(p != nullptr);
  env.type("dup");
  CHECK(env.searches.empty());  // debounced
  env.advance(200);
  REQUIRE_EQ(env.searches.size(), size_t(1));
  CHECK_EQ(env.searches[0].query, std::string("dup"));
  CHECK(!env.searches[0].opts.caseSensitive);
  // First batch: two files arriving out of order and one file split over two batches.
  env.searches[0].batch({match(env, "src/beta.cpp", 1, 4, 3, "int dup() { return 2; }"),
                         match(env, "src/alpha.cpp", 0, 6, 3, "  int dup() { return 1; }")});
  env.advance(100);
  p = env.popup();
  CHECK(p->status().find("2 matches in 2 files") != std::string::npos);
  CHECK(p->status().find("searching") != std::string::npos);
  env.searches[0].batch({match(env, "src/alpha.cpp", 2, 24, 3, "int alphaOnly() { return dup(); }")});
  env.advance(100);
  REQUIRE_EQ(p->rows().size(), size_t(5));  // alpha header, 2 matches, beta header, 1 match
  CHECK_EQ(p->rows()[0].primary, std::string("src/alpha.cpp"));
  CHECK_EQ(p->rows()[0].badge, std::string("2"));
  CHECK_EQ(p->rows()[1].primary, std::string("int dup() { return 1; }"));  // leading blanks trimmed
  CHECK(p->rows()[1].match == std::vector<int>{4, 5, 6});                  // the match is highlighted
  CHECK_EQ(p->rows()[1].detail, std::string("1"));
  CHECK_EQ(p->rows()[2].detail, std::string("3"));
  CHECK_EQ(p->rows()[3].primary, std::string("src/beta.cpp"));
  CHECK_EQ(p->selected(), 1);  // the first match, not the header
  SearchSummary s;
  s.matches = 3;
  s.filesMatched = 2;
  s.filesSearched = 9;
  s.truncated = true;
  env.searches[0].done(s);
  env.advance(100);
  CHECK(p->status().find("3 matches in 2 files (truncated)") != std::string::npos);
  CHECK(p->status().find("searching") == std::string::npos);
}

TEST(find_in_files_new_query_cancels_and_drops_late_batches_toggles_set_options) {
  Env env;
  env.fakeAll();
  env.open("src/main.cpp");
  env.key(Key('F'), ModCtrl | ModShift);
  env.type("du");
  env.advance(200);
  REQUIRE_EQ(env.searches.size(), size_t(1));
  env.type("p");
  CHECK(env.searches[0].handle.cancelled());  // typing cancels the running search at once
  env.advance(200);
  REQUIRE_EQ(env.searches.size(), size_t(2));
  env.searches[0].batch({match(env, "src/alpha.cpp", 0, 4, 3, "int dup()")});  // late batch of the old search
  env.advance(100);
  CHECK(env.popup()->rows().empty());
  env.key(Key('C'), ModAlt);  // case sensitive
  env.key(Key('W'), ModAlt);  // whole word
  env.key(Key('R'), ModAlt);  // regex
  env.advance(200);
  REQUIRE_EQ(env.searches.size(), size_t(3));
  CHECK(env.searches[2].opts.caseSensitive && env.searches[2].opts.wholeWord && env.searches[2].opts.regex);
  CHECK(env.popup()->status().find("[Aa]") != std::string::npos);
  env.key(Key::Escape);  // closing cancels
  CHECK(env.searches[2].handle.cancelled());
  CHECK(env.popup() == nullptr);
}

TEST(find_in_files_real_search_enter_jumps_f8_steps_and_alt_left_returns) {
  Env env;
  env.open("src/main.cpp");
  std::vector<std::pair<std::string, size_t>> marks;
  env.app->findInFiles().setMarkersFn(
      [&](const fs::path& f, const std::vector<Range>& r) { marks.push_back({f.filename().string(), r.size()}); });
  CHECK(env.key(Key('F'), ModCtrl | ModShift));
  env.type("dup");
  for (int i = 0; i < 300 && (env.app->findInFiles().running() || env.app->findInFiles().matches().empty()); ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    env.advance(20);
  }
  env.advance(200);
  ListPopup* p = env.popup();
  REQUIRE(p != nullptr);
  CHECK_EQ(env.app->findInFiles().matches().size(), size_t(4));  // main 1, alpha 2, beta 1
  REQUIRE_EQ(p->rows().size(), size_t(7));
  CHECK_EQ(p->rows()[0].primary, std::string("src/alpha.cpp"));
  CHECK(std::find(marks.begin(), marks.end(), std::make_pair(std::string("alpha.cpp"), size_t(2))) != marks.end());
  CHECK(env.key(Key::Enter));  // first match: alpha.cpp line 1
  CHECK(env.popup() == nullptr);
  CHECK_EQ(env.curFile(), std::string("alpha.cpp"));
  CHECK_EQ(env.ed->cursor().pos.line, 0);
  CHECK_EQ(env.ed->cursor().pos.column, 4);
  CHECK(env.key(Key::F8));  // next: alpha.cpp line 3
  CHECK_EQ(env.ed->cursor().pos.line, 2);
  CHECK(env.key(Key::F8));
  CHECK_EQ(env.curFile(), std::string("beta.cpp"));
  CHECK(env.key(Key::F8, ModShift));  // previous
  CHECK_EQ(env.curFile(), std::string("alpha.cpp"));
  CHECK_EQ(env.ed->cursor().pos.line, 2);
  CHECK(env.key(Key::Left, ModAlt));  // every jump is in the navigation history (constraint 1.3)
  CHECK_EQ(env.curFile(), std::string("beta.cpp"));
  CHECK(env.key(Key::Left, ModAlt));
  CHECK_EQ(env.curFile(), std::string("alpha.cpp"));
  CHECK_EQ(env.ed->cursor().pos.line, 2);
}

// ---------------------------------------------------------------------------------------------------------------
// Completion
// ---------------------------------------------------------------------------------------------------------------

TEST(completion_triggers_filters_and_accepts_as_one_undo_step) {
  Env env;
  env.fakeAll();
  env.open("scratch.cpp");
  env.type("f");
  CHECK(env.completions.empty());  // one character: no request
  env.type("o");
  REQUIRE_EQ(env.completions.size(), size_t(1));
  CHECK(!env.app->completion().isOpen());  // nothing shown before the answer
  env.answer(0, {item("zeta"), item("foo", "int", "Docs for foo."), item("foobar", "void"), item("fancy_object"),
                 item("bar")});
  CompletionPopup& c = env.app->completion();
  REQUIRE(c.isOpen());
  CHECK(c.shownLabels() == std::vector<std::string>{"foo", "foobar", "fancy_object"});  // prefix matches first
  CHECK(c.box()->visible());
  CHECK(!c.box()->side().empty());  // docs of the selected item
  env.type("o");                    // "foo": filtered client-side, no new request
  CHECK_EQ(env.completions.size(), size_t(1));
  CHECK(c.shownLabels() == std::vector<std::string>{"foo", "foobar"});
  CHECK(env.key(Key::Down));
  CHECK_EQ(c.selected(), 1);
  CHECK(env.key(Key::Tab));
  CHECK(!c.isOpen());
  CHECK_EQ(env.line(), std::string("foobar"));
  CHECK_EQ(env.ed->cursor().pos.column, 6);
  env.ed->undo();  // one step restores what was typed, not a half-replaced word
  CHECK_EQ(env.line(), std::string("foo"));
}

TEST(completion_member_triggers_and_enter_accepts_and_escape_closes) {
  Env env;
  env.fakeAll();
  env.open("scratch.cpp");
  env.type("obj.");
  REQUIRE_EQ(env.completions.size(), size_t(2));  // "ob" asked; "j" did not (in flight); "." asked again
  env.answer(1, {item("size", "size_t"), item("push_back")});
  CompletionPopup& c = env.app->completion();
  REQUIRE(c.isOpen());
  CHECK_EQ(c.shownLabels().size(), size_t(2));  // empty prefix: everything
  env.type("pu");
  CHECK(c.shownLabels() == std::vector<std::string>{"push_back"});
  CHECK(env.key(Key::Enter));
  CHECK_EQ(env.line(), std::string("obj.push_back"));
  env.type(";");
  env.key(Key::Enter);
  env.type("p->");
  CHECK_EQ(env.completions.size(), size_t(3));
  env.type("x::");  // also "::"
  CHECK_EQ(env.completions.size(), size_t(4));
  env.answer(3, {item("value")});
  REQUIRE(c.isOpen());
  CHECK(env.key(Key::Escape));  // closes the popup; the editor's Escape binding does not run
  CHECK(!c.isOpen());
  CHECK_EQ(env.line(1), std::string("p->x::"));
  env.type("va");  // the word was dismissed: no re-trigger until it changes
  CHECK_EQ(env.completions.size(), size_t(4));
}

TEST(completion_stale_answers_are_dropped_and_caret_motion_closes) {
  Env env;
  env.fakeAll();
  env.open("scratch.cpp");
  env.type("fo");
  env.type(" ab");  // the blank ends the first word's session; "ab" asks again
  REQUIRE_EQ(env.completions.size(), size_t(2));
  env.answer(0, {item("foo")});  // belongs to the first word: dropped
  CHECK(!env.app->completion().isOpen());
  env.answer(1, {item("abc")});
  CHECK(env.app->completion().isOpen());
  env.type("z");
  CHECK(!env.app->completion().isOpen());  // no item matches "abz"
  // An answer that arrives after the caret left the word is dropped as well.
  env.key(Key::Home);
  env.type("qq");
  REQUIRE_EQ(env.completions.size(), size_t(3));
  env.key(Key::End);
  env.answer(2, {item("qqfo")});
  CHECK(!env.app->completion().isOpen());
  // Moving the caret away closes an open list.
  env.type(" cd");
  env.answer(env.completions.size() - 1, {item("cdef")});
  CHECK(env.app->completion().isOpen());
  env.key(Key::Left);
  env.key(Key::Left);
  env.key(Key::Left);
  CHECK(!env.app->completion().isOpen());
}

TEST(completion_approximate_items_are_greyed_and_marked) {
  Env env;
  env.fakeAll();
  env.app->completion().setApproximateFn([] { return true; });
  env.open("scratch.cpp");
  env.type("fo");
  env.answer(0, {}, "clangd not running");  // an error answer shows nothing
  CHECK(!env.app->completion().isOpen());
  env.type("o");
  env.answer(1, {item("foobar")});
  PopupBox* b = env.app->completion().box();
  REQUIRE(b != nullptr);
  REQUIRE(!b->lines().empty());
  bool dim = false;
  for (const PopupSpan& s : b->lines()[0].spans)
    if (s.text == "bar") dim |= s.style == PopupSpan::Style::Dim;
  CHECK(dim);
  REQUIRE(!b->side().empty());
  CHECK(b->side().back().find("approximate") != std::string::npos);
}

TEST(completion_multi_caret_applies_only_when_prefixes_agree) {
  Env env;
  env.fakeAll();
  env.open("scratch.cpp");
  env.ed->insertText("fo\nfo\nfo");
  env.ed->setSelections({caretAt({0, 2}), caretAt({1, 2}), caretAt({2, 2})});
  env.type("o");
  REQUIRE(!env.completions.empty());
  env.answer(env.completions.size() - 1, {item("foolish")});
  REQUIRE(env.app->completion().isOpen());
  CHECK(env.key(Key::Tab));
  CHECK_EQ(env.line(0), std::string("foolish"));
  CHECK_EQ(env.line(1), std::string("foolish"));  // all carets shared the prefix "foo"
  CHECK_EQ(env.line(2), std::string("foolish"));
  env.ed->undo();
  CHECK_EQ(env.line(2), std::string("foo"));  // one undo step for all carets

  // The third caret sits after "fo", not "foo": the primary caret alone is completed.
  env.completions.clear();
  env.ed->setSelections({caretAt({0, 3}), caretAt({1, 3}), caretAt({2, 2})});
  env.type("l");  // "fool", "fool", "fol"
  REQUIRE(!env.completions.empty());
  env.answer(env.completions.size() - 1, {item("foolish")});
  REQUIRE(env.app->completion().isOpen());
  CHECK(env.key(Key::Tab));
  CHECK_EQ(env.line(0), std::string("foolish"));
  CHECK_EQ(env.line(1), std::string("fool"));
  CHECK_EQ(env.line(2), std::string("folo"));  // the third caret typed its "l" between "fo" and "o"
}

TEST(completion_keeps_the_keystroke_to_paint_budget_with_a_big_list) {
  Env env;
  env.fakeAll();
  env.open("scratch.cpp");
  std::vector<LspCompletionItem> items;
  for (int i = 0; i < 5000; ++i)
    items.push_back(item(("symbol_number_" + std::to_string(i) + "_value").c_str(), "int"));
  env.type("sy");
  env.answer(0, items);
  REQUIRE(env.app->completion().isOpen());
  const std::string more = "mbol_n";
  double worst = 0, total = 0;
  int n = 0;
  for (char ch : more) {
    const auto t0 = std::chrono::steady_clock::now();
    env.send(textEv(std::string(1, ch)));  // event, refilter, popup content, layout and paint recording
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    worst = std::max(worst, ms);
    total += ms;
    ++n;
  }
  std::cerr << "    PERF: completion keystroke-to-paint with 5000 items open: avg " << total / n << " ms, worst "
            << worst << " ms (budget 8 ms)\n";
  CHECK(env.app->completion().isOpen());
#ifdef NDEBUG  // the budget is a Release number; sanitizer / Debug builds only report it
  CHECK(worst < 8.0);
#endif
}

// ---------------------------------------------------------------------------------------------------------------
// Hover
// ---------------------------------------------------------------------------------------------------------------

TEST(hover_after_dwell_shows_diagnostics_first_and_dismisses_on_move_and_key) {
  Env env;
  env.fakeAll();
  env.open("src/alpha.cpp");
  const fs::path file = env.ed->currentDocument()->path();
  Diagnostic d;
  d.file = file;
  d.range = {{2, 4}, {2, 13}};  // alphaOnly
  d.severity = 1;
  d.message = "use of undeclared identifier";
  env.ed->setDiagnostics(file, {d});
  env.frame();
  TextView* v = env.view();
  REQUIRE(v != nullptr);
  RectF r = v->caretRect({0, 5});  // inside "dup"
  env.send(moveEv(r.x + 2, r.y + 3));
  HoverTooltip& h = env.app->hover();
  env.advance(399);
  CHECK(env.hovers.empty() && !h.shown());
  env.advance(2);
  REQUIRE_EQ(env.hovers.size(), size_t(1));
  CHECK_EQ(env.hoverAt[0].line, 0);
  CHECK_EQ(env.hoverAt[0].column, 4);  // word start
  env.hovers[0]("```cpp\nint dup()\n```\nReturns one.", "");
  env.frame();
  REQUIRE(h.shown());
  REQUIRE(!h.box()->lines().empty());
  CHECK_EQ(h.box()->lines()[0].spans[0].text, std::string("int dup()"));  // fences removed
  // Moving within the word keeps it, moving off dismisses.
  env.send(moveEv(r.x + 10, r.y + 3));
  CHECK(h.shown());
  RectF far = v->caretRect({1, 40});
  env.send(moveEv(far.x, far.y + 3));
  CHECK(!h.shown());
  // Over a squiggle the diagnostic comes first, at once; the hover text is appended below it.
  r = v->caretRect({2, 6});
  env.send(moveEv(r.x + 2, r.y + 3));
  env.advance(450);
  REQUIRE(h.shown());
  CHECK_EQ(h.box()->lines()[0].spans[0].text, std::string("use of undeclared identifier"));
  CHECK(h.box()->lines()[0].spans[0].style == PopupSpan::Style::Error);
  const size_t before = h.box()->lines().size();
  env.hovers.back()("int alphaOnly()", "");
  env.frame();
  REQUIRE(h.box()->lines().size() > before);
  CHECK_EQ(h.box()->lines()[0].spans[0].text, std::string("use of undeclared identifier"));
  CHECK_EQ(h.box()->lines().back().spans[0].text, std::string("int alphaOnly()"));
  env.key(Key::Right);  // a key press dismisses
  CHECK(!h.shown());
  // A late answer for a dismissed hover shows nothing.
  r = v->caretRect({0, 5});
  env.send(moveEv(r.x + 2, r.y + 3));
  env.advance(450);
  env.key(Key::Right);
  env.hovers.back()("late", "");
  env.frame();
  CHECK(!h.shown());
}

// ---------------------------------------------------------------------------------------------------------------
// Signature help
// ---------------------------------------------------------------------------------------------------------------

TEST(signature_help_paren_scan_and_comma_counting) {
  TextBuffer b;
  b.insert({0, 0}, "foo(a, bar(1, 2), \"x,y\", c)\nnext(1,\n  2, [3, 4], {5, 6}, x");
  CHECK(!SignatureHelp::openParen(b, {0, 0}).has_value());
  CHECK_EQ(SignatureHelp::openParen(b, {0, 6})->column, 3);
  CHECK_EQ(SignatureHelp::openParen(b, {0, 15})->column, 10);  // inside bar(
  CHECK_EQ(SignatureHelp::openParen(b, {0, 17})->column, 3);   // bar(...) closed again
  CHECK(!SignatureHelp::openParen(b, {0, 27}).has_value());    // after foo(...)
  CHECK_EQ(SignatureHelp::commasBefore(b, {0, 3}, {0, 5}), 0);
  CHECK_EQ(SignatureHelp::commasBefore(b, {0, 3}, {0, 6}), 1);
  CHECK_EQ(SignatureHelp::commasBefore(b, {0, 3}, {0, 17}), 2);  // the commas inside bar( do not count
  CHECK_EQ(SignatureHelp::commasBefore(b, {0, 3}, {0, 24}), 3);  // a comma inside a string neither
  CHECK_EQ(SignatureHelp::commasBefore(b, {1, 4}, {2, 22}), 4);  // brackets and braces are skipped
  CHECK(SignatureHelp::openParen(b, {2, 22}).has_value());
}

TEST(signature_help_follows_commas_and_closes_on_paren_and_escape) {
  Env env;
  env.fakeAll();
  env.open("scratch.cpp");
  env.type("draw(");
  REQUIRE_EQ(env.sigs.size(), size_t(1));
  LspSignatureHelp h;
  LspSignatureHelp::Signature s;
  s.label = "void draw(int x, int y, Color c)";
  s.parameters = {"int x", "int y", "Color c"};
  h.signatures = {s};
  env.sigs[0](h, "");
  env.frame();
  SignatureHelp& sh = env.app->signatureHelp();
  REQUIRE(sh.isOpen());
  auto marked = [&] {
    for (const PopupSpan& sp : sh.box()->lines()[0].spans)
      if (sp.mark) return sp.text;
    return std::string("-");
  };
  CHECK_EQ(sh.activeParameter(), 0);
  CHECK_EQ(marked(), std::string("int x"));
  env.type("1,");
  CHECK_EQ(sh.activeParameter(), 1);
  CHECK_EQ(marked(), std::string("int y"));
  env.type(" f(2,");  // a nested call asks again; the outer one is shown again after it ends
  CHECK(env.sigs.size() >= 2);
  env.type("3)");
  env.type(",");
  CHECK_EQ(sh.activeParameter(), 2);
  CHECK(sh.isOpen());
  CHECK_EQ(marked(), std::string("Color c"));
  env.type(")");
  CHECK(!sh.isOpen());
  env.type("; draw(");
  const size_t n = env.sigs.size();
  env.sigs[n - 1](h, "");
  env.frame();
  REQUIRE(sh.isOpen());
  CHECK(env.key(Key::Escape));
  CHECK(!sh.isOpen());
  // Without clangd the answer is an error and nothing is shown.
  env.type("g(");
  env.sigs.back()({}, "clangd not running");
  env.frame();
  CHECK(!sh.isOpen());
}

// ---------------------------------------------------------------------------------------------------------------
// Diagnostics list
// ---------------------------------------------------------------------------------------------------------------

TEST(diagnostics_panel_lists_open_files_errors_first_and_click_jumps) {
  Env env;
  env.open("src/alpha.cpp");
  env.open("src/beta.cpp");
  auto mk = [&](const char* rel, int line, int sev, const char* msg) {
    Diagnostic d;
    d.file = env.dir / rel;
    d.range = {{line, 4}, {line, 7}};
    d.severity = sev;
    d.message = msg;
    return d;
  };
  env.ed->setDiagnostics(env.dir / "src/alpha.cpp",
                         {mk("src/alpha.cpp", 2, 2, "unused variable"), mk("src/alpha.cpp", 0, 1, "expected ';'")});
  env.ed->setDiagnostics(env.dir / "src/beta.cpp", {mk("src/beta.cpp", 1, 1, "unknown type name")});
  CHECK(env.key(Key('M'), ModCtrl | ModShift));
  ListPopup* p = env.popup();
  REQUIRE(p != nullptr);
  CHECK(p->status().find("open files") != std::string::npos);
  CHECK(primaries(p) == std::vector<std::string>{"expected ';'", "unknown type name", "unused variable"});
  CHECK_EQ(p->rows()[1].detail, std::string("src/beta.cpp:2"));
  env.type("unknown");  // filter
  CHECK(primaries(env.popup()) == std::vector<std::string>{"unknown type name"});
  env.ed->setDiagnostics(env.dir / "src/beta.cpp",
                         {mk("src/beta.cpp", 1, 1, "unknown type name"), mk("src/beta.cpp", 2, 1, "unknown member")});
  env.advance(10);  // live update
  CHECK_EQ(primaries(env.popup()).size(), size_t(2));
  env.open("src/main.cpp");
  CHECK(env.key(Key('M'), ModCtrl | ModShift));
  p = env.popup();
  REQUIRE(p != nullptr);
  p->setSelected(2);  // an error in beta.cpp
  CHECK(env.key(Key::Enter));
  CHECK_EQ(env.curFile(), std::string("beta.cpp"));
  CHECK(env.key(Key::Left, ModAlt));  // gotoLocation: Alt+Left returns
  CHECK_EQ(env.curFile(), std::string("main.cpp"));
}

// ---------------------------------------------------------------------------------------------------------------
// Goldens
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

TEST(b5b_golden_completion_popup_and_find_in_files) {
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
  waitForIndex(ed);
  Workbench wb(*tr, ed);
  wb.openFile("src/main.cpp");
  wb.sync();
  EditorApp app(wb, ed, &wb);
  const fs::path root = ed.workspace().root();
  int64_t now = 1000;
  auto send = [&](const InputEvent& e) {
    app.handleEvent(e, now += 10);
    ed.pump();
    app.tick(now);
  };
  // ---- completion popup over the editor ----
  std::vector<CompletionPopup::Done> pending;
  app.completion().setCompleteFn([&](CompletionPopup::Done d) { pending.push_back(std::move(d)); });
  app.completion().setApproximateFn([] { return false; });
  fx->renderer->setOffscreen(true);
  wb.render(*fx->renderer, 1024, 640);  // layout first: the popup positions itself inside the window
  ed.moveCursor({2, 0});
  for (char c : std::string("sha")) send(textEv(std::string(1, c)));
  REQUIRE(!pending.empty());
  auto mk = [](const char* label, const char* detail, const char* doc, int kind) {
    LspCompletionItem it;
    it.label = label;
    it.detail = detail;
    it.documentation = doc;
    it.insertText = it.filterText = it.sortText = label;
    it.kind = kind;
    return it;
  };
  pending.back()(
      {mk("shape_count", "int", "", 3), mk("Shape", "class geo::Shape", "Base class of all shapes.\nHas area().", 7),
       mk("make_shape", "std::unique_ptr<Shape>", "", 3), mk("shadow", "", "", 6), mk("Shapes", "", "", 22)},
      "");
  app.tick(now += 10);
  REQUIRE(app.completion().isOpen());
  gfx::GlRenderer& r = *fx->renderer;
  checkGolden(r, wb, *tr, "b5b_completion.png");
  // ---- find in files results ----
  app.completion().close();
  app.findInFiles().setSearchFn([&](const std::string&, SearchOptions, Editor::SearchBatch b, Editor::SearchDone d) {
    auto m = [&](const char* f, int line, int col, int len, const char* text) {
      SearchMatch s;
      s.file = root / f;
      s.line = line;
      s.column = col;
      s.length = len;
      s.text = text;
      return s;
    };
    b({m("src/geometry.cpp", 5, 15, 4, "double Circle::area() const { return 3.14159 * r * r; }"),
       m("src/geometry.cpp", 9, 9, 4, "  return area() * 2;"),
       m("include/geometry.h", 11, 17, 4, "  virtual double area() const = 0;"),
       m("src/main.cpp", 7, 18, 4, "  std::cout << s->area() << \"\\n\";")});
    SearchSummary s;
    s.matches = 4;
    s.filesMatched = 3;
    s.filesSearched = 7;
    d(s);
    return SearchHandle::create();
  });
  send(keyEv(Key('F'), ModCtrl | ModShift));
  send(textEv("area"));
  app.tick(now += 300);
  app.tick(now += 300);
  ListPopup* p = dynamic_cast<ListPopup*>(wb.modal());
  REQUIRE(p != nullptr);
  REQUIRE(p->rows().size() == 7);
  p->setSelected(2);
  checkGolden(r, wb, *tr, "b5b_find_in_files.png");
}
