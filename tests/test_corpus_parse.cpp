// W9a corpus format: the anchor scanner, the expectation parser, the comparison rules and the statistics. No engine,
// no clangd.
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#if !defined(_WIN32)
#include <unistd.h>
#endif

#include "corpus/Corpus.h"
#include "test_main.h"

using namespace tf::corpus;

namespace {

AnchorMap scan(const std::string& file, const std::string& text, std::vector<std::string>* errs = nullptr) {
  AnchorMap m;
  std::vector<std::string> e;
  scanAnchorsInText(file, text, m, e);
  if (errs) *errs = e;
  return m;
}

// Anchors a, b, c, d on separate lines of "x.cpp" (token columns: 10, 10, 11, 10).
AnchorMap sample() {
  return scan("x.cpp",
              "int /*@a*/alpha() {}\n"
              "int /*@b*/beta() {}\n"
              "virtual /*@c*/~Gamma() {}\n"
              "int /*@d*/delta;\n");
}

bool parse(const std::string& text, const AnchorMap& anchors, std::vector<Item>& items,
           std::vector<std::string>& errs) {
  errs.clear();
  return parseExpectations(text, "expect.txt", anchors, items, errs);
}

bool hasError(const std::vector<std::string>& errs, const std::string& needle) {
  return std::any_of(errs.begin(), errs.end(),
                     [&](const std::string& e) { return e.find(needle) != std::string::npos; });
}

Loc at(const std::string& f, int line, int col) {
  Loc l;
  l.file = f;
  l.line = line;
  l.col = col;
  return l;
}

}  // namespace

TEST(corpus_anchor_scan) {
  std::vector<std::string> errs;
  AnchorMap m = sample();
  REQUIRE(m.size() == 4u);
  CHECK_EQ(m["a"].line, 0);
  CHECK_EQ(m["a"].col, 10);  // after "int /*@a*/" (10 columns): the token follows the comment directly
  CHECK_EQ(m["a"].len, 5);
  CHECK_EQ(m["c"].len, 6);  // ~Gamma: a destructor token includes the tilde
  CHECK_EQ(m["c"].skip, 1);
  CHECK_EQ(m["a"].skip, 0);
  CHECK_EQ(m["d"].line, 3);

  // Blanks between the comment and the token are skipped; several anchors can name one token.
  m = scan("y.cpp", "x /*@p*/  foo; /*@q*//*@r*/ bar(); /*@s*/(\n", &errs);
  CHECK(errs.empty());
  CHECK_EQ(m["p"].col, 10);
  CHECK_EQ(m["q"].col, m["r"].col);
  CHECK_EQ(m["s"].len, 1);  // punctuation: one character

  // Errors: duplicate names (with the first position), an anchor at the end of its line.
  m = scan("z.cpp", "int /*@dup*/a;\nint /*@dup*/b;\nint /*@lonely*/\n", &errs);
  REQUIRE(errs.size() == 2u);
  CHECK(hasError(errs, "z.cpp:2: duplicate anchor 'dup' (first at z.cpp:1)"));
  CHECK(hasError(errs, "z.cpp:3: anchor 'lonely' has no token"));

  // Things that merely look similar are not anchors.
  m = scan("w.cpp", "/*@ */ /*@bad name*/ x /* @plain */ y /*@{ doxygen */ z\n", &errs);
  CHECK(m.empty() && errs.empty());
  CHECK(anchorNamesIn("a /*@one*/b /*@two.x*/c").size() == 2u);
}

TEST(corpus_parse_every_expectation_kind) {
  const AnchorMap a = sample();
  std::vector<Item> it;
  std::vector<std::string> errs;
  const std::string text =
      "# comment\n"
      "\n"
      "def a -> b\n"
      "def a -> b | c   # trailing comment\n"
      "def a -> none\n"
      "decl a -> b\n"
      "typedef a -> b\n"
      "impl a -> b c\n"
      "impl a = b c\n"
      "refs a = b c d\n"
      "refs a >= b\n"
      "refs a = none\n"
      "hover a ~ \"int\" \"alpha \\\"x\\\"\"\n"
      "sym \"al pha\" -> a\n"
      "sym \"q\" \xE2\x88\x8B a b\n"
      "sym \"q\" has c\n"
      "fast def a -> b\n"
      "xfail def a -> b  # known wrong\n"
      "fast xfail sym \"q\" -> a # both prefixes\n"
      "config Debug|x64\n"
      "open src/x.cpp\n"
      "edit src/x.cpp:3:5 \"int /*@ed*/ed;\\n\"\n"
      "def ed -> a\n"
      "rename a newName\n"
      "  b c\n"
      "  # a comment inside the block\n"
      "  d\n"
      "def a -> b\n"
      "close src/x.cpp\n";
  REQUIRE(parse(text, a, it, errs));
  REQUIRE(it.size() == 24u);
  CHECK(it[0].kind == Kind::Def && it[0].rel == Rel::Arrow && it[0].targets == std::vector<std::string>{"b"});
  CHECK_EQ(it[0].line, 3);
  CHECK((it[1].targets == std::vector<std::string>{"b", "c"}));
  CHECK(it[2].none);
  CHECK(it[3].kind == Kind::Decl && it[4].kind == Kind::Typedef);
  CHECK(it[5].kind == Kind::Impl && it[5].targets.size() == 2u && it[5].rel == Rel::Arrow);
  CHECK(it[6].rel == Rel::Eq);
  CHECK(it[7].kind == Kind::Refs && it[7].rel == Rel::Eq && it[7].targets.size() == 3u);
  CHECK(it[8].rel == Rel::Ge);
  CHECK(it[9].none && it[9].rel == Rel::Eq);
  CHECK(it[10].kind == Kind::Hover && it[10].texts.size() == 2u && it[10].texts[1] == "alpha \"x\"");
  CHECK(it[11].kind == Kind::Sym && it[11].query == "al pha" && it[11].rel == Rel::Arrow);
  CHECK(it[12].rel == Rel::Has && it[12].targets.size() == 2u);
  CHECK(it[13].rel == Rel::Has);
  CHECK(it[14].fast && !it[14].xfail);
  CHECK(it[15].xfail && it[15].reason == "known wrong");
  CHECK(it[16].fast && it[16].xfail);
  CHECK(it[17].type == Item::Type::Config && it[17].arg == "Debug|x64");
  CHECK(it[18].type == Item::Type::Open && it[18].arg == "src/x.cpp");
  CHECK(it[19].type == Item::Type::Edit && it[19].arg == "src/x.cpp" && it[19].editLine == 3 && it[19].editCol == 5);
  CHECK_EQ(it[19].editText, "int /*@ed*/ed;\n");
  CHECK(it[20].anchor == "ed");  // an anchor introduced by an earlier edit
  CHECK(it[21].kind == Kind::Rename && it[21].query == "newName");
  CHECK((it[21].renamed == std::vector<std::string>{"b", "c", "d"}));
  CHECK(it[22].kind == Kind::Def);  // the block ended at the next unindented line
  CHECK(it[23].type == Item::Type::Close && it[23].arg == "src/x.cpp");
}

TEST(corpus_parse_errors_name_file_and_line) {
  const AnchorMap a = sample();
  std::vector<Item> it;
  std::vector<std::string> errs;
  struct Bad {
    const char* text;
    const char* needle;
  };
  const Bad bad[] = {
      {"def nope -> b\n", "expect.txt:1: unknown anchor 'nope'"},
      {"\n\ndef a -> nope\n", "expect.txt:3: unknown anchor 'nope'"},
      {"frobnicate a -> b\n", "expect.txt:1: unknown expectation 'frobnicate'"},
      {"def a b\n", "unknown relation 'b'"},
      {"def a = b\n", "def does not take '='"},
      {"refs a -> b\n", "refs does not take '->'"},
      {"hover a -> b\n", "hover does not take '->'"},
      {"def a ->\n", "missing anchors after '->'"},
      {"def a -> b c\n", "def takes one anchor or alternatives"},
      {"def a -> b |\n", "missing anchor after '|'"},
      {"def a -> | b\n", "misplaced '|'"},
      {"impl a -> b | c\n", "impl lists anchors separated by blanks"},
      {"hover a ~ text\n", "hover text must be in double quotes"},
      {"hover a ~ \"open\n", "unterminated string"},
      {"hover a ~ \"bad \\q\"\n", "bad escape"},
      {"sym foo -> a\n", "sym query must be in double quotes"},
      {"refs a >= none\n", "'none' makes no sense after '>='"},
      {"xfail def a -> b\n", "xfail needs a reason"},
      {"fast refs a = b\n", "refs has no fast tier"},
      {"fast rename a x\n  b\n", "rename has no fast tier"},
      {"fast fast def a -> b\n", "'fast' given twice"},
      {"fast config X\n", "'config' cannot follow fast/xfail"},
      {"rename a 1bad\n  b\n", "not an identifier"},
      {"rename a ok\ndef a -> b\n", "expect.txt:1: rename needs indented lines"},
      {"rename a ok\n  nope\n", "expect.txt:2: unknown anchor 'nope'"},
      {"  b c\n", "unexpected indented line"},
      {"config\n", "config needs a configuration name"},
      {"open\n", "usage: open <file>"},
      {"edit f.cpp \"x\"\n", "usage: edit <file>:<line>:<col>"},
      {"edit f.cpp:0:1 \"x\"\n", "1-based"},
      {"edit f.cpp:1:1 \"int /*@a*/x;\"\n", "duplicate anchor 'a'"},
  };
  for (auto& b : bad) {
    errs.clear();
    const bool ok = parse(b.text, a, it, errs);
    if (ok || !hasError(errs, b.needle)) {
      std::string got;
      for (auto& e : errs) got += "[" + e + "]";
      tftest::fail(__FILE__, __LINE__,
                   std::string("input ") + b.text + " expected error containing \"" + b.needle + "\" got " +
                       (ok ? "success" : got));
    }
  }
  // All problems are reported, not just the first.
  CHECK(!parse("def x -> a\ndef y -> a\n", a, it, errs));
  CHECK_EQ(errs.size(), 2u);
}

TEST(corpus_location_matching_and_description) {
  const AnchorMap a = sample();
  const Anchor& alpha = a.at("a");  // x.cpp line 0, cols 10..14
  CHECK(locMatchesAnchor(at("x.cpp", 0, alpha.col), alpha));
  CHECK(locMatchesAnchor(at("x.cpp", 0, alpha.col + 4), alpha));   // inside the token
  CHECK(!locMatchesAnchor(at("x.cpp", 0, alpha.col + 5), alpha));  // past it
  CHECK(!locMatchesAnchor(at("x.cpp", 0, alpha.col - 1), alpha));
  CHECK(!locMatchesAnchor(at("x.cpp", 1, alpha.col), alpha));
  CHECK(!locMatchesAnchor(at("y.cpp", 0, alpha.col), alpha));
  // A range that contains the anchor start matches too.
  Loc r = at("x.cpp", 0, 0);
  r.endLine = 1;
  r.endCol = 3;
  CHECK(locMatchesAnchor(r, alpha));
  CHECK(!locMatchesAnchor(r, a.at("c")));
  r.endLine = 0;
  r.endCol = alpha.col;  // ends where the token starts: end is exclusive
  CHECK(!locMatchesAnchor(r, alpha));

  CHECK_EQ(describeLoc(at("x.cpp", 0, alpha.col), a), "x.cpp:1:11 [a]");
  CHECK_EQ(describeLoc(at("x.cpp", 1, 0), a), "x.cpp:2:1 (near b)");
  CHECK_EQ(describeLoc(at("other.h", 4, 2), a), "other.h:5:3");
  CHECK_EQ(describeLocs({}, a), "none");
}

TEST(corpus_check_locations) {
  const AnchorMap a = sample();
  std::vector<Item> it;
  std::vector<std::string> errs;
  REQUIRE(
      parse("def a -> b | c\n"
            "def a -> none\n"
            "refs a = b c\n"
            "refs a >= b\n"
            "impl a -> b c\n"
            "sym \"q\" -> b | c\n"
            "sym \"q\" has b d\n"
            "refs a = none\n",
            a, it, errs));
  const Loc B = at("x.cpp", 1, a.at("b").col), C = at("x.cpp", 2, a.at("c").col), D = at("x.cpp", 3, a.at("d").col),
            A = at("x.cpp", 0, a.at("a").col);
  // def: non-empty, every location on an alternative
  CHECK(checkLocations(it[0], {B}, a).ok);
  CHECK(checkLocations(it[0], {C}, a).ok);
  CHECK(checkLocations(it[0], {B, C}, a).ok);
  CHECK(!checkLocations(it[0], {}, a).ok);
  CHECK(!checkLocations(it[0], {B, D}, a).ok);
  CHECK(!checkLocations(it[0], {A}, a).ok);
  CHECK_EQ(checkLocations(it[0], {D}, a).expected, "b | c");
  CHECK_EQ(checkLocations(it[0], {D}, a).actual, "x.cpp:4:11 [d]");
  // none
  CHECK(checkLocations(it[1], {}, a).ok);
  CHECK(!checkLocations(it[1], {B}, a).ok);
  // = exact set, duplicates are fine, an extra or missing location fails
  CHECK(checkLocations(it[2], {B, C}, a).ok);
  CHECK(checkLocations(it[2], {C, B, B}, a).ok);
  CHECK(!checkLocations(it[2], {B}, a).ok);
  CHECK(!checkLocations(it[2], {B, C, D}, a).ok);
  // >= subset
  CHECK(checkLocations(it[3], {B, C, D}, a).ok);
  CHECK(!checkLocations(it[3], {C}, a).ok);
  // impl -> is an exact set
  CHECK(checkLocations(it[4], {C, B}, a).ok);
  CHECK(!checkLocations(it[4], {B}, a).ok);
  // sym ->: the first result; sym has: membership
  CHECK(checkLocations(it[5], {C, D}, a).ok);
  CHECK(!checkLocations(it[5], {D, C}, a).ok);
  CHECK(!checkLocations(it[5], {}, a).ok);
  CHECK(checkLocations(it[6], {D, A, B}, a).ok);
  CHECK(!checkLocations(it[6], {D, A}, a).ok);
  // refs = none
  CHECK(checkLocations(it[7], {}, a).ok);
  CHECK(!checkLocations(it[7], {A}, a).ok);
}

TEST(corpus_check_hover_and_rename) {
  const AnchorMap a = sample();
  std::vector<Item> it;
  std::vector<std::string> errs;
  REQUIRE(parse("hover a ~ \"int\" \"alpha\"\nrename a newName\n  b\n", a, it, errs));
  CHECK(checkHover(it[0], "function `alpha` returns int").ok);
  CHECK(!checkHover(it[0], "function `alpha`").ok);
  CHECK(!checkHover(it[0], "").ok);
  CHECK_EQ(checkHover(it[0], "").actual, "(no hover text)");

  const std::map<std::string, std::string> before = {{"a", "alpha"}, {"b", "beta"}, {"c", "gamma"}};
  CHECK(checkRename(it[1], before, {{"a", "alpha"}, {"b", "newName"}, {"c", "gamma"}}).ok);
  Verdict v = checkRename(it[1], before, {{"a", "alpha"}, {"b", "beta"}, {"c", "gamma"}});
  CHECK(!v.ok);
  CHECK_EQ(v.actual, "not renamed: b(beta)");
  v = checkRename(it[1], before, {{"a", "alpha"}, {"b", "newName"}, {"c", "newName"}});
  CHECK(!v.ok);
  CHECK_EQ(v.actual, "also renamed: c");
  CHECK(!checkRename(it[1], before, {{"a", "alpha"}, {"c", "gamma"}}).ok);  // anchor b lost
}

TEST(corpus_status_and_statistics) {
  CHECK(classify(false, true) == Status::Pass);
  CHECK(classify(false, false) == Status::Fail);
  CHECK_EQ(std::string(toString(Status::Partial)), "PARTIAL");
  CHECK(classify(true, false) == Status::XFail);
  CHECK(classify(true, true) == Status::XPass);

  // Fast vs final answers compare by file and line, order and duplicates ignored.
  CHECK(sameAnswer({at("f", 1, 2), at("g", 3, 4)}, {at("g", 3, 9), at("f", 1, 0), at("f", 1, 7)}));
  CHECK(!sameAnswer({at("f", 1, 2)}, {at("f", 2, 2)}));
  CHECK(!sameAnswer({at("f", 1, 2)}, {}));
  CHECK(sameAnswer({}, {}));

  LatencyStats s = computeStats({});
  CHECK_EQ(s.n, 0u);
  s = computeStats({5, 1, 3});
  CHECK_EQ(s.median, 3.0);
  CHECK_EQ(s.max, 5.0);
  CHECK_EQ(s.p95, 5.0);
  std::vector<double> hundred;
  for (int i = 1; i <= 100; ++i) hundred.push_back(i);
  s = computeStats(hundred);
  CHECK_EQ(s.median, 50.5);
  CHECK_EQ(s.p95, 95.0);  // nearest rank
  CHECK_EQ(s.max, 100.0);
  s = computeStats({2, 4});
  CHECK_EQ(s.median, 3.0);
}

// W9-FIX-ENC: the scanner splits lines like clang (CRLF, LF, lone CR) and does not follow symlinks (W9e H1/H3).
TEST(corpus_anchor_scanner_ends_lines_at_lone_cr_and_crlf) {
  auto m = scan("cr.c", "int /*@a*/aa;\rint /*@b*/bb;\r\nint /*@c*/cc;\nint /*@d*/dd;\r");
  REQUIRE(m.size() == 4);
  CHECK_EQ(m["a"].line, 0);
  CHECK_EQ(m["b"].line, 1);
  CHECK_EQ(m["c"].line, 2);
  CHECK_EQ(m["d"].line, 3);
  CHECK_EQ(m["b"].col, 10);
  std::vector<std::string> errs;
  scan("e.c", "int /*@x*/\rint y;\r", &errs);  // an anchor at the end of its (CR) line is still an error
  CHECK(hasError(errs, "no token"));
}

TEST(corpus_anchor_scanner_skips_symlinks) {
#if !defined(_WIN32)
  namespace fs = std::filesystem;
  fs::path d = fs::temp_directory_path() / ("tf_scan_links_" + std::to_string(::getpid()));
  std::error_code ec;
  fs::remove_all(d, ec);
  fs::create_directories(d / "real");
  std::ofstream(d / "real/a.h") << "int /*@only_once*/x;\n";
  fs::create_directory_symlink("real", d / "linked_dir", ec);
  fs::create_symlink("real/a.h", d / "linked_file.h", ec);
  if (!ec) {
    AnchorMap m;
    std::vector<std::string> errs;
    CHECK(scanAnchors(d, m, errs));
    CHECK(errs.empty());
    CHECK_EQ(m.size(), size_t(1));
    CHECK_EQ(m["only_once"].file, std::string("real/a.h"));
  }
  fs::remove_all(d, ec);
#endif
}

TEST(corpus_anchor_tilde_is_a_destructor_only_in_a_declaration_context) {
  std::vector<std::string> errs;
  // Destructor names: class body (line start, specifier, access label), after `::`, a call through `->` / `.`.
  AnchorMap m = scan("d.cpp",
                     "struct S {\n"
                     "  /*@in_class*/~S();\n"
                     "  virtual /*@virt*/~S();\n"
                     "public: /*@label*/~S();\n"
                     "};\n"
                     "S::/*@qual*/~S() {}\n"
                     "void f(S* p, S& r) { p->/*@arrow*/~S(); r./*@dot*/~S(); }\n",
                     &errs);
  CHECK(errs.empty());
  for (const char* n : {"in_class", "virt", "label", "qual", "arrow", "dot"}) {
    CHECK_EQ(m[n].skip, 1);
    CHECK_EQ(m[n].len, 2);
  }
  // The unary operator is a one-character token: after `=`, `(`, `return`, an operator; or not followed by `(`.
  m = scan("u.cpp",
           "int a = /*@eq*/~b;\n"
           "int c = (/*@paren*/~d(1));\n"
           "int e = x & /*@amp*/~y(2);\n"
           "int g() { return /*@ret*/~h(3); }\n"
           "void k() { x = /*@semi*/~z; }\n",
           &errs);
  CHECK(errs.empty());
  for (const char* n : {"eq", "paren", "amp", "ret", "semi"}) {
    CHECK_EQ(m[n].skip, 0);
    CHECK_EQ(m[n].len, 1);
  }
  // Explicit opt-in `/*@name~*/` where the context cannot tell (a macro argument), and its misuse.
  m = scan("e.cpp", "DTOR(/*@opt~*/~S)\nint /*@bad~*/plain;\n", &errs);
  CHECK_EQ(m["opt"].skip, 1);
  CHECK_EQ(m["opt"].len, 2);
  REQUIRE(errs.size() == 1u);
  CHECK(hasError(errs, "e.cpp:2: anchor 'bad~' is not followed by a ~Identifier"));
  CHECK(anchorNamesIn("x /*@one~*/~S y /*@two*/z").size() == 2u);
}

TEST(corpus_sym_alternatives_and_groups) {
  const AnchorMap a = sample();
  std::vector<Item> it;
  std::vector<std::string> errs;
  REQUIRE(
      parse("sym \"q\" -> a | b\n"
            "sym \"q\" \xE2\x88\x8B a | b c\n"
            "sym \"q\" has a | b | d\n",
            a, it, errs));
  CHECK((it[0].targets == std::vector<std::string>{"a", "b"}));
  REQUIRE(it[1].groups.size() == 2u);
  CHECK((it[1].groups[0] == std::vector<std::string>{"a", "b"}));
  CHECK((it[1].groups[1] == std::vector<std::string>{"c"}));
  CHECK_EQ(it[1].targets.size(), 3u);
  CHECK_EQ(it[2].groups.size(), 1u);

  const Loc A = at("x.cpp", 0, 10), B = at("x.cpp", 1, 10), C = at("x.cpp", 2, 14), D = at("x.cpp", 3, 10);
  CHECK(checkLocations(it[0], {B, A}, a).ok);  // the first result is one of the alternatives
  CHECK(!checkLocations(it[0], {D, A}, a).ok);
  CHECK(checkLocations(it[1], {B, C}, a).ok);  // b stands in for a
  CHECK(checkLocations(it[1], {D, A, C}, a).ok);
  CHECK(!checkLocations(it[1], {A, B}, a).ok);  // the group {c} is missing
  CHECK(!checkLocations(it[1], {C}, a).ok);     // the group {a | b} is missing
  Verdict v = checkLocations(it[1], {C}, a);
  CHECK_EQ(v.expected, "contains a | b c");
  CHECK(checkLocations(it[2], {D}, a).ok);

  const char* bad[][2] = {{"sym \"q\" has | a\n", "misplaced '|'"},
                          {"sym \"q\" has a |\n", "missing anchor after '|'"},
                          {"sym \"q\" has a | | b\n", "misplaced '|'"},
                          {"sym \"q\" has none\n", "unknown anchor 'none'"}};
  for (auto& [text, needle] : bad) {
    CHECK(!parse(text, a, it, errs));
    CHECK(hasError(errs, needle));
  }
}

TEST(corpus_rename_none_and_refused) {
  const AnchorMap a = sample();
  std::vector<Item> it;
  std::vector<std::string> errs;
  REQUIRE(
      parse("rename a b\n  (none)\n"
            "rename a b !   # refused\n"
            "def a -> b\n"
            "rename a newName\n  b\n",
            a, it, errs));
  REQUIRE(it.size() == 4u);
  CHECK(it[0].none && !it[0].refused && it[0].renamed.empty());
  CHECK(it[1].refused && !it[1].none);
  CHECK(!it[3].none && !it[3].refused && it[3].renamed.size() == 1u);

  const std::map<std::string, std::string> before = {{"a", "alpha"}, {"b", "beta"}, {"c", "gamma"}};
  // (none): unchanged passes, with or without an error; a changed anchor or no answer fails.
  CHECK(checkRenameNoChange(it[0], true, "", 0, before, before).ok);
  CHECK(checkRenameNoChange(it[0], true, "invalid name", 0, before, before).ok);
  Verdict v = checkRenameNoChange(it[0], true, "", 3, before, {{"a", "b"}, {"b", "beta"}, {"c", "gamma"}});
  CHECK(!v.ok);
  CHECK_EQ(v.actual, "changed: a(b)");
  CHECK(!checkRenameNoChange(it[0], false, "", 0, before, before).ok);
  // `!`: the rename must also have been refused with an error and no edits.
  CHECK(checkRenameNoChange(it[1], true, "invalid name", 0, before, before).ok);
  v = checkRenameNoChange(it[1], true, "", 0, before, before);
  CHECK(!v.ok);
  CHECK_EQ(v.actual, "rename accepted (0 edits)");
  CHECK(!checkRenameNoChange(it[1], true, "x", 2, before, before).ok);

  const char* bad[][2] = {
      {"rename a b\n", "rename needs indented lines"},
      {"rename a b\n  (none)\n  c\n", "'(none)' cannot be mixed with anchors"},
      {"rename a b\n  c\n  (none)\n", "'(none)' cannot be mixed with anchors"},
      {"rename a b !\n  c\n", "unexpected indented line"},
      {"rename a b ! extra\n", "unexpected text after the new name"},
      {"fast rename a b !\n", "rename has no fast tier"},
  };
  for (auto& [text, needle] : bad) {
    CHECK(!parse(text, a, it, errs));
    CHECK(hasError(errs, needle));
  }
}

TEST(corpus_close_directive_errors) {
  const AnchorMap a = sample();
  std::vector<Item> it;
  std::vector<std::string> errs;
  CHECK(!parse("close\n", a, it, errs));
  CHECK(hasError(errs, "usage: close <file>"));
  CHECK(!parse("close a b\n", a, it, errs));
  CHECK(hasError(errs, "usage: close <file>"));
  CHECK(!parse("fast close x.cpp\n", a, it, errs));
  CHECK(hasError(errs, "'close' cannot follow fast/xfail"));
}
