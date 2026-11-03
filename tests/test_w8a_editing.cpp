// W8a: the 10x editing commands of the Editor (case, comment lines, delete to end of line, insert line, current word /
// scope selection, matching bracket, cut line, save all). Each edit is one undo step and works with several carets.
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>

#include "app/Editor.h"
#include "core/Document.h"
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
    dir = fs::temp_directory_path() /
          ("tenfold_w8a_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_" +
           std::to_string(counter++));
    fs::create_directories(dir);
    std::ofstream(dir / name, std::ios::binary) << text;
    ed = std::make_unique<Editor>(dir);
    ed->editing().indentUnit = "    ";
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
  // One command must be one undo step: after a single undo the text is `before`.
  void expectOneStep(const std::string& before) {
    CHECK(ed->undo());
    CHECK_EQ(text(), before);
  }
};

}  // namespace

TEST(w8a_uppercase_lowercase_keep_selections_and_undo_in_one_step) {
  Fx f("abc def\nGhi jkl");
  f.set({S(0, 0, 0, 3), S(1, 4, 1, 7)});
  f.ed->makeSelectionUppercase();
  CHECK_EQ(f.text(), "ABC def\nGhi JKL");
  CHECK_EQ(f.ed->selections().size(), size_t(2));
  CHECK(f.ed->selections()[0] == S(0, 0, 0, 3));
  CHECK(f.ed->selections()[1] == S(1, 4, 1, 7));
  f.expectOneStep("abc def\nGhi jkl");
  f.set({S(0, 0, 1, 3)});
  f.ed->makeSelectionLowercase();
  CHECK_EQ(f.text(), "abc def\nghi jkl");
  CHECK(f.primary() == S(0, 0, 1, 3));
  f.expectOneStep("abc def\nGhi jkl");
}

TEST(w8a_case_without_selection_or_change_is_no_edit) {
  Fx f("abc");
  f.set({C(0, 1)});
  const int v = f.doc->version();
  f.ed->makeSelectionUppercase();
  CHECK_EQ(f.text(), "abc");
  CHECK_EQ(f.doc->version(), v);
  f.set({S(0, 0, 0, 3)});
  f.ed->makeSelectionLowercase();  // already lower case
  CHECK_EQ(f.doc->version(), v);
}

TEST(w8a_case_changes_ascii_only_and_reversed_selection_stays_reversed) {
  Fx f("h\xC3\xA9llo");  // "héllo": the two-byte letter is left alone
  f.set({Selection{{0, 6}, {0, 0}}});
  f.ed->makeSelectionUppercase();
  CHECK_EQ(f.text(), "H\xC3\xA9LLO");
  CHECK(f.primary().reversed());
}

TEST(w8a_comment_and_uncomment_lines) {
  Fx f("    a;\n  b;\n\n    c;");
  f.set({S(0, 0, 3, 5)});
  f.ed->commentLines();
  CHECK_EQ(f.text(), "  //   a;\n  // b;\n\n  //   c;");
  f.ed->commentLines();  // Commentline always adds: nested comment
  CHECK_EQ(f.text(), "  // //   a;\n  // // b;\n\n  // //   c;");
  f.ed->uncommentLines();
  CHECK_EQ(f.text(), "  //   a;\n  // b;\n\n  //   c;");
  f.ed->uncommentLines();
  CHECK_EQ(f.text(), "    a;\n  b;\n\n    c;");
  f.ed->uncommentLines();  // nothing to remove: no edit, no undo step
  CHECK_EQ(f.text(), "    a;\n  b;\n\n    c;");
  CHECK(f.ed->undo());  // the last real step was the second uncomment
  CHECK_EQ(f.text(), "  //   a;\n  // b;\n\n  //   c;");
}

TEST(w8a_uncomment_only_touches_commented_lines_and_multi_caret) {
  Fx f("// a\nb\n//c\nd");
  f.set({C(0, 3), C(1, 0), C(2, 1)});
  f.ed->uncommentLines();
  CHECK_EQ(f.text(), "a\nb\nc\nd");
  f.expectOneStep("// a\nb\n//c\nd");
  f.set({C(1, 0), C(3, 0)});
  f.ed->commentLines();
  CHECK_EQ(f.text(), "// a\n// b\n//c\n// d");
  f.expectOneStep("// a\nb\n//c\nd");
}

TEST(w8a_toggle_comment_follows_the_setting) {
  Fx f("a b c\nsecond");
  CHECK(f.ed->editing().commentBlockUsesLineComments);  // 10x default
  f.set({S(0, 2, 0, 3)});
  f.ed->toggleComment();  // true: line comments, even with a selection
  CHECK_EQ(f.text(), "// a b c\nsecond");
  f.ed->toggleComment();
  CHECK_EQ(f.text(), "a b c\nsecond");
  f.ed->editing().commentBlockUsesLineComments = false;
  f.ed->toggleComment();  // false + selection: block comment around the selection
  CHECK_EQ(f.text(), "a /*b*/ c\nsecond");
  f.ed->toggleComment();
  CHECK_EQ(f.text(), "a b c\nsecond");
  f.set({C(1, 2)});  // no selection: line comment either way
  f.ed->toggleComment();
  CHECK_EQ(f.text(), "a b c\n// second");
  f.expectOneStep("a b c\nsecond");
  f.set({S(0, 0, 0, 1), C(1, 0)});  // block form needs every caret selected: this mix toggles line comments
  f.ed->toggleComment();
  CHECK_EQ(f.text(), "// a b c\n// second");
}

TEST(w8a_delete_to_end_of_line) {
  Fx f("abc def\nghi jkl\nmno");
  f.set({C(0, 3), C(1, 4)});
  f.ed->deleteToEndOfLine();
  CHECK_EQ(f.text(), "abc\nghi \nmno");
  CHECK(f.ed->selections()[0] == C(0, 3));
  CHECK(f.ed->selections()[1] == C(1, 4));
  f.expectOneStep("abc def\nghi jkl\nmno");
  f.set({S(0, 1, 1, 2)});  // a selection: from its start to the end of its last line
  f.ed->deleteToEndOfLine();
  CHECK_EQ(f.text(), "a\nmno");
  f.expectOneStep("abc def\nghi jkl\nmno");
  f.set({C(2, 3)});  // at the end of a line: nothing to delete, the line break stays
  const int v = f.doc->version();
  f.ed->deleteToEndOfLine();
  CHECK_EQ(f.doc->version(), v);
  f.set({C(0, 2), C(0, 5)});  // two carets on one line
  f.ed->deleteToEndOfLine();
  CHECK_EQ(f.text(), "ab\nghi jkl\nmno");
}

TEST(w8a_insert_line_above_and_below) {
  Fx f("    abc\ndef");
  f.set({C(0, 5)});
  f.ed->insertLineAbove();
  CHECK_EQ(f.text(), "    \n    abc\ndef");
  CHECK(f.caret() == (Position{0, 4}));
  f.expectOneStep("    abc\ndef");
  f.ed->insertLineBelow();
  CHECK_EQ(f.text(), "    abc\n    \ndef");
  CHECK(f.caret() == (Position{1, 4}));
  f.expectOneStep("    abc\ndef");
  f.set({C(0, 0), C(1, 2)});  // two carets, two lines
  f.ed->insertLineBelow();
  CHECK_EQ(f.text(), "    abc\n    \ndef\n");
  CHECK_EQ(f.ed->selections().size(), size_t(2));
  f.expectOneStep("    abc\ndef");
  f.set({C(0, 1), C(0, 4)});  // two carets on one line: one new line
  f.ed->insertLineAbove();
  CHECK_EQ(f.text(), "    \n    abc\ndef");
  f.expectOneStep("    abc\ndef");
}

TEST(w8a_insert_line_below_a_block_opener_indents_one_level) {
  Fx f("void f() {\n}");
  f.set({C(0, 3)});
  f.ed->insertLineBelow();
  CHECK_EQ(f.text(), "void f() {\n    \n}");
  CHECK(f.caret() == (Position{1, 4}));
  Fx g("    x");
  g.ed->editing().autoIndent = false;
  g.set({C(0, 0)});
  g.ed->insertLineAbove();
  CHECK_EQ(g.text(), "\n    x");
}

TEST(w8a_select_current_word) {
  Fx f("foo_bar baz  +  qux");
  f.set({C(0, 2)});
  f.ed->selectCurrentWord();
  CHECK(f.primary() == S(0, 0, 0, 7));
  f.set({C(0, 7), C(0, 18)});  // just after a word; in the middle of one
  f.ed->selectCurrentWord();
  CHECK_EQ(f.ed->selections().size(), size_t(2));
  CHECK(f.ed->selections()[0] == S(0, 0, 0, 7));
  CHECK(f.ed->selections()[1] == S(0, 16, 0, 19));
  f.set({C(0, 12)});  // between blanks: nothing
  f.ed->selectCurrentWord();
  CHECK(f.primary() == C(0, 12));
}

TEST(w8a_select_current_scope_grows_with_each_press) {
  Fx f("a {\n  b { c }\n  d\n}\ne");
  f.set({C(1, 6)});  // on the c
  f.ed->selectCurrentScope();
  CHECK(f.primary() == S(1, 5, 1, 8));  // inside the inner braces
  f.ed->selectCurrentScope();
  CHECK(f.primary() == S(1, 4, 1, 9));  // with them
  f.ed->selectCurrentScope();
  CHECK(f.primary() == S(0, 3, 3, 0));  // inside the outer braces
  f.ed->selectCurrentScope();
  CHECK(f.primary() == S(0, 2, 3, 1));  // with them
  f.ed->selectCurrentScope();           // nothing further out
  CHECK(f.primary() == S(0, 2, 3, 1));
  CHECK_EQ(f.text(), "a {\n  b { c }\n  d\n}\ne");  // selecting never edits
}

TEST(w8a_select_current_scope_ignores_braces_in_strings_and_comments) {
  Fx f("{ \"}\" /* } */ x // }\n}");
  f.set({C(0, 12)});
  f.ed->selectCurrentScope();
  CHECK(f.primary() == S(0, 1, 1, 0));
  Fx g("no braces here");
  g.set({C(0, 3)});
  g.ed->selectCurrentScope();
  CHECK(g.primary() == C(0, 3));
}

TEST(w8a_select_current_scope_multi_caret) {
  Fx f("{a}\n{b}");
  f.set({C(0, 1), C(1, 1)});
  f.ed->selectCurrentScope();
  CHECK_EQ(f.ed->selections().size(), size_t(2));
  CHECK(f.ed->selections()[0] == S(0, 1, 0, 2));
  CHECK(f.ed->selections()[1] == S(1, 1, 1, 2));
}

TEST(w8a_select_current_bracket_scope_uses_all_bracket_kinds) {
  Fx f("f(a[1], {b})");
  f.set({C(0, 4)});  // on the 1
  f.ed->selectCurrentBracketScope();
  CHECK(f.primary() == S(0, 4, 0, 5));
  f.ed->selectCurrentBracketScope();
  CHECK(f.primary() == S(0, 3, 0, 6));  // [1]
  f.ed->selectCurrentBracketScope();
  CHECK(f.primary() == S(0, 2, 0, 11));  // inside the parentheses
  f.ed->selectCurrentBracketScope();
  CHECK(f.primary() == S(0, 1, 0, 12));
  f.set({C(0, 9)});  // on the b: the braces are the innermost
  f.ed->selectCurrentBracketScope();
  CHECK(f.primary() == S(0, 9, 0, 10));
  f.set({C(0, 0)});  // before the whole thing: nothing encloses it
  f.ed->selectCurrentBracketScope();
  CHECK(f.primary() == C(0, 0));
}

TEST(w8a_move_to_matching_bracket) {
  Fx f("f(a, [b])\n{\n  x\n}");
  f.set({C(0, 1)});  // before '('
  CHECK(f.ed->moveToMatchingBracket());
  CHECK(f.caret() == (Position{0, 8}));  // before ')'
  CHECK(f.ed->moveToMatchingBracket());
  CHECK(f.caret() == (Position{0, 1}));  // and back
  f.set({C(0, 9)});                      // after ')'
  CHECK(f.ed->moveToMatchingBracket());
  CHECK(f.caret() == (Position{0, 2}));  // after '('
  f.set({C(0, 5)});                      // before [
  CHECK(f.ed->moveToMatchingBracket());
  CHECK(f.caret() == (Position{0, 7}));
  f.set({C(1, 0)});  // braces on other lines
  CHECK(f.ed->moveToMatchingBracket());
  CHECK(f.caret() == (Position{3, 0}));
  f.set({S(0, 0, 0, 1)});  // a selection collapses to the partner
  f.set({C(2, 3)});        // not at a bracket
  CHECK(!f.ed->moveToMatchingBracket());
  CHECK(f.caret() == (Position{2, 3}));
}

TEST(w8a_move_to_matching_bracket_multi_caret_and_literals) {
  Fx f("(a) \")\" (b)");
  f.set({C(0, 0), C(0, 8)});
  CHECK(f.ed->moveToMatchingBracket());
  CHECK_EQ(f.ed->selections().size(), size_t(2));
  CHECK(f.ed->selections()[0].head == (Position{0, 2}));
  CHECK(f.ed->selections()[1].head == (Position{0, 10}));
  f.set({C(0, 5)});  // the ")" in the string is not a bracket
  CHECK(!f.ed->moveToMatchingBracket());
}

TEST(w8a_cut_lines_returns_text_and_undoes_in_one_step) {
  Fx f("one\ntwo\nthree\nfour");
  f.set({C(1, 1)});
  CHECK_EQ(f.ed->cutLines(), std::string("two\n"));
  CHECK_EQ(f.text(), "one\nthree\nfour");
  f.expectOneStep("one\ntwo\nthree\nfour");
  f.set({C(0, 0), C(2, 2)});
  CHECK_EQ(f.ed->cutLines(), std::string("one\nthree\n"));
  CHECK_EQ(f.text(), "two\nfour");
  f.expectOneStep("one\ntwo\nthree\nfour");
  f.set({S(1, 1, 2, 2)});  // a selection cuts every line it touches
  CHECK_EQ(f.ed->cutLines(), std::string("two\nthree\n"));
  CHECK_EQ(f.text(), "one\nfour");
  f.expectOneStep("one\ntwo\nthree\nfour");
  f.set({C(3, 1)});  // the last line takes the break before it
  CHECK_EQ(f.ed->cutLines(), std::string("four\n"));
  CHECK_EQ(f.text(), "one\ntwo\nthree");
}

TEST(w8a_save_all_saves_every_modified_document) {
  Fx f("a0");
  {
    std::ofstream(f.dir / "b.cpp", std::ios::binary) << "b0";
    std::ofstream(f.dir / "c.cpp", std::ios::binary) << "c0";
  }
  Document* b = f.ed->open("b.cpp");
  Document* c = f.ed->open("c.cpp");
  REQUIRE(b && c);
  f.ed->gotoLocation({f.doc->path(), {0, 2}});
  f.ed->insertText("!");
  f.ed->gotoLocation({b->path(), {0, 2}});
  f.ed->insertText("?");
  CHECK(f.doc->dirty());
  CHECK(b->dirty());
  CHECK(!c->dirty());
  CHECK(f.ed->saveAll());
  CHECK(!f.doc->dirty());
  CHECK(!b->dirty());
  std::ifstream a(f.dir / "a.cpp"), bb(f.dir / "b.cpp");
  std::string sa((std::istreambuf_iterator<char>(a)), {}), sb((std::istreambuf_iterator<char>(bb)), {});
  CHECK_EQ(sa, std::string("a0!"));
  CHECK_EQ(sb, std::string("b0?"));
}
