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

TEST(textbuffer_crlf_lf_and_cr_mixed_keep_the_dominant_ending) {
  TextBuffer m("a\r\nb\r\nc\nd\re");
  CHECK_EQ(m.lineCount(), 5);
  CHECK(m.lineEnding() == LineEnding::CRLF);
  CHECK(m.mixedLineEndings());
  CHECK_EQ(m.textForDisk(), "a\r\nb\r\nc\r\nd\r\ne");  // normalised to the dominant ending (documented)
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
  CHECK(d.saveNote().empty());
  CHECK_EQ(Tmp::read(p),
           "const char* s=\"Gr\xF6\xDF"
           "e\";\r\n\xE4int x;\r\n");
  // A character Windows-1252 cannot hold: the file becomes UTF-8, with a note, and nothing is lost.
  d.insert({1, 0}, "\xE2\x89\xA0");
  REQUIRE(d.saveToDisk());
  CHECK(d.encoding() == Encoding::Utf8);
  CHECK(!d.saveNote().empty());
  CHECK(isValidUtf8(Tmp::read(p)));
  CHECK(Tmp::read(p).find("\xE2\x89\xA0") != std::string::npos);
  Document again(p);
  REQUIRE(again.loadFromDisk());
  CHECK(again.encoding() == Encoding::Utf8);
  CHECK_EQ(again.buffer().text(), d.buffer().text());
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
