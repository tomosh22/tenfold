// B8 editing conveniences: every Editor command, with several carets, undone as ONE step.
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>

#include "app/Editor.h"
#include "core/Document.h"
#include "test_main.h"

using namespace tf;
namespace fs = std::filesystem;

namespace {

Selection C(int l, int c) { return {{l, c}, {l, c}}; }
Selection S(int l1, int c1, int l2, int c2) { return {{l1, c1}, {l2, c2}}; }

std::string numbered(int n) {  // "0\n1\n...\n<n-1>"
  std::string s;
  for (int i = 0; i < n; ++i) s += (i ? "\n" : "") + std::to_string(i);
  return s;
}

struct Fx {
  fs::path dir;
  std::unique_ptr<Editor> ed;
  Document* doc = nullptr;

  explicit Fx(const std::string& text, const char* name = "a.cpp") {
    static int counter = 0;
    dir = fs::temp_directory_path() /
          ("tenfold_ed_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_" +
           std::to_string(counter++));
    fs::create_directories(dir);
    {
      std::ofstream out(dir / name, std::ios::binary);
      out << text;
    }
    ed = std::make_unique<Editor>(dir);
    doc = ed->open(name);
    if (doc) ed->gotoLocation({doc->path(), {0, 0}});
  }
  ~Fx() {
    ed.reset();
    std::error_code ec;
    fs::remove_all(dir, ec);
  }
  std::string text() const { return doc->buffer().text(); }
  void set(std::vector<Selection> s) { ed->setSelections(std::move(s)); }
  Selection primary() const { return ed->selections()[0]; }
  Position caret() const { return primary().head; }
  void type(const std::string& s) {
    for (char c : s) {
      if (c == '\n') ed->insertNewline();
      else ed->typeText(std::string(1, c));
    }
  }
  // Undo until the document is back to `original`; the number of steps taken.
  int undoAll() {
    int n = 0;
    while (ed->undo()) ++n;
    return n;
  }
  // One command must be one undo step: after a single undo the text is `before`.
  void expectOneStep(const std::string& before) {
    CHECK(ed->undo());
    CHECK_EQ(text(), before);
  }
};

}  // namespace

// --- auto-indent ----------------------------------------------------------------------------------------------

TEST(edit_newline_copies_indent) {
  Fx f("    foo");
  f.set({C(0, 7)});
  f.ed->insertNewline();
  CHECK_EQ(f.text(), "    foo\n    ");
  CHECK(f.caret() == (Position{1, 4}));
  f.expectOneStep("    foo");
}

TEST(edit_newline_after_open_brace_indents_and_splits_pair) {
  Fx f("if (x) {");
  f.set({C(0, 8)});
  f.ed->insertNewline();
  CHECK_EQ(f.text(), "if (x) {\n    ");
  CHECK(f.caret() == (Position{1, 4}));

  Fx g("  {}");
  g.ed->editing().detectIndent = false;
  g.set({C(0, 3)});
  g.ed->insertNewline();
  CHECK_EQ(g.text(), "  {\n      \n  }");
  CHECK(g.caret() == (Position{1, 6}));
  g.expectOneStep("  {}");
}

TEST(edit_newline_continuation_lines) {
  Fx f("x = a +");
  f.set({C(0, 7)});
  f.ed->insertNewline();
  CHECK_EQ(f.text(), "x = a +\n    ");
  f.type("b;");
  f.ed->insertNewline();  // the continued statement ended: back to the statement's indentation
  CHECK_EQ(f.text(), "x = a +\n    b;\n");
  CHECK(f.caret() == (Position{2, 0}));

  Fx g("foo(");  // an open paren continues too
  g.set({C(0, 4)});
  g.ed->insertNewline();
  CHECK_EQ(g.text(), "foo(\n    ");
  Fx h("i++");  // not an operator
  h.set({C(0, 3)});
  h.ed->insertNewline();
  CHECK_EQ(h.text(), "i++\n");
}

TEST(edit_newline_trims_blank_prefix_and_leading_blanks_of_rest) {
  Fx f("    \n  a   b");
  f.set({C(0, 4), C(1, 3)});
  f.ed->insertNewline();
  CHECK_EQ(f.text(), "\n    \n  a\n  b");
  f.expectOneStep("    \n  a   b");
}

TEST(edit_newline_off_and_unit_settings) {
  Fx f("  a");
  f.ed->editing().autoIndent = false;
  f.set({C(0, 3)});
  f.ed->insertNewline();
  CHECK_EQ(f.text(), "  a\n");

  Fx g("a {");
  g.ed->editing().indentUnit = "\t";
  g.set({C(0, 3)});
  g.ed->insertNewline();
  CHECK_EQ(g.text(), "a {\n\t");
}

TEST(edit_detect_indent_from_document) {
  Fx two("void f() {\n  a;\n  if (b) {\n    c;\n  }\n}\n");
  CHECK_EQ(two.ed->indentUnitFor(*two.doc), "  ");
  Fx tabs("void f() {\n\ta;\n\tb;\n}\n");
  CHECK_EQ(tabs.ed->indentUnitFor(*tabs.doc), "\t");
  Fx none("a\nb\n");
  CHECK_EQ(none.ed->indentUnitFor(*none.doc), "    ");
  none.ed->editing().indentUnit = "  ";
  CHECK_EQ(none.ed->indentUnitFor(*none.doc), "  ");
  two.ed->editing().detectIndent = false;
  CHECK_EQ(two.ed->indentUnitFor(*two.doc), "    ");
  // Enter uses the detected unit.
  Fx f("void f() {\n  a;\n  if (b) {\n    c;\n  }\n}\n");
  f.set({C(2, 10)});
  f.ed->insertNewline();
  CHECK_EQ(f.doc->buffer().line(3), "    ");
}

TEST(edit_close_brace_dedents_to_matching_open_brace) {
  Fx f("void f() {\n    if (x) {\n        y;\n        ");
  f.set({C(3, 8)});
  f.ed->typeText("}");
  CHECK_EQ(f.doc->buffer().line(3), "    }");
  f.ed->insertNewline();
  f.ed->typeText("}");
  CHECK_EQ(f.doc->buffer().line(4), "}");
  Fx g("a {\n    b } ");  // '}' after other text on the line is just typed
  g.set({C(1, 11)});
  g.ed->editing().autoClosePairs = false;
  g.ed->typeText("}");
  CHECK_EQ(g.doc->buffer().line(1), "    b } }");
}

// --- auto-close -----------------------------------------------------------------------------------------------

TEST(edit_autoclose_and_skip_over) {
  Fx f("");
  f.type("(");
  CHECK_EQ(f.text(), "()");
  CHECK(f.caret() == (Position{0, 1}));
  f.type(")");
  CHECK_EQ(f.text(), "()");
  CHECK(f.caret() == (Position{0, 2}));
  f.type("[{\"");
  CHECK_EQ(f.text(), "()[{\"\"}]");
  f.type("\"}]");  // typing the closers skips them
  CHECK_EQ(f.text(), "()[{\"\"}]");
  CHECK(f.caret() == (Position{0, 8}));
}

TEST(edit_autoclose_backspace_deletes_empty_pair) {
  Fx f("");
  f.type("{");
  CHECK_EQ(f.text(), "{}");
  f.ed->backspace();
  CHECK_EQ(f.text(), "");
  Fx g("(a)");
  g.set({C(0, 1)});
  g.ed->backspace();  // not an empty pair
  CHECK_EQ(g.text(), "a)");
  Fx h("()");
  h.set({C(0, 1)});
  h.ed->editing().autoClosePairs = false;
  h.ed->backspace();
  CHECK_EQ(h.text(), ")");
}

TEST(edit_autoclose_quotes_rules) {
  Fx f("abc");
  f.set({C(0, 3)});
  f.type("\"");  // inside / after an identifier: no pair
  CHECK_EQ(f.text(), "abc\"");
  Fx g("x = \\");
  g.set({C(0, 5)});
  g.type("\"");  // after a backslash
  CHECK_EQ(g.text(), "x = \\\"");
  Fx h("x = ");
  h.set({C(0, 4)});
  h.type("'");
  CHECK_EQ(h.text(), "x = ''");
  Fx i("foo");
  i.set({C(0, 0)});
  i.type("(");  // text follows: no pair
  CHECK_EQ(i.text(), "(foo");
}

TEST(edit_autoclose_wraps_selection) {
  Fx f("hello world");
  f.set({S(0, 0, 0, 5)});
  f.type("(");
  CHECK_EQ(f.text(), "(hello) world");
  CHECK(f.primary() == S(0, 1, 0, 6));
  f.type("\"");  // wrap again: the selection stayed
  CHECK_EQ(f.text(), "(\"hello\") world");
  f.expectOneStep("(hello) world");
}

TEST(edit_autoclose_multi_caret_one_undo_step) {
  Fx f("a\nb\nc");
  f.set({C(0, 1), C(1, 1), C(2, 1)});
  f.ed->typeText("(");
  CHECK_EQ(f.text(), "a()\nb()\nc()");
  CHECK_EQ(f.ed->selections().size(), static_cast<size_t>(3));
  f.ed->typeText(")");
  CHECK_EQ(f.text(), "a()\nb()\nc()");
  CHECK(f.ed->selections()[2].head == (Position{2, 3}));
  f.expectOneStep("a\nb\nc");
}

TEST(edit_autoclose_off_types_plain) {
  Fx f("");
  f.ed->editing().autoClosePairs = false;
  f.type("(\"");
  CHECK_EQ(f.text(), "(\"");
}

// --- comments -------------------------------------------------------------------------------------------------

TEST(edit_line_comment_toggle) {
  Fx f("    a;\n  b;\n\n    c;");
  f.set({S(0, 0, 3, 5)});
  f.ed->toggleLineComment();
  CHECK_EQ(f.text(), "  //   a;\n  // b;\n\n  //   c;");  // aligned at the minimum indentation, blank line untouched
  f.ed->toggleLineComment();                              // every line commented -> uncomment
  CHECK_EQ(f.text(), "    a;\n  b;\n\n    c;");
  f.undoAll();
  CHECK_EQ(f.text(), "    a;\n  b;\n\n    c;");
}

TEST(edit_line_comment_mixed_and_multi_caret) {
  Fx f("// a\nb\n// c");
  f.set({S(0, 0, 2, 4)});
  f.ed->toggleLineComment();  // not all commented: comment all
  CHECK_EQ(f.text(), "// // a\n// b\n// // c");
  Fx g("a\nb\nc\nd");
  g.set({C(0, 1), C(2, 0)});
  g.ed->toggleLineComment();
  CHECK_EQ(g.text(), "// a\nb\n// c\nd");
  g.expectOneStep("a\nb\nc\nd");
  Fx h("a\nb\nc");  // a selection ending at column 0 does not take that line
  h.set({S(0, 0, 1, 0)});
  h.ed->toggleLineComment();
  CHECK_EQ(h.text(), "// a\nb\nc");
}

TEST(edit_block_comment_toggle) {
  Fx f("a b c");
  f.set({S(0, 2, 0, 3)});
  f.ed->toggleBlockComment();
  CHECK_EQ(f.text(), "a /*b*/ c");
  CHECK(f.primary() == S(0, 2, 0, 7));
  f.ed->toggleBlockComment();
  CHECK_EQ(f.text(), "a b c");
  f.set({C(0, 1)});  // a caret comments its line
  f.ed->toggleBlockComment();
  CHECK_EQ(f.text(), "/*a b c*/");
  f.undoAll();
  CHECK_EQ(f.text(), "a b c");
  Fx g("a b\nc d");
  g.set({S(0, 0, 0, 1), S(1, 2, 1, 3)});
  g.ed->toggleBlockComment();
  CHECK_EQ(g.text(), "/*a*/ b\nc /*d*/");
  g.expectOneStep("a b\nc d");
}

// --- line commands --------------------------------------------------------------------------------------------

TEST(edit_duplicate_line_and_selection) {
  Fx f("abc\ndef");
  f.set({C(0, 2)});
  f.ed->duplicate();
  CHECK_EQ(f.text(), "abc\nabc\ndef");
  CHECK(f.caret() == (Position{1, 2}));
  f.expectOneStep("abc\ndef");
  f.set({S(0, 1, 0, 3)});
  f.ed->duplicate();
  CHECK_EQ(f.text(), "abcbc\ndef");
  CHECK(f.primary() == S(0, 3, 0, 5));
  f.undoAll();
  f.set({C(0, 0), C(1, 3)});  // two carets: both lines (the last line has no break)
  f.ed->duplicate();
  CHECK_EQ(f.text(), "abc\nabc\ndef\ndef");
  f.expectOneStep("abc\ndef");
  f.set({C(0, 0), C(0, 2)});  // two carets on one line: one copy
  f.ed->duplicate();
  CHECK_EQ(f.text(), "abc\nabc\ndef");
}

TEST(edit_move_lines) {
  Fx f("a\nb\nc\nd");
  f.set({C(1, 1)});
  f.ed->moveLines(false);
  CHECK_EQ(f.text(), "b\na\nc\nd");
  CHECK(f.caret() == (Position{0, 1}));
  f.ed->moveLines(false);  // at the top: nothing
  CHECK_EQ(f.text(), "b\na\nc\nd");
  f.ed->moveLines(true);
  f.ed->moveLines(true);
  CHECK_EQ(f.text(), "a\nc\nb\nd");
  CHECK(f.caret() == (Position{2, 1}));
  f.set({S(1, 0, 2, 1)});
  f.ed->moveLines(true);
  CHECK_EQ(f.text(), "a\nd\nc\nb");
  CHECK(f.primary() == S(2, 0, 3, 1));
  f.ed->moveLines(true);  // at the bottom
  CHECK_EQ(f.text(), "a\nd\nc\nb");
  f.expectOneStep("a\nc\nb\nd");
}

TEST(edit_move_lines_multi_caret) {
  Fx f("a\nb\nc\nd\ne");
  f.set({C(1, 0), C(3, 0)});
  f.ed->moveLines(false);
  CHECK_EQ(f.text(), "b\na\nd\nc\ne");
  CHECK(f.ed->selections()[0].head == (Position{0, 0}));
  CHECK(f.ed->selections()[1].head == (Position{2, 0}));
  f.expectOneStep("a\nb\nc\nd\ne");
  f.set({C(0, 0), C(3, 0)});  // the first one cannot move up, the second can
  f.ed->moveLines(false);
  CHECK_EQ(f.text(), "a\nb\nd\nc\ne");
  f.undoAll();
  f.set({S(1, 0, 2, 0)});  // ends at column 0: only line 1 moves
  f.ed->moveLines(true);
  CHECK_EQ(f.text(), "a\nc\nb\nd\ne");
  CHECK(f.primary() == S(2, 0, 3, 0));
}

TEST(edit_join_lines) {
  Fx f("a\n   b\nc  \n  d\n\ne");
  f.set({C(0, 0)});
  f.ed->joinLines();
  CHECK_EQ(f.text(), "a b\nc  \n  d\n\ne");
  f.expectOneStep("a\n   b\nc  \n  d\n\ne");
  f.set({S(2, 0, 4, 1)});  // all lines of the selection
  f.ed->joinLines();
  CHECK_EQ(f.text(), "a\n   b\nc  d\ne");
  f.undoAll();
  f.set({C(5, 0)});  // last line: nothing
  f.ed->joinLines();
  CHECK_EQ(f.text(), "a\n   b\nc  \n  d\n\ne");
  f.set({C(0, 0), C(2, 1)});
  f.ed->joinLines();
  CHECK_EQ(f.text(), "a b\nc  d\n\ne");
}

TEST(edit_delete_lines) {
  Fx f("a\nb\nc\nd");
  f.set({C(1, 1)});
  f.ed->deleteLines();
  CHECK_EQ(f.text(), "a\nc\nd");
  CHECK(f.caret() == (Position{1, 1}));
  f.set({C(2, 0)});  // the last line takes the break before it
  f.ed->deleteLines();
  CHECK_EQ(f.text(), "a\nc");
  CHECK(f.caret() == (Position{1, 0}));
  f.undoAll();
  CHECK_EQ(f.text(), "a\nb\nc\nd");
  f.set({C(0, 0), C(2, 0), C(3, 0)});
  f.ed->deleteLines();
  CHECK_EQ(f.text(), "b");
  f.expectOneStep("a\nb\nc\nd");
  f.set({S(0, 0, 3, 1)});
  f.ed->deleteLines();
  CHECK_EQ(f.text(), "");
}

TEST(edit_indent_outdent_tab) {
  Fx f("a\n  b\n\nc");
  f.ed->editing().detectIndent = false;
  f.set({S(0, 0, 3, 1)});
  f.ed->insertTab();  // several lines: indent them (blank lines stay empty), the selection keeps covering them
  CHECK_EQ(f.text(), "    a\n      b\n\n    c");
  CHECK(f.primary() == S(0, 0, 3, 5));
  f.ed->outdentLines();
  CHECK_EQ(f.text(), "a\n  b\n\nc");
  f.ed->outdentLines();  // less than one unit is removed as far as it goes
  CHECK_EQ(f.text(), "a\nb\n\nc");
  f.undoAll();
  CHECK_EQ(f.text(), "a\n  b\n\nc");
  f.set({C(0, 1)});  // a caret: Tab inserts the unit
  f.ed->insertTab();
  CHECK_EQ(f.text(), "a    \n  b\n\nc");
  f.set({S(0, 0, 0, 1)});  // inside one line the selection is replaced
  f.ed->insertTab();
  CHECK_EQ(f.doc->buffer().line(0), "        ");
  Fx g("\ta\n\t\tb");
  g.set({S(0, 0, 1, 2)});
  g.ed->outdentLines();
  CHECK_EQ(g.text(), "a\n\tb");
  g.ed->editing().indentUnit = "\t";
  g.ed->editing().detectIndent = false;
  g.ed->indentLines();
  CHECK_EQ(g.text(), "\ta\n\t\tb");
}

TEST(edit_goto_line_is_in_history) {
  Fx f(numbered(100));
  const size_t before = f.ed->navigation().entries().size();
  CHECK(f.ed->gotoLine(25, 2));
  CHECK(f.caret() == (Position{24, 1}));
  CHECK(f.ed->navigation().entries().size() > before);
  CHECK(f.ed->gotoLine(9999));  // clamped to the last line
  CHECK_EQ(f.caret().line, f.doc->buffer().lineCount() - 1);
  CHECK(f.ed->back());
  CHECK(f.caret() == (Position{24, 1}));
}

// --- find / replace -------------------------------------------------------------------------------------------

TEST(edit_find_in_document_options) {
  Fx f("Foo foo FOO\nfoobar foo_x\n(foo)");
  SearchOptions o;
  o.query = "foo";
  o.caseSensitive = true;
  CHECK_EQ(f.ed->findInDocument(o).size(), static_cast<size_t>(4));
  o.caseSensitive = false;
  CHECK_EQ(f.ed->findInDocument(o).size(), static_cast<size_t>(6));
  o.wholeWord = true;
  const auto words = f.ed->findInDocument(o);
  CHECK_EQ(words.size(), static_cast<size_t>(4));
  CHECK(words[0] == (Range{{0, 0}, {0, 3}}));
  CHECK(words[3] == (Range{{2, 1}, {2, 4}}));
  o.wholeWord = false;
  o.regex = true;
  o.query = "fo+\\w*";
  std::string err;
  CHECK_EQ(f.ed->findInDocument(o, &err).size(), static_cast<size_t>(6));
  o.query = "(";
  f.ed->findInDocument(o, &err);
  CHECK(!err.empty());
}

TEST(edit_replace_all_one_undo_step) {
  Fx f("foo bar foo\nfoo");
  f.set({C(1, 3)});
  SearchOptions o;
  o.query = "foo";
  CHECK_EQ(f.ed->replaceAll(o, "quux"), static_cast<size_t>(3));
  CHECK_EQ(f.text(), "quux bar quux\nquux");
  CHECK(f.caret() == (Position{1, 4}));
  f.expectOneStep("foo bar foo\nfoo");
  o.query = "zzz";
  CHECK_EQ(f.ed->replaceAll(o, "x"), static_cast<size_t>(0));
  CHECK(!f.ed->undo());
}

// --- bookmarks ------------------------------------------------------------------------------------------------

TEST(edit_bookmarks_toggle_navigate_and_follow_edits) {
  Fx f("0\n1\n2\n3\n4\n5\n6");
  f.set({C(2, 0)});
  CHECK(f.ed->toggleBookmark());
  f.set({C(5, 0)});
  CHECK(f.ed->toggleBookmark());
  CHECK_EQ(f.ed->bookmarks().count(), static_cast<size_t>(2));
  f.set({C(0, 0)});
  CHECK(f.ed->gotoBookmark(true));
  CHECK_EQ(f.caret().line, 2);
  CHECK(f.ed->gotoBookmark(true));
  CHECK_EQ(f.caret().line, 5);
  CHECK(f.ed->gotoBookmark(true));  // wraps
  CHECK_EQ(f.caret().line, 2);
  CHECK(f.ed->gotoBookmark(false));  // wraps backwards
  CHECK_EQ(f.caret().line, 5);
  // Inserting a line above moves them; deleting the lines around keeps them attached.
  f.ed->insertText("");
  f.set({C(0, 0)});
  f.ed->insertText("new\n");
  CHECK((f.ed->bookmarks().lines(f.doc->path()) == std::vector<int>{3, 6}));
  f.set({S(0, 0, 3, 0)});
  f.ed->insertText("");  // removes lines 0..2
  CHECK((f.ed->bookmarks().lines(f.doc->path()) == std::vector<int>{0, 3}));
  f.set({C(3, 0)});
  CHECK(!f.ed->toggleBookmark());  // toggling again removes it
  CHECK_EQ(f.ed->bookmarks().count(), static_cast<size_t>(1));
}

TEST(edit_bookmarks_persist_in_json) {
  Fx f("0\n1\n2\n3");
  f.set({C(1, 0)});
  f.ed->toggleBookmark();
  f.set({C(3, 0)});
  f.ed->toggleBookmark();
  CHECK(fs::exists(f.dir / ".tenfold" / "bookmarks.json"));
  Editor second(f.dir);  // another session of the same workspace
  second.open("a.cpp");
  CHECK((second.bookmarks().lines(f.doc->path()) == std::vector<int>{1, 3}));
  // Edits move them in memory; save() writes the new lines.
  f.set({C(0, 0)});
  f.ed->insertText("x\n");
  f.ed->save(*f.doc);
  Editor third(f.dir);
  third.open("a.cpp");
  CHECK((third.bookmarks().lines(f.doc->path()) == std::vector<int>{2, 4}));
}

TEST(edit_bookmarks_jump_lands_in_history) {
  Fx f(numbered(26));
  f.set({C(20, 0)});
  f.ed->toggleBookmark();
  f.set({C(0, 0)});
  CHECK(f.ed->gotoBookmark(true));
  CHECK_EQ(f.caret().line, 20);
  CHECK(f.ed->back());
  CHECK_EQ(f.caret().line, 0);
}

// --- save-time and encoding helpers ---------------------------------------------------------------------------

TEST(edit_trim_trailing_whitespace_on_save) {
  Fx f("a  \n\tb\t \nc");
  f.set({C(0, 3)});
  f.ed->save(*f.doc);  // off by default
  CHECK_EQ(f.text(), "a  \n\tb\t \nc");
  f.ed->editing().trimTrailingWhitespace = true;
  CHECK(f.ed->save(*f.doc));
  CHECK_EQ(f.text(), "a\n\tb\nc");
  CHECK(f.caret() == (Position{0, 1}));
  std::ifstream in(f.doc->path(), std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  CHECK_EQ(ss.str(), "a\n\tb\nc");
  f.expectOneStep("a  \n\tb\t \nc");
}

TEST(edit_line_ending_switch) {
  Fx f("a\nb\n");
  CHECK(f.doc->buffer().lineEnding() == LineEnding::LF);
  CHECK(!f.doc->dirty());
  f.ed->setLineEnding(*f.doc, LineEnding::CRLF);
  CHECK(f.doc->dirty());
  CHECK(f.ed->save(*f.doc));
  std::ifstream in(f.doc->path(), std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  CHECK_EQ(ss.str(), "a\r\nb\r\n");
  f.ed->setLineEnding(*f.doc, LineEnding::LF);
  CHECK(f.ed->save(*f.doc));
  std::ifstream in2(f.doc->path(), std::ios::binary);
  std::stringstream ss2;
  ss2 << in2.rdbuf();
  CHECK_EQ(ss2.str(), "a\nb\n");
}

TEST(edit_typing_still_coalesces_with_smart_typing) {
  Fx f("");
  f.type("ab");
  CHECK_EQ(f.text(), "ab");
  CHECK(f.ed->undo());
  CHECK_EQ(f.text(), "");  // plain characters keep joining into one word-level step
}
