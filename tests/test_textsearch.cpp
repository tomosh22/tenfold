#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>
#include <tuple>

#include "app/Editor.h"
#include "search/TextSearch.h"
#include "test_main.h"

using namespace tf;
namespace fs = std::filesystem;

namespace {

struct TempTree {
  fs::path root;
  TempTree() {
    static std::atomic<int> n{0};
    root = fs::temp_directory_path() / ("tenfold_search_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_" + std::to_string(n++));
    fs::remove_all(root);
    fs::create_directories(root);
  }
  ~TempTree() { std::error_code ec; fs::remove_all(root, ec); }
  fs::path write(const std::string& rel, const std::string& text) {
    fs::path p = root / rel;
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << text;
    return p;
  }
  SearchInput input(std::initializer_list<const char*> rels) {
    SearchInput in;
    in.root = root;
    for (auto r : rels) in.files.push_back(root / r);
    return in;
  }
};

// Sorted (file, line, column) for stable comparisons: workers finish in any order.
std::vector<SearchMatch> sorted(std::vector<SearchMatch> v) {
  std::sort(v.begin(), v.end(), [](const SearchMatch& a, const SearchMatch& b) {
    return std::tie(a.file, a.line, a.column) < std::tie(b.file, b.line, b.column);
  });
  return v;
}

std::vector<SearchMatch> search(SearchInput in, SearchOptions o, SearchSummary* sum = nullptr) {
  std::vector<SearchMatch> out;
  auto s = TextSearch::run(std::move(in), std::move(o), &out);
  if (sum) *sum = s;
  return sorted(std::move(out));
}

SearchOptions opts(const char* q) {
  SearchOptions o;
  o.query = q;
  return o;
}

}  // namespace

TEST(search_case_sensitivity) {
  TempTree t;
  t.write("a.cpp", "Foo foo FOO\nxfoox\n");
  auto in = [&] { return t.input({"a.cpp"}); };
  auto cs = search(in(), opts("foo"));
  REQUIRE(cs.size() == 2);  // "foo" and inside "xfoox"
  CHECK_EQ(cs[0].line, 0);
  CHECK_EQ(cs[0].column, 4);
  CHECK_EQ(cs[0].length, 3);
  CHECK_EQ(cs[0].text, std::string("Foo foo FOO"));
  CHECK_EQ(cs[1].line, 1);
  CHECK_EQ(cs[1].column, 1);
  SearchOptions ci = opts("fOo");
  ci.caseSensitive = false;
  auto r = search(in(), ci);
  REQUIRE(r.size() == 4);
  CHECK_EQ(r[0].column, 0);
  CHECK_EQ(r[2].column, 8);
  CHECK_EQ(search(in(), opts("_nothing_like_this_that_is_longer_than_the_file_____")).size(), size_t(0));
  CHECK_EQ(search(in(), opts("-x")).size(), size_t(0));
}

TEST(search_whole_word_uses_identifier_boundaries) {
  TempTree t;
  t.write("a.cpp", "Vector2 v;\nVector2i w;\nmy_Vector2 x;\nVector2_t y;\n(Vector2)\nVector2");
  SearchOptions o = opts("Vector2");
  o.wholeWord = true;
  auto r = search(t.input({"a.cpp"}), o);
  REQUIRE(r.size() == 3);
  CHECK_EQ(r[0].line, 0);
  CHECK_EQ(r[1].line, 4);
  CHECK_EQ(r[1].column, 1);
  CHECK_EQ(r[2].line, 5);  // end of file, no trailing byte
  // A query that itself ends in a non-word character only checks the other side.
  SearchOptions p = opts("Vector2)");
  p.wholeWord = true;
  CHECK_EQ(search(t.input({"a.cpp"}), p).size(), size_t(1));
  SearchOptions ci = opts("vector2");
  ci.wholeWord = true;
  ci.caseSensitive = false;
  CHECK_EQ(search(t.input({"a.cpp"}), ci).size(), size_t(3));
}

TEST(search_regex_or_graceful_error) {
  TempTree t;
  t.write("a.cpp", "int foo1 = 2;\nint bar22 = 3;\nfoo\n");
  SearchOptions o = opts("[a-z]+[0-9]+");
  o.regex = true;
  SearchSummary s;
  auto r = search(t.input({"a.cpp"}), o, &s);
  if (!TextSearch::regexSupported()) {
    CHECK_EQ(s.error, std::string("regex support not built"));
    CHECK(r.empty());
    // Literal search still works with no PCRE2 at all.
    CHECK_EQ(search(t.input({"a.cpp"}), opts("foo")).size(), size_t(2));
    return;
  }
  CHECK(s.error.empty());
  REQUIRE(r.size() == 2);
  CHECK_EQ(r[0].column, 4);
  CHECK_EQ(r[0].length, 4);
  CHECK_EQ(r[1].line, 1);
  CHECK_EQ(r[1].length, 5);
  // Anchors work per line; whole word wraps the pattern; case-insensitive flag.
  SearchOptions anchored = opts("^int");
  anchored.regex = true;
  CHECK_EQ(search(t.input({"a.cpp"}), anchored).size(), size_t(2));
  SearchOptions ww = opts("fo+");
  ww.regex = true;
  ww.wholeWord = true;
  CHECK_EQ(search(t.input({"a.cpp"}), ww).size(), size_t(1));  // not "foo1"
  SearchOptions ci = opts("INT");
  ci.regex = true;
  ci.caseSensitive = false;
  CHECK_EQ(search(t.input({"a.cpp"}), ci).size(), size_t(2));
  SearchOptions bad = opts("(unclosed");
  bad.regex = true;
  search(t.input({"a.cpp"}), bad, &s);
  CHECK(s.error.find("invalid regex") == 0);
}

TEST(search_glob_matcher) {
  CHECK(TextSearch::globMatch("*.cpp", "a.cpp"));
  CHECK(!TextSearch::globMatch("*.cpp", "src/a.cpp"));
  CHECK(TextSearch::globMatch("**/*.cpp", "src/deep/a.cpp"));
  CHECK(TextSearch::globMatch("**/*.cpp", "a.cpp"));
  CHECK(TextSearch::globMatch("src/**/a.cpp", "src/a.cpp"));
  CHECK(TextSearch::globMatch("src/**/a.cpp", "src/x/y/a.cpp"));
  CHECK(TextSearch::globMatch("a?c", "abc"));
  CHECK(!TextSearch::globMatch("a?c", "a/c"));
  CHECK(!TextSearch::globMatch("src/*", "src/x/y"));
  CHECK(TextSearch::pathMatchesGlob("*.h", "include/a.h"));  // bare pattern: any segment
  CHECK(TextSearch::pathMatchesGlob("third_party", "x/third_party/y.cpp"));
  CHECK(TextSearch::pathMatchesGlob("src/search", "src/search/TextSearch.cpp"));  // directory prefix
  CHECK(!TextSearch::pathMatchesGlob("src/search", "other/src/search/a.cpp"));    // anchored
  CHECK(TextSearch::pathMatchesGlob("src/search/", "src/search/a.cpp"));
  CHECK(!TextSearch::pathMatchesGlob("*.cpp", "a.hpp"));
}

TEST(search_include_exclude_globs) {
  TempTree t;
  t.write("src/a.cpp", "needle\n");
  t.write("src/b.h", "needle\n");
  t.write("third_party/c.cpp", "needle\n");
  t.write("tests/d.cpp", "needle\n");
  auto in = [&] { return t.input({"src/a.cpp", "src/b.h", "third_party/c.cpp", "tests/d.cpp"}); };
  CHECK_EQ(search(in(), opts("needle")).size(), size_t(4));
  SearchOptions inc = opts("needle");
  inc.includeGlobs = {"*.cpp"};
  CHECK_EQ(search(in(), inc).size(), size_t(3));
  inc.excludeGlobs = {"third_party", "tests/**"};
  auto r = search(in(), inc);
  REQUIRE(r.size() == 1);
  CHECK_EQ(r[0].file.filename().string(), std::string("a.cpp"));
  SearchOptions only = opts("needle");
  only.includeGlobs = {"src"};
  CHECK_EQ(search(in(), only).size(), size_t(2));
  SearchOptions none = opts("needle");
  none.includeGlobs = {"*.rs"};
  SearchSummary s;
  CHECK_EQ(search(in(), none, &s).size(), size_t(0));
  CHECK_EQ(s.filesSearched, size_t(0));
}

TEST(search_open_document_text_beats_disk) {
  TempTree t;
  t.write("a.cpp", "disk_only\nshared\n");
  t.write("b.cpp", "shared\n");
  SearchInput in = t.input({"a.cpp", "b.cpp"});
  in.overrides["a.cpp"] = "buffer_only\nshared\n";
  CHECK_EQ(search(in, opts("disk_only")).size(), size_t(0));
  auto r = search(in, opts("buffer_only"));
  REQUIRE(r.size() == 1);
  CHECK_EQ(r[0].file, t.root / "a.cpp");
  CHECK_EQ(search(in, opts("shared")).size(), size_t(2));
}

TEST(search_result_cap_and_truncated_flag) {
  TempTree t;
  std::string text;
  for (int i = 0; i < 50; ++i) text += "hit hit\n";
  t.write("a.txt", text);
  t.write("b.txt", text);
  SearchOptions o = opts("hit");
  o.maxResults = 30;
  o.batchSize = 7;
  SearchSummary s;
  size_t batches = 0;
  std::vector<SearchMatch> out;
  auto h = TextSearch::start(t.input({"a.txt", "b.txt"}), o, [&](std::vector<SearchMatch>&& b) {
    ++batches;
    CHECK(b.size() <= 7);
    for (auto& m : b) out.push_back(std::move(m));
  }, [&](const SearchSummary& x) { s = x; });
  h.wait();
  CHECK_EQ(out.size(), size_t(30));
  CHECK_EQ(s.matches, size_t(30));
  CHECK(s.truncated);
  CHECK(!s.cancelled);
  CHECK(batches >= 5);
  o.maxResults = 200;  // exactly the number of matches: not truncated
  search(t.input({"a.txt", "b.txt"}), o, &s);
  CHECK_EQ(s.matches, size_t(200));
  CHECK(!s.truncated);
}

TEST(search_crlf_columns_and_text_exclude_cr) {
  TempTree t;
  t.write("w.cpp", "first\r\nsecond target\r\nthird target\r\n");
  auto r = search(t.input({"w.cpp"}), opts("target"));
  REQUIRE(r.size() == 2);
  CHECK_EQ(r[0].line, 1);
  CHECK_EQ(r[0].column, 7);
  CHECK_EQ(r[0].length, 6);
  CHECK_EQ(r[0].text, std::string("second target"));
  CHECK_EQ(r[1].line, 2);
  CHECK_EQ(r[1].text, std::string("third target"));
  if (TextSearch::regexSupported()) {
    SearchOptions eol = opts("target$");  // $ sits before \r\n
    eol.regex = true;
    auto e = search(t.input({"w.cpp"}), eol);
    REQUIRE(e.size() == 2);
    CHECK_EQ(e[0].length, 6);
    CHECK_EQ(e[0].text, std::string("second target"));
  }
}

TEST(search_utf8_byte_columns) {
  TempTree t;
  t.write("u.cpp", "// h\xC3\xA9llo w\xC3\xB6rld \xE2\x82\xAC needle\n\xC3\xA9needle\n");
  auto r = search(t.input({"u.cpp"}), opts("needle"));
  REQUIRE(r.size() == 2);
  CHECK_EQ(r[0].column, 3 + 6 + 1 + 6 + 1 + 3 + 1);  // bytes, not code points
  CHECK_EQ(r[1].column, 2);
  auto e = search(t.input({"u.cpp"}), opts("\xC3\xA9llo"));  // multi-byte needle
  REQUIRE(e.size() == 1);
  CHECK_EQ(e[0].column, 4);
  CHECK_EQ(e[0].length, 5);
  SearchOptions ww = opts("needle");  // non-ASCII bytes count as word characters
  ww.wholeWord = true;
  CHECK_EQ(search(t.input({"u.cpp"}), ww).size(), size_t(1));
}

TEST(search_skips_binary_and_handles_missing_files) {
  TempTree t;
  t.write("bin.dat", std::string("needle\0needle", 13));
  t.write("ok.txt", "needle\n");
  SearchInput in = t.input({"bin.dat", "ok.txt", "missing.txt"});
  SearchSummary s;
  CHECK_EQ(search(in, opts("needle"), &s).size(), size_t(1));
  CHECK(s.error.empty());
  CHECK_EQ(s.filesMatched, size_t(1));
  SearchSummary e;
  search(in, opts(""), &e);
  CHECK_EQ(e.error, std::string("empty query"));
}

TEST(search_cancel_mid_search_is_prompt_and_stops_batches) {
  // 200 virtual files x 20k matching lines with a deliberately slow consumer: the full
  // search would take tens of seconds, so finishing quickly proves cancellation works.
  std::string body;
  for (int i = 0; i < 20000; ++i) body += "foo line\n";
  SearchInput in;
  in.root = "/virtual";
  for (int i = 0; i < 200; ++i) {
    std::string name = "f" + std::to_string(i) + ".cpp";
    in.files.push_back(in.root / name);
    in.overrides[name] = body;
  }
  SearchOptions o = opts("foo");
  o.maxResults = size_t(1) << 40;
  o.batchSize = 100;
  std::atomic<int> batches{0};
  std::atomic<bool> doneSeen{false};
  std::atomic<int> batchesAfterDone{0};
  SearchSummary sum;
  auto h = SearchHandle::create();
  TextSearch::start(in, o, [&](std::vector<SearchMatch>&&) {
    if (doneSeen) ++batchesAfterDone;
    ++batches;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }, [&](const SearchSummary& s) { sum = s; doneSeen = true; }, h);
  while (batches < 3) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  auto t0 = std::chrono::steady_clock::now();
  h.cancel();
  h.wait();
  double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  CHECK(ms < 1000);
  CHECK(sum.cancelled);
  CHECK(!sum.truncated);
  CHECK(doneSeen.load());
  int at = batches;
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  CHECK_EQ(batches.load(), at);
  CHECK_EQ(batchesAfterDone.load(), 0);
  CHECK(sum.filesSearched < 200);
}

TEST(search_long_lines_are_windowed) {
  TempTree t;
  t.write("min.js", std::string(50000, 'x') + "needle" + std::string(50000, 'y') + "\n");
  auto r = search(t.input({"min.js"}), opts("needle"));
  REQUIRE(r.size() == 1);
  CHECK_EQ(r[0].column, 50000);
  CHECK(r[0].text.size() < 2000);
  CHECK_EQ(r[0].text.substr(static_cast<size_t>(r[0].column - r[0].textOffset), 6), std::string("needle"));
}

TEST(editor_find_in_workspace_uses_open_buffers_and_jump_records_history) {
  TempTree t;
  t.write("src/a.cpp", "int main() {\n  return 0;\n}\n");
  t.write("src/b.cpp", "// disk token\nint other;\n");
  Editor ed(t.root);
  ed.workspace().scan();
  Document* a = ed.open(t.root / "src/a.cpp");
  REQUIRE(a != nullptr);
  a->insert({1, 0}, "  // unsaved token\n");  // dirty buffer, not on disk
  Document* b = ed.open(t.root / "src/b.cpp");
  REQUIRE(b != nullptr);
  b->replace({{0, 0}, {0, 13}}, "// buffer edit");  // the disk text "// disk token" is gone from the buffer

  std::vector<SearchMatch> got;
  SearchSummary sum;
  bool done = false;
  auto h = ed.findInWorkspace("token", SearchOptions(), [&](const std::vector<SearchMatch>& m) {
    got.insert(got.end(), m.begin(), m.end());
  }, [&](const SearchSummary& s) { sum = s; done = true; });
  CHECK(ed.waitFor([&] { return done; }, 10000));
  h.wait();
  REQUIRE(done);
  CHECK(sum.error.empty());
  REQUIRE(got.size() == 1);  // only the unsaved line in a.cpp
  CHECK_EQ(got[0].line, 1);
  CHECK_EQ(got[0].column, 13);

  // Jump: lands in history (origin + destination).
  ed.gotoLocation({t.root / "src/b.cpp", {0, 0}});
  size_t before = ed.navigation().entries().size();
  CHECK(ed.gotoSearchMatch(got[0]));
  CHECK(ed.navigation().entries().size() > before);
  CHECK(ed.cursor() == (Location{t.root / "src/a.cpp", {1, 13}}));
  CHECK(ed.back());
  CHECK_EQ(ed.cursor().file.filename().string(), std::string("b.cpp"));
}

TEST(editor_find_cancel_drops_queued_batches_but_still_reports_done) {
  TempTree t;
  std::string body;
  for (int i = 0; i < 2000; ++i) body += "token\n";
  for (int i = 0; i < 20; ++i) t.write("f" + std::to_string(i) + ".cpp", body);
  Editor ed(t.root);
  ed.workspace().scan();
  size_t batches = 0;
  bool done = false;
  SearchOptions o;
  o.maxResults = 1000000;
  o.batchSize = 10;
  auto h = ed.findInWorkspace("token", o, [&](const std::vector<SearchMatch>&) { ++batches; },
                              [&](const SearchSummary&) { done = true; });
  h.cancel();  // before anything was pumped
  CHECK(ed.waitFor([&] { return done; }, 10000));
  CHECK_EQ(batches, size_t(0));
}
