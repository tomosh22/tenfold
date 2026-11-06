// W9f: pathological generated workspaces. Each test writes its workspace into a temp dir (small sizes, seconds) and
// checks that the engine neither crashes, hangs, nor blows up in memory / time on it: huge trees, giant and one-line
// files, deep nesting, enormous identifiers, binary / odd-encoded files, symlink loops, unreadable files, truncated
// code, and a workspace that is being mutated while it is indexed.
//
// The same bodies run at full size from tools/stress/run_pathological.py: `pathological_full_*` read the workspaces
// that tools/stress/gen_pathological.py wrote to $TENFOLD_PATHOLOGICAL_DIR (and skip when it is unset).
#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <random>
#include <set>
#include <thread>

#include "app/Editor.h"
#include "core/Document.h"
#include "core/PathUtil.h"
#include "semantic/FastIndex.h"
#include "test_main.h"
#include "workspace/Workspace.h"

#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

using namespace tf;
namespace fs = std::filesystem;

namespace {

#if defined(__SANITIZE_ADDRESS__)
constexpr size_t kSan = 5;  // sizes shrink under AddressSanitizer (about 10x slower)
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
constexpr size_t kSan = 5;
#else
constexpr size_t kSan = 1;
#endif
#else
constexpr size_t kSan = 1;
#endif
size_t scaled(size_t n) { return n / kSan; }
#ifdef _WIN32
constexpr bool kWindows = true;
#else
constexpr bool kWindows = false;
#endif

using clk = std::chrono::steady_clock;
double msSince(clk::time_point t) { return std::chrono::duration<double, std::milli>(clk::now() - t).count(); }

struct TempDir {
  fs::path path;
  explicit TempDir(const std::string& tag) {
    static std::atomic<int> n{0};
    std::error_code ec;
    path = fs::weakly_canonical(fs::temp_directory_path(), ec) /
           ("tenfold_patho_" + tag + "_" + std::to_string(clk::now().time_since_epoch().count()) + "_" +
            std::to_string(n++));
    fs::create_directories(path);
  }
  ~TempDir() {
    std::error_code ec;
    // Directories made unreadable by a test must be openable again before they can be removed.
    for (auto it = fs::recursive_directory_iterator(path, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
      if (ec) {
        ec.clear();
        continue;
      }
      if (it->is_directory(ec) && !it->is_symlink(ec))
        fs::permissions(it->path(), fs::perms::owner_all, fs::perm_options::add, ec);
    }
    fs::remove_all(path, ec);
#ifdef _WIN32
    // Trees deeper than MAX_PATH can only be removed through the extended-length form.
    if (fs::exists(path, ec) || ec) fs::remove_all(fs::path(L"\\\\?\\" + path.wstring()), ec);
#endif
  }
};

void put(const fs::path& p, const std::string& data) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary) << data;
}

Editor::Options fastOnly() {
  Editor::Options o;
  o.semantic.useClangd = false;
  o.vcs.enabled = false;
  return o;
}

// Starts an editor with the fast tier only and waits for its index. `ms` is a liveness bound, not a budget.
struct Ws {
  std::unique_ptr<Editor> ed;
  double startMs = 0, indexMs = 0;
  explicit Ws(const fs::path& root, int ms = 60000, Editor::Options o = fastOnly()) {
    ed = std::make_unique<Editor>(root, std::move(o));
    auto t = clk::now();
    std::string err;
    if (!ed->start(&err)) std::cerr << "    start: " << err << "\n";
    startMs = msSince(t);
    t = clk::now();
    if (!ed->semantic().waitForFastIndex(ms)) tftest::fail(__FILE__, __LINE__, "fast index did not finish in time");
    indexMs = msSince(t);
  }
  ~Ws() { ed->shutdown(); }
  size_t symbols(const std::string& q) const { return ed->semantic().approximateSymbols(q).size(); }  // fuzzy
  size_t exact(const std::string& name) const { return ed->semantic().fastIndex().lookup(name).size(); }
};

void stress(const std::string& what, double ms) {
  std::printf("STRESS: %s: %.1f ms\n", what.c_str(), ms);
  std::fflush(stdout);
}

std::string rep(const std::string& s, size_t n) {
  std::string out;
  out.reserve(s.size() * n);
  for (size_t i = 0; i < n; ++i) out += s;
  return out;
}

// A plausible class + function pair; `n` makes the names unique.
std::string unit(int n) {
  const std::string s = std::to_string(n);
  return "namespace ns" + std::to_string(n % 13) + " {\nclass Widget" + s + " {\n public:\n  int update" + s +
         "(int x) const;\n  static constexpr int kMagic = " + s + ";\n};\nint Widget" + s + "::update" + s +
         "(int x) const { return x * " + s + "; }\n}\n";
}

std::string giantCpp(int lines) {
  std::string t;
  t.reserve(static_cast<size_t>(lines) * 28);
  for (int i = 0; i < lines; ++i) {
    switch (i % 8) {
      case 0: t += "int giant_fn_" + std::to_string(i) + "(int a, int b) {\n"; break;
      case 1: t += "  int local_" + std::to_string(i) + " = a * " + std::to_string(i % 1000) + " + b;\n"; break;
      case 2: t += "  if (local_" + std::to_string(i - 1) + " > 5) return 1;\n"; break;
      case 3: t += "  const char* s = \"string // not a comment /* nor this */\";\n"; break;
      case 4: t += "  // trailing comment line\n"; break;
      case 5: t += "  return a;\n"; break;
      case 6: t += "}\n"; break;
      default: t += "\n"; break;
    }
  }
  t += "int giant_tail_marker() { return giant_fn_0(1, 2); }\n";
  return t;
}

// Editor operations on one giant document: open, scroll to the end, edit, undo, redo, find, replace.
void giantDocumentOps(Editor& ed, const fs::path& file, const std::string& marker, const std::string& label,
                      double livenessMs = 60000) {
  auto t = clk::now();
  std::string err;
  Document* d = ed.open(file, &err);
  REQUIRE(d != nullptr);
  stress(label + " open", msSince(t));
  CHECK(msSince(t) < livenessMs);

  const int lines = d->buffer().lineCount();
  t = clk::now();
  CHECK(ed.gotoLocation({file, {lines - 1, 0}}, &err));
  ed.moveCarets(Editor::Direction::DocEnd);
  stress(label + " goto end", msSince(t));

  const size_t before = d->buffer().byteSize();
  t = clk::now();
  ed.insertText("x");
  CHECK_EQ(d->buffer().byteSize(), before + 1);
  stress(label + " type", msSince(t));
  t = clk::now();
  CHECK(ed.undo());
  CHECK_EQ(d->buffer().byteSize(), before);
  CHECK(ed.redo());
  CHECK_EQ(d->buffer().byteSize(), before + 1);
  CHECK(ed.undo());
  stress(label + " undo+redo+undo", msSince(t));

  if (!marker.empty()) {
    SearchOptions so;
    so.query = marker;
    so.maxResults = 1000000;
    t = clk::now();
    std::string ferr;
    auto hits = ed.findInDocument(so, &ferr);
    stress(label + " find in document", msSince(t));
    CHECK(!hits.empty());
    if (hits.empty()) std::cerr << "    find error: " << ferr << "\n";
    t = clk::now();
    const size_t n = ed.replaceAll(so, marker + "_r", &ferr);
    CHECK_EQ(n, hits.size());
    CHECK(ed.undo());
    stress(label + " replace all + undo", msSince(t));
  }
  CHECK(msSince(t) < livenessMs);
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// Regressions found by the stress runs
// ---------------------------------------------------------------------------------------------------------------

// 10k nested namespaces / classes gave every symbol a scope string of its full nesting (O(depth^2) memory and time:
// 330 MB and 2 s for a 180 KB file). The scope is cut to the innermost components now.
TEST(pathological_nested_scopes_cost_linear_memory) {
  const int depth = 6000;
  std::string ns, cls, cpp17 = "namespace ";
  for (int i = 0; i < depth; ++i) ns += "namespace n" + std::to_string(i) + " { ";
  ns += "int leaf_ns_fn() { return 1; }" + rep(" }", depth) + "\n";
  for (int i = 0; i < depth; ++i) cls += "struct S" + std::to_string(i) + " { ";
  cls += "int leaf_member;" + rep(" };", depth) + "\n";
  for (int i = 0; i < depth; ++i) cpp17 += (i ? "::a" : "a") + std::to_string(i);
  cpp17 += " { int leaf_17; }\n";
  size_t total = 0;
  for (const std::string* src : {&ns, &cls, &cpp17}) {
    auto t = clk::now();
    auto syms = extractSymbols(*src);
    stress("extractSymbols, " + std::to_string(depth) + " nested scopes", msSince(t));
    CHECK(syms.size() >= static_cast<size_t>(depth));
    size_t bytes = 0, longest = 0;
    for (auto& s : syms) {
      bytes += s.scope.size();
      longest = std::max(longest, s.scope.size());
    }
    total += bytes;
    CHECK(longest < 2000);      // 64 components of a few characters each
    CHECK(bytes < (8u << 20));  // was 6000 * 3000 * 6 = 100 MB
    CHECK(msSince(t) < 5000);
  }
  // The innermost scopes are the ones that are kept.
  auto syms = extractSymbols(ns);
  bool found = false;
  for (auto& s : syms)
    if (s.name == "leaf_ns_fn") {
      found = true;
      CHECK(s.scope.find("n" + std::to_string(depth - 1)) != std::string::npos);  // the innermost scope is kept
    }
  CHECK(found);
  (void)total;
}

#ifndef _WIN32
// Reading /dev/zero or /dev/urandom never ends (std::bad_alloc after eating all the memory, 12 GB on the stress run)
// and a FIFO blocks forever; none of them is a document. Symlinks to regular files keep working.
TEST(pathological_non_regular_files_are_refused) {
  TempDir d("nonreg");
  put(d.path / "real.cpp", "int real_fn() { return 1; }\n");
  fs::create_symlink("/dev/zero", d.path / "zero.cpp");
  fs::create_symlink("/dev/urandom", d.path / "urandom.h");
  fs::create_symlink("real.cpp", d.path / "link.cpp");
  fs::create_directory(d.path / "dir.cpp");
  REQUIRE(::mkfifo((d.path / "fifo.cpp").c_str(), 0600) == 0);

  std::string out, err;
  for (const char* n : {"zero.cpp", "urandom.h", "fifo.cpp", "dir.cpp"}) {
    err.clear();
    auto t = clk::now();
    CHECK(!readRegularFile(d.path / n, out, &err));
    CHECK(!err.empty());
    CHECK(msSince(t) < 2000);
  }
  CHECK(!readRegularFile(d.path / "missing.cpp", out, &err));
  CHECK(readRegularFile(d.path / "link.cpp", out, &err));
  CHECK_EQ(out, "int real_fn() { return 1; }\n");

  Ws w(d.path);
  for (const char* n : {"zero.cpp", "urandom.h", "fifo.cpp", "dir.cpp"}) {
    err.clear();
    CHECK(w.ed->open(d.path / n, &err) == nullptr);
    CHECK(!err.empty());
  }
  Document* link = w.ed->open(d.path / "link.cpp", &err);
  REQUIRE(link != nullptr);
  CHECK_EQ(link->buffer().lineCount(), 2);
  // A file that turns into a FIFO after it was opened: reload refuses it, the document stays.
  fs::remove(d.path / "link.cpp");
  REQUIRE(::mkfifo((d.path / "link.cpp").c_str(), 0600) == 0);
  CHECK(!w.ed->reloadFromDisk(*link, &err));
  CHECK_EQ(link->buffer().lineCount(), 2);
  CHECK(w.symbols("real_fn") >= 1);
}
#endif

// Every edit of an open document used to copy its whole text and re-lex it as C++ on the typing thread, whatever the
// file type: 65 ms per key in a 5 MB log, 2.5 s per key in a 200 MB one. Only sources and headers are indexed now.
TEST(pathological_large_non_code_documents_skip_the_fast_index) {
  TempDir d("noncode");
  put(d.path / "main.cpp", "int main() { return 0; }\n");
  put(d.path / "notes.txt",
      "int txt_marker_fn() { return 1; }\n" + rep("2026-01-01 INFO request processed id=42 status=ok\n", 150000));
  put(d.path / "data.json", "{\"a\": \"int json_marker_fn() { return 1; }\"}\n" + rep("[1,2,3,4,5,6,7,8,9]\n", 100000));
  Ws w(d.path);
  for (const char* f : {"notes.txt", "data.json"}) {
    Document* doc = w.ed->open(d.path / f);
    REQUIRE(doc != nullptr);
    CHECK_EQ(w.symbols("txt_marker_fn"), 0u);
    CHECK_EQ(w.symbols("json_marker_fn"), 0u);
    w.ed->moveCarets(Editor::Direction::DocEnd);
    auto t = clk::now();
    for (int i = 0; i < 50; ++i) w.ed->insertText("x");
    const double ms = msSince(t);
    stress(std::string("50 keystrokes in a ") + std::to_string(doc->buffer().byteSize() >> 20) + " MB " + f, ms);
    PERF_CHECK(ms / 50 < 8.0, std::string("typing in a large ") + f + " (8 ms per key)");
    CHECK_EQ(w.symbols("txt_marker_fn"), 0u);
    w.ed->close(doc->path());
  }
}

// A code document above 1 MB re-indexes once typing pauses (300 ms) instead of on every key; smaller ones at once.
TEST(pathological_edits_in_large_code_documents_reindex_lazily) {
  TempDir d("lazy");
  put(d.path / "big.cpp", giantCpp(60000));  // about 2 MB
  put(d.path / "small.cpp", "int small_fn() { return 1; }\n");
  Ws w(d.path);
  Document* big = w.ed->open(d.path / "big.cpp");
  Document* small = w.ed->open(d.path / "small.cpp");
  REQUIRE(big != nullptr && small != nullptr);
  REQUIRE(big->buffer().byteSize() > (1u << 20));

  w.ed->activateDocument(*small, {1, 0});
  w.ed->insertText("int small_new_fn() { return 2; }\n");
  CHECK_EQ(w.exact("small_new_fn"), 1u);  // immediately

  w.ed->activateDocument(*big, {big->buffer().lineCount() - 1, 0});
  auto t = clk::now();
  w.ed->insertText("int lazy_new_fn() { return 2; }\n");
  for (int i = 0; i < 40; ++i) w.ed->insertText("x");
  const double ms = msSince(t);
  stress("41 edits in a 2 MB C++ document", ms);
  PERF_CHECK(ms / 41 < 8.0, "typing in a large C++ document (8 ms per key)");
  CHECK_EQ(w.exact("lazy_new_fn"), 0u);  // not yet: the re-index waits for the typing to pause
  CHECK(w.ed->waitFor([&] { return w.exact("lazy_new_fn") == 1; }, 10000));
  // a closed document does not leave a pending re-index behind
  w.ed->insertText("int lazy_closed_fn() { return 2; }\n");
  const fs::path bigPath = big->path();
  w.ed->close(bigPath);
  w.ed->waitFor([] { return false; }, 700);
}

// With clangd running, opening (or editing) a file that is not valid UTF-8 threw nlohmann type_error 316 out of
// Editor::open, because the didOpen message could not be serialised: a Latin-1 source, a binary with a .cpp extension.
// The invalid bytes are sent as U+FFFD now. (The fake clangd of test_crash.cpp stands in for the server.)
TEST(pathological_invalid_utf8_documents_open_and_edit_with_a_language_server) {
  TempDir d("badutf8");
  const std::string latin1 =
      "// caf\xe9 \xfc"
      "ber\nint latin1_fn() { return 1; }\n";
  const std::string binary =
      std::string("\x87\xff\xfe\xc3\x28\xa0\xa1\xe2\x82\xf0\x28\x8c\xbc\n") + std::string(2000, '\x80');
  put(d.path / "latin1.cpp", latin1);
  put(d.path / "binary.c", binary);
  put(d.path / "ok.cpp", "int ok_fn() { return 1; }\n");
  const fs::path log = d.path / "fake.log";
  Editor::Options o;
  o.vcs.enabled = false;
  o.semantic.clangdPath = TENFOLD_TEST_HELPER;
  o.semantic.clangdArgs = {"--fake-clangd", "--log=" + log.string()};
  o.semantic.semanticTokensDebounceMs = -1;
  Editor ed(d.path, o);
  std::string err;
  REQUIRE(ed.start(&err));
  REQUIRE(ed.waitFor([&] { return ed.semantic().clangd() != nullptr; }, 20000));
  for (const char* f : {"latin1.cpp", "binary.c"}) {
    err.clear();
    Document* doc = nullptr;
    try {
      doc = ed.open(d.path / f, &err);
    } catch (const std::exception& e) { err = e.what(); }
    CHECK(doc != nullptr);
    if (!doc) std::cerr << "    open " << f << ": " << err << "\n";
    if (!doc) continue;
    try {
      ed.gotoLocation({doc->path(), {0, 0}});
      ed.moveCarets(Editor::Direction::DocEnd);
      ed.insertText("x");
      CHECK(ed.undo());
    } catch (const std::exception& e) {
      CHECK(false);
      std::cerr << "    edit " << f << ": " << e.what() << "\n";
    }
    std::string bytes;
    CHECK(readRegularFile(d.path / f, bytes));
    CHECK_EQ(bytes, std::string(f) == "latin1.cpp" ? latin1 : binary);  // the file itself is untouched
  }
  CHECK(ed.open(d.path / "ok.cpp", &err) != nullptr);
  ed.shutdown();
  std::string logText;
  readRegularFile(log, logText);
  CHECK(logText.find("latin1.cpp") != std::string::npos);  // the server did receive the document
}

// The extractor keeps every token of a file in memory (a 200 MB generated .cpp took 3.4 GB and 20 s), so a source above
// 64 MiB is not indexed at all; one just below is.
TEST(pathological_huge_source_files_are_not_indexed) {
  constexpr size_t kMaxIndexedFileBytes = size_t{64} << 20;  // the limit in FastIndex.cpp
  const std::string tail = "\nint huge_tail_fn() { return 1; }\n";
  std::string below(kMaxIndexedFileBytes - tail.size(), ' ');
  below += tail;
  auto t = clk::now();
  auto some = extractSymbols(below);
  stress("extract from a source of exactly the limit", msSince(t));
  CHECK_EQ(some.size(), 1u);
  std::string above(kMaxIndexedFileBytes + 1 - tail.size(), ' ');
  above += tail;
  std::vector<std::string> includes;
  t = clk::now();
  CHECK(extractSymbols(above, &includes).empty());
  CHECK(includes.empty());
  CHECK(msSince(t) < 1000);
}

// ---------------------------------------------------------------------------------------------------------------
// Scenarios
// ---------------------------------------------------------------------------------------------------------------

TEST(pathological_many_files_wide_deep_and_flat) {
  TempDir d("tree");
  const int flat = 1500, dirs = 60, per = 25;
  for (int i = 0; i < flat; ++i)
    put(d.path / "flat" / ("f" + std::to_string(i) + (i % 2 ? ".cpp" : ".h")),
        "int flat_fn_" + std::to_string(i) + "(int x) { return x + " + std::to_string(i) + "; }\n");
  for (int i = 0; i < dirs; ++i)
    for (int j = 0; j < per; ++j)
      put(d.path / "wide" / ("g" + std::to_string(i / 10)) / ("d" + std::to_string(i)) /
              ("u" + std::to_string(j) + ".cpp"),
          unit(i * per + j));
  // Nesting: 40 levels. A Windows path is limited to MAX_PATH (260) unless the caller opts in with the extended-length
  // prefix, so the tree stops at 240 characters there; the rest is built separately below with that prefix.
  fs::path deep = d.path / "deep";
  int deepMade = 0;
  for (int i = 0; i < 40; ++i) {
    deep /= "level" + std::to_string(i);
    if (kWindows && deep.native().size() + 10 > 240) break;
    put(deep / "leaf.cpp", "int deep_fn_" + std::to_string(i) + "() { return 0; }\n");
    ++deepMade;
  }
  size_t beyondLimit = 0;  // files made past MAX_PATH (Windows only); they may or may not be indexed
#ifdef _WIN32
  // Past MAX_PATH only the extended-length form can create files. The product must neither throw nor stop while it
  // walks, watches and indexes such a tree, whether or not the system lets it see those files.
  try {
    fs::path cur = deep;
    for (int i = deepMade; i < 40; ++i) {
      cur /= "level" + std::to_string(i);
      const fs::path ext = fs::path(L"\\\\?\\" + cur.wstring());
      fs::create_directories(ext);
      std::ofstream(ext / "leaf.cpp", std::ios::binary) << "int beyond_fn_" << i << "() { return 0; }\n";
      ++beyondLimit;
    }
  } catch (const std::exception& e) { std::cerr << "    (beyond MAX_PATH stopped early: " << e.what() << ")\n"; }
#endif
  Ws w(d.path);
  stress("tree: start " + std::to_string(flat + dirs * per + deepMade) + " files", w.startMs);
  stress("tree: fast index", w.indexMs);
  const size_t expected = static_cast<size_t>(flat + dirs * per + deepMade);
  const size_t seen = w.ed->workspace().sourceFiles().size() + w.ed->workspace().headerFiles().size();
  CHECK(seen >= expected && seen <= expected + beyondLimit);
  CHECK_EQ(w.exact("flat_fn_777"), 1u);
  CHECK(deepMade >= 10);
  CHECK(w.exact("deep_fn_" + std::to_string(deepMade - 1)) == 1);
  CHECK(w.symbols("Widget123") >= 1);
  auto t = clk::now();
  auto approx = w.ed->semantic().approximateSymbols("update");
  stress("tree: symbol search 'update'", msSince(t));
  CHECK(!approx.empty());
  // Find in files over all of them.
  SearchOptions so;
  so.query = "flat_fn_1";
  bool done = false;
  size_t matches = 0;
  w.ed->findInWorkspace(
      so.query, so, [&](const std::vector<SearchMatch>& ms) { matches += ms.size(); },
      [&](const SearchSummary&) { done = true; });
  CHECK(w.ed->waitFor([&] { return done; }, 30000));
  CHECK(matches >= 111);  // flat_fn_1, _10-_19, _100-_199, _1000-_1499 -> 1 + 10 + 100 + 500
}

TEST(pathological_giant_files_and_long_lines) {
  TempDir d("giant");
  put(d.path / "src/giant.cpp", giantCpp(static_cast<int>(scaled(150000))));
  put(d.path / "data/big.txt",
      rep("2026-01-01 INFO worker processed request id=0000000 needle_x status=ok\n", scaled(60000)));
  // One line of 1 MB: an expression, a comment and a string; the symbol after each must still be found.
  put(d.path / "src/oneline_expr.cpp",
      "int long_expr = " + rep("1+", scaled(500000)) + "1;\nint after_expr() { return 1; }\n");
  put(d.path / "src/oneline_comment.cpp",
      "// " + rep("word ", scaled(200000)) + "\nint after_comment() { return 1; }\n");
  put(d.path / "src/oneline_string.cpp",
      "const char* s = \"" + rep("ab\\n", scaled(300000)) + "\";\nint after_string() { return 1; }\n");
  put(d.path / "src/oneline_nonl.cpp", "int a = " + rep("x_", 50000) + "0;");
  std::string mini;
  for (int i = 0; i < static_cast<int>(scaled(400)); ++i) {
    std::string u = unit(i);
    for (char& c : u)
      if (c == '\n') c = ' ';
    mini += u;
  }
  put(d.path / "src/minified.cpp", mini + "int minified_main(){return 0;}");

  Ws w(d.path);
  stress("giant: start+index", w.startMs + w.indexMs);
  CHECK_EQ(w.exact("giant_tail_marker"), 1u);
  CHECK_EQ(w.exact("after_expr"), 1u);
  CHECK_EQ(w.exact("after_comment"), 1u);
  CHECK_EQ(w.exact("after_string"), 1u);
  CHECK_EQ(w.exact("minified_main"), 1u);
  CHECK(w.symbols("Widget" + std::to_string(scaled(400) - 1)) >= 1);

  giantDocumentOps(*w.ed, d.path / "src/giant.cpp", "giant_tail_marker", "150k-line file");
  giantDocumentOps(*w.ed, d.path / "data/big.txt", "needle_x", "text file");
  giantDocumentOps(*w.ed, d.path / "src/oneline_expr.cpp", "after_expr", "1 MB one-line expression");
  giantDocumentOps(*w.ed, d.path / "src/oneline_comment.cpp", "after_comment", "1 MB one-line comment");
  giantDocumentOps(*w.ed, d.path / "src/oneline_nonl.cpp", "", "100 kB one-line file without newline");
  giantDocumentOps(*w.ed, d.path / "src/minified.cpp", "minified_main", "minified C++");

  // go to definition from the end of the giant file, and from the middle of the minified line
  Document* g = w.ed->document(d.path / "src/giant.cpp");
  REQUIRE(g != nullptr);
  const int last = g->buffer().lineCount() - 2;  // "int giant_tail_marker() { return giant_fn_0(1, 2); }"
  size_t found = 0;
  bool done = false;
  w.ed->gotoLocation({g->path(), {last, 36}});  // inside giant_fn_0
  w.ed->gotoDefinition([&](Confidence, const std::vector<SymbolLocation>& l, const std::string&) {
    found = std::max(found, l.size());
    done = true;
  });
  CHECK(w.ed->waitFor([&] { return done; }, 20000));
  CHECK(found >= 1);
}

TEST(pathological_deep_nesting_huge_identifiers_and_macros) {
  TempDir d("nest");
  const int n = 4000;
  std::string braces =
      "void f() {" + rep("{", n) + "int innermost = 1;" + rep("}", n) + "}\nint after_braces() { return 3; }\n";
  put(d.path / "braces.cpp", braces);
  put(d.path / "open.cpp", "void f() {" + rep("{ if (x) ", n) + "\nint after_open() { return 1; }\n");
  put(d.path / "close.cpp", "int a;\n" + rep("}", n) + "\nint after_close() { return 1; }\n");
  put(d.path / "parens.cpp", "int p = " + rep("(", n) + "1" + rep(")", n) + ";\nint after_parens() { return p; }\n");
  put(d.path / "templates.cpp", "template <typename T> struct Box { T v; };\nusing Deep = " + rep("Box<", n) + "int" +
                                    rep(">", n) + ";\nint after_templates() { return 1; }\n");
  put(d.path / "templates_open.cpp",
      "using Deep = " + rep("Box<", n) + "int;\nint after_open_templates() { return 1; }\n");
  put(d.path / "lambdas.cpp",
      "auto l = " + rep("[]{ return ", n / 4) + "1" + rep("; }()", n / 4) + ";\nint after_lambdas() { return 1; }\n");
  std::string ifs;
  for (int i = 0; i < n; ++i) ifs += "#if A" + std::to_string(i) + "\n";
  put(d.path / "ifs.cpp", ifs + "int in_ifs;\n" + rep("#endif\n", n) + "int after_ifs;\n");
  put(d.path / "ifs_open.cpp", ifs + "int in_open_ifs;\n");
  put(d.path / "comments.cpp", rep("/* ", n) + "text */\nint after_comments() { return 1; }\n");
  std::string ns;
  for (int i = 0; i < n; ++i) ns += "namespace n" + std::to_string(i) + " { ";
  put(d.path / "namespaces.cpp", ns + "int deepest_ns_fn() { return 1; }" + rep(" }", n) + "\n");
  const std::string big = "Ident" + rep("_abcdefghij", 64000 / 11);
  put(d.path / "ident.cpp",
      "int " + big + " = 1;\nint use_long() { return " + big + "; }\nstruct " + big + "_T { int " + big + "_m; };\n");
  put(d.path / "ident.h",
      "#define " + big + "_MACRO 1\nvoid " + big + "_fn();\nnamespace " + big + "_ns { void g(); }\n");
  std::string decls = "namespace big {\n";
  for (int i = 0; i < static_cast<int>(scaled(30000)); ++i)
    decls += "int var_" + std::to_string(i) + ";\nvoid fn_" + std::to_string(i) + "(int);\n";
  put(d.path / "decls.cpp", decls + "}\nint decls_tail = big::var_0;\n");
  std::string alts = "#define ALT_SUM ", params = "#define BIG_PARAMS(", args;
  for (int i = 0; i < static_cast<int>(scaled(20000)); ++i) {
    alts += (i ? " + alt_" : "alt_") + std::to_string(i);
    params += (i ? ",p" : "p") + std::to_string(i);
    args += (i ? "," : "") + std::to_string(i);
  }
  put(d.path / "macros.cpp", alts + "\n" + params + ") p0\nint alt_macro_use() { return BIG_PARAMS(" + args + "); }\n");
  put(d.path / "macro_recursive.cpp",
      "#define A B\n#define B C\n#define C A\nint v = A;\n#define F(x) F(x) F(x)\nint w = F(1);\n#include "
      "\"macro_recursive.cpp\"\n");
  put(d.path / "cycle_a.h", "#include \"cycle_b.h\"\nint cyc_a;\n");
  put(d.path / "cycle_b.h", "#include \"cycle_a.h\"\nint cyc_b;\n");

  Ws w(d.path);
  stress("nesting: start+index", w.startMs + w.indexMs);
  CHECK(w.exact("after_braces") == 1);
  CHECK(w.exact("after_parens") == 1);
  CHECK(w.exact("after_templates") == 1);
  CHECK(w.exact("after_close") == 1);
  CHECK(w.exact("use_long") == 1);
  CHECK(w.exact("alt_macro_use") == 1);
  CHECK(w.exact("var_" + std::to_string(scaled(30000) - 1)) == 1);
  CHECK(w.exact("deepest_ns_fn") == 1);
  CHECK(w.exact("cyc_a") == 1);

  // Editor commands that walk the nesting: scope selection and bracket matching at the innermost point.
  Document* doc = w.ed->open(d.path / "braces.cpp");
  REQUIRE(doc != nullptr);
  w.ed->gotoLocation({doc->path(), {0, 10 + n / 2}});
  auto t = clk::now();
  w.ed->selectCurrentScope();
  w.ed->selectCurrentScope();
  stress("nesting: select scope in 4000 braces", msSince(t));
  CHECK(msSince(t) < 10000);
  w.ed->gotoLocation({doc->path(), {0, 10}});
  t = clk::now();
  CHECK(w.ed->moveToMatchingBracket());
  stress("nesting: matching bracket across 4000 braces", msSince(t));
  CHECK(msSince(t) < 10000);
  for (const char* f : {"parens.cpp", "templates.cpp", "ident.cpp", "macros.cpp", "decls.cpp", "comments.cpp"}) {
    Document* x = w.ed->open(d.path / f);
    REQUIRE(x != nullptr);
    w.ed->gotoLocation({x->path(), {0, 0}});
    w.ed->moveCarets(Editor::Direction::LineEnd);
    w.ed->insertText(" ");
    CHECK(w.ed->undo());
    bool done = false;
    w.ed->gotoDefinition([&](Confidence, const std::vector<SymbolLocation>&, const std::string&) { done = true; });
    CHECK(w.ed->waitFor([&] { return done; }, 20000));
  }
}

TEST(pathological_binary_and_oddly_encoded_files) {
  TempDir d("enc");
  std::mt19937 rng(7);
  auto bytes = [&](size_t n) {
    std::string s(n, '\0');
    for (auto& c : s) c = static_cast<char>(rng() & 0xFF);
    return s;
  };
  auto u16 = [](const std::string& ascii, bool le, bool bom) {
    std::string out = bom ? (le ? "\xff\xfe" : "\xfe\xff") : "";
    for (char c : ascii) {
      if (le) {
        out += c;
        out += '\0';
      } else {
        out += '\0';
        out += c;
      }
    }
    return out;
  };
  const std::string good = "int enc_marker() { return 1; }\n";
  put(d.path / "random.cpp", bytes(300000));
  put(d.path / "random.h", bytes(100000));
  put(d.path / "elf.cpp", std::string("\x7f"
                                      "ELF") +
                              bytes(50000));
  put(d.path / "bad_utf8.cpp",
      std::string(
          "int ok1;\n\xff\xfe\xfd int bad;\n\xc3\x28 \xa0\xa1 \xe2\x82 \xf0\x28\x8c\xbc\nint ok2_after_bad;\n"));
  put(d.path / "overlong.cpp",
      std::string(
          "int a; // \xc0\xaf \xe0\x80\xaf \xf0\x80\x80\xaf \xed\xa0\x80 \xf4\x90\x80\x80\nint b_after_overlong;\n"));
  put(d.path / "utf16le_bom.cpp", u16(good, true, true));
  put(d.path / "utf16be_bom.cpp", u16(good, false, true));
  put(d.path / "utf16le.cpp", u16(good, true, false));
  put(d.path / "utf8_bom.cpp", "\xef\xbb\xbf" + good);
  put(d.path / "latin1.cpp", std::string("// caf\xe9 \xfc"
                                         "ber\nint latin1_marker() { return 1; }\n"));
  static const char kNul[] = "int before_nul;\n\0\0\0\0 int in_nul;\n";
  put(d.path / "nul.cpp", std::string(kNul, sizeof(kNul) - 1) + std::string(100000, '\0') + "\nint after_nul;\n");
  put(d.path / "nul_ident.cpp", std::string("int na\0me = 1;\nint nul_after;\n", 30));
  put(d.path / "all_nul.cpp", std::string(500000, '\0'));
  put(d.path / "cr_only.cpp", rep("int cr_a;\rint cr_b;\r// comment\rint cr_c;\r", 2000));
  put(d.path / "mixed.cpp", rep("int m1;\r\nint m2;\nint m3;\rint m4;\r\r\nint m5;\n\n\r\n", 2000));
  put(d.path / "cont.cpp", "#define M(x) \\\r\n  x + \\\n  x \\\r  + 1\nint after_cont = M(1);\n\\\n\\\n\\");
  put(d.path / "empty.cpp", "");
  put(d.path / "bom_only.cpp", "\xef\xbb\xbf");
  put(d.path / "one_brace.cpp", "{");
  put(d.path / "ctrl.cpp", [] {
    std::string s;
    for (int i = 0; i < 1000; ++i)
      for (int c = 1; c < 32; ++c) s += static_cast<char>(c);
    return s + "\nint ctrl_marker;\n";
  }());
  put(d.path / "cjk_line.cpp", "// " + rep("\xe4\xb8\xad", 100000) + "\nint wide_marker;\n");
  put(d.path / "unicode_idents.cpp",
      "int \xc3\xa4\xc3\xb6\xc3\xbc = 1; int \xe4\xb8\xad\xe6\x96\x87 = 2; auto \xce\xbb = [](){};\n");
  put(d.path / "data.txt", bytes(200000));
  put(d.path / "main.cpp", "#include \"latin1.cpp\"\nint enc_main() { return enc_marker(); }\n");

  Ws w(d.path);
  stress("encodings: start+index", w.startMs + w.indexMs);
  CHECK(w.exact("enc_main") == 1);
  CHECK(w.exact("latin1_marker") == 1);
  CHECK(w.symbols("enc_marker") >= 1);  // the UTF-8 / BOM'd copies
  CHECK(w.exact("ok1") == 1);           // (an unbalanced "(" in the garbage swallows what follows, as in any C++ lexer)
  CHECK(w.exact("after_cont") == 1);

  for (auto& e : fs::directory_iterator(d.path)) {
    if (e.path().filename() == ".tenfold") continue;
    std::string err;
    Document* doc = w.ed->open(e.path(), &err);
    REQUIRE(doc != nullptr);
    w.ed->gotoLocation({doc->path(), {0, 0}});
    w.ed->moveCarets(Editor::Direction::DocEnd);
    w.ed->insertText("q");
    w.ed->insertNewline();
    w.ed->undo();  // (typing and the newline may be one undo group)
    w.ed->undo();
    w.ed->moveCarets(Editor::Direction::DocStart);
    SearchOptions so;
    so.query = "marker";
    (void)w.ed->findInDocument(so);
    bool done = false;
    w.ed->gotoDefinition([&](Confidence, const std::vector<SymbolLocation>&, const std::string&) { done = true; });
    CHECK(w.ed->waitFor([&] { return done; }, 20000));
  }
  // A round trip through the editor does not change bytes it did not touch (binary, Latin-1, NULs).
  for (const char* f : {"random.cpp", "latin1.cpp", "nul.cpp", "bad_utf8.cpp"}) {
    Document* doc = w.ed->document(d.path / f);
    REQUIRE(doc != nullptr);
    std::string a, b;
    CHECK(readRegularFile(d.path / f, a));
    CHECK(doc->buffer().textForDisk().size() > 0);
    std::string err;
    CHECK(w.ed->save(*doc, &err));
    CHECK(readRegularFile(d.path / f, b));
    if (std::string(f) == "latin1.cpp" || std::string(f) == "bad_utf8.cpp") CHECK_EQ(a, b);
  }
}

TEST(pathological_symlinks_unreadable_and_odd_names) {
  TempDir d("fsodd");
  fs::path ws = d.path / "ws", out = d.path / "outside";
  put(out / "secret.cpp", "int outside_secret() { return 1; }\n");
  put(out / "dir" / "x.cpp", "int outside_dir_symbol;\n");
  put(ws / "real/a.cpp", "int fs_real_a() { return 1; }\n");
#ifndef _WIN32
  fs::create_directory(ws / "loop");
  fs::create_directory_symlink("..", ws / "loop" / "up");
  fs::create_directory_symlink("../loop", ws / "loop" / "self");
  fs::create_symlink("b", ws / "loop" / "a");
  fs::create_symlink("a", ws / "loop" / "b");
  fs::create_symlink("c.cpp", ws / "loop" / "c.cpp");
  fs::create_directory_symlink("loop2", ws / "loop2");
  fs::create_symlink(out / "secret.cpp", ws / "link_secret.cpp");
  fs::create_directory_symlink(out / "dir", ws / "link_dir");
  fs::create_directory_symlink("/", ws / "link_root");
  fs::create_symlink("/dev/null", ws / "devnull.cpp");
  fs::create_symlink("/dev/zero", ws / "devzero.cpp");
  fs::create_symlink("does/not/exist.cpp", ws / "broken.cpp");
  fs::create_symlink("real/a.cpp", ws / "link_in.cpp");
  ::mkfifo((ws / "fifo.cpp").c_str(), 0600);
  put(ws / "noread.cpp", "int noread_marker;\n");
  fs::permissions(ws / "noread.cpp", fs::perms::none);
  put(ws / "locked/inside.cpp", "int locked_inside;\n");
  fs::permissions(ws / "locked", fs::perms::none);
#endif
  const std::vector<std::string> names = {"spaces in name.cpp",
                                          "  padded  .cpp",
                                          "caf\xc3\xa9.cpp",
                                          "\xe4\xb8\xad\xe6\x96\x87.cpp",
                                          "emoji_\xf0\x9f\x98\x80.cpp",
                                          "tab\tname.cpp",
                                          "new\nline.cpp",
                                          "quote\"name.cpp",
                                          "semi;colon&pipe|dollar$.cpp",
                                          "-dash.cpp",
                                          "UPPER.CPP",
                                          "dots...cpp",
                                          ".hidden.cpp",
                                          std::string(250, 'a') + ".cpp",
                                          "back\\slash.cpp",
                                          "percent%20.cpp"};
  size_t made = 0;
  for (size_t i = 0; i < names.size(); ++i) {
    try {
      put(ws / "names" / names[i], "int name_marker_" + std::to_string(i) + "() { return 1; }\n");
      ++made;
    } catch (...) {}
  }
  for (const char* dn : {"dir with spaces", "d\xc3\xbcr", "\xe7\x9b\xae\xe5\xbd\x95", "dir.cpp", "-dash"})
    put(ws / "names" / dn / "inner.cpp", "int inner_marker() { return 1; }\n");
#ifndef _WIN32
  // a path of about 3500 bytes, built one component at a time
  fs::path cur = ws / "longpath";
  fs::create_directory(cur);
  size_t len = cur.string().size();
  try {
    for (int i = 0; len + 202 < 3500; ++i) {
      cur /= std::string(180, 'p') + std::to_string(i);
      fs::create_directory(cur);
      len += 182;
    }
    put(cur / "deep_end.cpp", "int path_max_marker() { return 1; }\n");
  } catch (const std::exception& e) { std::cerr << "    (long path stopped early: " << e.what() << ")\n"; }
#endif

  Ws w(ws);
  stress("fsodd: start+index", w.startMs + w.indexMs);
  CHECK(w.symbols("fs_real_a") >= 1);
  CHECK(w.symbols("name_marker") >= made - 4);  // names the file system or the lexer cannot hold are skipped
  CHECK(w.symbols("inner_marker") >= 4);
  CHECK(w.symbols("outside_dir_symbol") == 0);  // directory symlinks are not followed out of the workspace
  CHECK(w.symbols("loop") == 0);
  // Opening the odd ones answers promptly, never hangs, never aborts.
  for (const char* n : {"devnull.cpp", "devzero.cpp", "broken.cpp", "fifo.cpp", "noread.cpp", "locked/inside.cpp",
                        "loop/c.cpp", "loop2"}) {
    std::string err;
    auto t = clk::now();
    Document* doc = w.ed->open(ws / n, &err);
    CHECK(msSince(t) < 5000);
    if (doc) w.ed->close(doc->path());
  }
  // find in files tolerates all of it
  SearchOptions so;
  so.query = "marker";
  bool done = false;
  size_t n = 0;
  w.ed->findInWorkspace(
      so.query, so, [&](const std::vector<SearchMatch>& ms) { n += ms.size(); },
      [&](const SearchSummary&) { done = true; });
  CHECK(w.ed->waitFor([&] { return done; }, 30000));
  CHECK(n >= 5);
}

TEST(pathological_truncated_and_unbalanced_code) {
  std::mt19937 rng(11);
  std::string text = "#include <string>\n#define CHECK(x) do { if (!(x)) abort(); } while (0)\n";
  for (int i = 0; i < 6; ++i) {
    text += unit(i);
    text += "const char* kDoc" + std::to_string(i) + " = R\"doc(multi\nline \"raw\" { string\n)doc\";\n";
    text += "/* block comment " + std::to_string(i) + "\n   spanning lines */\nauto lam" + std::to_string(i) +
            " = [](int a) { return a + " + std::to_string(i) + "; };\n";
    text += "#if defined(FEATURE_" + std::to_string(i) + ")\nint feat" + std::to_string(i) +
            "() { return \"s\\\"tr\" ? 1 : 0; }\n#endif\n";
  }
  // every possible cut of the first 3000 bytes, then random cuts of the rest
  auto t = clk::now();
  size_t symbols = 0;
  for (size_t cut = 0; cut < text.size() && cut < 3000; ++cut)
    symbols += extractSymbols(std::string_view(text).substr(0, cut)).size();
  for (int i = 0; i < 300; ++i) symbols += extractSymbols(std::string_view(text).substr(0, rng() % text.size())).size();
  stress("truncated: ~3300 extractSymbols calls", msSince(t));
  CHECK(symbols > 0);
  CHECK(msSince(t) < 20000);

  const char* endings[] = {"int a = 1;\nint some_long_identifi",
                           "int a = 0x1F + 1.5e",
                           "const char* s = \"unterminated \\",
                           "const char* s = \"unterminated\nint after_string_nl;\n",
                           "char c = '",
                           "char c = '\\",
                           "const char* r = R\"delim(raw never closed\nint inside_raw;\n)delimx\" ",
                           "const char* r = R\"abcdefghijklmnopq",
                           "const char* r = R\"",
                           "int a;\n// comment without newline",
                           "int a;\n// continued \\\nstill \\",
                           "int a;\n/* never closed\nint hidden;\n",
                           "int a;\n/* almost *",
                           "#define M(x) \\\n  x + \\",
                           "#include \"never_closed",
                           "#include <vector",
                           "#if 1\nint a;\n#else\nint b;\n",
                           "#if defined(FOO) && (",
                           "int a = 1 +",
                           "template <typename T, typename U = std::vector<",
                           "class C : public Base {\n public:\n  int m(",
                           "namespace n { namespace m { struct S { void f() {",
                           "auto l = [&](int x) -> decltype(",
                           "[[nodiscard(",
                           "extern \"C",
                           "int x = a::",
                           "p->",
                           "<:",
                           "int \\u00",
                           "{",
                           "\"",
                           "/",
                           "#",
                           "\\",
                           "int a; // \xe4\xb8"};
  for (const char* e : endings) {
    std::string src = std::string("int before_cut_marker() { return 1; }\nstruct BeforeCut {};\n") + e;
    auto syms = extractSymbols(src);
    bool marker = false;
    for (auto& s : syms) marker |= s.name == "before_cut_marker";
    CHECK(marker);
  }
  // As files: the workspace indexes them and the symbols before the cut are still found.
  TempDir d("trunc");
  size_t k = 0;
  for (const char* e : endings)
    put(d.path / ("end_" + std::to_string(k++) + (k % 2 ? ".cpp" : ".h")),
        std::string("int before_cut_") + std::to_string(k) + "() { return 1; }\n" + e);
  for (int i = 0; i < 40; ++i)
    put(d.path / "cut" / ("c" + std::to_string(i) + ".cpp"), text.substr(0, rng() % text.size()));
  Ws w(d.path);
  CHECK(w.symbols("before_cut_1") >= 1);
  for (auto& e : fs::directory_iterator(d.path)) {
    if (!e.is_regular_file()) continue;
    Document* doc = w.ed->open(e.path());
    if (!doc) continue;
    w.ed->moveCarets(Editor::Direction::DocEnd);
    w.ed->insertText("x");
    bool done = false;
    w.ed->gotoDefinition([&](Confidence, const std::vector<SymbolLocation>&, const std::string&) { done = true; });
    CHECK(w.ed->waitFor([&] { return done; }, 20000));
  }
}

// The workspace is mutated from another thread (create, modify, rename, delete, file <-> directory, truncate, partial
// writes) while the editor watches it and re-indexes. Afterwards the workspace's file lists match the disk.
TEST(pathological_churn_while_indexing) {
  TempDir d("churn");
  for (int i = 0; i < 200; ++i)
    put(d.path / "src" / ("s" + std::to_string(i) + ".cpp"), "int stable_" + std::to_string(i) + "() { return 1; }\n");
  put(d.path / "src/main.cpp", "int main() { return 0; }\n");
  Editor::Options o = fastOnly();
  o.watchPollMs = 100;
  Ws w(d.path, 60000, o);
  std::atomic<bool> stop{false}, finished{false};
  std::map<std::string, int> ops;
  std::thread mut([&] {
    std::mt19937 rng(5);
    std::vector<fs::path> live;
    auto pick = [&]() -> fs::path { return live.empty() ? fs::path() : live[rng() % live.size()]; };
    std::error_code ec;
    int seq = 0;
    auto end = clk::now() + std::chrono::milliseconds(2500);
    while (clk::now() < end) {
      switch (rng() % 12) {
        case 0:
        case 1:
        case 2: {
          fs::path p = d.path / "churn" / ("d" + std::to_string(rng() % 12)) /
                       ("c" + std::to_string(seq++) + (rng() % 3 ? ".cpp" : ".h"));
          fs::create_directories(p.parent_path(), ec);
          std::ofstream(p) << "int churn_" << seq << "() { return " << seq << "; }\n";
          live.push_back(p);
          ++ops["create"];
          break;
        }
        case 3:
          if (auto p = pick(); !p.empty()) {
            std::ofstream(p) << "int churn_mod_" << rng() << "() { return 1; }\n";
            ++ops["modify"];
          }
          break;
        case 4:
          if (auto p = pick(); !p.empty()) {
            std::ofstream(p, std::ios::app) << "int appended_" << rng() << ";\n";
            ++ops["append"];
          }
          break;
        case 5:
          if (auto p = pick(); !p.empty()) {
            fs::path q = p;
            q.replace_filename(p.stem().string() + "_r" + p.extension().string());
            fs::rename(p, q, ec);
            if (!ec) live[std::find(live.begin(), live.end(), p) - live.begin()] = q;
            ++ops["rename"];
          }
          break;
        case 6:
          if (auto p = pick(); !p.empty()) {
            fs::remove(p, ec);
            live.erase(std::find(live.begin(), live.end(), p));
            ++ops["delete"];
          }
          break;
        case 7:
          if (auto p = pick(); !p.empty()) {  // replace a file by a directory
            fs::remove(p, ec);
            live.erase(std::find(live.begin(), live.end(), p));
            fs::create_directory(p, ec);
            std::ofstream(p / "inner.cpp") << "int was_a_file;\n";
            ++ops["file2dir"];
          }
          break;
        case 8: {  // replace a directory by a file
          fs::path dd = d.path / "churn" / ("d" + std::to_string(rng() % 12));
          if (fs::is_directory(dd, ec)) {
            fs::remove_all(dd, ec);
            std::ofstream(dd) << "int was_a_dir;\n";
            live.erase(std::remove_if(live.begin(), live.end(),
                                      [&](const fs::path& x) {
                                        return x.parent_path() == dd || x.string().rfind(dd.string() + "/", 0) == 0;
                                      }),
                       live.end());
            ++ops["dir2file"];
          }
        } break;
        case 9:
          if (auto p = pick(); !p.empty()) {
            fs::resize_file(p, rng() % 20, ec);
            ++ops["truncate"];
          }
          break;
        case 10: {
          fs::path a = d.path / "churn" / ("d" + std::to_string(rng() % 12));
          if (fs::is_directory(a, ec)) {
            fs::path b = a.string() + "_m";
            fs::rename(a, b, ec);
            if (!ec) {
              for (auto& x : live)
                if (x.string().rfind(a.string() + "/", 0) == 0) x = b / fs::relative(x, a);
              ++ops["rename_dir"];
            }
          }
        } break;
        default: std::this_thread::sleep_for(std::chrono::milliseconds(1)); break;
      }
    }
    finished = true;
  });
  // The editor keeps pumping and querying while the files change under it.
  size_t queries = 0;
  while (!finished) {
    w.ed->pump();
    (void)w.ed->semantic().approximateSymbols("churn");
    (void)w.ed->semantic().approximateSymbols("stable_7");
    ++queries;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  mut.join();
  size_t total = 0;
  for (auto& [k, v] : ops) total += static_cast<size_t>(v);
  stress("churn: " + std::to_string(total) + " operations, " + std::to_string(queries) + " queries", 0);
  CHECK(total > 100);

  auto onDisk = [&] {
    std::set<std::string> s;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(d.path, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
      if (ec) {
        ec.clear();
        continue;
      }
      if (it->path().string().find("/.tenfold") != std::string::npos) continue;
      if (it->is_regular_file(ec)) {
        auto e = it->path().extension().string();
        if (e == ".cpp" || e == ".h") s.insert(it->path().string());
      }
    }
    return s;
  };
  auto inWorkspace = [&] {
    std::set<std::string> s;
    for (auto& p : w.ed->workspace().sourceFiles()) s.insert(p.string());
    for (auto& p : w.ed->workspace().headerFiles()) s.insert(p.string());
    return s;
  };
  const bool consistent = w.ed->waitFor([&] { return inWorkspace() == onDisk(); }, 15000);
  if (!consistent) {
    auto a = onDisk(), b = inWorkspace();
    int shown = 0;
    for (auto& x : a)
      if (!b.count(x) && shown++ < 5) std::cerr << "    missing from workspace: " << x << "\n";
    for (auto& x : b)
      if (!a.count(x) && shown++ < 10) std::cerr << "    stale in workspace: " << x << "\n";
  }
  CHECK(consistent);
  CHECK_EQ(w.ed->semantic().fastIndex().lookup("stable_7").size(), 1u);
}

// ---------------------------------------------------------------------------------------------------------------
// Full-size runs: workspaces written by tools/stress/gen_pathological.py
// ---------------------------------------------------------------------------------------------------------------

TEST(pathological_full_bigfiles) {
  const char* env = std::getenv("TENFOLD_PATHOLOGICAL_DIR");
  if (!env) return;  // only from tools/stress/run_pathological.py
  fs::path root = fs::path(env);
  if (!fs::is_directory(root / "src")) {
    std::cerr << "    SKIP: " << root << " has no bigfiles scenario\n";
    return;
  }
  Ws w(root, 900000);
  stress("full bigfiles: start+index", w.startMs + w.indexMs);
  for (auto [f, marker] : {std::pair<const char*, const char*>{"src/million_lines.cpp", "giant_tail_marker"},
                           {"src/oneline_long.cpp", "after_long_line"},
                           {"src/oneline_100k.cpp", ""},
                           {"src/oneline_comment.cpp", "after_comment"},
                           {"src/oneline_string.cpp", "after_string"},
                           {"src/minified.cpp", "minified_main"},
                           {"data/huge.txt", "needle_4999"}}) {
    if (!fs::exists(root / f)) continue;
    giantDocumentOps(*w.ed, root / f, marker, f, 300000);
    w.ed->close(root / f);
  }
}

TEST(pathological_full_nesting) {
  const char* env = std::getenv("TENFOLD_PATHOLOGICAL_DIR");
  if (!env) return;
  fs::path root = fs::path(env);
  if (!fs::is_directory(root / "src") || !fs::exists(root / "src/braces.cpp")) return;
  Ws w(root, 900000);
  stress("full nesting: start+index", w.startMs + w.indexMs);
  for (auto f : {"braces.cpp", "parens.cpp", "namespaces.cpp", "templates.cpp", "long_ident.cpp", "macro_alts.cpp",
                 "many_decls.cpp", "comments.cpp"}) {
    Document* x = w.ed->open(root / "src" / f);
    REQUIRE(x != nullptr);
    auto t = clk::now();
    w.ed->gotoLocation({x->path(), {0, 12}});
    w.ed->selectCurrentScope();
    w.ed->selectCurrentBracketScope();
    w.ed->moveToMatchingBracket();
    w.ed->moveCarets(Editor::Direction::DocEnd);
    w.ed->insertText("x");
    CHECK(w.ed->undo());
    bool done = false;
    w.ed->gotoDefinition([&](Confidence, const std::vector<SymbolLocation>&, const std::string&) { done = true; });
    CHECK(w.ed->waitFor([&] { return done; }, 60000));
    stress(std::string("full nesting: editor ops on ") + f, msSince(t));
    w.ed->close(root / "src" / f);
  }
}
