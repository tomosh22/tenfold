#include "core/Document.h"
#include "core/TextBuffer.h"
#include "test_main.h"

using namespace tf;

TEST(textbuffer_split_and_join) {
  TextBuffer b("a\nbb\n\nccc");
  CHECK_EQ(b.lineCount(), 4);
  CHECK_EQ(b.line(1), "bb");
  CHECK_EQ(b.line(2), "");
  CHECK_EQ(b.text(), "a\nbb\n\nccc");
  TextBuffer t("x\ny\n");
  CHECK_EQ(t.lineCount(), 3);
  CHECK_EQ(t.text(), "x\ny\n");
  TextBuffer e;
  CHECK_EQ(e.lineCount(), 1);
  CHECK_EQ(e.text(), "");
}

TEST(textbuffer_crlf_detection_and_roundtrip) {
  TextBuffer b("one\r\ntwo\r\nthree");
  CHECK(b.lineEnding() == LineEnding::CRLF);
  CHECK_EQ(b.line(0), "one");
  CHECK_EQ(b.text(), "one\ntwo\nthree");
  CHECK_EQ(b.textForDisk(), "one\r\ntwo\r\nthree");
}

TEST(textbuffer_replace_single_line) {
  TextBuffer b("hello world");
  auto r = b.replace({{0, 6}, {0, 11}}, "there");
  CHECK_EQ(b.text(), "hello there");
  CHECK_EQ(r.lineDelta, 0);
  CHECK(r.inserted.end == Position{0, 11});
}

TEST(textbuffer_replace_multiline) {
  TextBuffer b("line1\nline2\nline3\nline4");
  // Replace "ne2\nli" with "X\nY\nZ"
  auto r = b.replace({{1, 2}, {2, 2}}, "X\nY\nZ");
  CHECK_EQ(b.text(), "line1\nliX\nY\nZne3\nline4");
  CHECK_EQ(r.lineDelta, 1);
  CHECK(r.inserted.start == Position{1, 2});
  CHECK(r.inserted.end == Position{3, 1});
  // Delete across lines
  auto d = b.erase({{1, 0}, {3, 1}});
  CHECK_EQ(b.text(), "line1\nne3\nline4");
  CHECK_EQ(d.lineDelta, -2);
}

TEST(textbuffer_insert_newline_and_clamp) {
  TextBuffer b("ab");
  b.insert({0, 1}, "\n");
  CHECK_EQ(b.lineCount(), 2);
  CHECK_EQ(b.line(0), "a");
  CHECK_EQ(b.line(1), "b");
  CHECK(b.clamp({5, 5}) == Position{1, 1});
  CHECK(b.clamp({0, 99}) == Position{0, 1});
  CHECK(b.clamp({-1, 0}) == Position{0, 0});
}

TEST(textbuffer_offsets) {
  TextBuffer b("ab\ncd\nef");
  CHECK_EQ(b.offsetOf({1, 1}), 4u);
  CHECK(b.positionAt(4) == Position{1, 1});
  CHECK(b.positionAt(2) == Position{0, 2});
  CHECK(b.positionAt(3) == Position{1, 0});
  CHECK(b.positionAt(999) == Position{2, 2});
  CHECK_EQ(b.textInRange({{0, 1}, {2, 1}}), "b\ncd\ne");
}

TEST(textbuffer_utf16_columns) {
  // "aé😀b": a(1 byte) é(2 bytes) 😀(4 bytes, 2 utf16 units) b
  std::string line =
      "a\xC3\xA9\xF0\x9F\x98\x80"
      "b";
  CHECK_EQ(TextBuffer::utf8ToUtf16Column(line, 0), 0);
  CHECK_EQ(TextBuffer::utf8ToUtf16Column(line, 1), 1);
  CHECK_EQ(TextBuffer::utf8ToUtf16Column(line, 3), 2);
  CHECK_EQ(TextBuffer::utf8ToUtf16Column(line, 7), 4);
  CHECK_EQ(TextBuffer::utf8ToUtf16Column(line, 8), 5);
  CHECK_EQ(TextBuffer::utf16ToUtf8Column(line, 2), 3);
  CHECK_EQ(TextBuffer::utf16ToUtf8Column(line, 4), 7);
  CHECK_EQ(TextBuffer::utf16ToUtf8Column(line, 5), 8);
}

TEST(textbuffer_word_at) {
  TextBuffer b("  foo_bar->baz(1)");
  CHECK_EQ(b.wordAt({0, 4}).text, "foo_bar");
  CHECK_EQ(b.wordAt({0, 9}).text, "foo_bar");  // just after the word
  CHECK_EQ(b.wordAt({0, 11}).text, "baz");
  CHECK_EQ(b.wordAt({0, 17}).text, "");
  CHECK_EQ(b.wordAt({0, 15}).text, "1");
  CHECK(b.wordAt({0, 4}).range == Range{{0, 2}, {0, 9}});
}

TEST(document_undo_redo_and_listeners) {
  Document d = Document::fromText("/tmp/x.cpp", "int a;\nint b;\n");
  int calls = 0;
  Document::Change last;
  d.addListener([&](Document&, const Document::Change& c) {
    ++calls;
    last = c;
  });
  d.replace({{0, 4}, {0, 5}}, "alpha");
  CHECK_EQ(d.buffer().line(0), "int alpha;");
  CHECK_EQ(calls, 1);
  CHECK(last.removed == Range{{0, 4}, {0, 5}});
  CHECK_EQ(last.newText, "alpha");
  d.insert({1, 0}, "// c\n");
  CHECK_EQ(d.buffer().lineCount(), 4);
  CHECK(d.undo());
  CHECK_EQ(d.buffer().lineCount(), 3);
  CHECK(d.undo());
  CHECK_EQ(d.buffer().line(0), "int a;");
  CHECK(!d.undo());
  CHECK(d.redo());
  CHECK_EQ(d.buffer().line(0), "int alpha;");
  CHECK(d.redo());
  CHECK_EQ(d.buffer().line(1), "// c");
  CHECK(!d.redo());
  CHECK_EQ(calls, 6);
  CHECK(d.version() == 7);
}

TEST(uri_roundtrip) {
  CHECK_EQ(pathToUri("/home/u/my file.cpp"), "file:///home/u/my%20file.cpp");
  CHECK(uriToPath("file:///home/u/my%20file.cpp") == std::filesystem::path("/home/u/my file.cpp"));
  CHECK(uriToPath("file:///C:/src/a.cpp") == std::filesystem::path("C:/src/a.cpp"));
}

// ---------------------------------------------------------------------------
// Per-line terminators (W10 line endings, docs/VS_BEHAVIOUR.md section 1)
// ---------------------------------------------------------------------------

TEST(textbuffer_mixed_endings_round_trip_byte_for_byte) {
  const std::string src = "a\r\nb\nc\rd\r\n\r\ne\n\nlast";
  TextBuffer b(src);
  CHECK_EQ(b.lineCount(), 8);
  CHECK(b.mixedLineEndings());
  CHECK(b.hasPerLineEndings());
  CHECK(b.lineEnding() == LineEnding::LF);  // 3 CRLF, 3 LF, 1 CR: a tie, and ties prefer LF
  CHECK_EQ(b.textForDisk(), src);
  CHECK(b.endingAfter(0) == LineEnding::CRLF);
  CHECK(b.endingAfter(1) == LineEnding::LF);
  CHECK(b.endingAfter(2) == LineEnding::CR);
  CHECK(b.endingAfter(5) == LineEnding::LF);
  CHECK(!b.hasTerminator(7));
  CHECK_EQ(b.text(), "a\nb\nc\nd\n\ne\n\nlast");
  const auto c = b.endingCounts();
  CHECK_EQ(c.crlf, 3);
  CHECK_EQ(c.lf, 3);
  CHECK_EQ(c.cr, 1);
  CHECK_EQ(b.textInRangeRaw({{0, 0}, {4, 0}}), "a\r\nb\nc\rd\r\n");
  CHECK_EQ(b.textInRange({{0, 0}, {4, 0}}), "a\nb\nc\nd\n");
}

TEST(textbuffer_uniform_documents_keep_no_per_line_storage) {
  for (const char* eol : {"\n", "\r\n", "\r"}) {
    std::string s;
    for (int i = 0; i < 100; ++i) s += "line" + std::to_string(i) + eol;
    TextBuffer b(s);
    CHECK(!b.hasPerLineEndings());
    CHECK(!b.mixedLineEndings());
    CHECK_EQ(b.textForDisk(), s);
    // edits that use the same ending, in the middle and at both ends, stay compact
    b.replace({{3, 2}, {3, 2}}, std::string("X") + eol + "Y");
    b.replace({{10, 0}, {12, 3}}, "");
    b.replace({{0, 0}, {0, 0}}, std::string(eol));
    CHECK(!b.hasPerLineEndings());
    CHECK(!b.mixedLineEndings());
    CHECK(b.endingCounts().total() == b.lineCount() - 1);
  }
}

TEST(textbuffer_edits_keep_the_terminators_of_untouched_lines) {
  TextBuffer b("one\r\ntwo\nthree\rfour\r\nfive");
  // a keystroke inside a line changes nothing else
  b.insert({1, 1}, "X");
  CHECK_EQ(b.textForDisk(), "one\r\ntXwo\nthree\rfour\r\nfive");
  // joining two lines keeps the terminator of the later one
  b.erase({{1, 4}, {2, 0}});  // "tXwo" + "three" -> one line that ends like "three" (CR)
  CHECK_EQ(b.textForDisk(), "one\r\ntXwothree\rfour\r\nfive");
  b.erase({{0, 3}, {1, 0}});  // join "one" and "tXwothree": ends like the later line (CR)
  CHECK_EQ(b.textForDisk(), "onetXwothree\rfour\r\nfive");
  // splitting with a literal break stores that break; the tail keeps the old terminator
  b.insert({0, 3}, "\n");
  CHECK_EQ(b.textForDisk(), "one\ntXwothree\rfour\r\nfive");
  b.insert({1, 4}, "\r\n");
  CHECK_EQ(b.textForDisk(), "one\ntXwo\r\nthree\rfour\r\nfive");
  // a multi-line replace: the inserted text's own breaks, the last piece keeps the old last terminator
  b.replace({{1, 0}, {2, 2}}, "A\rB\nC");
  CHECK_EQ(b.textForDisk(), "one\nA\rB\nCree\rfour\r\nfive");
  // the last line has no terminator: a break typed there stores the typed one
  b.insert({5, 4}, "\r");
  CHECK_EQ(b.textForDisk(), "one\nA\rB\nCree\rfour\r\nfive\r");
  CHECK_EQ(b.lineCount(), 7);
  CHECK(b.endingAfter(5) == LineEnding::CR);
}

TEST(textbuffer_replace_many_keeps_and_sets_terminators) {
  TextBuffer b("a\r\nb\nc\r\nd\ne");
  // join a/b, split c, and break the last line, in one pass
  std::vector<TextBuffer::Edit> es{{{{0, 1}, {1, 0}}, "-"}, {{{2, 1}, {2, 1}}, "\r"}, {{{4, 1}, {4, 1}}, "\r\n"}};
  b.replaceMany(es);
  CHECK_EQ(b.textForDisk(), "a-b\nc\r\r\nd\ne\r\n");
  // the same edits one at a time, last to first, give the same text
  TextBuffer o("a\r\nb\nc\r\nd\ne");
  for (size_t k = es.size(); k-- > 0;) o.replace(es[k].range, es[k].text);
  CHECK_EQ(o.textForDisk(), b.textForDisk());
  // inserting a different ending into a uniform document
  TextBuffer u("x\ny\nz");
  std::vector<TextBuffer::Edit> ue{{{{0, 1}, {0, 1}}, "\r\n"}, {{{1, 1}, {1, 1}}, "!"}};
  u.replaceMany(ue);
  CHECK_EQ(u.textForDisk(), "x\r\n\ny!\nz");
  CHECK(u.mixedLineEndings());
}

TEST(textbuffer_first_break_of_a_one_line_document_decides_its_ending) {
  TextBuffer b("abc");
  b.insert({0, 3}, "\r\ndef");
  CHECK(!b.hasPerLineEndings());
  CHECK(b.lineEnding() == LineEnding::CRLF);
  CHECK_EQ(b.textForDisk(), "abc\r\ndef");
  TextBuffer e;  // an empty document with a chosen ending (a new file)
  e.setLineEnding(LineEnding::CRLF);
  e.insert({0, 0}, "x\ny");
  CHECK(e.mixedLineEndings() == false);
  CHECK_EQ(e.textForDisk(), "x\ny");  // the typed LF is kept literally, the chosen ending only serves Enter
  CHECK(e.lineEnding() == LineEnding::LF);
}

TEST(textbuffer_ending_for_a_new_line_replicates_the_caret_line) {
  TextBuffer b("a\r\nb\nc");
  const LineEnding dflt = LineEnding::CR;
  CHECK(b.endingForNewLine(0, dflt) == LineEnding::CRLF);
  CHECK(b.endingForNewLine(1, dflt) == LineEnding::LF);
  CHECK(b.endingForNewLine(2, dflt) == LineEnding::LF);  // last line: the line before it
  CHECK(b.endingForNewLine(99, dflt) == LineEnding::LF);
  TextBuffer one("abc");
  CHECK(one.endingForNewLine(0, dflt) == LineEnding::CR);  // no break at all: the default
  one.setLineEnding(LineEnding::LF);
  CHECK(one.endingForNewLine(0, dflt) == LineEnding::LF);  // an explicitly chosen ending wins
}

TEST(textbuffer_lsp_text_sends_lf_for_lone_cr_and_keeps_crlf) {
  TextBuffer b("a\r\nb\nc\rd");
  CHECK_EQ(b.textForLsp(), "a\r\nb\nc\nd");
  CHECK_EQ(TextBuffer::lspText("x\ry\r\nz\n"), "x\ny\r\nz\n");
  CHECK_EQ(TextBuffer::countBreaks("a\r\nb\rc\nd"), 3);
  TextBuffer cr("a\rb\rc");
  CHECK_EQ(cr.textForLsp(), "a\nb\nc");
  CHECK_EQ(cr.textForDisk(), "a\rb\rc");
}

TEST(document_undo_restores_mixed_terminators_byte_for_byte) {
  const std::string src = "a\r\nb\nc\rd\r\ne";
  Document d = Document::fromText("/tmp/mixed.cpp", src);
  d.replace({{0, 0}, {4, 1}}, "ALL");  // removes every kind of break
  CHECK_EQ(d.buffer().textForDisk(), "ALL");
  CHECK(d.undo());
  CHECK_EQ(d.buffer().textForDisk(), src);
  CHECK(d.redo());
  CHECK_EQ(d.buffer().textForDisk(), "ALL");
  CHECK(d.undo());
  d.replaceMany({{{{1, 1}, {2, 0}}, ""}, {{{3, 1}, {4, 0}}, ""}});
  CHECK_EQ(d.buffer().textForDisk(), "a\r\nbc\rde");
  CHECK(d.undo());
  CHECK_EQ(d.buffer().textForDisk(), src);
}

TEST(document_convert_line_endings_is_one_undo_step_touching_only_other_lines) {
  const std::string src = "a\r\nb\nc\rd\r\ne";
  Document d = Document::fromText("/tmp/mixed.cpp", src);
  int events = 0;
  d.addListener([&](Document&, const Document::Change&) { ++events; });
  d.convertLineEndings(LineEnding::CRLF);
  CHECK_EQ(events, 2);  // only the LF and the CR line were rewritten
  CHECK_EQ(d.buffer().textForDisk(), "a\r\nb\r\nc\r\nd\r\ne");
  CHECK(d.dirty());
  CHECK(!d.buffer().hasPerLineEndings());  // uniform again: compact
  CHECK(d.undo());
  CHECK_EQ(d.buffer().textForDisk(), src);
  CHECK(!d.canUndo());
  CHECK(d.redo());
  CHECK_EQ(d.buffer().textForDisk(), "a\r\nb\r\nc\r\nd\r\ne");
  d.convertLineEndings(LineEnding::LF);
  CHECK_EQ(d.buffer().textForDisk(), "a\nb\nc\nd\ne");
  events = 0;
  d.convertLineEndings(LineEnding::LF);  // already uniform: nothing to do
  CHECK_EQ(events, 0);
}
