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
  std::string line = "a\xC3\xA9\xF0\x9F\x98\x80" "b";
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
  d.addListener([&](Document&, const Document::Change& c) { ++calls; last = c; });
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
