// W9-FIX-ENC: encodings, line endings and the BOM. Decode / encode round trips, Document load and save in the file's
// own encoding, TextBuffer lines ending at CRLF / LF / lone CR, JSON that cannot throw on invalid UTF-8, the fast
// lexer on BOM / CR-only / C files, find-in-files on CR-only files, and an editor with a running clangd opening a
// Latin-1 file without terminating.
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "app/Editor.h"
#include "core/Document.h"
#include "core/JsonUtil.h"
#include "core/PathUtil.h"
#include "core/TextBuffer.h"
#include "core/TextEncoding.h"
#include "search/ReplaceInFiles.h"
#include "search/TextSearch.h"
#include "semantic/CppLexer.h"
#include "semantic/FastIndex.h"
#include "test_main.h"

using namespace tf;
namespace fs = std::filesystem;

namespace {

struct Tmp {
  fs::path dir;
  Tmp() {
    static int n = 0;
    dir = fs::weakly_canonical(fs::temp_directory_path()) /
          ("tenfold_enc_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_" +
           std::to_string(n++));
    fs::create_directories(dir);
  }
  ~Tmp() {
    std::error_code ec;
    fs::remove_all(dir, ec);
  }
  fs::path write(const std::string& name, const std::string& bytes) const {
    std::ofstream(dir / name, std::ios::binary) << bytes;
    return dir / name;
  }
  static std::string read(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
  }
};

const SymbolKind kAny = static_cast<SymbolKind>(-1);
const Symbol* findSym(const std::vector<Symbol>& v, const std::string& name, SymbolKind k = kAny) {
  for (auto& s : v)
    if (s.name == name && (k == kAny || s.kind == k)) return &s;
  return nullptr;
}

}  // namespace

// ---- decoding / encoding ----------------------------------------------------------------------------------------

TEST(encoding_detects_utf8_utf16_and_falls_back_to_windows1252) {
  CHECK(decodeFileBytes("plain ascii\n").encoding == Encoding::Utf8);
  CHECK(decodeFileBytes("Gr\xC3\xB6\xC3\x9F"
                        "e")
            .encoding == Encoding::Utf8);
  CHECK(decodeFileBytes("\xEF\xBB\xBFstruct S;").encoding == Encoding::Utf8);  // BOM stays in the text
  CHECK_EQ(decodeFileBytes("\xEF\xBB\xBFx").text, std::string("\xEF\xBB\xBF"
                                                              "x"));
  CHECK(decodeFileBytes("").encoding == Encoding::Utf8);

  auto latin = decodeFileBytes(
      "const char* s=\"Gr\xF6\xDF"
      "e\";");
  CHECK(latin.encoding == Encoding::Windows1252);
  CHECK_EQ(latin.text, std::string("const char* s=\"Gr\xC3\xB6\xC3\x9F"
                                   "e\";"));
  CHECK_EQ(decodeFileBytes("\x80 \x93x\x94 \x85").text,
           std::string("\xE2\x82\xAC \xE2\x80\x9Cx\xE2\x80\x9D \xE2\x80\xA6"));

  auto le =
      decodeFileBytes(std::string("\xFF\xFE"
                                  "a\0\xE9\0\x3D\xD8\x00\xDE",
                                  10));  // a, é, U+1F600
  CHECK(le.encoding == Encoding::Utf16LE);
  CHECK_EQ(le.text, std::string("a\xC3\xA9\xF0\x9F\x98\x80"));
  auto be = decodeFileBytes(std::string("\xFE\xFF\0a\0\xE9", 6));
  CHECK(be.encoding == Encoding::Utf16BE);
  CHECK_EQ(be.text, std::string("a\xC3\xA9"));
  // Not well-formed UTF-16 (odd length, lone surrogate): kept losslessly as Windows-1252, never an error.
  CHECK(decodeFileBytes(std::string("\xFF\xFE"
                                    "a\0b",
                                    5))
            .encoding == Encoding::Windows1252);
  CHECK(decodeFileBytes(std::string("\xFF\xFE\x00\xD8", 4)).encoding == Encoding::Windows1252);
  // Overlong, surrogate and too-large UTF-8 are invalid.
  CHECK(!isValidUtf8("\xC0\x80"));
  CHECK(!isValidUtf8("\xED\xA0\x80"));
  CHECK(!isValidUtf8("\xF4\x90\x80\x80"));
  CHECK(!isValidUtf8("\xE2\x82"));
  CHECK(isValidUtf8("\xF0\x9F\x98\x80"));
}

TEST(encoding_every_byte_string_round_trips_through_decode_and_encode) {
  std::string all;
  for (int i = 0; i < 256; ++i) all += static_cast<char>(i);
  auto d = decodeFileBytes(all);
  CHECK(d.encoding == Encoding::Windows1252);
  CHECK(isValidUtf8(d.text));
  std::string back;
  CHECK(encodeText(d.text, d.encoding, back));
  CHECK_EQ(back, all);
  // A pseudo-random mixture of valid and invalid bytes: whatever it decodes as, encoding gives the bytes back.
  unsigned x = 12345;
  for (int round = 0; round < 200; ++round) {
    std::string b;
    for (int i = 0; i < 64; ++i) {
      x = x * 1664525u + 1013904223u;
      b += static_cast<char>((x >> 24) & 0xFF);
    }
    auto dd = decodeFileBytes(b);
    CHECK(isValidUtf8(dd.text));
    std::string e;
    CHECK(encodeText(dd.text, dd.encoding, e));
    CHECK_EQ(e, b);
  }
  // UTF-16 round trip, with the BOM.
  for (Encoding enc : {Encoding::Utf16LE, Encoding::Utf16BE}) {
    const std::string text = "a\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80\n";
    std::string bytes;
    CHECK(encodeText(text, enc, bytes));
    auto r = decodeFileBytes(bytes);
    CHECK(r.encoding == enc);
    CHECK_EQ(r.text, text);
  }
}

TEST(encoding_windows1252_reports_characters_it_cannot_hold) {
  std::string out;
  size_t bad = 99;
  CHECK(
      encodeText("Gr\xC3\xB6\xC3\x9F"
                 "e \xE2\x82\xAC",
                 Encoding::Windows1252, out, &bad));
  CHECK_EQ(out, std::string("Gr\xF6\xDF"
                            "e \x80"));
  CHECK(
      !encodeText("ab\xE2\x89\xA0"
                  "c",
                  Encoding::Windows1252, out, &bad));  // U+2260
  CHECK_EQ(bad, size_t(2));
}

TEST(encoding_sanitize_makes_any_bytes_valid_utf8) {
  CHECK_EQ(sanitizeUtf8("ok \xC3\xA9"), std::string("ok \xC3\xA9"));
  CHECK_EQ(sanitizeUtf8("a\xF6"
                        "b"),
           std::string("a\xEF\xBF\xBD"
                       "b"));
  CHECK(isValidUtf8(sanitizeUtf8("\xFF\xFE\xC3")));
}

// ---- JSON ---------------------------------------------------------------------------------------------------------

TEST(json_dump_of_invalid_utf8_never_throws_through_dumpSafe) {
  nlohmann::json j = {{"path",
                       "dir/Gr\xF6\xDF"
                       "e.cpp"},
                      {"text", "x\xFF"}};
  bool threw = false;
  try {
    (void)j.dump();
  } catch (const nlohmann::json::type_error&) {
    threw = true;  // the default handler is what used to terminate the editor
  }
  CHECK(threw);
  std::string s;
  try {
    s = dumpSafe(j);
    s += dumpSafe(j, 2);
  } catch (...) { CHECK(false); }
  CHECK(s.find("\xEF\xBF\xBD") != std::string::npos);
  CHECK(nlohmann::json::parse(dumpSafe(j))["path"].get<std::string>().find("Gr") == 4);
}

// ---- line endings ---------------------------------------------------------------------------------------------------

TEST(textbuffer_lone_cr_is_a_line_break) {
  TextBuffer b("one\rtwo\rthree");
  CHECK_EQ(b.lineCount(), 3);
  CHECK_EQ(b.line(1), "two");
  CHECK(b.lineEnding() == LineEnding::CR);
  CHECK(!b.mixedLineEndings());
  CHECK_EQ(b.text(), "one\ntwo\nthree");
  CHECK_EQ(b.textForDisk(), "one\rtwo\rthree");
  TextBuffer t("a\r");  // trailing CR: an empty last line, like "a\n"
  CHECK_EQ(t.lineCount(), 2);
  CHECK_EQ(t.line(1), "");
  CHECK_EQ(t.textForDisk(), "a\r");
}

TEST(textbuffer_crlf_lf_and_cr_mixed_keep_each_lines_ending_and_report_the_dominant_one) {
  TextBuffer m("a\r\nb\r\nc\nd\re");
  CHECK_EQ(m.lineCount(), 5);
  CHECK(m.lineEnding() == LineEnding::CRLF);
  CHECK(m.mixedLineEndings());
  CHECK_EQ(m.textForDisk(), "a\r\nb\r\nc\nd\re");  // no normalisation: every line keeps its own ending (W10)
  TextBuffer lf("a\nb\nc\r\nd\r");
  CHECK(lf.lineEnding() == LineEnding::LF);
  CHECK(lf.mixedLineEndings());
  CHECK_EQ(lf.lineCount(), 5);
  TextBuffer cr("a\rb\rc\nd");
  CHECK(cr.lineEnding() == LineEnding::CR);
  TextBuffer tie("a\nb\r\nc");  // one each: LF wins ties
  CHECK(tie.lineEnding() == LineEnding::LF);
  TextBuffer crcrlf("a\r\r\nb");  // a lone CR, then a CRLF
  CHECK_EQ(crcrlf.lineCount(), 3);
  CHECK_EQ(crcrlf.line(0), "a");
  CHECK_EQ(crcrlf.line(1), "");
  TextBuffer none("no breaks");
  CHECK(none.lineEnding() == LineEnding::LF && !none.mixedLineEndings());
  CHECK_EQ(TextBuffer::withLineEnding("a\nb\r\nc\rd", LineEnding::CRLF), std::string("a\r\nb\r\nc\r\nd"));
  CHECK_EQ(TextBuffer::withLineEnding("a\nb\r\nc\rd", LineEnding::CR), std::string("a\rb\rc\rd"));
  CHECK_EQ(TextBuffer::withLineEnding("a\r\nb", LineEnding::LF), std::string("a\nb"));
}

TEST(textbuffer_edits_split_inserted_text_at_every_kind_of_break) {
  TextBuffer b("ab\ncd");
  auto r = b.replace({{0, 1}, {0, 1}}, "X\rY\r\nZ");
  CHECK_EQ(b.text(), "aX\nY\nZb\ncd");
  CHECK(r.inserted.end == Position{2, 1});
  CHECK_EQ(r.lineDelta, 2);
  // Typing a lone CR goes through the slow path and splits the line.
  TextBuffer t("xy");
  t.insert({0, 1}, "\r");
  CHECK_EQ(t.lineCount(), 2);
  CHECK_EQ(t.line(1), "y");
  // replaceMany takes the same path for several edits.
  TextBuffer m("a b c");
  auto res = m.replaceMany({{{{0, 1}, {0, 2}}, "\r"}, {{{0, 3}, {0, 4}}, "\r\n"}});
  CHECK_EQ(m.text(), "a\nb\nc");
  CHECK_EQ(res.size(), size_t(2));
  // textInRange/offsets still count a break as one byte (text() is LF-joined).
  CHECK_EQ(TextBuffer("a\rb").offsetOf({1, 0}), size_t(2));
}

// ---- Document: load and save in the file's encoding ---------------------------------------------------------------

TEST(document_latin1_file_loads_decoded_and_saves_byte_exact_or_re_encoded) {
  Tmp t;
  const std::string raw =
      "const char* s=\"Gr\xF6\xDF"
      "e\";\r\nint x;\r\n";
  auto p = t.write("latin1.cpp", raw);
  Document d(p);
  std::string err;
  REQUIRE(d.loadFromDisk(&err));
  CHECK(d.encoding() == Encoding::Windows1252);
  CHECK_EQ(d.buffer().line(0), std::string("const char* s=\"Gr\xC3\xB6\xC3\x9F"
                                           "e\";"));
  CHECK(isValidUtf8(d.buffer().text()));
  CHECK(d.buffer().lineEnding() == LineEnding::CRLF);
  REQUIRE(d.saveToDisk());  // unchanged: the very bytes
  CHECK_EQ(Tmp::read(p), raw);
  // An edit with a representable character is written in the old encoding.
  d.insert({1, 0}, "\xC3\xA4");
  REQUIRE(d.saveToDisk());
  CHECK(d.encoding() == Encoding::Windows1252);
  CHECK_EQ(Tmp::read(p),
           "const char* s=\"Gr\xF6\xDF"
           "e\";\r\n\xE4int x;\r\n");
  // A character Windows-1252 cannot hold: the save is refused (see the W10 tests below).
  d.insert({1, 0}, "\xE2\x89\xA0");
  CHECK(!d.saveToDisk());
  CHECK(d.encoding() == Encoding::Windows1252);
  CHECK_EQ(Tmp::read(p),
           "const char* s=\"Gr\xF6\xDF"
           "e\";\r\n\xE4int x;\r\n");  // untouched
}

TEST(document_utf16_utf8_bom_and_cr_only_files_round_trip_byte_exact) {
  Tmp t;
  struct C {
    const char* name;
    std::string bytes;
    Encoding enc;
  };
  std::string u16le;
  encodeText("int \xC3\xA9;\r\nint b;\r\n", Encoding::Utf16LE, u16le);
  for (const C& c :
       {C{"u16.cpp", u16le, Encoding::Utf16LE}, C{"bom.cpp", "\xEF\xBB\xBFstruct S { int v; };\r\n", Encoding::Utf8},
        C{"cr.c", "int a;\rint b;\rint c;\r", Encoding::Utf8}, C{"lf.c", "int a;\nint b;", Encoding::Utf8},
        C{"empty.c", "", Encoding::Utf8},
        C{"bin.dat", std::string("\x00\xFF\xFE\x01\x80", 5), Encoding::Windows1252}}) {
    auto p = t.write(c.name, c.bytes);
    Document d(p);
    REQUIRE(d.loadFromDisk());
    CHECK(d.encoding() == c.enc);
    CHECK(isValidUtf8(d.buffer().text()));
    REQUIRE(d.saveToDisk());
    CHECK_EQ(Tmp::read(p), c.bytes);
  }
  Document cr(t.dir / "cr.c");
  REQUIRE(cr.loadFromDisk());
  CHECK_EQ(cr.buffer().lineCount(), 4);
  CHECK(cr.buffer().lineEnding() == LineEnding::CR);
  Document u(t.dir / "u16.cpp");
  REQUIRE(u.loadFromDisk());
  CHECK_EQ(u.buffer().line(0), std::string("int \xC3\xA9;"));
}

// ---- the editor: no crash on any bytes, clangd gets valid UTF-8 -----------------------------------------------------

TEST(editor_opens_edits_and_saves_a_latin1_file_with_clangd_running) {
  Tmp t;
  t.write("compile_commands.json", "[]");
  auto lat = t.write("latin1.cpp",
                     "const char* s = \"Gr\xF6\xDF"
                     "e\";\nint latin_fn() { return 1; }\n");
  auto bin = t.write("junk.cpp", std::string("int a;\xFF\xFE\x80 \xC0\xAF\x00 int b;\n", 21));
  auto u16 = t.write("u16.cpp", std::string("\xFF\xFE"
                                            "i\0n\0t\0 \0x\0;\0",
                                            14));
  Editor::Options o;
  o.semantic.useClangd = !findExecutable("clangd").empty();
  Editor ed(t.dir, o);
  std::string err;
  REQUIRE(ed.start(&err));
  for (const auto& p : {lat, bin, u16}) {
    Document* d = ed.open(p, &err);  // used to terminate with nlohmann type_error 316 while clangd ran
    REQUIRE(d != nullptr);
    CHECK(isValidUtf8(d->buffer().text()));
  }
  Document* d = ed.document(lat);
  CHECK(d->encoding() == Encoding::Windows1252);
  d->insert({1, 0}, "// \xC3\xA4\n");  // an edit while clangd has the document: incremental sync
  CHECK(ed.save(*d, &err));
  CHECK_EQ(Tmp::read(lat),
           "const char* s = \"Gr\xF6\xDF"
           "e\";\n// \xE4\nint latin_fn() { return 1; }\n");
  // Reload from disk (the external-change path) keeps the same view.
  CHECK(ed.reloadFromDisk(*d, &err));
  CHECK_EQ(d->buffer().line(1), std::string("// \xC3\xA4"));
  // A restart re-sends every open document (SemanticEngine::reopenDocuments).
  if (o.semantic.useClangd) CHECK(ed.restartClangd(&err));
  ed.shutdown();
}

// ---- the fast lexer ---------------------------------------------------------------------------------------------

TEST(lexer_skips_the_utf8_bom_so_line_one_is_parsed) {
  const std::string bom = "\xEF\xBB\xBF";
  auto syms = extractSymbols(bom + "struct S { int v; };\nnamespace N { int inner; }\nenum class K { A, B };\n");
  const Symbol* v = findSym(syms, "v");
  REQUIRE(v != nullptr);
  CHECK_EQ(v->scope, "S");
  CHECK_EQ(v->line, 0);
  CHECK_EQ(v->col, 3 + 15);  // columns still count the BOM's three bytes, as clangd's do
  CHECK(findSym(syms, "inner") != nullptr);
  auto ns = extractSymbols(bom + "namespace bomns { int in_ns; }\n");
  REQUIRE(findSym(ns, "in_ns") != nullptr);
  CHECK_EQ(findSym(ns, "in_ns")->scope, "bomns");
  const std::string lexSrc = bom + "struct S";  // the lexer only views its text
  CppLexer lx(lexSrc);
  auto first = lx.next();
  CHECK(first.kind == Tok::Keyword);
  CHECK_EQ(std::string(first.text), "struct");
  CHECK_EQ(first.col, 3);
}

TEST(lexer_lone_cr_ends_lines_and_comments_and_directives) {
  const std::string src =
      "#ifndef H\r#define H\r// comment\rint first;\r/* block\rcomment */\rint second;\r\nint third;\n"
      "#define M(a) \\\r  a\rint fourth;\r";
  auto syms = extractSymbols(src);
  CHECK_EQ(findSym(syms, "first") ? findSym(syms, "first")->line : -1, 3);
  CHECK_EQ(findSym(syms, "second") ? findSym(syms, "second")->line : -1, 6);
  CHECK_EQ(findSym(syms, "third") ? findSym(syms, "third")->line : -1, 7);
  CHECK_EQ(findSym(syms, "fourth") ? findSym(syms, "fourth")->line : -1, 10);
  // The same text with every break turned into LF gives the same symbols at the same places.
  auto lf = extractSymbols(TextBuffer::withLineEnding(src, LineEnding::LF));
  CHECK_EQ(lf.size(), syms.size());
  for (size_t i = 0; i < lf.size() && i < syms.size(); ++i)
    CHECK(lf[i].name == syms[i].name && lf[i].line == syms[i].line && lf[i].col == syms[i].col);
}

TEST(lexer_c_files_treat_cpp_keywords_as_identifiers) {
  const std::string src = "int class(int);\nint delete(int x) { return x; }\nint x = class(1) + delete(2);\n";
  auto c = extractSymbols(src, nullptr, true);
  CHECK(findSym(c, "class", SymbolKind::Function) != nullptr);
  CHECK(findSym(c, "delete", SymbolKind::Function) != nullptr);
  auto cpp = extractSymbols(src, nullptr, false);
  CHECK(findSym(cpp, "delete", SymbolKind::Function) == nullptr);
  CHECK(CppLexer::isKeyword("class") && !CppLexer::isCKeyword("class"));
  for (const char* w : {"new", "template", "this", "namespace", "typename", "virtual", "public", "using", "try"})
    CHECK(CppLexer::isKeyword(w) && !CppLexer::isCKeyword(w));
  for (const char* w : {"struct", "int", "if", "return", "typedef", "_Bool", "restrict", "_Generic"})
    CHECK(CppLexer::isCKeyword(w));
  CHECK(isCSourceFile("a/b.c") && !isCSourceFile("a/b.cpp") && !isCSourceFile("a/b.h") && !isCSourceFile("a/b.C"));
}

// ---- find in files on CR-only files ------------------------------------------------------------------------------

TEST(search_and_replace_count_lone_cr_lines) {
  Tmp t;
  t.write("cr.c", "int a;\rint needle;\rint b;\rneedle();\r");
  SearchInput in;
  in.root = t.dir;
  in.files = {t.dir / "cr.c"};
  SearchOptions o;
  o.query = "needle";
  std::vector<SearchMatch> out;
  TextSearch::run(std::move(in), o, &out);
  std::sort(out.begin(), out.end(), [](auto& a, auto& b) { return a.line < b.line; });
  REQUIRE(out.size() == 2);
  CHECK_EQ(out[0].line, 1);
  CHECK_EQ(out[0].column, 4);
  CHECK_EQ(out[0].text, std::string("int needle;"));
  CHECK_EQ(out[1].line, 3);
  CHECK_EQ(out[1].column, 0);
  ReplaceCounts counts;
  const std::string r = replaceMatchesInText("int a;\rint needle;\rint b;\rneedle();\r", out, "pin", &counts);
  CHECK_EQ(r, std::string("int a;\rint pin;\rint b;\rpin();\r"));
  CHECK_EQ(counts.replaced, size_t(2));
  CHECK_EQ(counts.stale, size_t(0));
}

// ---- W10-ENC: characters the file's encoding cannot hold are refused -----------------------------------------------

namespace {
const std::string kHan = "\xE6\xBC\xA2";        // U+6F22
const std::string kEmoji = "\xF0\x9F\x98\x80";  // U+1F600
const std::string kLatin1File =
    "int a; // Gr\xF6\xDF"
    "e\r\nint b;\r\n";
}  // namespace

TEST(unencodable_detection_lists_every_character_with_code_point_line_and_column) {
  Tmp t;
  auto p = t.write("l.cpp", kLatin1File);
  Document d(p);
  REQUIRE(d.loadFromDisk());
  CHECK(d.findUnencodable().empty());
  d.insert({1, 4}, kHan + "x" + kEmoji);  // "int " + 漢 x 😀 + "b;"
  d.insert({0, 0}, "\xE2\x82\xAC");       // U+20AC IS in Windows-1252 (0x80): not reported
  const UnencodableReport rep = d.findUnencodable();
  CHECK(rep.encoding == Encoding::Windows1252);
  REQUIRE((rep.total) == size_t(2));
  REQUIRE((rep.chars.size()) == size_t(2));
  CHECK_EQ(rep.chars[0].codePoint, uint32_t(0x6F22));
  CHECK_EQ(rep.chars[0].line, 1);
  CHECK_EQ(rep.chars[0].column, 4);
  CHECK_EQ(rep.chars[0].text, kHan);
  CHECK_EQ(rep.chars[1].codePoint, uint32_t(0x1F600));
  CHECK_EQ(rep.chars[1].column, 8);  // bytes: 4 + 3 (漢) + 1 (x)
  CHECK_EQ(codePointLabel(0x6F22), std::string("U+6F22"));
  CHECK_EQ(codePointLabel(0x1F600), std::string("U+1F600"));
  CHECK_EQ(codePointLabel(0x41), std::string("U+0041"));
  CHECK_EQ(describeUnencodableChar(rep.chars[0]), std::string("U+6F22 at line 2, column 5"));
  CHECK_EQ(describeUnencodable(rep),
           std::string("Windows-1252 cannot hold 2 characters (first: U+6F22 at line 2, column 5)"));
  CHECK(d.findUnencodable(Encoding::Utf8).empty());
  CHECK(d.findUnencodable(Encoding::Utf16LE).empty());  // UTF-16 holds all of them
}

TEST(unencodable_list_is_capped_but_counts_everything) {
  Tmp t;
  Document d(t.write("l.cpp", kLatin1File));
  REQUIRE(d.loadFromDisk());
  std::string many;
  for (size_t i = 0; i < kMaxUnencodableListed + 50; ++i) many += kHan;
  d.insert({1, 0}, many);
  const UnencodableReport rep = d.findUnencodable();
  CHECK_EQ(rep.total, kMaxUnencodableListed + 50);
  CHECK_EQ(rep.chars.size(), kMaxUnencodableListed);
  CHECK_EQ(rep.chars.back().column, static_cast<int>(3 * (kMaxUnencodableListed - 1)));
}

TEST(save_of_a_windows1252_file_with_an_unencodable_character_is_refused_and_nothing_changes) {
  Tmp t;
  auto p = t.write("l.cpp", kLatin1File);
  Document d(p);
  REQUIRE(d.loadFromDisk());
  d.insert({0, 0}, kHan);
  d.insert({1, 0}, kEmoji);
  std::string err;
  UnencodableReport rep;
  CHECK(!d.saveToDisk(&err, &rep));
  CHECK_EQ(Tmp::read(p), kLatin1File);  // nothing written
  CHECK(d.dirty());
  CHECK(d.encoding() == Encoding::Windows1252);
  CHECK_EQ(rep.total, size_t(2));
  CHECK(err.find("l.cpp was not saved") != std::string::npos);
  CHECK(err.find("Windows-1252 cannot hold 2 characters") != std::string::npos);
  CHECK(err.find("U+6F22 at line 1, column 1") != std::string::npos);
  // Remove them and the save works again, byte exact.
  d.erase({{0, 0}, {0, 3}});
  d.erase({{1, 0}, {1, 4}});
  CHECK(d.saveToDisk(&err, &rep));
  CHECK_EQ(Tmp::read(p), kLatin1File);
  CHECK(!d.dirty());
}

TEST(utf16_holds_every_character_but_not_stray_bytes_and_utf8_never_refuses) {
  Tmp t;
  std::string u16;
  encodeText("int a;\r\n", Encoding::Utf16LE, u16);
  auto p = t.write("u.cpp", u16);
  Document d(p);
  REQUIRE(d.loadFromDisk());
  CHECK(d.encoding() == Encoding::Utf16LE);
  d.insert({0, 0}, kHan + kEmoji);  // every Unicode scalar value fits (surrogate pairs)
  REQUIRE(d.saveToDisk());
  Document back(p);
  REQUIRE(back.loadFromDisk());
  CHECK(back.encoding() == Encoding::Utf16LE);
  CHECK_EQ(back.buffer().line(0), kHan + kEmoji + "int a;");
  // What the UTF-8 text can hold beyond scalar values: a stray byte (0xFF) or an encoded lone surrogate (ED A0 80).
  // UTF-16 cannot represent them, so the save is refused instead of writing U+FFFD.
  d.insert({1, 0}, std::string("\xFF") + "\xED\xA0\x80");
  const UnencodableReport rep = d.findUnencodable();
  REQUIRE((rep.total) == size_t(2));
  CHECK_EQ(rep.chars[0].invalidByte, 0xFF);
  CHECK_EQ(describeUnencodableChar(rep.chars[0]), std::string("invalid byte 0xFF at line 2, column 1"));
  CHECK_EQ(rep.chars[1].codePoint, uint32_t(0xD800));
  CHECK_EQ(rep.chars[1].column, 1);
  const std::string before = Tmp::read(p);
  CHECK(!d.saveToDisk());
  CHECK_EQ(Tmp::read(p), before);
  CHECK(d.encoding() == Encoding::Utf16LE && d.dirty());
  std::string out;
  size_t bad = 0;
  CHECK(!encodeText("a\xFF", Encoding::Utf16BE, out, &bad));
  CHECK_EQ(bad, size_t(1));
  // UTF-8 files are written as they are.
  auto q = t.write("u8.cpp", "int a;\n");
  Document u(q);
  REQUIRE(u.loadFromDisk());
  u.insert({0, 0}, kHan + kEmoji + "\xFF");
  CHECK(u.findUnencodable().empty());
  CHECK(u.saveToDisk());
}

// The save-refusal tests below do not need clangd (and some CI jobs have none: start() fails without it).
static Editor::Options noClangdOptions() {
  Editor::Options o;
  o.semantic.useClangd = false;
  return o;
}

TEST(save_as_of_an_untitled_document_in_another_encoding_is_refused_and_stays_untitled) {
  Tmp t;
  Editor ed(t.dir, noClangdOptions());
  std::string err;
  REQUIRE(ed.start(&err));
  Document* d = ed.newUntitled(LineEnding::LF);
  d->setEncoding(Encoding::Windows1252);
  d->insert({0, 0}, "ok " + kHan);
  const fs::path was = d->path();
  UnencodableReport rep;
  CHECK(!ed.saveAs(*d, "new.cpp", &err, &rep));
  CHECK_EQ(rep.total, size_t(1));
  CHECK(d->isUntitled());
  CHECK(d->path() == was);
  CHECK(!fs::exists(t.dir / "new.cpp"));
  d->erase({{0, 3}, {0, 6}});
  CHECK(ed.saveAs(*d, "new.cpp", &err, &rep));
  CHECK_EQ(Tmp::read(t.dir / "new.cpp"), std::string("ok "));
  ed.shutdown();
}

TEST(editor_save_and_save_all_refuse_unencodable_text_and_save_the_other_documents) {
  Tmp t;
  auto bad = t.write("bad.cpp", kLatin1File);
  auto good = t.write("good.cpp", "int g;\n");
  auto bad2 = t.write("bad2.cpp", kLatin1File);
  Editor ed(t.dir, noClangdOptions());
  std::string err;
  REQUIRE(ed.start(&err));
  Document* b = ed.open(bad, &err);
  Document* g = ed.open(good, &err);
  Document* b2 = ed.open(bad2, &err);
  REQUIRE(b && g && b2);
  b->insert({1, 0}, kHan);
  g->insert({0, 0}, "// edit\n");
  b2->insert({0, 0}, kEmoji);
  b2->insert({1, 0}, kEmoji);
  UnencodableReport rep;
  CHECK(!ed.save(*b, &err, &rep));
  CHECK(err.find("was not saved") != std::string::npos);
  CHECK_EQ(rep.total, size_t(1));
  CHECK_EQ(rep.chars[0].line, 1);
  CHECK_EQ(Tmp::read(bad), kLatin1File);
  CHECK(b->dirty() && b->encoding() == Encoding::Windows1252);
  // Save All: the good document is written, both bad ones are reported with their characters.
  std::vector<SaveRefusal> refused;
  err.clear();
  CHECK(!ed.saveAll(&err, &refused));
  CHECK(err.empty());  // refusals are reported through `refused`
  REQUIRE((refused.size()) == size_t(2));
  CHECK(refused[0].file == bad);
  CHECK_EQ(refused[0].report.total, size_t(1));
  CHECK(refused[1].file == bad2);
  CHECK_EQ(refused[1].report.total, size_t(2));
  CHECK_EQ(Tmp::read(good), std::string("// edit\nint g;\n"));
  CHECK(!g->dirty() && b->dirty() && b2->dirty());
  CHECK_EQ(Tmp::read(bad), kLatin1File);
  CHECK_EQ(Tmp::read(bad2), kLatin1File);
  // Without the out parameter the refusal is the error.
  CHECK(!ed.saveAll(&err));
  CHECK(err.find("was not saved") != std::string::npos);
  // Fix one: Save All saves it and still reports the other.
  b->erase({{1, 0}, {1, 3}});
  refused.clear();
  CHECK(!ed.saveAll(nullptr, &refused));
  CHECK_EQ(refused.size(), size_t(1));
  CHECK(!b->dirty());
  ed.shutdown();
}

TEST(rename_edits_that_would_put_an_unencodable_character_in_a_closed_file_do_not_write_it) {
  Tmp t;
  auto closed = t.write("c.cpp", "int old_name;\r\nint x = old_name;\r\n");
  auto latin = t.write("l.cpp",
                       "int Gr\xF6\xDF"
                       "e = old_name;\r\n");
  Editor ed(t.dir, noClangdOptions());
  std::string err;
  REQUIRE(ed.start(&err));
  FileEdits fe;
  fe[closed].push_back({Range{{0, 4}, {0, 12}}, "new_" + kHan});  // UTF-8 file: fine
  fe[latin].push_back({Range{{0, 11}, {0, 19}}, "new_" + kHan});  // Windows-1252 file: refused
  const Editor::RenameResult res = ed.applyRenameEdits(fe, &err);
  CHECK(res.written.size() == 1 && res.written[0] == closed);
  REQUIRE((res.unsaved.size()) == size_t(1));
  CHECK(res.unsaved[0] == latin);
  REQUIRE((res.refused.size()) == size_t(1));
  CHECK(res.refused[0].file == latin);
  CHECK_EQ(res.refused[0].report.chars[0].codePoint, uint32_t(0x6F22));
  CHECK_EQ(Tmp::read(latin), std::string("int Gr\xF6\xDF"
                                         "e = old_name;\r\n"));
  CHECK(ed.document(latin) != nullptr && ed.document(latin)->dirty());  // left open and dirty for the user
  CHECK_EQ(Tmp::read(closed), std::string("int new_") + kHan + ";\r\nint x = old_name;\r\n");
  ed.shutdown();
}

TEST(replace_in_files_does_not_write_a_windows1252_file_when_the_replacement_is_unencodable) {
  Tmp t;
  const std::string latinBytes =
      "int needle; // Gr\xF6\xDF"
      "e\r\nneedle();\r\n";
  auto latin = t.write("l.cpp", latinBytes);
  auto utf8 = t.write("u.cpp", "int needle;\n");
  auto search = [&](const fs::path& f) {
    SearchInput in;
    in.root = t.dir;
    in.files = {f};
    SearchOptions o;
    o.query = "needle";
    std::vector<SearchMatch> out;
    TextSearch::run(std::move(in), o, &out);
    return out;
  };
  std::vector<SearchMatch> ms = search(latin);
  const auto u = search(utf8);
  ms.insert(ms.end(), u.begin(), u.end());
  REQUIRE((ms.size()) == size_t(3));
  Editor ed(t.dir, noClangdOptions());
  std::string err;
  REQUIRE(ed.start(&err));
  Editor::ReplaceInFilesResult r = ed.replaceInFiles(ms, kHan);
  CHECK_EQ(Tmp::read(latin), latinBytes);            // not written, not even partly
  CHECK_EQ(Tmp::read(utf8), "int " + kHan + ";\n");  // the UTF-8 file is fine
  CHECK_EQ(r.filesWritten, size_t(1));
  CHECK_EQ(r.replaced, size_t(1));
  REQUIRE((r.errors.size()) == size_t(1));
  CHECK(r.errors[0].find("l.cpp was not changed: Windows-1252 cannot hold") != std::string::npos);
  REQUIRE((r.refused.size()) == size_t(1));
  CHECK(r.refused[0].file == latin);
  REQUIRE((r.refused[0].report.total) == size_t(2));  // one per match
  CHECK_EQ(r.refused[0].report.chars[0].codePoint, uint32_t(0x6F22));
  CHECK_EQ(r.refused[0].report.chars[0].line, 0);
  CHECK_EQ(r.refused[0].report.chars[0].column, 4);
  CHECK_EQ(r.refused[0].report.chars[1].line, 1);
  // A replacement the code page can hold is converted to it (not inserted as UTF-8 bytes) and the rest is untouched.
  ms = search(latin);
  r = ed.replaceInFiles(ms,
                        "n\xC3\xA4"
                        "dle");  // "nädle"
  CHECK(r.errors.empty() && r.refused.empty());
  CHECK_EQ(r.replaced, size_t(2));
  CHECK_EQ(Tmp::read(latin), std::string("int n\xE4"
                                         "dle; // Gr\xF6\xDF"
                                         "e\r\nn\xE4"
                                         "dle();\r\n"));
  // The function itself: the same through replaceMatchesInFile, with the offending characters out.
  auto again = t.write("l2.cpp", latinBytes);
  ms = search(again);
  UnencodableReport rep;
  ReplaceCounts counts;
  CHECK(!replaceMatchesInFile(again, ms, kEmoji, &counts, &err, &rep));
  CHECK_EQ(counts.replaced, size_t(0));
  CHECK_EQ(rep.total, size_t(2));
  CHECK_EQ(Tmp::read(again), latinBytes);
  ed.shutdown();
}

// ---------------------------------------------------------------------------
// VS-ENC: encoding picker, Reopen / Save with Encoding, autoDetectUtf8, defaultEncoding, the fallback code page
// ---------------------------------------------------------------------------

namespace {
const std::string kBom = "\xEF\xBB\xBF";
const std::string kUtf8Cafe = "caf\xC3\xA9;\n";  // "café;"
}  // namespace

TEST(encoding_choices_have_vs_labels_and_setting_names) {
  CHECK_EQ(std::string(encodingLabel(EncodingChoice::Utf8)), std::string("UTF-8"));
  CHECK_EQ(std::string(encodingLabel(EncodingChoice::Utf8Signature)), std::string("UTF-8 with signature"));
  CHECK_EQ(std::string(encodingLabel(EncodingChoice::Utf16LE)), std::string("UTF-16 LE"));
  CHECK_EQ(std::string(encodingLabel(EncodingChoice::Utf16BE)), std::string("UTF-16 BE"));
  CHECK_EQ(std::string(encodingLabel(EncodingChoice::Windows1252)), std::string("Windows-1252"));
  for (EncodingChoice c : kEncodingChoices) {
    auto back = parseEncodingChoice(encodingChoiceSetting(c));
    REQUIRE(back.has_value());
    CHECK(*back == c);
  }
  CHECK(*parseEncodingChoice(" UTF-8-BOM ") == EncodingChoice::Utf8Signature);
  CHECK(!parseEncodingChoice("shift_jis").has_value());
  CHECK(!parseEncodingChoice("").has_value());
}

TEST(document_encoding_choice_is_derived_from_the_bytes_it_was_read_from) {
  Tmp t;
  struct C {
    const char* name;
    std::string bytes;
    EncodingChoice want;
  };
  std::string u16le, u16be;
  encodeText("a\n", Encoding::Utf16LE, u16le);
  encodeText("a\n", Encoding::Utf16BE, u16be);
  for (const C& c :
       {C{"a.cpp", "int a;\n", EncodingChoice::Utf8}, C{"b.cpp", kBom + "int a;\n", EncodingChoice::Utf8Signature},
        C{"c.cpp", u16le, EncodingChoice::Utf16LE}, C{"d.cpp", u16be, EncodingChoice::Utf16BE},
        C{"e.cpp", "caf\xE9\n", EncodingChoice::Windows1252}}) {
    Document d(t.write(c.name, c.bytes));
    REQUIRE(d.loadFromDisk());
    CHECK(d.encodingChoice() == c.want);
  }
}

TEST(reopen_with_encoding_decodes_the_bytes_again_and_resets_undo) {
  Tmp t;
  auto p = t.write("a.cpp", kUtf8Cafe);
  Editor ed(t.dir, noClangdOptions());
  std::string err;
  REQUIRE(ed.start(&err));
  Document* d = ed.open(p, &err);
  REQUIRE(d);
  CHECK(d->encoding() == Encoding::Utf8);
  d->insert({0, 0}, "// edit\n");  // unsaved edit, undo history
  CHECK(d->dirty() && d->canUndo());
  // UTF-8 bytes read as Windows-1252: the two bytes of U+00E9 become two characters; edits are discarded.
  REQUIRE(ed.reopenWithEncoding(*d, EncodingChoice::Windows1252, &err));
  CHECK(d->encoding() == Encoding::Windows1252);
  CHECK_EQ(d->buffer().line(0), std::string("caf\xC3\x83\xC2\xA9;"));
  CHECK(!d->dirty() && !d->canUndo() && !d->canRedo());
  CHECK_EQ(Tmp::read(p), kUtf8Cafe);  // the file is untouched
  // Back to UTF-8.
  REQUIRE(ed.reopenWithEncoding(*d, EncodingChoice::Utf8, &err));
  CHECK(d->encoding() == Encoding::Utf8);
  CHECK_EQ(d->buffer().line(0), std::string("caf\xC3\xA9;"));
  // A Latin-1 file is Windows-1252 by detection; as UTF-8 its stray byte stays a byte (saving writes it back).
  auto q = t.write("l.cpp", "caf\xE9;\n");
  Document* l = ed.open(q, &err);
  REQUIRE(l);
  CHECK(l->encoding() == Encoding::Windows1252);
  CHECK_EQ(l->buffer().line(0), std::string("caf\xC3\xA9;"));
  REQUIRE(ed.reopenWithEncoding(*l, EncodingChoice::Utf8, &err));
  CHECK(l->encoding() == Encoding::Utf8);
  CHECK_EQ(l->buffer().line(0), std::string("caf\xE9;"));
  REQUIRE(ed.save(*l, &err));  // nothing changed: nothing lost
  l->insert({0, 0}, "x");
  REQUIRE(ed.save(*l, &err));
  CHECK_EQ(Tmp::read(q), std::string("xcaf\xE9;\n"));
  ed.shutdown();
}

TEST(reopen_with_utf16_needs_well_formed_text_and_drops_a_matching_bom) {
  Tmp t;
  std::string le;
  encodeText("A\xC3\xA9\n", Encoding::Utf16LE, le);
  auto p = t.write("u.cpp", le);
  auto raw = t.write("raw.cpp", std::string("a\0\n\0", 4));  // UTF-16 LE without a BOM: detected as ASCII/UTF-8
  auto odd = t.write("odd.cpp", "abc\n");                    // 4 bytes: well-formed UTF-16 (two units) after all
  auto bad = t.write("bad.cpp", "abcde\n");                  // 6 bytes... use an odd length instead
  auto odd5 = t.write("odd5.cpp", "abcd\n");                 // 5 bytes: odd, not UTF-16
  Editor ed(t.dir, noClangdOptions());
  std::string err;
  REQUIRE(ed.start(&err));
  Document* d = ed.open(p, &err);
  REQUIRE(d && d->encoding() == Encoding::Utf16LE);
  // As UTF-16 BE the same bytes are different (FF FE is no BOM there): U+FFFE then more; reopening still works.
  REQUIRE(ed.reopenWithEncoding(*d, EncodingChoice::Utf16BE, &err));
  CHECK(d->encoding() == Encoding::Utf16BE);
  REQUIRE(ed.reopenWithEncoding(*d, EncodingChoice::Utf16LE, &err));
  CHECK_EQ(d->buffer().line(0), std::string("A\xC3\xA9"));
  Document* r = ed.open(raw, &err);
  REQUIRE(r);
  REQUIRE(ed.reopenWithEncoding(*r, EncodingChoice::Utf16LE, &err));  // no BOM needed to ask for UTF-16
  CHECK_EQ(r->buffer().line(0), std::string("a"));
  REQUIRE(ed.save(*r, &err));  // written with a BOM
  CHECK_EQ(Tmp::read(raw), std::string("\xFF\xFE"
                                       "a\0\n\0",
                                       6));
  // Not well-formed: refused with a reason, the document is untouched.
  Document* o = ed.open(odd5, &err);
  REQUIRE(o);
  o->insert({0, 0}, "x");
  CHECK(!ed.reopenWithEncoding(*o, EncodingChoice::Utf16LE, &err));
  CHECK(err.find("not well-formed UTF-16 LE") != std::string::npos);
  CHECK(o->dirty() && o->buffer().line(0) == "xabcd" && o->encoding() == Encoding::Utf8);
  (void)odd;
  (void)bad;
  // An untitled document has no file to read.
  Document* u = ed.newUntitled(LineEnding::LF);
  CHECK(!ed.reopenWithEncoding(*u, EncodingChoice::Utf8, &err));
  ed.shutdown();
}

TEST(save_with_encoding_converts_the_file_and_the_document_keeps_the_new_encoding) {
  Tmp t;
  auto p = t.write("a.cpp", kUtf8Cafe);
  Editor ed(t.dir, noClangdOptions());
  std::string err;
  REQUIRE(ed.start(&err));
  Document* d = ed.open(p, &err);
  REQUIRE(d);
  REQUIRE(ed.saveWithEncoding(*d, EncodingChoice::Windows1252, std::nullopt, &err));
  CHECK_EQ(Tmp::read(p), std::string("caf\xE9;\n"));
  CHECK(d->encoding() == Encoding::Windows1252 && !d->dirty());
  REQUIRE(ed.saveWithEncoding(*d, EncodingChoice::Utf16BE, std::nullopt, &err));
  std::string be;
  encodeText(kUtf8Cafe, Encoding::Utf16BE, be);
  CHECK_EQ(Tmp::read(p), be);
  REQUIRE(ed.saveWithEncoding(*d, EncodingChoice::Utf8Signature, std::nullopt, &err));
  CHECK_EQ(Tmp::read(p), kBom + kUtf8Cafe);
  CHECK(d->encodingChoice() == EncodingChoice::Utf8Signature && !d->dirty());
  CHECK_EQ(d->buffer().line(0), std::string("caf\xC3\xA9;"));  // the signature added by a save is not text
  // A plain save keeps the signature.
  d->insert({0, 0}, "x");
  REQUIRE(ed.save(*d, &err));
  CHECK_EQ(Tmp::read(p), kBom + "xcaf\xC3\xA9;\n");
  // ... and so does UTF-8 with signature -> UTF-8 (the BOM leaves the file).
  REQUIRE(ed.saveWithEncoding(*d, EncodingChoice::Utf8, std::nullopt, &err));
  CHECK_EQ(Tmp::read(p), std::string("xcaf\xC3\xA9;\n"));
  CHECK(d->encodingChoice() == EncodingChoice::Utf8);
  ed.shutdown();
}

TEST(save_with_encoding_handles_a_signature_that_is_part_of_the_text) {
  Tmp t;
  auto p = t.write("b.cpp", kBom + "int a;\r\nint b;\r\n");
  Editor ed(t.dir, noClangdOptions());
  std::string err;
  REQUIRE(ed.start(&err));
  Document* d = ed.open(p, &err);
  REQUIRE(d);
  CHECK(d->encodingChoice() == EncodingChoice::Utf8Signature);
  d->insert({1, 0}, "// ");
  // U+FEFF is not in Windows-1252, but the signature leaves the text with this conversion: no refusal.
  CHECK(d->findUnencodable(EncodingChoice::Windows1252).empty());
  REQUIRE(ed.saveWithEncoding(*d, EncodingChoice::Windows1252, std::nullopt, &err));
  CHECK_EQ(Tmp::read(p), std::string("int a;\r\n// int b;\r\n"));
  CHECK_EQ(d->buffer().line(0), std::string("int a;"));
  CHECK(d->encoding() == Encoding::Windows1252);
  // The edit that removed the BOM is part of the undo history; the encoding is not.
  Document* e = ed.open(t.write("e.cpp", kBom + "x\n"), &err);
  REQUIRE(e);
  REQUIRE(ed.saveWithEncoding(*e, EncodingChoice::Utf16LE, std::nullopt, &err));
  CHECK_EQ(e->buffer().line(0), std::string("x"));
  CHECK(e->undo());
  CHECK_EQ(e->buffer().line(0), kBom + "x");
  ed.shutdown();
}

TEST(save_with_encoding_refusal_keeps_the_old_encoding_text_and_line_endings) {
  Tmp t;
  auto p = t.write("a.cpp", kUtf8Cafe + "x\n");
  Editor ed(t.dir, noClangdOptions());
  std::string err;
  REQUIRE(ed.start(&err));
  Document* d = ed.open(p, &err);
  REQUIRE(d);
  d->insert({0, 0}, kHan);
  const std::string before = Tmp::read(p);
  UnencodableReport rep;
  CHECK(!ed.saveWithEncoding(*d, EncodingChoice::Windows1252, LineEnding::CRLF, &err, &rep));
  CHECK_EQ(rep.total, size_t(1));
  CHECK(rep.encoding == Encoding::Windows1252);
  CHECK_EQ(rep.chars[0].codePoint, uint32_t(0x6F22));
  CHECK(err.find("was not saved") != std::string::npos);
  CHECK(d->encoding() == Encoding::Utf8 && d->dirty());
  CHECK(d->buffer().endingAfter(0) == LineEnding::LF);  // the requested line endings were not applied either
  CHECK_EQ(Tmp::read(p), before);
  // UTF-16 holds the character.
  REQUIRE(ed.saveWithEncoding(*d, EncodingChoice::Utf16LE, LineEnding::CRLF, &err));
  CHECK(d->encoding() == Encoding::Utf16LE);
  std::string want;
  encodeText(kHan + "caf\xC3\xA9;\r\nx\r\n", Encoding::Utf16LE, want);
  CHECK_EQ(Tmp::read(p), want);
  ed.shutdown();
}

TEST(save_with_encoding_utf8_is_the_way_out_of_a_refused_windows1252_save) {
  Tmp t;
  auto p = t.write("l.cpp", kLatin1File);
  Editor ed(t.dir, noClangdOptions());
  std::string err;
  REQUIRE(ed.start(&err));
  Document* d = ed.open(p, &err);
  REQUIRE(d && d->encoding() == Encoding::Windows1252);
  d->insert({1, 0}, kHan);
  UnencodableReport rep;
  CHECK(!ed.save(*d, &err, &rep));  // the refusal
  CHECK_EQ(rep.total, size_t(1));
  CHECK_EQ(Tmp::read(p), kLatin1File);
  REQUIRE(ed.saveWithEncoding(*d, EncodingChoice::Utf8, std::nullopt, &err));
  CHECK(d->encoding() == Encoding::Utf8 && !d->dirty());
  CHECK_EQ(Tmp::read(p), std::string("int a; // Gr\xC3\xB6\xC3\x9F"
                                     "e\r\n" +
                                     kHan + "int b;\r\n"));
  // The same through "UTF-8 with signature".
  auto q = t.write("m.cpp", kLatin1File);
  Document* m = ed.open(q, &err);
  REQUIRE(m);
  m->insert({1, 0}, kEmoji);
  REQUIRE(ed.saveWithEncoding(*m, EncodingChoice::Utf8Signature, std::nullopt, &err));
  CHECK_EQ(Tmp::read(q).substr(0, 3), kBom);
  ed.shutdown();
}

TEST(save_with_encoding_converts_line_endings_in_the_same_save_and_untitled_only_takes_the_encoding) {
  Tmp t;
  auto p = t.write("a.cpp", "a\nb\n");
  Editor ed(t.dir, noClangdOptions());
  std::string err;
  REQUIRE(ed.start(&err));
  Document* d = ed.open(p, &err);
  REQUIRE(d);
  REQUIRE(ed.saveWithEncoding(*d, EncodingChoice::Windows1252, LineEnding::CRLF, &err));
  CHECK_EQ(Tmp::read(p), std::string("a\r\nb\r\n"));
  Document* u = ed.newUntitled(LineEnding::LF);
  u->insert({0, 0}, "caf\xC3\xA9");
  REQUIRE(ed.saveWithEncoding(*u, EncodingChoice::Windows1252, std::nullopt, &err));  // writes nothing
  CHECK(u->isUntitled() && u->encoding() == Encoding::Windows1252);
  REQUIRE(ed.saveAs(*u, "new.cpp", &err));
  CHECK_EQ(Tmp::read(t.dir / "new.cpp"), std::string("caf\xE9"));
  ed.shutdown();
}

TEST(auto_detect_utf8_off_reads_bomless_non_ascii_as_the_fallback_code_page) {
  Tmp t;
  auto p = t.write("a.cpp", kUtf8Cafe);
  auto ascii = t.write("b.cpp", "int a;\n");
  auto bom = t.write("c.cpp", kBom + kUtf8Cafe);
  DetectOptions off;
  off.autoDetectUtf8 = false;
  CHECK(decodeFileBytes(kUtf8Cafe).encoding == Encoding::Utf8);
  const DecodedText t0 = decodeFileBytes(kUtf8Cafe, off);
  CHECK(t0.encoding == Encoding::Windows1252);
  CHECK_EQ(t0.text, std::string("caf\xC3\x83\xC2\xA9;\n"));
  CHECK(decodeFileBytes("int a;\n", off).encoding == Encoding::Utf8);        // ASCII is both
  CHECK(decodeFileBytes(kBom + kUtf8Cafe, off).encoding == Encoding::Utf8);  // a signature is a signature
  CHECK(decodeFileBytes(std::string("\xFF\xFE"
                                    "a\0",
                                    4),
                        off)
            .encoding == Encoding::Utf16LE);  // so is a UTF-16 BOM
  Editor ed(t.dir, noClangdOptions());
  std::string err;
  REQUIRE(ed.start(&err));
  ed.editing().autoDetectUtf8 = false;
  Document* d = ed.open(p, &err);
  REQUIRE(d);
  CHECK(d->encoding() == Encoding::Windows1252);
  CHECK_EQ(d->buffer().line(0), std::string("caf\xC3\x83\xC2\xA9;"));
  CHECK(ed.open(ascii, &err)->encoding() == Encoding::Utf8);
  CHECK(ed.open(bom, &err)->encodingChoice() == EncodingChoice::Utf8Signature);
  // On again: reload (an external change) detects UTF-8 again.
  ed.editing().autoDetectUtf8 = true;
  REQUIRE(ed.reloadFromDisk(*d, &err));
  CHECK(d->encoding() == Encoding::Utf8);
  // The unsaved bytes survive a round trip in the chosen encoding: with detection off, saving writes the file as read.
  ed.editing().autoDetectUtf8 = false;
  REQUIRE(ed.reloadFromDisk(*d, &err));
  d->insert({0, 0}, "x");
  REQUIRE(ed.save(*d, &err));
  CHECK_EQ(Tmp::read(p), std::string("xcaf\xC3\xA9;\n"));
  ed.shutdown();
}

TEST(new_untitled_documents_take_the_default_encoding) {
  Tmp t;
  Editor ed(t.dir, noClangdOptions());
  std::string err;
  REQUIRE(ed.start(&err));
  CHECK(ed.newUntitled(LineEnding::LF)->encodingChoice() == EncodingChoice::Utf8);  // the default
  struct C {
    EncodingChoice c;
    std::string file, want;
  };
  std::string u16;
  encodeText("h\xC3\xA9\n", Encoding::Utf16LE, u16);
  int n = 0;
  for (const C& c :
       {C{EncodingChoice::Utf8Signature, "a.txt", kBom + "h\xC3\xA9\n"}, C{EncodingChoice::Utf16LE, "b.txt", u16},
        C{EncodingChoice::Windows1252, "c.txt", "h\xE9\n"}, C{EncodingChoice::Utf8, "d.txt", "h\xC3\xA9\n"}}) {
    ed.editing().defaultEncoding = c.c;
    Document* d = ed.newUntitled(LineEnding::LF);
    CHECK(d->encodingChoice() == c.c);
    CHECK(!d->dirty());  // a new document with a signature or UTF-16 is still empty and clean
    d->insert({0, 0}, "h\xC3\xA9\n");
    REQUIRE(ed.saveAs(*d, c.file, &err));
    CHECK_EQ(Tmp::read(t.dir / c.file), c.want);
    ++n;
  }
  CHECK_EQ(n, 4);
  // A new Windows-1252 document refuses what the page cannot hold, like any other.
  ed.editing().defaultEncoding = EncodingChoice::Windows1252;
  Document* d = ed.newUntitled(LineEnding::LF);
  d->insert({0, 0}, kHan);
  UnencodableReport rep;
  CHECK(!ed.saveAs(*d, "e.txt", &err, &rep));
  CHECK_EQ(rep.total, size_t(1));
  ed.shutdown();
}

TEST(fallback_code_page_is_injectable_and_other_pages_read_as_windows1252) {
  bool exact = false;
  setAnsiCodePageForTesting(0);
  CHECK(fallbackEncoding(1252, &exact) == Encoding::Windows1252);
  CHECK(exact);
  CHECK(fallbackEncoding(932, &exact) == Encoding::Windows1252);  // Shift-JIS: the codec has no table for it
  CHECK(!exact);
  CHECK(fallbackEncoding(0, &exact) == Encoding::Windows1252);  // the system page: 1252 off Windows
#ifndef _WIN32
  CHECK(exact);
  CHECK_EQ(systemAnsiCodePage(), 1252u);
#endif
  setAnsiCodePageForTesting(932);
  CHECK_EQ(systemAnsiCodePage(), 932u);
  CHECK(fallbackEncoding(0, &exact) == Encoding::Windows1252);
  CHECK(!exact);
  DetectOptions o;
  o.ansiCodePage = 932;
  const DecodedText d = decodeFileBytes("caf\xE9", o);
  CHECK(d.encoding == Encoding::Windows1252);
  CHECK_EQ(d.text, std::string("caf\xC3\xA9"));
  // The editor says so once (in its log) when it opens a non-UTF-8 file.
  Tmp t;
  int warnings = 0;
  Editor::Options eo = noClangdOptions();
  eo.log = [&](const std::string& m) {
    if (m.find("system ANSI code page is 932") != std::string::npos) ++warnings;
  };
  Editor ed(t.dir, eo);
  std::string err;
  REQUIRE(ed.start(&err));
  REQUIRE(ed.open(t.write("a.cpp", "caf\xE9\n"), &err));
  REQUIRE(ed.open(t.write("b.cpp", "caf\xE9\n"), &err));
  CHECK_EQ(warnings, 1);
  ed.shutdown();
  setAnsiCodePageForTesting(0);
}

// A file whose line endings changed on disk (an external tool, a git checkout with autocrlf) while lines were also
// added above must not count as a replacement of every line: positions below the change shift, they are not lost.
TEST(reload_of_a_file_whose_line_endings_changed_on_disk_keeps_positions_and_takes_the_new_endings) {
  Tmp t;
  auto f = t.write("e.cpp", "a\r\nb\r\nc\r\n");
  Editor ed(t.dir, noClangdOptions());
  std::string err;
  REQUIRE(ed.start(&err));
  REQUIRE(ed.gotoLocation({f, Position{2, 0}}, &err));  // opens it as the current document
  Document* d = ed.currentDocument();
  REQUIRE(d != nullptr);
  ed.insertText("Q");
  REQUIRE(ed.save(*d));
  REQUIRE(ed.lastEditLocation().has_value());
  CHECK_EQ(ed.lastEditLocation()->pos.line, 2);
  t.write("e.cpp", "x\ny\nz\na\nb\nQc\n");  // three lines above, every terminator now LF
  REQUIRE(ed.reloadFromDisk(*d, &err));
  CHECK_EQ(d->buffer().text(), std::string("x\ny\nz\na\nb\nQc\n"));
  CHECK(d->buffer().lineEnding() == LineEnding::LF);
  CHECK(!d->buffer().mixedLineEndings());
  CHECK_EQ(Tmp::read(f), std::string("x\ny\nz\na\nb\nQc\n"));
  REQUIRE(ed.lastEditLocation().has_value());
  CHECK_EQ(ed.lastEditLocation()->pos.line, 5);
  CHECK_EQ(ed.lastEditLocation()->pos.column, 1);
  // Only the terminators changed: nothing moves, and a mixed file keeps each line's own ending.
  t.write("e.cpp", "x\r\ny\nz\r\na\nb\nQc\n");
  REQUIRE(ed.reloadFromDisk(*d, &err));
  CHECK(d->buffer().mixedLineEndings());
  CHECK_EQ(d->buffer().textForDisk(), std::string("x\r\ny\nz\r\na\nb\nQc\n"));
  CHECK_EQ(ed.lastEditLocation()->pos.line, 5);
  ed.shutdown();
}
