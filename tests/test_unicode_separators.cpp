// NEL (U+0085), LS (U+2028) and PS (U+2029) are line breaks in the editor, like in Visual Studio (user decision
// 2026-11-07, docs/VS_BEHAVIOUR.md 1.3 item 8) while clang and clangd do not know them: the buffer, its consumers (find,
// replace, the fast lexer, the corpus anchors, the diff) and the clangd position mapping (LspLineMap). The clangd tests
// need a clangd and are skipped without one.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>

#include "app/Editor.h"
#include "core/Document.h"
#include "core/LspLineMap.h"
#include "core/Platform.h"
#include "core/TextBuffer.h"
#include "corpus/Corpus.h"
#include "search/ReplaceInFiles.h"
#include "search/TextSearch.h"
#include "semantic/CppLexer.h"
#include "semantic/FastIndex.h"
#include "test_main.h"
#include "vcs/LineDiff.h"

using namespace tf;
namespace fs = std::filesystem;

namespace {

const std::string LS = "\xE2\x80\xA8";
const std::string PS = "\xE2\x80\xA9";
const std::string NEL = "\xC2\x85";

struct TempDir {
  fs::path path;
  TempDir() {
    static std::atomic<int> n{0};
    path = fs::temp_directory_path() /
           ("tenfold_sep_" + std::to_string(currentProcessId()) + "_" + std::to_string(n++) + "_" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::remove_all(path);
    fs::create_directories(path);
    std::error_code ec;
    path = fs::weakly_canonical(path, ec);
  }
  ~TempDir() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
  fs::path write(const std::string& rel, const std::string& text) const {
    const fs::path p = path / rel;
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary | std::ios::trunc) << text;
    return p;
  }
};

std::string slurp(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

bool clangdAvailable() { return !findExecutable("clangd").empty(); }

#define SKIP_WITHOUT_CLANGD()                               \
  if (!clangdAvailable()) {                                 \
    std::cerr << "    SKIPPED: clangd not found on PATH\n"; \
    return;                                                 \
  }

}  // namespace

// ---------------------------------------------------------------------------------------------------------------------
// The buffer
// ---------------------------------------------------------------------------------------------------------------------

TEST(sep_buffer_splits_at_nel_ls_ps_and_round_trips_byte_exact) {
  const std::string src = "a" + LS + "b" + PS + "c" + NEL + "d\ne\r\nf\rg";
  TextBuffer b(src);
  REQUIRE(b.lineCount() == 7);
  const char* want[] = {"a", "b", "c", "d", "e", "f", "g"};
  for (int i = 0; i < 7; ++i) CHECK_EQ(b.line(i), std::string(want[i]));
  CHECK_EQ(b.textForDisk(), src);  // every terminator kept
  CHECK_EQ(b.text(), "a\nb\nc\nd\ne\nf\ng");
  const auto c = b.endingCounts();
  CHECK_EQ(c.ls, 1);
  CHECK_EQ(c.ps, 1);
  CHECK_EQ(c.nel, 1);
  CHECK_EQ(c.lf, 1);
  CHECK_EQ(c.crlf, 1);
  CHECK_EQ(c.cr, 1);
  CHECK_EQ(c.total(), 6);
  CHECK(b.mixedLineEndings());
  CHECK(b.hasSeparators());
  CHECK(b.endingAfter(0) == LineEnding::LS);
  CHECK(b.endingAfter(1) == LineEnding::PS);
  CHECK(b.endingAfter(2) == LineEnding::NEL);
  CHECK_EQ(TextBuffer::countBreaks(src), 6);
  // A byte that merely looks like the start of one is not a break.
  TextBuffer t("\xC2\xA9 \xE2\x80\xA6 \xE2\x80\xA7 \xC2");
  CHECK_EQ(t.lineCount(), 1);
  CHECK(!t.hasSeparators());
}

TEST(sep_uniform_documents_report_their_ending_and_dominant_wins) {
  TextBuffer ls("a" + LS + "b" + LS + "c");
  CHECK(ls.lineEnding() == LineEnding::LS);
  CHECK(!ls.mixedLineEndings());
  CHECK(!ls.hasPerLineEndings());
  CHECK_EQ(ls.textForDisk(), "a" + LS + "b" + LS + "c");
  TextBuffer m("a" + LS + "b" + LS + "c\nd");
  CHECK(m.lineEnding() == LineEnding::LS);  // 2 LS beat 1 LF
  CHECK(m.mixedLineEndings());
  TextBuffer tie("a" + LS + "b\nc");
  CHECK(tie.lineEnding() == LineEnding::LF);  // ties: LF first
  TextBuffer tie2("a" + PS + "b" + LS + "c");
  CHECK(tie2.lineEnding() == LineEnding::LS);  // then NEL, LS, PS in that order
  TextBuffer nel("a" + NEL + "b" + PS + "c");
  CHECK(nel.lineEnding() == LineEnding::NEL);
}

TEST(sep_replace_inserts_literal_breaks_and_joining_keeps_the_later_terminator) {
  TextBuffer b("one\ntwo");
  b.replace({{0, 3}, {0, 3}}, "x" + LS + "y");  // a literal LS in inserted text is a break
  REQUIRE(b.lineCount() == 3);
  CHECK_EQ(b.line(0), "onex");
  CHECK_EQ(b.line(1), "y");
  CHECK(b.endingAfter(0) == LineEnding::LS);
  CHECK(b.endingAfter(1) == LineEnding::LF);
  CHECK_EQ(b.textForDisk(), "onex" + LS + "y\ntwo");
  // Delete the LS (join lines 0 and 1): the terminator after the joined line is the later one (LF).
  b.replace({{0, 4}, {1, 0}}, "");
  CHECK_EQ(b.textForDisk(), "onexy\ntwo");
  CHECK(!b.hasSeparators());
  // replaceMany with breaks of three kinds
  TextBuffer m("aaa\nbbb\nccc");
  m.replaceMany({{{{0, 1}, {0, 1}}, PS}, {{{1, 1}, {1, 1}}, NEL + "z"}, {{{2, 3}, {2, 3}}, "\r\n"}});
  CHECK_EQ(m.textForDisk(), "a" + PS + "aa\nb" + NEL + "zbb\nccc\r\n");
  CHECK_EQ(m.lineCount(), 6);
  CHECK(m.endingCounts().ps == 1 && m.endingCounts().nel == 1 && m.endingCounts().crlf == 1);
  // A multi-line replace over lines with separators keeps the byte exact text elsewhere.
  m.replace({{0, 0}, {2, 1}}, "");
  CHECK_EQ(m.textForDisk(), NEL + "zbb\nccc\r\n");  // the joined line keeps the terminator of the later line (NEL)
}

TEST(sep_with_line_ending_and_convert_line_endings_cover_ls_ps_nel) {
  const std::string src = "a\r\nb\nc" + LS + "d\re" + NEL + "f" + PS + "g";
  CHECK_EQ(TextBuffer::withLineEnding(src, LineEnding::LS), "a" + LS + "b" + LS + "c" + LS + "d" + LS + "e" + LS + "f" + LS + "g");
  CHECK_EQ(TextBuffer::withLineEnding(src, LineEnding::CRLF), "a\r\nb\r\nc\r\nd\r\ne\r\nf\r\ng");
  Document d = Document::fromText("/tmp/x.txt", src);
  d.convertLineEndings(LineEnding::PS);
  CHECK_EQ(d.buffer().textForDisk(), "a" + PS + "b" + PS + "c" + PS + "d" + PS + "e" + PS + "f" + PS + "g");
  CHECK(d.buffer().lineEnding() == LineEnding::PS);
  CHECK(!d.buffer().mixedLineEndings());
  CHECK(d.undo());  // one undo step restores every original terminator
  CHECK_EQ(d.buffer().textForDisk(), src);
  d.convertLineEndings(LineEnding::LF);
  CHECK_EQ(d.buffer().textForDisk(), "a\nb\nc\nd\ne\nf\ng");
  d.convertLineEndings(LineEnding::NEL);
  CHECK_EQ(d.buffer().textForDisk(), "a" + NEL + "b" + NEL + "c" + NEL + "d" + NEL + "e" + NEL + "f" + NEL + "g");
  // A one-line document only remembers the ending of its next break.
  Document one = Document::fromText("/tmp/y.txt", "abc");
  one.convertLineEndings(LineEnding::LS);
  CHECK(one.buffer().endingForNewLine(0, LineEnding::CRLF) == LineEnding::LS);
  CHECK(one.buffer().lineEnding() == LineEnding::LS);
}

TEST(sep_text_for_lsp_sends_the_separators_as_characters) {
  TextBuffer b("a" + LS + "b\r\nc\rd" + NEL + "e");
  CHECK_EQ(b.textForLsp(), "a" + LS + "b\r\nc\nd" + NEL + "e");
  CHECK_EQ(b.textForIndex(), "a" + LS + "b\nc\nd" + NEL + "e");
  TextBuffer plain("a\r\nb\rc");
  CHECK_EQ(plain.textForIndex(), plain.text());
  TextBuffer uniform("p" + PS + "q");
  CHECK_EQ(uniform.textForLsp(), "p" + PS + "q");
}

TEST(sep_windows_1252_zero_x85_is_an_ellipsis_not_a_line_break) {
  const std::string bytes = "a\x85" "b\nc";  // 0x85 is U+2026 in Windows-1252
  const DecodedText d = decodeFileBytes(bytes);
  CHECK(d.encoding == Encoding::Windows1252);
  CHECK_EQ(d.text, "a\xE2\x80\xA6" "b\nc");
  TextBuffer b(d.text);
  CHECK_EQ(b.lineCount(), 2);
  CHECK(!b.hasSeparators());
  // A document holding LS terminators cannot be saved as Windows-1252 (neither can NEL: U+0085 is not in the code page).
  Document doc = Document::fromText("/tmp/z.txt", "a" + LS + "b" + NEL + "c");
  const UnencodableReport rep = doc.findUnencodable(EncodingChoice::Windows1252);
  REQUIRE(rep.total == 2);
  CHECK_EQ(rep.chars[0].codePoint, 0x2028u);
  CHECK_EQ(rep.chars[0].line, 0);
  CHECK_EQ(rep.chars[1].codePoint, 0x85u);
  CHECK(doc.findUnencodable(EncodingChoice::Utf16LE).empty());
  CHECK(doc.findUnencodable(EncodingChoice::Utf8).empty());
}

TEST(sep_utf16_file_with_u2028_loads_as_a_break_and_saves_byte_exact) {
  TempDir dir;
  // UTF-16 LE with BOM: "a", U+2028, "b", U+0085, "c", LF, "d".
  const std::string bytes = std::string("\xFF\xFE", 2) + std::string("a\0\x28\x20" "b\0\x85\0" "c\0\n\0" "d\0", 14);
  const fs::path f = dir.write("u16.txt", bytes);
  Document d(f);
  REQUIRE(d.loadFromDisk());
  CHECK(d.encoding() == Encoding::Utf16LE);
  CHECK_EQ(d.buffer().lineCount(), 4);
  CHECK_EQ(d.buffer().line(1), "b");
  CHECK(d.buffer().endingAfter(0) == LineEnding::LS);
  CHECK(d.buffer().endingAfter(1) == LineEnding::NEL);
  d.replace({{3, 1}, {3, 1}}, "!");
  std::string err;
  REQUIRE(d.saveToDisk(&err));
  CHECK_EQ(slurp(f), bytes + std::string("!\0", 2));  // every terminator back as its UTF-16 unit, "!" appended to "d"
}

// ---------------------------------------------------------------------------------------------------------------------
// The editor
// ---------------------------------------------------------------------------------------------------------------------

TEST(sep_enter_replicates_the_ending_of_the_line_and_load_check_sees_lf_plus_ls_as_mixed) {
  TempDir dir;
  dir.write("ls.cpp", "int a;" + LS + "int b;" + LS + "int c;");
  dir.write("mix.cpp", "int a;\nint b;" + LS + "int c;\n");
  Editor::Options o;
  o.semantic.useClangd = false;
  Editor ed(dir.path, o);
  std::vector<std::string> asked;
  ed.onInconsistentLineEndings([&](Document& d) { asked.push_back(d.path().filename().string()); });
  std::string err;
  REQUIRE(ed.start(&err));
  Document* d = ed.open(dir.path / "ls.cpp");
  REQUIRE(d != nullptr);
  CHECK(asked.empty());
  ed.gotoLocation({d->path(), {1, 6}});
  ed.insertNewline();
  CHECK_EQ(d->buffer().textForDisk(), "int a;" + LS + "int b;" + LS + "" + LS + "int c;");
  Document* m = ed.open(dir.path / "mix.cpp");
  REQUIRE(m != nullptr);
  REQUIRE(asked.size() == 1u);
  CHECK_EQ(asked[0], std::string("mix.cpp"));
  ed.setLineEnding(*m, LineEnding::PS);
  CHECK_EQ(m->buffer().textForDisk(), "int a;" + PS + "int b;" + PS + "int c;" + PS + "");
  CHECK(ed.save(*m));
  CHECK_EQ(slurp(m->path()), "int a;" + PS + "int b;" + PS + "int c;" + PS + "");
}

TEST(sep_save_with_encoding_refuses_unicode_separators_in_windows_1252) {
  TempDir dir;
  dir.write("a.txt", "x\ny\n");
  Editor::Options o;
  o.semantic.useClangd = false;
  Editor ed(dir.path, o);
  std::string err;
  REQUIRE(ed.start(&err));
  Document* d = ed.open(dir.path / "a.txt");
  REQUIRE(d != nullptr);
  UnencodableReport rep;
  CHECK(!ed.saveWithEncoding(*d, EncodingChoice::Windows1252, LineEnding::LS, &err, &rep));
  CHECK(!rep.empty());
  CHECK_EQ(d->buffer().text(), "x\ny\n");  // nothing changed
  CHECK(ed.saveWithEncoding(*d, EncodingChoice::Utf16LE, LineEnding::LS, &err, &rep));
  CHECK(d->buffer().lineEnding() == LineEnding::LS);
}

// ---------------------------------------------------------------------------------------------------------------------
// Find, replace, diff, lexer, anchors
// ---------------------------------------------------------------------------------------------------------------------

TEST(sep_find_in_files_counts_lines_at_separators) {
  TempDir dir;
  const std::string text = "first" + LS + "second needle" + PS + "third\r\nfourth needle" + NEL + "needle";
  dir.write("a.txt", text);
  SearchInput in;
  in.root = dir.path;
  in.files = {dir.path / "a.txt"};
  SearchOptions so;
  so.query = "needle";
  std::vector<SearchMatch> r;
  TextSearch::run(in, so, &r);
  std::sort(r.begin(), r.end(), [](auto& a, auto& b) { return a.line < b.line; });
  REQUIRE(r.size() == 3u);
  CHECK_EQ(r[0].line, 1);
  CHECK_EQ(r[0].column, 7);
  CHECK_EQ(r[0].text, std::string("second needle"));
  CHECK_EQ(r[1].line, 3);
  CHECK_EQ(r[1].text, std::string("fourth needle"));
  CHECK_EQ(r[2].line, 4);
  CHECK_EQ(r[2].column, 0);
  CHECK_EQ(r[2].text, std::string("needle"));
  // The same lines the editor shows.
  TextBuffer b(text);
  CHECK_EQ(b.line(1), r[0].text);
  CHECK_EQ(b.line(4), r[2].text);
  if (TextSearch::regexSupported()) {  // the same lines and columns for a regex (its ^ $ . know only CR and LF)
    so.regex = true;
    so.query = "ne+dle";
    std::vector<SearchMatch> e;
    TextSearch::run(in, so, &e);
    std::sort(e.begin(), e.end(), [](auto& a, auto& b) { return a.line < b.line; });
    REQUIRE(e.size() == 3u);
    CHECK(e[0].line == 1 && e[0].column == 7 && e[0].text == "second needle");
    CHECK(e[1].line == 3 && e[1].text == "fourth needle");
    CHECK(e[2].line == 4 && e[2].column == 0 && e[2].text == "needle");
  }
}

TEST(sep_replace_in_files_uses_the_same_lines) {
  TempDir dir;
  const std::string text = "aa" + LS + "x needle" + PS + "needle y\nneedle";
  const fs::path f = dir.write("r.txt", text);
  SearchInput in;
  in.root = dir.path;
  in.files = {f};
  SearchOptions so;
  so.query = "needle";
  std::vector<SearchMatch> ms;
  TextSearch::run(in, so, &ms);
  REQUIRE(ms.size() == 3u);
  ReplaceCounts counts;
  std::string err;
  CHECK(replaceMatchesInFile(f, ms, "N", &counts, &err));
  CHECK_EQ(counts.replaced, 3u);
  CHECK_EQ(counts.stale, 0u);
  CHECK_EQ(slurp(f), "aa" + LS + "x N" + PS + "N y\nN");
  // The in-memory variant (open documents) agrees.
  ReplaceCounts c2;
  CHECK_EQ(replaceMatchesInText(text, ms, "N", &c2), "aa" + LS + "x N" + PS + "N y\nN");
  CHECK_EQ(c2.replaced, 3u);
}

TEST(sep_vcs_split_lines_and_corpus_anchors_use_the_editor_lines) {
  const std::vector<std::string> l = splitLines("a" + LS + "b" + PS + "c" + NEL + "d\ne");
  CHECK_EQ(l.size(), 5u);
  CHECK_EQ(l[3], std::string("d"));
  corpus::AnchorMap m;
  std::vector<std::string> errs;
  corpus::scanAnchorsInText("x.cpp", "// c" + LS + "int /*@a*/alpha;" + PS + "  int /*@b*/beta;\nint /*@c*/gamma;", m, errs);
  REQUIRE(m.size() == 3u);
  CHECK(errs.empty());
  CHECK_EQ(m["a"].line, 1);
  CHECK_EQ(m["a"].col, 10);
  CHECK_EQ(m["b"].line, 2);
  CHECK_EQ(m["b"].col, 12);
  CHECK_EQ(m["c"].line, 3);
}

TEST(sep_lexer_and_fast_index_place_symbols_on_the_editor_lines) {
  // The comment continues past the LS (clang), so `hidden` is not declared; the symbols after it are on the lines the
  // editor shows.
  const std::string src = "// note" + LS + "int hidden;\nint alpha;" + LS + "  int beta;\n/* c" + PS + " d */ int gamma;\n";
  CppLexer lx(src);
  std::vector<Token> toks = lx.all();
  auto at = [&](const char* name, int line, int col) {
    for (const Token& t : toks)
      if (t.text == name) {
        CHECK_EQ(t.line, line);
        CHECK_EQ(t.col, col);
        return;
      }
    CHECK(false);
  };
  at("alpha", 2, 4);
  at("beta", 3, 6);
  at("gamma", 5, 10);
  for (const Token& t : toks) CHECK(t.text != "hidden");
  const std::vector<Symbol> syms = extractSymbols(src);
  bool sawHidden = false;
  int g = -1;
  for (const Symbol& s : syms) {
    if (s.name == "hidden") sawHidden = true;
    if (s.name == "gamma") g = s.line * 100 + s.col;
  }
  CHECK(!sawHidden);
  CHECK_EQ(g, 510);
  // Same answer through the buffer path (the text the engine indexes for an open document).
  TextBuffer b(src);
  const std::vector<Symbol> viaBuffer = extractSymbols(b.textForIndex());
  REQUIRE(viaBuffer.size() == syms.size());
  for (size_t i = 0; i < syms.size(); ++i) CHECK(syms[i].line == viaBuffer[i].line && syms[i].col == viaBuffer[i].col);
  // A #define after a separator and a separator inside its block comment.
  const std::string def = "int a;\n#define /* x" + LS + " y */ NAME 1\n";
  bool found = false;
  for (const Symbol& s : extractSymbols(def))
    if (s.name == "NAME") {
      found = true;
      CHECK_EQ(s.line, 2);
      CHECK_EQ(s.col, 6);
    }
  CHECK(found);
}

// ---------------------------------------------------------------------------------------------------------------------
// LspLineMap
// ---------------------------------------------------------------------------------------------------------------------

TEST(sep_line_map_is_absent_without_separators_and_maps_runs) {
  CHECK(LspLineMap::build(TextBuffer("a\nb\r\nc")) == nullptr);
  //   editor lines            server lines
  //   0 "x"                   0 "x"
  //   1 "ab"  LS              1 "ab" LS "\xC3\xA9" PS "cd"   (e-acute: 2 bytes, 1 UTF-16 unit)
  //   2 "é"   PS
  //   3 "cd"
  //   4 "y"                   2 "y"
  //   5 "z"   NEL             3 "z" NEL "w"
  //   6 "w"
  const std::string src = "x\nab" + LS + "\xC3\xA9" + PS + "cd\ny\nz" + NEL + "w";
  TextBuffer b(src);
  auto m = LspLineMap::build(b);
  REQUIRE(m != nullptr);
  CHECK_EQ(m->runCount(), 2u);
  CHECK_EQ(m->serverLineCount(b.lineCount()), 4);
  const auto& L = b.lines();
  // editor -> server, UTF-16 columns
  auto to16 = [&](int l, int c) { return m->toServer({l, c}, L, true); };
  CHECK(to16(0, 1) == (Position{0, 1}));
  CHECK(to16(1, 2) == (Position{1, 2}));
  CHECK(to16(2, 0) == (Position{1, 3}));  // after "ab" + LS
  CHECK(to16(2, 2) == (Position{1, 4}));  // after the 2-byte e-acute: one unit
  CHECK(to16(3, 1) == (Position{1, 6}));  // "ab" LS é PS "c"
  CHECK(to16(4, 0) == (Position{2, 0}));
  CHECK(to16(6, 1) == (Position{3, 3}));
  // UTF-8 columns count bytes (LS is 3, NEL 2, é 2)
  auto to8 = [&](int l, int c) { return m->toServer({l, c}, L, false); };
  CHECK(to8(2, 0) == (Position{1, 5}));
  CHECK(to8(3, 1) == (Position{1, 5 + 2 + 3 + 1}));
  CHECK(to8(6, 0) == (Position{3, 1 + 2}));
  // server -> editor, and the round trip for every column of every line
  CHECK(m->fromServer({1, 3}, L, true) == (Position{2, 0}));
  CHECK(m->fromServer({1, 4}, L, true) == (Position{2, 2}));
  CHECK(m->fromServer({1, 2}, L, true) == (Position{1, 2}));  // before the LS: end of "ab"
  CHECK(m->fromServer({1, 6}, L, true) == (Position{3, 1}));
  CHECK(m->fromServer({2, 0}, L, true) == (Position{4, 0}));
  CHECK(m->fromServer({3, 3}, L, true) == (Position{6, 1}));
  CHECK(m->fromServer({1, 99}, L, true) == (Position{3, 2}));  // past the end of the server line: end of its last line
  for (bool u16 : {true, false})
    for (int l = 0; l < b.lineCount(); ++l)
      for (int c = 0; c <= static_cast<int>(L[static_cast<size_t>(l)].size()); ++c) {
        if (c == 1 && L[static_cast<size_t>(l)] == "\xC3\xA9") continue;  // inside a UTF-8 sequence
        const Position e{l, c};
        CHECK(m->fromServer(m->toServer(e, L, u16), L, u16) == e);
      }
}

TEST(sep_line_map_agrees_with_the_server_text_for_arbitrary_documents) {
  // Reference: split textForLsp at '\n' like clangd and each server line at the separators.
  const std::string srcs[] = {
      "a" + LS + "b" + LS + "c" + LS,
      LS + LS + "x",
      "\xE2\x82\xAC" + NEL + "\xF0\x9F\x98\x80" + PS + "q\r\nr" + LS + "s\r",
      "plain\nlines\n",
      PS,
  };
  for (const std::string& src : srcs) {
    TextBuffer b(src);
    auto m = LspLineMap::build(b);
    const std::string server = b.textForLsp();
    int editorLine = 0, serverLine = 0;
    size_t pos = 0;
    while (pos <= server.size()) {
      size_t e = server.find('\n', pos);
      std::string_view sl = std::string_view(server).substr(pos, e == std::string::npos ? std::string::npos : e - pos);
      if (!sl.empty() && sl.back() == '\r') sl.remove_suffix(1);
      // segments
      size_t at = 0;
      int u16 = 0;
      while (true) {
        size_t k = at;
        while (k < sl.size() && !linebreak::separatorLen(sl, k)) ++k;
        const std::string_view seg = sl.substr(at, k - at);
        REQUIRE(editorLine < b.lineCount());
        CHECK_EQ(std::string(seg), b.line(editorLine));
        if (m) {
          const Position p16 = m->toServer({editorLine, 0}, b.lines(), true);
          CHECK_EQ(p16.line, serverLine);
          CHECK_EQ(p16.column, u16);
        }
        u16 += TextBuffer::utf8ToUtf16Column(seg, static_cast<int>(seg.size())) + 1;
        ++editorLine;
        if (k >= sl.size()) break;
        at = k + linebreak::separatorLen(sl, k);
      }
      ++serverLine;
      if (e == std::string::npos) break;
      pos = e + 1;
    }
    CHECK_EQ(editorLine, b.lineCount());
    if (m) CHECK_EQ(m->serverLineCount(b.lineCount()), serverLine);
  }
}

// ---------------------------------------------------------------------------------------------------------------------
// clangd
// ---------------------------------------------------------------------------------------------------------------------

namespace {

// A project whose a.cpp has separators: an LS inside a block comment puts `beta` on an editor line that shares its
// clangd line with the comment's first line; an LS before the closing of a line comment shows that clang keeps going.
const std::string kSource =
    "/* caf\xC3\xA9" + LS + " more */ int beta = 2;\n"  // editor lines 0, 1; clangd line 0
    "// a note" + PS + "int hidden = 3;\n"               // editor lines 2, 3; clangd line 1 (all one comment)
    "int alpha = beta;\n"                                // editor line 4; clangd line 2
    "int use() { return alpha + beta; }\n";              // editor line 5; clangd line 3

struct Project {
  TempDir dir;
  fs::path file;
  Project() {
    file = dir.write("a.cpp", kSource);
    std::ostringstream cc;
    cc << "[{\"directory\": \"" << dir.path.generic_string() << "\", \"file\": \"" << file.generic_string()
       << "\", \"arguments\": [\"clang++\", \"-std=c++17\", \"-c\", \"" << file.generic_string() << "\"]}]";
    dir.write("compile_commands.json", cc.str());
  }
};

struct Session {
  Editor ed;
  bool ok = false;
  Document* doc = nullptr;
  Session(const Project& p, bool utf16)
      : ed(p.dir.path, [&] {
          Editor::Options o;
          o.semantic.useClangd = true;
          o.semantic.utf16Positions = utf16;
          o.semantic.semanticTokensDebounceMs = 50;
          o.log = [](const std::string& m) {
            if (std::getenv("TENFOLD_TEST_VERBOSE")) std::cerr << "    [engine] " << m << "\n";
          };
          return o;
        }()) {
    std::string err;
    ok = ed.start(&err);
    if (!ok) std::cerr << "    engine start: " << err << "\n";
    if (!ok) return;
    ed.gotoLocation({p.file, {0, 0}});
    doc = ed.document(p.file);
    if (doc) ed.semantic().waitForFile(p.file, 60000);
  }
  template <class T, class F>
  bool call(F issue, T* out, std::string* err = nullptr, int ms = 60000) {
    bool done = false;
    std::string e;
    issue(Editor::Done<T>([&](const T& v, const std::string& er) {
      *out = v;
      e = er;
      done = true;
    }));
    const bool finished = ed.waitFor([&] { return done; }, ms);
    if (err) *err = finished ? e : "timeout";
    return finished && e.empty();
  }
};

std::vector<SymbolLocation> definitionAt(Session& s, Position at) {
  std::vector<SymbolLocation> out;
  bool done = false;
  s.ed.gotoDefinitionAt(*s.doc, at, [&](Confidence c, const std::vector<SymbolLocation>& l, const std::string&) {
    if (c == Confidence::Authoritative) {
      out = l;
      done = true;
    }
  });
  s.ed.waitFor([&] { return done; }, 60000);
  return out;
}

void runClangdChecks(bool utf16) {
  Project p;
  Session s(p, utf16);
  REQUIRE(s.ok);
  REQUIRE(s.doc != nullptr);
  const TextBuffer& b = s.doc->buffer();
  REQUIRE(b.lineCount() == 7);
  CHECK_EQ(b.line(1), std::string(" more */ int beta = 2;"));
  CHECK_EQ(s.ed.semantic().clangd() != nullptr, true);
  CHECK_EQ(s.ed.semantic().clangd()->offsetEncoding() == "utf-8", !utf16);
  s.ed.semantic().waitForClangdIndex(60000);

  // Go to definition of `beta` in `alpha + beta` (editor line 5): the definition is on editor line 1, column 13, which
  // clangd calls line 0, column 7 + 1 + 13 (+1 for the e-acute) in the encoding it speaks.
  const int useCol = static_cast<int>(b.line(5).rfind("beta"));
  auto defs = definitionAt(s, {5, useCol});
  REQUIRE(defs.size() == 1u);
  CHECK(defs[0].loc.pos == (Position{1, 13}));

  // References: the definition, the use in `alpha = beta`, and the use in use().
  std::vector<SymbolLocation> refs;
  bool done = false;
  s.ed.gotoLocation({p.file, {5, useCol}});
  s.ed.findReferences([&](Confidence c, const std::vector<SymbolLocation>& l, const std::string&) {
    if (c == Confidence::Authoritative) {
      refs = l;
      done = true;
    }
  });
  s.ed.waitFor([&] { return done; }, 60000);
  std::set<std::pair<int, int>> got;
  for (auto& r : refs) got.insert({r.loc.pos.line, r.loc.pos.column});
  CHECK(got.count({1, 13}) == 1);
  CHECK(got.count({4, 12}) == 1);
  CHECK(got.count({5, useCol}) == 1);
  CHECK_EQ(got.size(), 3u);

  // `hidden` is inside the comment for clang: not a symbol, no definition there.
  CHECK(definitionAt(s, {3, 4}).empty() || definitionAt(s, {3, 4})[0].loc.pos.line != 3);

  // Semantic tokens: `beta` at its editor position carries a variable token.
  std::vector<TokenSpan> toks;
  std::string err;
  REQUIRE(s.call<std::vector<TokenSpan>>([&](auto d) { s.ed.semanticTokens(d); }, &toks, &err));
  auto legend = s.ed.semantic().tokenLegend();
  bool sawBeta = false, sawAlpha = false;
  for (auto& t : toks) {
    const std::string ty = t.type < legend.types.size() ? legend.types[t.type] : "";
    if (t.line == 1 && t.col == 13 && t.len == 4 && ty == "variable") sawBeta = true;
    if (t.line == 4 && t.col == 4 && t.len == 5 && ty == "variable") sawAlpha = true;
    CHECK(t.line >= 0 && t.line < 6);
  }
  CHECK(sawBeta);
  CHECK(sawAlpha);

  // Rename beta -> gamma: three edits in editor coordinates.
  Editor::RenameResult rr;
  s.ed.gotoLocation({p.file, {5, useCol}});
  REQUIRE(s.call<Editor::RenameResult>([&](auto d) { s.ed.renameSymbol({5, useCol}, "gamma", d, true); }, &rr, &err));
  CHECK_EQ(rr.edits, 3u);
  CHECK_EQ(b.line(1), std::string(" more */ int gamma = 2;"));
  CHECK_EQ(b.line(4), std::string("int alpha = gamma;"));
  CHECK_EQ(b.line(5), std::string("int use() { return alpha + gamma; }"));
  CHECK_EQ(b.line(0), std::string("/* caf\xC3\xA9"));
  CHECK_EQ(b.textForDisk(), "/* caf\xC3\xA9" + LS + " more */ int gamma = 2;\n// a note" + PS +
                                "int hidden = 3;\nint alpha = gamma;\nint use() { return alpha + gamma; }\n");
  REQUIRE(s.doc->undo());
  CHECK_EQ(b.line(1), std::string(" more */ int beta = 2;"));
}

}  // namespace

TEST(sep_clangd_positions_after_a_separator_utf8_offsets) {
  SKIP_WITHOUT_CLANGD();
  runClangdChecks(false);
}

TEST(sep_clangd_positions_after_a_separator_utf16_offsets) {
  SKIP_WITHOUT_CLANGD();
  runClangdChecks(true);
}

TEST(sep_clangd_diagnostics_and_incremental_edits_around_separators) {
  SKIP_WITHOUT_CLANGD();
  for (bool utf16 : {false, true}) {
    Project p;
    Session s(p, utf16);
    REQUIRE(s.ok);
    REQUIRE(s.doc != nullptr);
    // An error on the line after the LS in the comment: `int  bad = "x";` replaces `int beta = 2;`.
    const TextBuffer& b = s.doc->buffer();
    s.ed.gotoLocation({p.file, {1, 13}});
    s.ed.setSelections({Selection{{1, 9}, {1, 22}}});
    s.ed.insertText("int bad = \"x\";");
    CHECK_EQ(b.line(1), std::string(" more */ int bad = \"x\";"));
    REQUIRE(s.ed.waitFor(
        [&] {
          for (auto& d : s.ed.diagnostics(p.file))
            if (d.severity == 1 && d.range.start.line == 1) return true;
          return false;
        },
        60000));
    for (auto& d : s.ed.diagnostics(p.file))
      if (d.severity == 1 && d.range.start.line == 1) {
        CHECK_EQ(d.range.start.column, 13);  // clangd puts this one on the name `bad`  // the string literal "x"
        CHECK_EQ(d.range.end.line, 1);
        CHECK_EQ(d.range.end.column, 16);
      }
    // Edit before the separator (changes the e-acute line), at the separator (delete the LS: joins the lines), and
    // after it; the diagnostics must stay on the right editor lines each time.
    s.ed.setSelections({Selection{{0, 3}, {0, 3}}});
    s.ed.insertText("xx");
    CHECK_EQ(b.line(0), std::string("/* xxcaf\xC3\xA9"));
    s.ed.setSelections({Selection{{0, static_cast<int>(b.line(0).size())}, {1, 0}}});  // select the LS
    s.ed.insertText(" ");
    CHECK_EQ(b.lineCount(), 6);
    CHECK_EQ(b.line(0), std::string("/* xxcaf\xC3\xA9  more */ int bad = \"x\";"));
    REQUIRE(s.ed.waitFor(
        [&] {
          for (auto& d : s.ed.diagnostics(p.file))
            if (d.severity == 1 && d.range.start.line == 0) return true;
          return false;
        },
        60000));
    // Put a break back with a literal LS: the text is split again and clangd still agrees about the lines after it.
    s.ed.setSelections({Selection{{0, 14}, {0, 14}}});
    s.ed.insertText(LS, true);
    CHECK_EQ(b.lineCount(), 7);
    REQUIRE(s.ed.waitFor(
        [&] {
          for (auto& d : s.ed.diagnostics(p.file))
            if (d.severity == 1 && d.range.start.line == 1) return true;
          return false;
        },
        60000));
  }
}
