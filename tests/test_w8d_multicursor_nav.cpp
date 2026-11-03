// W8d: the 10x multi-cursor and navigation commands of the Editor: undo of SelectNextMultiCursor steps,
// SelectAllOccurrences, AlignMultiCursor, MultiCursorMoveLeft/Right, per-file navigation history, per-file bookmark
// stepping and removal, breakpoint removal per file (synced to the debugger backend) and the search/build result
// source.
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>

#include "app/Editor.h"
#include "core/Document.h"
#include "core/PathUtil.h"
#include "debug/FakeDebugger.h"
#include "test_main.h"

using namespace tf;
namespace fs = std::filesystem;

namespace {

Selection C(int l, int c) { return {{l, c}, {l, c}}; }
Selection S(int l1, int c1, int l2, int c2) { return {{l1, c1}, {l2, c2}}; }

struct Fx {
  fs::path dir;
  std::unique_ptr<Editor> ed;
  Document* doc = nullptr;

  explicit Fx(const std::string& text, const char* name = "a.cpp") {
    static int counter = 0;
    std::error_code cec;
    dir = fs::weakly_canonical(fs::temp_directory_path(), cec) /
          ("tenfold_w8d_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_" +
           std::to_string(counter++));
    fs::create_directories(dir);
    std::ofstream(dir / name, std::ios::binary) << text;
    Editor::Options o;
    o.semantic.useClangd = false;
    ed = std::make_unique<Editor>(dir, o);
    doc = ed->open(name);
    if (doc) ed->gotoLocation({doc->path(), {0, 0}});
  }
  ~Fx() {
    ed.reset();
    std::error_code ec;
    fs::remove_all(dir, ec);
  }
  void write(const char* name, const std::string& text) { std::ofstream(dir / name, std::ios::binary) << text; }
  fs::path file(const char* name = "a.cpp") const { return dir / name; }
  std::string text() const { return doc->buffer().text(); }
  void set(std::vector<Selection> s) { ed->setSelections(std::move(s)); }
  Selection primary() const { return ed->selections()[0]; }
};

std::string numbered(int n) {
  std::string s;
  for (int i = 0; i < n; ++i) s += "line" + std::to_string(i) + "\n";
  return s;
}

}  // namespace

// ---- UndoSelectNextMultiCursor ----

TEST(w8d_undo_select_next_steps_back_one_occurrence_at_a_time) {
  Fx f("foo bar foo baz foo\n");
  f.set({C(0, 1)});
  f.ed->addCaretAtNextOccurrence();  // selects the word
  CHECK(f.primary() == S(0, 0, 0, 3));
  f.ed->addCaretAtNextOccurrence();
  f.ed->addCaretAtNextOccurrence();
  CHECK_EQ(f.ed->selections().size(), size_t(3));
  CHECK(f.ed->undoAddCaretAtNextOccurrence());
  CHECK_EQ(f.ed->selections().size(), size_t(2));
  CHECK(f.primary() == S(0, 8, 0, 11));  // the second occurrence is the primary again
  CHECK(f.ed->undoAddCaretAtNextOccurrence());
  CHECK_EQ(f.ed->selections().size(), size_t(1));
  CHECK(f.primary() == S(0, 0, 0, 3));
  CHECK(f.ed->undoAddCaretAtNextOccurrence());  // back to the plain caret the first step started from
  CHECK(f.ed->selections().size() == 1 && f.primary() == C(0, 1));
  CHECK(!f.ed->undoAddCaretAtNextOccurrence());  // nothing left
}

TEST(w8d_undo_select_next_is_dropped_when_carets_or_text_changed) {
  Fx f("foo foo foo\n");
  f.set({C(0, 1)});
  f.ed->addCaretAtNextOccurrence();
  f.ed->addCaretAtNextOccurrence();
  f.ed->moveCarets(Editor::Direction::Right);  // the carets moved: the remembered step no longer applies
  CHECK(!f.ed->undoAddCaretAtNextOccurrence());
  CHECK(!f.ed->undoAddCaretAtNextOccurrence());  // and the stack is gone

  f.set({C(0, 1)});
  f.ed->addCaretAtNextOccurrence();
  f.ed->addCaretAtNextOccurrence();
  f.ed->insertText("x");  // an edit
  f.ed->undo();           // carets restored to the same place, text identical, but a different version
  CHECK(!f.ed->undoAddCaretAtNextOccurrence());
}

TEST(w8d_undo_select_next_ignores_steps_that_found_nothing) {
  Fx f("foo bar\n");
  f.set({C(0, 1)});
  f.ed->addCaretAtNextOccurrence();
  f.ed->addCaretAtNextOccurrence();  // no other occurrence: no step
  CHECK(f.ed->undoAddCaretAtNextOccurrence());
  CHECK(f.primary() == C(0, 1));
  CHECK(!f.ed->undoAddCaretAtNextOccurrence());
}

// ---- SelectAllOccurrences ----

TEST(w8d_select_all_occurrences_of_word_and_selection) {
  Fx f("foo foobar foo\nxfoo foo\n");
  f.set({C(0, 1)});  // the word foo, whole word
  CHECK_EQ(f.ed->selectAllOccurrences(), size_t(3));
  CHECK_EQ(f.ed->selections().size(), size_t(3));
  CHECK(f.primary() == S(0, 0, 0, 3));  // the one at the caret is primary
  CHECK(f.ed->selections()[1] == S(0, 11, 0, 14));
  CHECK(f.ed->selections()[2] == S(1, 5, 1, 8));

  f.set({S(0, 0, 0, 3)});  // a selection: its text, anywhere
  CHECK_EQ(f.ed->selectAllOccurrences(), size_t(5));
  f.ed->insertText("Z");
  CHECK_EQ(f.text(), "Z Zbar Z\nxZ Z\n");
  CHECK(f.ed->undo());
  CHECK_EQ(f.text(), "foo foobar foo\nxfoo foo\n");
}

TEST(w8d_select_all_occurrences_with_find_options_and_primary_after_caret) {
  Fx f("Foo foo FOO foo\n");
  SearchOptions o;
  o.query = "foo";
  o.caseSensitive = false;
  f.set({C(0, 5)});
  CHECK_EQ(f.ed->selectAllOccurrences(&o), size_t(4));
  CHECK(f.primary() == S(0, 4, 0, 7));  // contains the caret
  o.caseSensitive = true;
  f.set({C(0, 8)});  // between matches: the next one is primary
  CHECK_EQ(f.ed->selectAllOccurrences(&o), size_t(2));
  CHECK(f.primary() == S(0, 12, 0, 15));
  o.query = "(";
  o.regex = true;
  std::string err;
  CHECK_EQ(f.ed->selectAllOccurrences(&o, &err), size_t(0));
  CHECK(!err.empty());
  CHECK_EQ(f.ed->selections().size(), size_t(2));  // unchanged by the failed search
}

TEST(w8d_select_all_occurrences_without_a_word_does_nothing) {
  Fx f("a  b\n");
  f.set({C(0, 2)});
  CHECK_EQ(f.ed->selectAllOccurrences(), size_t(0));
  CHECK_EQ(f.ed->selections().size(), size_t(1));
}

// ---- AlignMultiCursor ----

TEST(w8d_align_pads_to_the_rightmost_caret_in_one_undo_step) {
  Fx f("a = 1\nlonger = 2\nxy = 3\n");
  f.set({C(0, 1), C(1, 6), C(2, 2)});
  CHECK(f.ed->alignCarets());
  CHECK_EQ(f.text(), "a      = 1\nlonger = 2\nxy     = 3\n");
  for (const Selection& s : f.ed->selections()) CHECK_EQ(s.head.column, 6);
  f.ed->insertText("|");
  CHECK_EQ(f.text(), "a     | = 1\nlonger| = 2\nxy    | = 3\n");
  CHECK(f.ed->undo());  // the typing
  CHECK(f.ed->undo());  // the alignment
  CHECK_EQ(f.text(), "a = 1\nlonger = 2\nxy = 3\n");
  f.set({C(0, 1)});
  CHECK(!f.ed->alignCarets());  // a single caret
}

TEST(w8d_align_counts_tabs_as_tab_stops) {
  Fx f("\tx = 1\nab = 2\n");
  f.ed->editing().tabWidth = 4;
  f.set({C(0, 2), C(1, 2)});  // visual columns 5 and 2
  CHECK(f.ed->alignCarets());
  CHECK_EQ(f.text(), "\tx = 1\nab    = 2\n");
  CHECK_EQ(f.ed->selections()[0].head.column, 2);  // the tab line is untouched
  CHECK_EQ(f.ed->selections()[1].head.column, 5);
  f.ed->undo();
  f.ed->editing().tabWidth = 8;  // the tab is eight columns wide: x ends at column 9
  f.set({C(0, 2), C(1, 2)});
  CHECK(f.ed->alignCarets());
  CHECK_EQ(f.text(), "\tx = 1\nab        = 2\n");
}

TEST(w8d_align_when_already_aligned_or_several_carets_on_a_line) {
  Fx f("ab cd\nab cd\n");
  f.set({C(0, 2), C(1, 2)});
  CHECK(!f.ed->alignCarets());
  CHECK_EQ(f.text(), "ab cd\nab cd\n");
  Fx g("ab cd\nx\n");
  g.set({C(0, 2), C(0, 5), C(1, 1)});  // rightmost is column 5; only the first caret of line 0 moves
  CHECK(g.ed->alignCarets());
  CHECK_EQ(g.text(), "ab    cd\nx    \n");
  CHECK_EQ(g.ed->selections().size(), size_t(3));
}

TEST(w8d_align_with_a_selection_collapses_it_to_the_head) {
  Fx f("abc\nxyzw\n");
  f.set({S(0, 0, 0, 1), C(1, 4)});
  CHECK(f.ed->alignCarets());
  CHECK_EQ(f.text(), "a   bc\nxyzw\n");
}

// ---- MultiCursorMoveLeft / Right ----

TEST(w8d_adjust_selection_ends_grows_and_shrinks_every_selection) {
  Fx f("abcdef\nghijkl\n");
  f.set({S(0, 1, 0, 3), S(1, 1, 1, 3)});
  f.ed->adjustSelectionEnds(true);  // "Increase selection end"
  CHECK(f.ed->selections()[0] == S(0, 1, 0, 4));
  CHECK(f.ed->selections()[1] == S(1, 1, 1, 4));
  f.ed->adjustSelectionEnds(false);  // "Decrease selection end"
  f.ed->adjustSelectionEnds(false);
  CHECK(f.ed->selections()[0] == S(0, 1, 0, 2));
  f.ed->adjustSelectionEnds(false);
  CHECK(f.ed->selections()[0] == C(0, 1));  // never past the start
  f.ed->adjustSelectionEnds(false);
  CHECK(f.ed->selections()[0] == C(0, 1));
  f.ed->adjustSelectionEnds(true);  // a caret becomes a one-character selection
  CHECK(f.ed->selections()[0] == S(0, 1, 0, 2));
  CHECK_EQ(f.text(), "abcdef\nghijkl\n");  // not an edit
}

TEST(w8d_adjust_selection_ends_keeps_reversed_selections_reversed_and_crosses_lines) {
  Fx f("ab\ncd\n");
  f.set({S(0, 2, 0, 0)});  // reversed: head at the start, the end is the anchor
  f.ed->adjustSelectionEnds(true);
  CHECK(f.primary() == S(1, 0, 0, 0));  // the end crossed the line break
  f.ed->adjustSelectionEnds(false);
  CHECK(f.primary() == S(0, 2, 0, 0));
  f.set({C(0, 2)});
  f.ed->adjustSelectionEnds(true);
  CHECK(f.primary() == S(0, 2, 1, 0));
}

TEST(w8d_adjust_selection_ends_merges_selections_that_meet) {
  Fx f("abcd\n");
  f.set({S(0, 0, 0, 1), S(0, 2, 0, 3)});
  f.ed->adjustSelectionEnds(true);  // [0,2) and [2,4) touch
  CHECK_EQ(f.ed->selections().size(), size_t(1));
  CHECK(f.primary() == S(0, 0, 0, 4));
}

// ---- AddCursorAtMousePos (engine part) ----

TEST(w8d_add_caret_at_makes_the_new_caret_primary_and_keeps_the_others) {
  Fx f("abc\ndef\n");
  f.set({C(0, 1)});
  f.ed->addCaretAt({1, 2});
  CHECK_EQ(f.ed->selections().size(), size_t(2));
  CHECK(f.primary() == C(1, 2));
  CHECK(f.ed->selections()[1] == C(0, 1));
  f.ed->addCaretAt({1, 2});  // on an existing caret: merges
  CHECK_EQ(f.ed->selections().size(), size_t(2));
  f.ed->addCaretAt({9, 9});  // clamped
  CHECK(f.primary() == C(2, 0));
}

// ---- Prev/NextLocationCurrentFile ----

TEST(w8d_history_in_file_skips_other_files_and_keeps_order) {
  Fx f(numbered(100));
  f.write("b.cpp", numbered(100));
  const fs::path a = f.file("a.cpp"), b = f.file("b.cpp");
  // a:10 -> a:50 -> b:5 -> b:70 -> a:90 -> b:20 (live)
  REQUIRE(f.ed->gotoLocation({a, {10, 0}}));
  REQUIRE(f.ed->gotoLocation({a, {50, 0}}));
  REQUIRE(f.ed->gotoLocation({b, {5, 0}}));
  REQUIRE(f.ed->gotoLocation({b, {70, 0}}));
  REQUIRE(f.ed->gotoLocation({a, {90, 0}}));
  REQUIRE(f.ed->gotoLocation({b, {20, 0}}));
  CHECK(pathsEqual(f.ed->cursor().file, b));
  // back in b: b:70, then b:5, then nothing
  CHECK(f.ed->backInFile());
  CHECK(pathsEqual(f.ed->cursor().file, b));
  CHECK_EQ(f.ed->cursor().pos.line, 70);
  CHECK(f.ed->backInFile());
  CHECK_EQ(f.ed->cursor().pos.line, 5);
  CHECK(!f.ed->backInFile());
  CHECK_EQ(f.ed->cursor().pos.line, 5);
  // forward in b: 70, then the live position 20, then nothing
  CHECK(f.ed->forwardInFile());
  CHECK_EQ(f.ed->cursor().pos.line, 70);
  CHECK(f.ed->forwardInFile());
  CHECK_EQ(f.ed->cursor().pos.line, 20);
  CHECK(!f.ed->forwardInFile());
  // the ordinary Back still walks every file, from the live position
  CHECK(f.ed->back());
  CHECK(pathsEqual(f.ed->cursor().file, a));
  CHECK_EQ(f.ed->cursor().pos.line, 90);
}

TEST(w8d_history_in_file_from_another_file_only_sees_that_files_entries) {
  Fx f(numbered(100));
  f.write("b.cpp", numbered(100));
  const fs::path a = f.file("a.cpp"), b = f.file("b.cpp");
  REQUIRE(f.ed->gotoLocation({a, {10, 0}}));
  REQUIRE(f.ed->gotoLocation({a, {60, 0}}));
  REQUIRE(f.ed->gotoLocation({b, {30, 0}}));
  // b has no older entry: nothing happens and the history is left alone
  size_t before = f.ed->navigation().entries().size();
  size_t cursor = f.ed->navigation().cursor();
  CHECK(!f.ed->backInFile());
  CHECK(!f.ed->forwardInFile());
  CHECK_EQ(f.ed->navigation().entries().size(), before);
  CHECK_EQ(f.ed->navigation().cursor(), cursor);
  CHECK(pathsEqual(f.ed->cursor().file, b));
  // back in a file: the entries of b in between are skipped
  REQUIRE(f.ed->gotoLocation({a, {5, 0}}));
  CHECK(f.ed->backInFile());
  CHECK(pathsEqual(f.ed->cursor().file, a));
  CHECK_EQ(f.ed->cursor().pos.line, 60);
  CHECK(f.ed->backInFile());
  CHECK_EQ(f.ed->cursor().pos.line, 10);
}

TEST(w8d_history_in_file_does_not_add_entries_when_walking) {
  Fx f(numbered(200));
  const fs::path a = f.file("a.cpp");
  REQUIRE(f.ed->gotoLocation({a, {20, 0}}));
  REQUIRE(f.ed->gotoLocation({a, {80, 0}}));
  REQUIRE(f.ed->gotoLocation({a, {150, 0}}));
  CHECK(f.ed->backInFile());
  const size_t n = f.ed->navigation().entries().size();
  CHECK_EQ(f.ed->cursor().pos.line, 80);
  CHECK(f.ed->backInFile());
  CHECK_EQ(f.ed->cursor().pos.line, 20);
  CHECK_EQ(f.ed->navigation().entries().size(), n);
  CHECK(f.ed->forwardInFile());
  CHECK_EQ(f.ed->cursor().pos.line, 80);
}

// ---- bookmarks in the current file ----

TEST(w8d_bookmark_in_file_steps_within_the_file_and_wraps) {
  Fx f(numbered(40));
  f.write("b.cpp", numbered(40));
  const fs::path a = f.file("a.cpp"), b = f.file("b.cpp");
  f.set({C(5, 0)});
  f.ed->toggleBookmark();
  f.set({C(20, 0)});
  f.ed->toggleBookmark();
  REQUIRE(f.ed->gotoLocation({b, {7, 0}}));
  f.ed->toggleBookmark();  // a bookmark in the other file must not be visited
  REQUIRE(f.ed->gotoLocation({a, {0, 0}}));
  CHECK(f.ed->gotoBookmarkInFile(true));
  CHECK_EQ(f.ed->cursor().pos.line, 5);
  CHECK(f.ed->gotoBookmarkInFile(true));
  CHECK_EQ(f.ed->cursor().pos.line, 20);
  CHECK(f.ed->gotoBookmarkInFile(true));  // wraps to the first of this file
  CHECK_EQ(f.ed->cursor().pos.line, 5);
  CHECK(pathsEqual(f.ed->cursor().file, a));
  CHECK(f.ed->gotoBookmarkInFile(false));  // wraps backwards to the last
  CHECK_EQ(f.ed->cursor().pos.line, 20);
  CHECK(f.ed->gotoBookmarkInFile(false));
  CHECK_EQ(f.ed->cursor().pos.line, 5);
  // jumps are history entries (gotoLocation): Back returns
  CHECK(f.ed->back());
  CHECK_EQ(f.ed->cursor().pos.line, 20);
  // the global stepper still crosses files
  REQUIRE(f.ed->gotoLocation({a, {30, 0}}));
  CHECK(f.ed->gotoBookmark(true));
  CHECK(pathsEqual(f.ed->cursor().file, b));
}

TEST(w8d_bookmark_in_file_without_bookmarks_is_false) {
  Fx f(numbered(10));
  f.write("b.cpp", numbered(10));
  REQUIRE(f.ed->gotoLocation({f.file("b.cpp"), {3, 0}}));
  f.ed->toggleBookmark();
  REQUIRE(f.ed->gotoLocation({f.file("a.cpp"), {3, 0}}));
  CHECK(!f.ed->gotoBookmarkInFile(true));
  CHECK(!f.ed->gotoBookmarkInFile(false));
  CHECK_EQ(f.ed->cursor().pos.line, 3);
}

TEST(w8d_remove_all_bookmarks_from_file_keeps_the_others_and_persists) {
  Fx f(numbered(40));
  f.write("b.cpp", numbered(40));
  f.set({C(1, 0)});
  f.ed->toggleBookmark();
  f.set({C(9, 0)});
  f.ed->toggleBookmark();
  REQUIRE(f.ed->gotoLocation({f.file("b.cpp"), {4, 0}}));
  f.ed->toggleBookmark();
  REQUIRE(f.ed->gotoLocation({f.file("a.cpp"), {0, 0}}));
  CHECK_EQ(f.ed->removeBookmarksInFile(), size_t(2));
  CHECK(f.ed->bookmarks().lines(f.file("a.cpp")).empty());
  CHECK_EQ(f.ed->bookmarks().lines(f.file("b.cpp")).size(), size_t(1));
  CHECK_EQ(f.ed->removeBookmarksInFile(), size_t(0));
  Bookmarks loaded;  // saved to .tenfold/bookmarks.json
  REQUIRE(loaded.load(f.ed->workspace().cacheDir() / "bookmarks.json", f.ed->workspace().root()));
  CHECK_EQ(loaded.count(), size_t(1));
}

// ---- breakpoints in the current file ----

TEST(w8d_remove_all_breakpoints_from_file_is_synced_to_the_backend) {
  Fx f(numbered(40));
  f.write("b.cpp", numbered(40));
  auto fake = std::make_shared<FakeDebugger::State>();
  f.ed->debug().setPollIntervals(2, 2);
  f.ed->debug().setBackend(std::make_unique<FakeDebugger>(fake));
  const auto lines = [&](const char* name) {
    std::vector<int> v;
    for (const Breakpoint& b : fake->breakpoints())
      if (b.file.filename() == name) v.push_back(b.line);
    return v;
  };
  f.set({C(2, 0)});
  f.ed->toggleBreakpoint();
  f.set({C(8, 0)});
  f.ed->toggleBreakpoint();
  REQUIRE(f.ed->gotoLocation({f.file("b.cpp"), {5, 0}}));
  f.ed->toggleBreakpoint();
  f.ed->debug().connect();
  REQUIRE(f.ed->waitFor([&] { return f.ed->debug().mode() == DebugMode::Design; }, 3000));
  REQUIRE(f.ed->waitFor([&] { return fake->breakpoints().size() == 3; }, 3000));
  REQUIRE(f.ed->gotoLocation({f.file("a.cpp"), {0, 0}}));

  CHECK_EQ(f.ed->removeBreakpointsInFile(), size_t(2));
  CHECK(f.ed->breakpoints().lines(f.file("a.cpp")).empty());
  CHECK_EQ(f.ed->breakpoints().lines(f.file("b.cpp")).size(), size_t(1));
  CHECK(f.ed->waitFor([&] { return lines("a.cpp").empty(); }, 3000));
  CHECK_EQ(lines("b.cpp").size(), size_t(1));
  CHECK_EQ(f.ed->removeBreakpointsInFile(), size_t(0));
  Breakpoints loaded;  // saved to .tenfold/breakpoints.json
  REQUIRE(loaded.load(f.ed->workspace().cacheDir() / "breakpoints.json", f.ed->workspace().root()));
  CHECK_EQ(loaded.count(), size_t(1));
}

// ---- GotoNextResult / GotoPrevResult source ----

TEST(w8d_result_source_follows_the_last_search_or_build) {
  Fx f("int a;\n");
  CHECK(f.ed->lastResultSource() == Editor::ResultSource::None);
  bool done = false;
  f.ed->findInWorkspace("int", {}, {}, [&](const SearchSummary&) { done = true; });
  CHECK(f.ed->lastResultSource() == Editor::ResultSource::Search);
  REQUIRE(f.ed->waitFor([&] { return done; }, 10000));

  BuildOverrides o;
#ifdef _WIN32
  o.command = "echo a.cpp:1:5: error: bad";
#else
  o.command = "echo 'a.cpp:1:5: error: bad'";
#endif
  bool built = false;
  std::string err;
  REQUIRE(f.ed->build(o, {}, [&](const BuildResult&) { built = true; }, &err));
  CHECK(f.ed->lastResultSource() == Editor::ResultSource::Build);
  REQUIRE(f.ed->waitFor([&] { return built; }, 20000));
  CHECK(f.ed->lastResultSource() == Editor::ResultSource::Build);

  done = false;
  f.ed->findInWorkspace("int", {}, {}, [&](const SearchSummary&) { done = true; });
  CHECK(f.ed->lastResultSource() == Editor::ResultSource::Search);
  REQUIRE(f.ed->waitFor([&] { return done; }, 10000));
  f.ed->noteResultSource(Editor::ResultSource::Build);  // a front end running its own search says so the same way
  CHECK(f.ed->lastResultSource() == Editor::ResultSource::Build);
}
