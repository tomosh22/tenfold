#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>

#include "app/Editor.h"
#include "core/Document.h"
#include "core/Selection.h"
#include "test_main.h"

using namespace tf;
namespace fs = std::filesystem;

namespace {

Position P(int l, int c) { return {l, c}; }
Selection C(int l, int c) { return {{l, c}, {l, c}}; }
Selection S(int l1, int c1, int l2, int c2) { return {{l1, c1}, {l2, c2}}; }

// An Editor over a temp workspace with one open document and a manual clock.
struct Fx {
  fs::path dir;
  std::unique_ptr<Editor> ed;
  Document* doc = nullptr;
  std::shared_ptr<int64_t> now = std::make_shared<int64_t>(0);

  explicit Fx(const std::string& text, const char* grouping = "word") {
    static int counter = 0;
    dir = fs::temp_directory_path() /
          ("tenfold_mc_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_" +
           std::to_string(counter++));
    fs::create_directories(dir);
    {
      std::ofstream out(dir / "a.txt", std::ios::binary);
      out << text;
    }
    Editor::Options o;
    auto n = now;
    o.clock = [n] { return *n; };
    ed = std::make_unique<Editor>(dir, o);
    ed->workspace().settings().undoGrouping = grouping;
    doc = ed->open("a.txt");
    if (doc) ed->gotoLocation({doc->path(), {0, 0}});
  }
  ~Fx() {
    ed.reset();
    std::error_code ec;
    fs::remove_all(dir, ec);
  }
  std::string text() const { return doc->buffer().text(); }
  void set(std::vector<Selection> s) { ed->setSelections(std::move(s)); }
  void type(const std::string& s, int64_t gapMs = 50) {
    for (size_t i = 0; i < s.size();) {
      size_t len = 1;
      unsigned char c = static_cast<unsigned char>(s[i]);
      if (c >= 0xF0) len = 4;
      else if (c >= 0xE0) len = 3;
      else if (c >= 0xC0) len = 2;
      *now += gapMs;
      ed->insertText(s.substr(i, len));
      i += len;
    }
  }
  int undoAll() {
    int n = 0;
    while (ed->undo()) ++n;
    return n;
  }
};

}  // namespace

TEST(selection_helpers) {
  Selection s{{2, 5}, {1, 3}};
  CHECK(!s.empty());
  CHECK(s.reversed());
  CHECK(s.min() == P(1, 3));
  CHECK(s.max() == P(2, 5));
  CHECK(s.range() == (Range{P(1, 3), P(2, 5)}));
  Selection c = caretAt(P(4, 4));
  CHECK(c.empty());
  CHECK(!c.reversed());
  CHECK(c.range().empty());
}

TEST(multicaret_insert_same_line) {
  Fx f("abcdef\nxyz");
  f.set({C(0, 1), C(0, 3), C(0, 6)});
  f.ed->insertText("-");
  CHECK_EQ(f.text(), "a-bc-def-\nxyz");
  CHECK(f.ed->selections() == (std::vector<Selection>{C(0, 2), C(0, 5), C(0, 9)}));
  CHECK(f.ed->cursor().pos == P(0, 2));
  // One command is one undo step and restores the caret set.
  CHECK(f.ed->undo());
  CHECK_EQ(f.text(), "abcdef\nxyz");
  CHECK(f.ed->selections() == (std::vector<Selection>{C(0, 1), C(0, 3), C(0, 6)}));
  CHECK(!f.ed->undo());
  CHECK(f.ed->redo());
  CHECK_EQ(f.text(), "a-bc-def-\nxyz");
  CHECK(f.ed->selections() == (std::vector<Selection>{C(0, 2), C(0, 5), C(0, 9)}));
}

TEST(multicaret_primary_stays_first) {
  Fx f("abcdef");
  f.set({C(0, 4), C(0, 1)});  // primary is the right-hand caret
  CHECK(f.ed->selections() == (std::vector<Selection>{C(0, 4), C(0, 1)}));
  f.ed->insertText("X");
  CHECK_EQ(f.text(), "aXbcdXef");
  CHECK(f.ed->selections() == (std::vector<Selection>{C(0, 6), C(0, 2)}));
  CHECK(f.ed->cursor().pos == P(0, 6));
}

TEST(multicaret_insert_newlines_across_lines) {
  Fx f("ab\ncd\nef");
  f.set({C(0, 1), C(1, 1), C(2, 1)});
  f.ed->insertText("X\nYY");
  CHECK_EQ(f.text(), "aX\nYYb\ncX\nYYd\neX\nYYf");
  CHECK(f.ed->selections() == (std::vector<Selection>{C(1, 2), C(3, 2), C(5, 2)}));
  CHECK(f.ed->undo());
  CHECK_EQ(f.text(), "ab\ncd\nef");
  CHECK(f.ed->selections() == (std::vector<Selection>{C(0, 1), C(1, 1), C(2, 1)}));
}

TEST(multicaret_insert_at_line_ends_and_empty_lines) {
  Fx f("ab\n\ncd");
  f.set({C(0, 2), C(1, 0), C(2, 2)});
  f.ed->insertText("!");
  CHECK_EQ(f.text(), "ab!\n!\ncd!");
  CHECK(f.ed->selections() == (std::vector<Selection>{C(0, 3), C(1, 1), C(2, 3)}));
}

TEST(multicaret_adjacent_and_overlapping_merge) {
  {
    Fx f("abcdef");
    f.set({C(0, 3), C(0, 3), C(0, 1)});  // duplicate position
    CHECK_EQ(f.ed->selections().size(), 2u);
    CHECK(f.ed->selections()[0] == C(0, 3));
  }
  {
    Fx f("abcdef");
    f.set({S(0, 0, 0, 3), S(0, 2, 0, 5)});  // overlapping
    CHECK(f.ed->selections() == (std::vector<Selection>{S(0, 0, 0, 5)}));
    f.ed->insertText("Z");
    CHECK_EQ(f.text(), "Zf");
    CHECK(f.ed->selections() == (std::vector<Selection>{C(0, 1)}));
  }
  {
    Fx f("abcdef");
    f.set({S(0, 0, 0, 2), S(0, 2, 0, 4)});  // touching selections
    CHECK(f.ed->selections() == (std::vector<Selection>{S(0, 0, 0, 4)}));
  }
  {
    Fx f("abcdef");
    f.set({C(0, 3), S(0, 3, 0, 5), C(0, 5)});  // caret touching a selection on both sides
    CHECK(f.ed->selections() == (std::vector<Selection>{S(0, 3, 0, 5)}));
  }
  {
    Fx f("abcdef");
    f.set({C(0, 1), C(0, 2)});  // distinct neighbours stay separate
    CHECK_EQ(f.ed->selections().size(), 2u);
  }
}

TEST(multicaret_backspace_and_delete_merge_neighbours) {
  {
    Fx f("abcdef");
    f.set({C(0, 3), C(0, 4)});
    f.ed->backspace();  // deletes 'c' and 'd', both carets land on col 2
    CHECK_EQ(f.text(), "abef");
    CHECK(f.ed->selections() == (std::vector<Selection>{C(0, 2)}));
    CHECK(f.ed->undo());
    CHECK_EQ(f.text(), "abcdef");
    CHECK(f.ed->selections() == (std::vector<Selection>{C(0, 3), C(0, 4)}));
  }
  {
    Fx f("abcdef");
    f.set({C(0, 2), C(0, 3)});
    f.ed->del();
    CHECK_EQ(f.text(), "abef");
    CHECK(f.ed->selections() == (std::vector<Selection>{C(0, 2)}));
  }
  {
    Fx f("abcdef");
    f.set({C(0, 1), C(0, 3), C(0, 5)});
    f.ed->backspace();
    CHECK_EQ(f.text(), "bdf");
    CHECK(f.ed->selections() == (std::vector<Selection>{C(0, 0), C(0, 1), C(0, 2)}));
    f.ed->del();
    CHECK_EQ(f.text(), "");
    CHECK(f.ed->selections() == (std::vector<Selection>{C(0, 0)}));
  }
}

TEST(multicaret_backspace_delete_across_newlines) {
  Fx f("ab\ncd\nef");
  f.set({C(1, 0), C(2, 0)});
  f.ed->backspace();
  CHECK_EQ(f.text(), "abcdef");
  CHECK(f.ed->selections() == (std::vector<Selection>{C(0, 2), C(0, 4)}));
  CHECK(f.ed->undo());
  CHECK_EQ(f.text(), "ab\ncd\nef");
  f.set({C(0, 2), C(1, 2)});
  f.ed->del();
  CHECK_EQ(f.text(), "abcdef");
  CHECK(f.ed->selections() == (std::vector<Selection>{C(0, 2), C(0, 4)}));
  // Nothing to delete at the document edges: no edit, no undo step.
  Fx g("x");
  g.ed->backspace();
  CHECK_EQ(g.text(), "x");
  CHECK(!g.doc->canUndo());
  g.set({C(0, 1)});
  g.ed->del();
  CHECK_EQ(g.text(), "x");
  CHECK(!g.doc->canUndo());
}

TEST(multicaret_backspace_delete_multibyte) {
  Fx f("\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80x");  // e-acute(2) euro(3) emoji(4) x(1)
  f.set({C(0, 9)});
  f.ed->backspace();  // removes x? no: caret is after the emoji
  CHECK_EQ(f.text(),
           "\xC3\xA9\xE2\x82\xAC"
           "x");
  CHECK(f.ed->selections() == (std::vector<Selection>{C(0, 5)}));
  f.ed->backspace();
  CHECK_EQ(f.text(), "\xC3\xA9x");
  f.set({C(0, 0)});
  f.ed->del();
  CHECK_EQ(f.text(), "x");
  // Several carets, each next to a different multibyte character.
  Fx g("\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80");
  g.set({C(0, 2), C(0, 5), C(0, 9)});
  g.ed->backspace();
  CHECK_EQ(g.text(), "");
  CHECK(g.ed->selections() == (std::vector<Selection>{C(0, 0)}));
}

TEST(multicaret_insert_replaces_selections) {
  Fx f("hello world");
  f.set({S(0, 0, 0, 5), S(0, 6, 0, 11)});
  f.ed->insertText("X");
  CHECK_EQ(f.text(), "X X");
  CHECK(f.ed->selections() == (std::vector<Selection>{C(0, 1), C(0, 3)}));
  CHECK(f.ed->undo());
  CHECK_EQ(f.text(), "hello world");
  CHECK(f.ed->selections() == (std::vector<Selection>{S(0, 0, 0, 5), S(0, 6, 0, 11)}));
  // Reversed selection, backspace deletes the selection only.
  f.set({S(0, 5, 0, 0)});
  f.ed->backspace();
  CHECK_EQ(f.text(), " world");
  // Multi-line selection.
  Fx g("ab\ncd\nef");
  g.set({S(0, 1, 2, 1)});
  CHECK_EQ(g.ed->selectedText(), "b\ncd\ne");
  g.ed->insertText("-");
  CHECK_EQ(g.text(), "a-f");
}

TEST(multicaret_selected_text) {
  Fx f("one two three");
  f.set({S(0, 8, 0, 13), S(0, 0, 0, 3), C(0, 5)});
  CHECK_EQ(f.ed->selectedText(), "one\nthree");  // document order, carets skipped
}

TEST(multicaret_move_vertical_keeps_preferred_column) {
  Fx f("long line here\nab\nlong line again");
  f.set({C(0, 10)});
  f.ed->moveCarets(Editor::Direction::Down);
  CHECK(f.ed->cursor().pos == P(1, 2));
  f.ed->moveCarets(Editor::Direction::Down);
  CHECK(f.ed->cursor().pos == P(2, 10));  // preferred column restored
  f.ed->moveCarets(Editor::Direction::Up);
  f.ed->moveCarets(Editor::Direction::Up);
  CHECK(f.ed->cursor().pos == P(0, 10));
  f.ed->moveCarets(Editor::Direction::Up);  // top line: to the start of the document
  CHECK(f.ed->cursor().pos == P(0, 0));
  f.ed->moveCarets(Editor::Direction::Right);  // a horizontal move resets the preferred column
  f.ed->moveCarets(Editor::Direction::Down);
  CHECK(f.ed->cursor().pos == P(1, 1));
  f.ed->moveCarets(Editor::Direction::DocEnd);
  CHECK(f.ed->cursor().pos == P(2, 15));
  f.ed->moveCarets(Editor::Direction::Down);  // bottom line: to the end of the line
  CHECK(f.ed->cursor().pos == P(2, 15));
}

TEST(multicaret_move_vertical_utf8_never_inside_character) {
  Fx f(
      "a\xC3\xA9\xE2\x82\xAC"
      "b\nxxxxxx\n\xE2\x82\xAC\xE2\x82\xAC\xE2\x82\xAC");
  f.set({C(1, 3)});  // 3 code points in
  f.ed->moveCarets(Editor::Direction::Up);
  CHECK(f.ed->cursor().pos == P(0, 6));  // after a, e-acute, euro
  f.ed->moveCarets(Editor::Direction::Down);
  CHECK(f.ed->cursor().pos == P(1, 3));
  f.ed->moveCarets(Editor::Direction::Down);
  CHECK(f.ed->cursor().pos == P(2, 9));  // line 2 has only 3 code points: end of line
  f.set({C(1, 2)});
  f.ed->moveCarets(Editor::Direction::Down);
  CHECK(f.ed->cursor().pos == P(2, 6));  // 2 euro signs = 6 bytes
  // Horizontal moves step over whole characters.
  f.set({C(0, 1)});
  f.ed->moveCarets(Editor::Direction::Right);
  CHECK(f.ed->cursor().pos == P(0, 3));
  f.ed->moveCarets(Editor::Direction::Right);
  CHECK(f.ed->cursor().pos == P(0, 6));
  f.ed->moveCarets(Editor::Direction::Left);
  CHECK(f.ed->cursor().pos == P(0, 3));
  f.ed->moveCarets(Editor::Direction::Left);
  f.ed->moveCarets(Editor::Direction::Left);
  CHECK(f.ed->cursor().pos == P(0, 0));
  f.ed->moveCarets(Editor::Direction::Left);  // stays at the start of the document
  CHECK(f.ed->cursor().pos == P(0, 0));
}

TEST(multicaret_move_horizontal_and_extend) {
  Fx f("abc\ndef");
  f.set({C(0, 3)});
  f.ed->moveCarets(Editor::Direction::Right);  // wraps to the next line
  CHECK(f.ed->cursor().pos == P(1, 0));
  f.ed->moveCarets(Editor::Direction::Left);
  CHECK(f.ed->cursor().pos == P(0, 3));
  f.ed->moveCarets(Editor::Direction::Left, Editor::Unit::Char, true);
  f.ed->moveCarets(Editor::Direction::Left, Editor::Unit::Char, true);
  CHECK(f.ed->selections() == (std::vector<Selection>{S(0, 3, 0, 1)}));
  f.ed->moveCarets(Editor::Direction::Right);  // collapses to the right edge
  CHECK(f.ed->selections() == (std::vector<Selection>{C(0, 3)}));
  f.set({S(0, 3, 0, 1)});
  f.ed->moveCarets(Editor::Direction::Left);  // collapses to the left edge
  CHECK(f.ed->selections() == (std::vector<Selection>{C(0, 1)}));
  f.ed->moveCarets(Editor::Direction::LineEnd, Editor::Unit::Char, true);
  CHECK(f.ed->selections() == (std::vector<Selection>{S(0, 1, 0, 3)}));
  f.ed->moveCarets(Editor::Direction::DocStart, Editor::Unit::Char, true);
  CHECK(f.ed->selections() == (std::vector<Selection>{S(0, 1, 0, 0)}));
  f.ed->moveCarets(Editor::Direction::Down, Editor::Unit::Line, true);
  CHECK(f.ed->selections() == (std::vector<Selection>{S(0, 1, 1, 0)}));
  f.ed->moveCarets(Editor::Direction::LineStart);
  CHECK(f.ed->selections() == (std::vector<Selection>{C(1, 0)}));
}

TEST(multicaret_move_word) {
  Fx f("foo_bar  baz.qux(1)\n  next");
  f.set({C(0, 0)});
  auto right = [&] { f.ed->moveCarets(Editor::Direction::Right, Editor::Unit::Word); };
  auto left = [&] { f.ed->moveCarets(Editor::Direction::Left, Editor::Unit::Word); };
  right();
  CHECK(f.ed->cursor().pos == P(0, 9));  // identifier plus trailing blanks
  right();
  CHECK(f.ed->cursor().pos == P(0, 12));
  right();
  CHECK(f.ed->cursor().pos == P(0, 13));  // punctuation run
  left();
  CHECK(f.ed->cursor().pos == P(0, 12));
  left();
  CHECK(f.ed->cursor().pos == P(0, 9));
  left();
  CHECK(f.ed->cursor().pos == P(0, 0));
  f.set({C(0, 19)});
  right();  // end of line -> start of next line
  CHECK(f.ed->cursor().pos == P(1, 0));
  right();
  CHECK(f.ed->cursor().pos == P(1, 2));  // leading blanks
  right();
  CHECK(f.ed->cursor().pos == P(1, 6));
  left();
  CHECK(f.ed->cursor().pos == P(1, 2));
  left();
  CHECK(f.ed->cursor().pos == P(1, 0));
  left();
  CHECK(f.ed->cursor().pos == P(0, 19));  // back over the line break
  // Multibyte identifier characters count as identifier characters.
  Fx g("caf\xC3\xA9 x");
  g.set({C(0, 0)});
  g.ed->moveCarets(Editor::Direction::Right, Editor::Unit::Word);
  CHECK(g.ed->cursor().pos == P(0, 6));
  // Word extend and multiple carets.
  Fx h("aa bb\ncc dd");
  h.set({C(0, 0), C(1, 0)});
  h.ed->moveCarets(Editor::Direction::Right, Editor::Unit::Word, true);
  CHECK(h.ed->selections() == (std::vector<Selection>{S(0, 0, 0, 3), S(1, 0, 1, 3)}));
}

TEST(multicaret_move_page) {
  std::string t;
  for (int i = 0; i < 20; ++i) t += "line" + std::to_string(i) + (i < 19 ? "\n" : "");
  Fx f(t);
  f.ed->setPageLines(5);
  f.set({C(2, 3)});
  f.ed->moveCarets(Editor::Direction::PageDown);
  CHECK(f.ed->cursor().pos == P(7, 3));
  f.ed->moveCarets(Editor::Direction::Down, Editor::Unit::Page);
  CHECK(f.ed->cursor().pos == P(12, 3));
  f.ed->moveCarets(Editor::Direction::PageUp);
  f.ed->moveCarets(Editor::Direction::PageUp);
  CHECK(f.ed->cursor().pos == P(2, 3));
  f.ed->moveCarets(Editor::Direction::PageUp);  // clamps to the first line, keeping the column
  CHECK(f.ed->cursor().pos == P(0, 3));
}

TEST(multicaret_move_merges_carets) {
  Fx f("ab\ncd");
  f.set({C(0, 2), C(1, 0)});
  f.ed->moveCarets(Editor::Direction::Left);  // (0,1) and (0,2): fine
  CHECK_EQ(f.ed->selections().size(), 2u);
  f.set({C(0, 0), C(0, 1)});
  f.ed->moveCarets(Editor::Direction::Left);  // both end up at col 0
  CHECK(f.ed->selections() == (std::vector<Selection>{C(0, 0)}));
  f.set({C(0, 1), C(1, 1)});
  f.ed->moveCarets(Editor::Direction::LineStart);
  CHECK_EQ(f.ed->selections().size(), 2u);
  f.ed->moveCarets(Editor::Direction::DocStart);
  CHECK_EQ(f.ed->selections().size(), 1u);
}

TEST(multicaret_add_caret_above_below_short_lines) {
  Fx f("0123456789\nab\n0123456789\ncd");
  f.set({C(2, 8)});
  f.ed->addCaretAbove();
  CHECK(f.ed->selections() == (std::vector<Selection>{C(2, 8), C(1, 2)}.size() == 2
                                   ? std::vector<Selection>{C(2, 8), C(1, 2)}
                                   : std::vector<Selection>{}));
  f.ed->addCaretAbove();  // from the short line: preferred column 8 comes back
  CHECK(f.ed->selections() == (std::vector<Selection>{C(2, 8), C(0, 8), C(1, 2)}));
  CHECK(f.ed->cursor().pos == P(2, 8));  // primary unchanged
  f.ed->addCaretAbove();                 // already on line 0: nothing to add
  CHECK_EQ(f.ed->selections().size(), 3u);
  f.ed->insertText("|");
  CHECK_EQ(f.text(), "01234567|89\nab|\n01234567|89\ncd");

  Fx g("0123456789\nab\n0123456789\ncd");
  g.set({C(0, 8)});
  g.ed->addCaretBelow();
  g.ed->addCaretBelow();
  g.ed->addCaretBelow();
  CHECK(g.ed->selections() == (std::vector<Selection>{C(0, 8), C(1, 2), C(2, 8), C(3, 2)}));
  g.ed->addCaretBelow();  // last line
  CHECK_EQ(g.ed->selections().size(), 4u);
  g.ed->collapseToPrimary();
  CHECK(g.ed->selections() == (std::vector<Selection>{C(0, 8)}));
}

TEST(multicaret_add_caret_utf8_columns) {
  Fx f("\xE2\x82\xAC\xE2\x82\xAC\xE2\x82\xAC\nabc");
  f.set({C(0, 6)});  // after two euro signs (code point column 2)
  f.ed->addCaretBelow();
  CHECK(f.ed->selections() == (std::vector<Selection>{C(0, 6), C(1, 2)}));
}

TEST(multicaret_next_occurrence_and_wrap) {
  Fx f("foo bar foo baz foo");
  f.set({C(0, 9)});  // inside the middle foo
  f.ed->addCaretAtNextOccurrence();
  CHECK(f.ed->selections() == (std::vector<Selection>{S(0, 8, 0, 11)}));  // selected the word first
  f.ed->addCaretAtNextOccurrence();
  CHECK(f.ed->selections() == (std::vector<Selection>{S(0, 16, 0, 19), S(0, 8, 0, 11)}));
  f.ed->addCaretAtNextOccurrence();  // wraps to the start
  CHECK(f.ed->selections() == (std::vector<Selection>{S(0, 0, 0, 3), S(0, 8, 0, 11), S(0, 16, 0, 19)}));
  f.ed->addCaretAtNextOccurrence();  // everything selected: nothing new
  CHECK_EQ(f.ed->selections().size(), 3u);
  CHECK_EQ(f.ed->selectedText(), "foo\nfoo\nfoo");
  f.ed->insertText("X");
  CHECK_EQ(f.text(), "X bar X baz X");
  CHECK(f.ed->undo());
  CHECK_EQ(f.text(), "foo bar foo baz foo");
  CHECK_EQ(f.ed->selections().size(), 3u);

  Fx g("a\nfoo\nx foo\nfoo");
  g.set({C(1, 1)});
  g.ed->addCaretAtNextOccurrence();
  g.ed->addCaretAtNextOccurrence();
  g.ed->addCaretAtNextOccurrence();
  CHECK_EQ(g.ed->selections().size(), 3u);
  CHECK(g.ed->selections()[0] == S(3, 0, 3, 3));
  // A caret on no word does nothing.
  Fx h("  ");
  h.set({C(0, 1)});
  h.ed->addCaretAtNextOccurrence();
  CHECK(h.ed->selections() == (std::vector<Selection>{C(0, 1)}));
}

TEST(multicaret_next_occurrence_multiline_selection) {
  Fx f("ab\ncd\nxx\nab\ncd\n");
  f.set({S(0, 0, 1, 2)});
  f.ed->addCaretAtNextOccurrence();
  CHECK(f.ed->selections() == (std::vector<Selection>{S(3, 0, 4, 2), S(0, 0, 1, 2)}));
  f.ed->addCaretAtNextOccurrence();
  CHECK_EQ(f.ed->selections().size(), 2u);
}

TEST(multicaret_column_select_ragged_lines) {
  Fx f("abcdef\nab\nabcdefgh\n\nabcdef");
  f.ed->columnSelect(P(0, 2), P(4, 4));
  CHECK(f.ed->selections() ==
        (std::vector<Selection>{S(4, 2, 4, 4), S(0, 2, 0, 4), S(1, 2, 1, 2), S(2, 2, 2, 4), S(3, 0, 3, 0)}));
  CHECK_EQ(f.ed->selectedText(), "cd\ncd\ncd");
  f.ed->insertText("|");
  CHECK_EQ(f.text(), "ab|ef\nab|\nab|efgh\n|\nab|ef");
  CHECK(f.ed->undo());
  CHECK_EQ(f.text(), "abcdef\nab\nabcdefgh\n\nabcdef");
  CHECK_EQ(f.ed->selections().size(), 5u);

  // Dragging right-to-left / bottom-to-top gives reversed selections.
  Fx g("abcdef\nabcdef");
  g.ed->columnSelect(P(1, 4), P(0, 1));
  CHECK(g.ed->selections() == (std::vector<Selection>{S(0, 4, 0, 1), S(1, 4, 1, 1)}));
  // Vertical motion afterwards keeps the dragged column.
  g.ed->columnSelect(P(0, 3), P(0, 3));
  CHECK(g.ed->selections() == (std::vector<Selection>{C(0, 3)}));

  // Columns are counted in code points: line 0 is e-acute, euro, x.
  Fx h(
      "\xC3\xA9\xE2\x82\xAC"
      "x\nabc");
  h.ed->columnSelect(P(0, 0), P(1, 2));
  CHECK(h.ed->selections() == (std::vector<Selection>{S(1, 0, 1, 2), S(0, 0, 0, 5)}));
}

TEST(multicaret_selections_follow_external_edits) {
  Fx f("abc\ndef");
  f.set({C(1, 3), C(0, 1)});
  f.doc->erase({P(1, 1), P(1, 3)});  // an edit that did not go through the Editor
  CHECK(f.ed->selections()[0].head.column <= 1);
  f.doc->erase({P(0, 0), P(1, 1)});
  for (const Selection& s : f.ed->selections()) {
    CHECK(f.doc->buffer().clamp(s.head) == s.head);
    CHECK(f.doc->buffer().clamp(s.anchor) == s.anchor);
  }
}

TEST(multicaret_feeds_navigation_history_as_before) {
  std::string t;
  for (int i = 0; i < 100; ++i) t += "line" + std::to_string(i) + "\n";
  Fx f(t), ref(t);
  // The same far moves through moveCarets (several carets) and through the legacy moveCursor record the same history.
  f.set({C(0, 0), C(1, 0), C(2, 0)});
  f.ed->moveCarets(Editor::Direction::Down);  // near move: nothing new
  ref.ed->moveCursor(P(1, 0));
  f.ed->moveCarets(Editor::Direction::DocEnd);  // far move of the primary
  ref.ed->moveCursor(P(100, 0));
  CHECK(f.ed->cursor().pos == P(100, 0));
  CHECK_EQ(f.ed->selections().size(), 1u);  // all carets met at the end and merged
  f.ed->moveCarets(Editor::Direction::DocStart);
  ref.ed->moveCursor(P(0, 0));
  f.ed->moveCarets(Editor::Direction::PageDown);
  ref.ed->moveCursor(P(30, 0));
  auto& ea = f.ed->navigation().entries();
  auto& eb = ref.ed->navigation().entries();
  REQUIRE(ea.size() == eb.size());
  CHECK(ea.size() >= 2u);
  for (size_t i = 0; i < ea.size(); ++i) CHECK(ea[i].loc.pos == eb[i].loc.pos);

  // Edits above a history entry shift it, also for multi-caret edits.
  f.ed->moveCursor(P(50, 0));
  f.ed->moveCursor(P(90, 0));  // records line 50
  bool at50 = false;
  for (auto& e : f.ed->navigation().entries()) at50 = at50 || e.loc.pos.line == 50;
  CHECK(at50);
  f.set({C(0, 0), C(1, 0)});
  f.ed->insertText("\n");
  bool at52 = false;
  for (auto& e : f.ed->navigation().entries()) at52 = at52 || e.loc.pos.line == 52;
  CHECK(at52);
}

TEST(multicaret_legacy_api_still_works) {
  Fx f("hello\nworld");
  f.ed->moveCursor(P(0, 5));
  CHECK(f.ed->cursor().pos == P(0, 5));
  f.ed->insertText(", there");
  CHECK_EQ(f.text(), "hello, there\nworld");
  CHECK(f.ed->cursor().pos == P(0, 12));
  f.ed->deleteRange({P(0, 5), P(0, 12)});
  CHECK_EQ(f.text(), "hello\nworld");
  CHECK(f.ed->cursor().pos == P(0, 5));
  f.ed->replaceRange({P(1, 0), P(1, 5)}, "there");
  CHECK_EQ(f.text(), "hello\nthere");
  CHECK(f.ed->cursor().pos == P(1, 0));
  CHECK(f.doc->undo());  // the Document-level undo still works directly
  CHECK_EQ(f.text(), "hello\nworld");
  f.ed->moveCursor(P(99, 99));  // clamped
  CHECK(f.ed->cursor().pos == P(1, 5));
  CHECK_EQ(f.ed->selections().size(), 1u);
  f.ed->moveCursor(P(0, 0));
  f.set({C(0, 1), C(1, 1)});
  f.ed->moveCursor(P(0, 2));  // collapses to one caret
  CHECK_EQ(f.ed->selections().size(), 1u);
}

// --- undo grouping ---------------------------------------------------------------------------------------------

TEST(undo_group_typed_word_is_one_step) {
  Fx f("");
  f.type("hello world");
  CHECK_EQ(f.text(), "hello world");
  // Whitespace closes a step: "hello " and "world".
  CHECK(f.ed->undo());
  CHECK_EQ(f.text(), "hello ");
  CHECK(f.ed->selections() == (std::vector<Selection>{C(0, 6)}));
  CHECK(f.ed->undo());
  CHECK_EQ(f.text(), "");
  CHECK(f.ed->selections() == (std::vector<Selection>{C(0, 0)}));
  CHECK(!f.ed->undo());
  CHECK(f.ed->redo());
  CHECK_EQ(f.text(), "hello ");
  CHECK(f.ed->selections() == (std::vector<Selection>{C(0, 6)}));
  CHECK(f.ed->redo());
  CHECK_EQ(f.text(), "hello world");
  CHECK(f.ed->selections() == (std::vector<Selection>{C(0, 11)}));
  CHECK(!f.ed->redo());
}

TEST(undo_group_pause_boundary_uses_injected_clock) {
  Fx f("");
  f.type("ab");
  *f.now += 1000;  // exactly the interval: still the same step
  f.ed->insertText("c");
  CHECK(f.ed->undo());
  CHECK_EQ(f.text(), "");
  f.ed->redo();
  *f.now += 1001;  // longer pause: new step
  f.ed->insertText("d");
  CHECK_EQ(f.text(), "abcd");
  CHECK(f.ed->undo());
  CHECK_EQ(f.text(), "abc");
  CHECK(f.ed->undo());
  CHECK_EQ(f.text(), "");

  Fx g("", "word");
  g.ed->workspace().settings().undoGroupPauseMs = 100;
  g.type("ab", 100);
  g.type("c", 101);
  CHECK(g.ed->undo());
  CHECK_EQ(g.text(), "ab");
}

TEST(undo_group_char_mode) {
  Fx f("", "char");
  f.type("abc");
  CHECK(f.ed->undo());
  CHECK_EQ(f.text(), "ab");
  CHECK(f.ed->undo());
  CHECK_EQ(f.text(), "a");
  CHECK(f.ed->undo());
  CHECK_EQ(f.text(), "");
  CHECK(!f.ed->undo());
}

TEST(undo_group_boundaries_caret_move_backspace_paste_newline) {
  {  // caret moved by something other than typing
    Fx f("");
    f.type("ab");
    f.ed->moveCarets(Editor::Direction::Left);
    f.type("c");
    CHECK_EQ(f.text(), "acb");
    CHECK(f.ed->undo());
    CHECK_EQ(f.text(), "ab");
    CHECK(f.ed->undo());
    CHECK_EQ(f.text(), "");
  }
  {  // moveCursor and setSelections too
    Fx f("");
    f.type("ab");
    f.ed->moveCursor(P(0, 2));
    f.type("c");
    f.set({C(0, 3)});
    f.type("d");
    CHECK_EQ(f.undoAll(), 3);
  }
  {  // backspace after typing is its own step, typing after it starts a new one
    Fx f("");
    f.type("abc");
    f.ed->backspace();
    f.type("d");
    CHECK_EQ(f.text(), "abd");
    CHECK(f.ed->undo());
    CHECK_EQ(f.text(), "ab");
    CHECK(f.ed->undo());
    CHECK_EQ(f.text(), "abc");
    CHECK(f.ed->undo());
    CHECK_EQ(f.text(), "");
  }
  {  // multi-character insert (paste) never joins
    Fx f("");
    f.type("ab");
    f.ed->insertText("xyz");
    f.type("c");
    CHECK_EQ(f.text(), "abxyzc");
    CHECK_EQ(f.undoAll(), 3);
  }
  {  // a typed newline is whitespace: it ends the step
    Fx f("");
    f.type("a\nb");
    CHECK_EQ(f.text(), "a\nb");
    CHECK(f.ed->undo());
    CHECK_EQ(f.text(), "a\n");
    CHECK(f.ed->undo());
    CHECK_EQ(f.text(), "");
  }
  {  // typing over a selection starts its own step, restoring the selection on undo
    Fx f("hello");
    f.set({C(0, 5)});
    f.type("!");
    f.set({S(0, 0, 0, 5)});
    f.type("xy");  // replaces the selection; the next character continues that step
    CHECK_EQ(f.text(), "xy!");
    CHECK(f.ed->undo());
    CHECK_EQ(f.text(), "hello!");
    CHECK(f.ed->selections() == (std::vector<Selection>{S(0, 0, 0, 5)}));
    CHECK(f.ed->undo());
    CHECK_EQ(f.text(), "hello");
  }
  {  // an undo in between ends the step
    Fx f("");
    f.type("ab");
    CHECK(f.ed->undo());
    f.type("c");
    CHECK_EQ(f.text(), "c");
    CHECK(f.ed->undo());
    CHECK_EQ(f.text(), "");
    CHECK(!f.ed->undo());
  }
  {  // an edit made behind the editor's back ends the step
    Fx f("");
    f.type("ab");
    f.doc->insert(P(0, 0), "Z");
    f.type("c");
    CHECK(f.ed->undo());
    CHECK_EQ(f.text(), "Zab");
  }
}

TEST(undo_group_multicaret_typing_is_one_step_and_restores_carets) {
  Fx f("x\ny\nz");
  f.set({C(0, 1), C(1, 1), C(2, 1)});
  f.type("ab");
  CHECK_EQ(f.text(), "xab\nyab\nzab");
  CHECK(f.ed->undo());
  CHECK_EQ(f.text(), "x\ny\nz");
  CHECK(f.ed->selections() == (std::vector<Selection>{C(0, 1), C(1, 1), C(2, 1)}));
  CHECK(f.ed->redo());
  CHECK(f.ed->selections() == (std::vector<Selection>{C(0, 3), C(1, 3), C(2, 3)}));
}

TEST(undo_group_settings_in_workspace_json) {
  Fx f("");
  CHECK_EQ(f.ed->workspace().settings().undoGrouping, std::string("word"));
  CHECK_EQ(f.ed->workspace().settings().undoGroupPauseMs, 1000);
  fs::create_directories(f.dir / ".tenfold");
  {
    std::ofstream out(f.dir / ".tenfold" / "workspace.json");
    out << R"({"undoGrouping": "char", "undoGroupPauseMs": 250})";
  }
  CHECK(f.ed->workspace().loadSettings());
  CHECK_EQ(f.ed->workspace().settings().undoGrouping, std::string("char"));
  CHECK_EQ(f.ed->workspace().settings().undoGroupPauseMs, 250);
  {
    std::ofstream out(f.dir / ".tenfold" / "workspace.json");
    out << R"({"undoGrouping": "bogus", "undoGroupPauseMs": "soon"})";
  }
  CHECK(f.ed->workspace().loadSettings());
  CHECK_EQ(f.ed->workspace().settings().undoGrouping, std::string("char"));  // invalid values ignored
  CHECK_EQ(f.ed->workspace().settings().undoGroupPauseMs, 250);
  f.ed->workspace().settings().undoGrouping = "word";
  CHECK(f.ed->workspace().saveSettings());
  Workspace w(f.dir);
  CHECK(w.loadSettings());
  CHECK_EQ(w.settings().undoGrouping, std::string("word"));
  CHECK_EQ(w.settings().undoGroupPauseMs, 250);
}

TEST(undo_group_document_nesting) {
  Document d = Document::fromText("x.txt", "abc");
  d.beginGroup();
  d.insert(P(0, 3), "d");
  d.beginGroup();
  d.insert(P(0, 4), "e");
  d.endGroup();  // inner end does not close the step
  CHECK_EQ(d.groupDepth(), 1);
  CHECK(!d.undo());  // cannot undo while a group is open
  d.insert(P(0, 5), "f");
  d.endGroup();
  CHECK_EQ(d.groupDepth(), 0);
  CHECK_EQ(d.buffer().text(), "abcdef");
  d.insert(P(0, 6), "g");  // ungrouped edit: its own step
  CHECK(d.undo());
  CHECK_EQ(d.buffer().text(), "abcdef");
  CHECK(d.undo());
  CHECK_EQ(d.buffer().text(), "abc");
  CHECK(!d.undo());
  CHECK(d.redo());
  CHECK_EQ(d.buffer().text(), "abcdef");
  CHECK(d.redo());
  CHECK_EQ(d.buffer().text(), "abcdefg");
  CHECK(!d.redo());

  // Empty groups leave no step; unbalanced endGroup is ignored.
  Document e = Document::fromText("y.txt", "q");
  e.beginGroup();
  e.endGroup();
  e.endGroup();
  CHECK(!e.canUndo());
  CHECK_EQ(e.groupDepth(), 0);

  // A multi-line group undoes in reverse order.
  Document m = Document::fromText("m.txt", "1\n2\n3");
  m.beginGroup({C(0, 0)});
  m.erase({P(2, 0), P(2, 1)});
  m.insert(P(1, 1), "\nx\ny");
  m.insert(P(0, 0), "top\n");
  m.endGroup({C(9, 9)});
  const std::string after = m.buffer().text();
  CHECK(m.undo());
  CHECK_EQ(m.buffer().text(), "1\n2\n3");
  CHECK(m.restoredCarets() == (std::vector<Selection>{C(0, 0)}));
  CHECK(m.redo());
  CHECK_EQ(m.buffer().text(), after);
  CHECK(m.restoredCarets() == (std::vector<Selection>{C(9, 9)}));
}

TEST(undo_group_replace_returns_edit_result) {
  Document d = Document::fromText("x.txt", "abc\ndef");
  auto r = d.replace({P(0, 1), P(1, 1)}, "X\nY\nZ");
  CHECK_EQ(d.buffer().text(), "aX\nY\nZef");
  CHECK_EQ(r.lineDelta, 1);
  CHECK(r.inserted.end == P(2, 1));
}

// --- property test (risk register: multi-caret edits corrupting history / LSP sync) ---------------------------

namespace {

// Naive reference: a flat string plus carets as byte offsets; every operation is re-derived from scratch.
struct RefCaret {
  size_t a = 0, h = 0;
  size_t lo() const { return std::min(a, h); }
  size_t hi() const { return std::max(a, h); }
};
struct RefState {
  std::string text;
  std::vector<RefCaret> carets;  // ranges only matter; kept sorted + merged
};

bool isCont(char c) { return (static_cast<unsigned char>(c) & 0xC0) == 0x80; }

std::vector<RefCaret> mergeRef(std::vector<RefCaret> cs) {
  std::sort(cs.begin(), cs.end(),
            [](const RefCaret& x, const RefCaret& y) { return x.lo() != y.lo() ? x.lo() < y.lo() : x.hi() < y.hi(); });
  std::vector<RefCaret> out;
  for (const RefCaret& c : cs) {
    if (!out.empty() && c.lo() <= out.back().hi()) out.back() = {out.back().lo(), std::max(out.back().hi(), c.hi())};
    else out.push_back({c.lo(), c.hi()});
  }
  return out;
}

struct RefEdit {
  size_t lo, hi;
  std::string text;
};

// Applies `edits` (ascending, disjoint) to the state; returns false if nothing but no-ops.
bool refApply(RefState& st, const std::vector<RefEdit>& edits) {
  bool any = false;
  std::string out;
  size_t cursor = 0;
  std::vector<RefCaret> carets;
  for (const RefEdit& e : edits) {
    if (e.lo != e.hi || !e.text.empty()) any = true;
    out.append(st.text, cursor, e.lo - cursor);
    out += e.text;
    cursor = e.hi;
    carets.push_back({out.size(), out.size()});
  }
  out.append(st.text, cursor, std::string::npos);
  st.text = out;
  st.carets = mergeRef(carets);
  return any;
}

struct Rng {
  std::mt19937 g;
  explicit Rng(unsigned seed) : g(seed) {}
  int in(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(g); }
};

const char* kAlphabet[] = {"a", "b", "c", "\xC3\xA9", "\xE2\x82\xAC", "\xF0\x9F\x98\x80", " ", "\n", "_"};

std::string randomText(Rng& r, int maxPieces) {
  std::string s;
  int n = r.in(1, maxPieces);
  for (int i = 0; i < n; ++i) s += kAlphabet[r.in(0, 8)];
  return s;
}

size_t offsetOf(const std::string& text, Position p) {
  size_t off = 0;
  for (int l = 0; l < p.line; ++l) off = text.find('\n', off) + 1;
  return off + static_cast<size_t>(p.column);
}

std::vector<RefCaret> editorCaretsAsRef(const Fx& f, const std::string& text) {
  std::vector<RefCaret> out;
  for (const Selection& s : f.ed->selections()) out.push_back({offsetOf(text, s.anchor), offsetOf(text, s.head)});
  return out;
}

bool sameRanges(std::vector<RefCaret> a, std::vector<RefCaret> b) {
  auto key = [](const RefCaret& c) { return std::make_pair(c.lo(), c.hi()); };
  std::sort(a.begin(), a.end(), [&](const RefCaret& x, const RefCaret& y) { return key(x) < key(y); });
  std::sort(b.begin(), b.end(), [&](const RefCaret& x, const RefCaret& y) { return key(x) < key(y); });
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i)
    if (key(a[i]) != key(b[i])) return false;
  return true;
}

// Random aligned (character boundary) offset.
size_t randomBoundary(Rng& r, const std::string& t) {
  size_t o = static_cast<size_t>(r.in(0, static_cast<int>(t.size())));
  while (o < t.size() && isCont(t[o])) ++o;
  return o;
}

void runProperty(unsigned seed, int ops, bool charMode) {
  Fx f("ab\xC3\xA9 c\n\nfoo bar\n\xE2\x82\xAC x\nlast", charMode ? "char" : "word");
  REQUIRE(f.doc != nullptr);
  const std::string initial = f.text();
  Rng r(seed);

  // Shadow buffer fed only by Document change events: what an LSP server would reconstruct.
  TextBuffer shadow(initial);
  f.doc->addListener([&](Document&, const Document::Change& ch) { shadow.replace(ch.removed, ch.newText); });

  RefState ref;
  ref.text = initial;
  ref.carets = {{0, 0}};
  struct Step {
    RefState before, after;
  };
  std::vector<Step> undoStack, redoStack;  // only meaningful in char mode (one command = one step)

  auto setBoth = [&](const std::vector<RefCaret>& cs) {
    std::vector<Selection> sels;
    for (const RefCaret& c : cs) sels.push_back({f.doc->buffer().positionAt(c.a), f.doc->buffer().positionAt(c.h)});
    f.ed->setSelections(sels);
    ref.carets = mergeRef(cs);
  };

  for (int i = 0; i < ops; ++i) {
    *f.now += r.in(0, 1500);
    const int op = r.in(0, 99);
    if (op < 20) {  // new random caret / selection set
      std::vector<RefCaret> cs;
      int n = r.in(1, 6);
      for (int k = 0; k < n; ++k) {
        size_t a = randomBoundary(r, ref.text);
        size_t h = r.in(0, 2) == 0 ? randomBoundary(r, ref.text) : a;
        cs.push_back({a, h});
      }
      setBoth(cs);
    } else if (op < 55) {  // insert
      std::string t = r.in(0, 2) == 0 ? randomText(r, 4) : std::string(kAlphabet[r.in(0, 8)]);
      RefState before = ref;
      std::vector<RefEdit> edits;
      for (const RefCaret& c : ref.carets) edits.push_back({c.lo(), c.hi(), t});
      f.ed->insertText(t);
      if (refApply(ref, edits)) {
        undoStack.push_back({before, ref});
        redoStack.clear();
      }
    } else if (op < 70) {  // backspace
      RefState before = ref;
      std::vector<RefEdit> edits;
      for (const RefCaret& c : ref.carets) {
        size_t lo = c.lo();
        if (c.lo() == c.hi() && lo > 0) {
          --lo;
          while (lo > 0 && isCont(ref.text[lo])) --lo;
        }
        edits.push_back({lo, c.hi(), ""});
      }
      f.ed->backspace();
      if (refApply(ref, edits)) {
        undoStack.push_back({before, ref});
        redoStack.clear();
      }
    } else if (op < 80) {  // delete
      RefState before = ref;
      std::vector<RefEdit> edits;
      for (const RefCaret& c : ref.carets) {
        size_t hi = c.hi();
        if (c.lo() == c.hi() && hi < ref.text.size()) {
          ++hi;
          while (hi < ref.text.size() && isCont(ref.text[hi])) ++hi;
        }
        edits.push_back({c.lo(), hi, ""});
      }
      f.ed->del();
      if (refApply(ref, edits)) {
        undoStack.push_back({before, ref});
        redoStack.clear();
      }
    } else if (op < 88) {  // undo
      bool did = f.ed->undo();
      if (charMode) {
        CHECK_EQ(did, !undoStack.empty());
        if (!undoStack.empty()) {
          ref = undoStack.back().before;
          redoStack.push_back(std::move(undoStack.back()));
          undoStack.pop_back();
        }
      } else if (did) {
        ref.text = f.text();  // word mode: only compare text consistency below
        ref.carets = mergeRef(editorCaretsAsRef(f, ref.text));
      }
    } else if (op < 94) {  // redo
      bool did = f.ed->redo();
      if (charMode) {
        CHECK_EQ(did, !redoStack.empty());
        if (!redoStack.empty()) {
          ref = redoStack.back().after;
          undoStack.push_back(std::move(redoStack.back()));
          redoStack.pop_back();
        }
      } else if (did) {
        ref.text = f.text();
        ref.carets = mergeRef(editorCaretsAsRef(f, ref.text));
      }
    } else if (op < 97) {  // far single-caret jump (feeds the navigation history)
      size_t o = randomBoundary(r, ref.text);
      f.ed->moveCursor(f.doc->buffer().positionAt(o));
      ref.carets = {{o, o}};
    } else {  // horizontal char move without extend: reference is simple for single carets only
      f.ed->moveCarets(Editor::Direction::DocEnd);
      ref.carets = {{ref.text.size(), ref.text.size()}};
    }

    // 1. buffer text equals the reference; the incrementally-synced shadow equals both
    const std::string text = f.text();
    if (text != ref.text || shadow.text() != text) {
      tftest::fail(
          __FILE__, __LINE__,
          "text diverged: seed " + std::to_string(seed) + " op " + std::to_string(i) + " kind " + std::to_string(op));
      return;
    }
    // 2. carets equal the reference (as ranges) and are valid
    const auto& sels = f.ed->selections();
    if (!sameRanges(editorCaretsAsRef(f, text), ref.carets)) {
      tftest::fail(
          __FILE__, __LINE__,
          "carets diverged: seed " + std::to_string(seed) + " op " + std::to_string(i) + " kind " + std::to_string(op));
      return;
    }
    std::vector<Selection> sorted(sels.begin(), sels.end());
    std::sort(sorted.begin(), sorted.end(), [](const Selection& x, const Selection& y) { return x.min() < y.min(); });
    for (size_t k = 0; k < sorted.size(); ++k) {
      const TextBuffer& b = f.doc->buffer();
      bool ok = b.clamp(sorted[k].anchor) == sorted[k].anchor && b.clamp(sorted[k].head) == sorted[k].head;
      for (Position p : {sorted[k].anchor, sorted[k].head}) {
        const std::string& l = b.line(p.line);
        ok = ok && !(p.column < static_cast<int>(l.size()) && isCont(l[static_cast<size_t>(p.column)]));
      }
      if (k > 0) ok = ok && sorted[k].range().start > sorted[k - 1].range().end;
      if (!ok) {
        tftest::fail(__FILE__, __LINE__, "invalid caret: seed " + std::to_string(seed) + " op " + std::to_string(i));
        return;
      }
    }
    CHECK(f.ed->cursor().pos == sels[0].head);
    // 3. navigation history entries stay inside the buffer
    for (auto& e : f.ed->navigation().entries()) {
      if (e.loc.file != f.doc->path()) continue;
      if (e.loc.pos.line < 0 || e.loc.pos.line >= f.doc->buffer().lineCount() || e.loc.pos.column < 0) {
        tftest::fail(__FILE__, __LINE__,
                     "history out of bounds: seed " + std::to_string(seed) + " op " + std::to_string(i));
        return;
      }
    }
  }
  // Undoing everything restores the original text; redoing everything restores the final text.
  const std::string finalText = f.text();
  f.undoAll();
  CHECK_EQ(f.text(), initial);
  CHECK_EQ(shadow.text(), initial);
  while (f.ed->redo()) {}
  CHECK_EQ(f.text(), finalText);
  CHECK_EQ(shadow.text(), finalText);
}

}  // namespace

TEST(multicaret_property_random_edits_char_mode) {
  for (unsigned seed : {1u, 2u, 3u, 42u, 20240607u}) runProperty(seed, 2500, true);
}

TEST(multicaret_property_random_edits_word_mode) {
  for (unsigned seed : {7u, 8u, 9u}) runProperty(seed, 2500, false);
}

// --- benchmark (opt-in: TENFOLD_BENCH=1) -----------------------------------------------------------------------

TEST(multicaret_bench_keystroke) {
  if (!std::getenv("TENFOLD_BENCH")) return;
  std::string t;
  for (int i = 0; i < 100000; ++i) t += "    int value_" + std::to_string(i) + " = " + std::to_string(i) + ";  // x\n";
  Fx f(t);
  using Clk = std::chrono::steady_clock;
  auto ms = [](Clk::time_point a, Clk::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
  };
  f.ed->workspace().settings().undoGrouping = "char";

  f.ed->moveCursor(P(50000, 8));
  const int N = 200;
  auto t0 = Clk::now();
  for (int i = 0; i < N; ++i) f.ed->insertText("x");
  auto t1 = Clk::now();
  std::cerr << "    bench: insertText(\"x\") 1 caret, 100k lines: " << ms(t0, t1) / N << " ms/op\n";

  std::vector<Selection> many;
  for (int i = 0; i < 1000; ++i) many.push_back(C(i * 100, 4));
  f.ed->setSelections(many);
  const int M = 5;
  double ins = 0, und = 0, red = 0;
  for (int i = 0; i < M; ++i) {
    f.ed->setSelections(many);
    auto a = Clk::now();
    f.ed->insertText("x");
    auto b = Clk::now();
    f.ed->undo();
    auto c = Clk::now();
    f.ed->redo();
    auto d = Clk::now();
    f.ed->undo();
    ins += ms(a, b);
    und += ms(b, c);
    red += ms(c, d);
  }
  std::cerr << "    bench: insertText(\"x\") 1000 carets, 100k lines: " << ins / M << " ms/op\n";
  std::cerr << "    bench: undo of a 1000-caret edit: " << und / M << " ms, redo: " << red / M << " ms\n";
  f.ed->setSelections(many);
  auto a = Clk::now();
  f.ed->backspace();
  std::cerr << "    bench: backspace 1000 carets: " << ms(a, Clk::now()) << " ms\n";
}
