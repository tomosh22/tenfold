// A10: fast-tier symbol search. FastIndex::search must return exactly what the historical algorithm (score every
// name, materialise every match, stable_sort, truncate) returned; SemanticEngine::workspaceSymbols must be
// asynchronous and cancel superseded queries.
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <map>
#include <mutex>
#include <random>
#include <thread>

#include "app/Editor.h"
#include "semantic/FastIndex.h"
#include "test_main.h"

using namespace tf;
namespace fs = std::filesystem;

namespace {

#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
constexpr bool kSanitized = true;
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
constexpr bool kSanitized = true;
#else
constexpr bool kSanitized = false;
#endif
#else
constexpr bool kSanitized = false;
#endif

using Clock = std::chrono::steady_clock;
double msSince(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

// ---- the oracle: the pre-A10 algorithm, kept verbatim ----------------------------------------------------------

int oracleFuzzy(std::string_view q, std::string_view c) {
  if (q.empty()) return 1;
  if (q.size() > c.size()) return 0;
  auto lower = [](char ch) { return static_cast<char>(std::tolower(static_cast<unsigned char>(ch))); };
  bool exactCI = q.size() == c.size();
  bool prefixCI = true;
  for (size_t i = 0; i < q.size(); ++i) {
    if (lower(q[i]) != lower(c[i])) {
      exactCI = false;
      prefixCI = false;
      break;
    }
  }
  if (exactCI) return q == c ? 1000 : 950;
  if (prefixCI) return 800 - static_cast<int>(std::min<size_t>(c.size() - q.size(), 200));
  int score = 0;
  size_t ci = 0;
  int prevMatch = -2;
  for (size_t qi = 0; qi < q.size(); ++qi) {
    bool found = false;
    while (ci < c.size()) {
      if (lower(q[qi]) == lower(c[ci])) {
        bool wordStart =
            ci == 0 || c[ci - 1] == '_' || c[ci - 1] == ':' ||
            (std::islower(static_cast<unsigned char>(c[ci - 1])) && std::isupper(static_cast<unsigned char>(c[ci])));
        score += 10;
        if (wordStart) score += 30;
        if (static_cast<int>(ci) == prevMatch + 1) score += 15;
        if (q[qi] == c[ci]) score += 2;
        prevMatch = static_cast<int>(ci);
        ++ci;
        found = true;
        break;
      }
      ++ci;
    }
    if (!found) return 0;
  }
  score -= static_cast<int>(std::min<size_t>(c.size(), 100)) / 4;
  return std::max(score, 1);
}

struct Hit {
  std::string file;
  size_t index;
  int score;
  bool operator==(const Hit& o) const { return file == o.file && index == o.index && score == o.score; }
};

using Files = std::vector<std::shared_ptr<const FileSymbols>>;

struct OracleItem {
  const FileSymbols* f;
  size_t idx;
  int score;
};

using ByName = std::map<std::string, std::vector<std::pair<const FileSymbols*, size_t>>>;

// Group by name; refs keep insertion order (import order of the files, symbol order inside a file).
ByName groupByName(const Files& files) {
  ByName byName;
  for (auto& f : files)
    for (size_t i = 0; i < f->symbols.size(); ++i) byName[f->symbols[i].name].push_back({f.get(), i});
  return byName;
}

std::vector<Hit> oracleSearch(const Files& files, const ByName& byName, std::string_view query, size_t limit) {
  std::string q(query);
  std::erase_if(q, [](char ch) { return ch == ' ' || ch == '\t'; });
  const bool qualified = q.find("::") != std::string::npos;
  std::vector<OracleItem> out;
  if (!qualified) {
    for (auto& [name, refs] : byName) {
      int s = oracleFuzzy(q, name);
      if (s <= 0) continue;
      for (auto& [f, i] : refs) out.push_back({f, i, s + (f->symbols[i].definition ? 5 : 0)});
    }
  } else {
    for (auto& f : files)
      for (size_t i = 0; i < f->symbols.size(); ++i) {
        int s = oracleFuzzy(q, f->symbols[i].qualified());
        if (s > 0) out.push_back({f.get(), i, s + (f->symbols[i].definition ? 5 : 0)});
      }
  }
  std::stable_sort(out.begin(), out.end(), [](const OracleItem& a, const OracleItem& b) {
    if (a.score != b.score) return a.score > b.score;
    return a.f->symbols[a.idx].name < b.f->symbols[b.idx].name;
  });
  if (out.size() > limit) out.resize(limit);
  std::vector<Hit> r;
  for (auto& o : out) r.push_back({o.f->file.generic_string(), o.idx, o.score});
  return r;
}

std::vector<Hit> toHits(const std::vector<FastIndex::Match>& ms) {
  std::vector<Hit> r;
  for (auto& m : ms) r.push_back({m.file->file.generic_string(), m.index, m.score});
  return r;
}

// ---- synthetic index --------------------------------------------------------------------------------------------

Files makeSynthetic(size_t nFiles, size_t perFile, uint32_t seed) {
  static const char* words[] = {"node", "get",   "set",  "transform", "vector",  "render", "server", "pos",
                                "size", "scene", "tree", "update",    "process", "mesh",   "shape",  "3d",
                                "2d",   "id",    "name", "global",    "local",   "Rid",    "Object", "Rect"};
  std::mt19937 rng(seed);
  auto pick = [&](size_t n) { return static_cast<size_t>(rng() % n); };
  auto makeName = [&] {
    std::string s;
    int parts = 1 + static_cast<int>(pick(3));
    int style = static_cast<int>(pick(4));
    for (int p = 0; p < parts; ++p) {
      std::string w = words[pick(sizeof(words) / sizeof(words[0]))];
      if (style == 1 && p > 0) s += '_';
      if (style == 2 || style == 3) w[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(w[0])));
      s += w;
    }
    if (pick(10) == 0) s += std::to_string(pick(100));
    return s;
  };
  std::vector<std::string> namePool, scopePool;
  for (int i = 0; i < 60000; ++i) namePool.push_back(makeName());
  scopePool.push_back("");
  for (int i = 0; i < 400; ++i) {
    std::string sc = makeName();
    if (pick(3) == 0) sc = makeName() + "::" + sc;
    scopePool.push_back(sc);
  }
  Files files;
  for (size_t f = 0; f < nFiles; ++f) {
    auto fsym = std::make_shared<FileSymbols>();
    fsym->file = fs::path("/synthetic") / ("f" + std::to_string(f) + ".cpp");
    for (size_t i = 0; i < perFile; ++i) {
      Symbol s;
      // Zipf-ish: low pool indices repeat a lot (many refs per name, equal-score ties inside one name).
      size_t ix = pick(namePool.size());
      if (pick(3) == 0) ix = pick(200);
      s.name = namePool[ix];
      s.scope = scopePool[pick(pick(4) == 0 ? scopePool.size() : 40)];
      s.kind = SymbolKind::Function;
      s.line = static_cast<int>(i);
      s.definition = pick(2) == 0;
      fsym->symbols.push_back(std::move(s));
    }
    files.push_back(std::move(fsym));
  }
  return files;
}

// Random query derived from an existing symbol, with assorted perturbations.
std::string randomQuery(const Files& files, std::mt19937& rng) {
  auto pick = [&](size_t n) { return static_cast<size_t>(rng() % n); };
  const FileSymbols& f = *files[pick(files.size())];
  const Symbol& s = f.symbols[pick(f.symbols.size())];
  std::string base = s.name;
  int kind = static_cast<int>(pick(9));
  std::string q;
  switch (kind) {
    case 0: q = base.substr(0, 1 + pick(std::min<size_t>(base.size(), 4))); break;  // short prefix
    case 1: {                                                                       // substring
      size_t a = pick(base.size());
      q = base.substr(a, 1 + pick(5));
      break;
    }
    case 2: {  // subsequence (skip characters)
      for (char ch : base)
        if (pick(2) == 0) q += ch;
      break;
    }
    case 3: q = base; break;  // exact
    case 4: {                 // qualified
      std::string sc = s.scope.empty() ? std::string("x") : s.scope;
      q = sc.substr(0, 1 + pick(sc.size())) + "::" + base.substr(0, pick(base.size() + 1));
      break;
    }
    case 5: q = "::" + base.substr(0, pick(3)); break;
    case 6: {  // random letters, mostly no match
      size_t n = 1 + pick(6);
      for (size_t i = 0; i < n; ++i) q += static_cast<char>('a' + pick(26));
      break;
    }
    case 7: q = base.substr(0, 1 + pick(3)) + " " + base.substr(base.size() / 2); break;  // whitespace stripped
    default: q = ""; break;                                                               // empty
  }
  // Case perturbation.
  if (pick(3) == 0)
    for (auto& ch : q) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
  else if (pick(3) == 0)
    for (auto& ch : q) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  return q;
}

void compareAgainstOracle(const Files& files, int queries, uint32_t seed, const char* what) {
  FastIndex idx;
  idx.importFiles(files);
  const ByName byName = groupByName(files);
  std::mt19937 rng(seed);
  const size_t limits[] = {1, 7, 200, 200, 200, 1500, 1000000};
  int mismatches = 0;
  for (int qi = 0; qi < queries && mismatches < 5; ++qi) {
    std::string q = randomQuery(files, rng);
    size_t limit = limits[rng() % (sizeof(limits) / sizeof(limits[0]))];
    auto want = oracleSearch(files, byName, q, limit);
    for (int threads : {1, 4}) {
      idx.setSearchThreads(threads);
      auto got = toHits(idx.search(q, limit));
      if (!(got == want)) {
        ++mismatches;
        tftest::fail(__FILE__, __LINE__,
                     std::string(what) + ": mismatch for query '" + q + "' limit " + std::to_string(limit) +
                         " threads " + std::to_string(threads) + " (got " + std::to_string(got.size()) +
                         " hits, want " + std::to_string(want.size()) + ")");
        break;
      }
    }
  }
}

}  // namespace

TEST(symsearch_fuzzy_score_matches_historical_implementation) {
  std::mt19937 rng(7);
  const char alphabet[] = "abcXYZ_:09 .\x80\xe9";
  for (int i = 0; i < 200000; ++i) {
    std::string q, c;
    for (size_t n = rng() % 6; n > 0; --n) q += alphabet[rng() % (sizeof(alphabet) - 1)];
    for (size_t n = rng() % 12; n > 0; --n) c += alphabet[rng() % (sizeof(alphabet) - 1)];
    if (rng() % 3 == 0) c = q + c;  // prefix hits
    if (FastIndex::fuzzyScore(q, c) != oracleFuzzy(q, c)) {
      tftest::fail(__FILE__, __LINE__, "fuzzyScore differs for '" + q + "' vs '" + c + "'");
      return;
    }
  }
}

TEST(symsearch_ranking_equivalent_synthetic_200k) {
  Files files = makeSynthetic(2000, 100, 12345);
  size_t total = 0;
  for (auto& f : files) total += f->symbols.size();
  CHECK_EQ(total, size_t(200000));
  compareAgainstOracle(files, kSanitized ? 20 : 100, 99, "synthetic");
}

TEST(symsearch_ranking_equivalent_small_index_many_ties) {
  // Few distinct names, many refs: stresses the equal-score/equal-name insertion-order tie break and a limit
  // that cuts through a group of equal entries.
  Files files = makeSynthetic(40, 60, 4242);
  compareAgainstOracle(files, 300, 5, "small");
}

TEST(symsearch_ranking_equivalent_sample_project) {
  const fs::path root = fs::path(TENFOLD_SOURCE_DIR) / "examples/sample_project";
  std::vector<fs::path> paths;
  for (auto& e : fs::recursive_directory_iterator(root)) {
    auto ext = e.path().extension().string();
    if (e.is_regular_file() && (ext == ".cpp" || ext == ".h" || ext == ".hpp" || ext == ".cc"))
      paths.push_back(e.path());
  }
  std::sort(paths.begin(), paths.end());
  REQUIRE(!paths.empty());
  FastIndex built;
  built.indexFiles(paths, 2);
  Files files = built.exportFiles();
  std::sort(files.begin(), files.end(), [](auto& a, auto& b) { return a->file < b->file; });
  size_t symbols = 0;
  for (auto& f : files) symbols += f->symbols.size();
  REQUIRE(symbols > 20);
  compareAgainstOracle(files, 400, 31337, "sample_project");
}

TEST(symsearch_cancellation_stops_scan_and_returns_empty) {
  Files files = makeSynthetic(1000, 100, 1);
  FastIndex idx;
  idx.importFiles(files);
  std::atomic<int> polls{0};
  auto r = idx.search("n", 200, [&] {
    ++polls;
    return true;
  });
  CHECK(r.empty());
  CHECK(polls.load() >= 1);
  // Cancelled after a few blocks: also empty, and not every block was visited.
  std::atomic<int> n{0};
  idx.setSearchThreads(1);
  r = idx.search("n", 200, [&] { return ++n > 3; });
  CHECK(r.empty());
  CHECK(n.load() < 20);
  // A predicate that never fires changes nothing.
  CHECK(toHits(idx.search("node", 50, [] { return false; })) == toHits(idx.search("node", 50)));
}

TEST(symsearch_index_mutations_invalidate_prefilter_table) {
  Files files = makeSynthetic(20, 50, 77);
  FastIndex idx;
  idx.importFiles(files);
  auto before = toHits(idx.search("get", 30));
  // Remove and re-add a file with a brand-new symbol; searches must see every mutation immediately.
  auto extra = std::make_shared<FileSymbols>();
  extra->file = "/synthetic/extra.cpp";
  Symbol s;
  s.name = "zzUniqueMarker";
  extra->symbols.push_back(s);
  idx.importFiles({extra});
  auto hit = idx.search("zzUniqueM", 5);
  REQUIRE(hit.size() == 1u);
  CHECK_EQ(hit[0].sym().name, "zzUniqueMarker");
  idx.removeFile("/synthetic/extra.cpp");
  CHECK(idx.search("zzUniqueM", 5).empty());
  CHECK(toHits(idx.search("get", 30)) == before);
  idx.updateFile("/synthetic/live.cpp", "int liveSymbolHere() { return 1; }\n");
  CHECK(!idx.search("liveSymbolH", 5).empty());
  idx.clear();
  CHECK(idx.search("get", 30).empty());
}

TEST(symsearch_concurrent_searches_and_mutations) {
  Files files = makeSynthetic(300, 100, 5);
  FastIndex idx;
  idx.importFiles(files);
  std::atomic<bool> stop{false};
  std::atomic<int> bad{0};
  std::vector<std::thread> readers;
  for (int t = 0; t < 3; ++t)
    readers.emplace_back([&, t] {
      std::mt19937 rng(100 + t);
      while (!stop) {
        auto r = idx.search(randomQuery(files, rng), 50);
        for (size_t i = 1; i < r.size(); ++i)
          if (r[i - 1].score < r[i].score) ++bad;
        // glibc rwlocks prefer readers: back-to-back readers would starve the writer below.
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
    });
  for (int i = 0; i < 40; ++i) {
    idx.updateFile("/synthetic/churn" + std::to_string(i % 5) + ".cpp",
                   "int churn_" + std::to_string(i) + "() { return 0; }\n");
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  stop = true;
  for (auto& th : readers) th.join();
  CHECK_EQ(bad.load(), 0);
}

// ---- engine / editor ---------------------------------------------------------------------------------------------

namespace {
struct EngineFixture {
  Editor ed;
  bool ok = false;
  static Editor::Options opts() {
    Editor::Options o;
    o.semantic.useClangd = false;
    o.semantic.persistFastIndex = false;
    return o;
  }
  EngineFixture() : ed(fs::path(TENFOLD_SOURCE_DIR) / "examples/sample_project", opts()) {
    ok = ed.start();
    if (ok) ed.semantic().waitForFastIndex(30000);
  }
};
}  // namespace

TEST(symsearch_engine_delivers_correct_result_asynchronously) {
  EngineFixture f;
  REQUIRE(f.ok);
  std::mutex mu;
  bool done = false;
  std::vector<SymbolLocation> got;
  std::string err;
  f.ed.findSymbols("Vec2", [&](Confidence c, const std::vector<SymbolLocation>& l, const std::string& e) {
    (void)c;
    got = l;
    err = e;
    done = true;
  });
  CHECK(!done);  // never inline
  f.ed.waitFor([&] { return done; }, 10000);
  REQUIRE(done);
  auto want = f.ed.semantic().approximateSymbols("Vec2");
  REQUIRE(got.size() == want.size());
  for (size_t i = 0; i < got.size(); ++i) {
    CHECK_EQ(got[i].name, want[i].name);
    CHECK(got[i].loc.file == want[i].loc.file);
    CHECK_EQ(got[i].loc.pos.line, want[i].loc.pos.line);
    CHECK(got[i].confidence == Confidence::Approximate);
  }
  CHECK(!err.empty());  // no clangd: approximate answer + "clangd not running"
}

TEST(symsearch_engine_superseded_queries_are_never_delivered) {
  EngineFixture f;
  REQUIRE(f.ok);
  // Many overlapping queries from several threads (the engine API is thread-safe), then a final one from here.
  std::mutex mu;
  std::map<std::string, int> deliveries;  // query -> times delivered
  std::vector<std::string> order;
  auto issue = [&](const std::string& q) {
    f.ed.semantic().workspaceSymbols(q, [&, q](Confidence, std::vector<SymbolLocation> l, const std::string&) {
      std::lock_guard<std::mutex> lk(mu);
      ++deliveries[q];
      order.push_back(q);
      auto want = f.ed.semantic().approximateSymbols(q);
      if (l.size() != want.size()) tftest::fail(__FILE__, __LINE__, "wrong result size for " + q);
    });
  };
  std::vector<std::thread> threads;
  for (int t = 0; t < 4; ++t)
    threads.emplace_back([&, t] {
      for (int i = 0; i < 60; ++i) issue("q" + std::to_string(t) + "_" + std::to_string(i) + (i % 2 ? "Vec" : "a"));
    });
  for (auto& th : threads) th.join();
  issue("FINAL_Vec2");
  for (int i = 0; i < 2000; ++i) {
    {
      std::lock_guard<std::mutex> lk(mu);
      if (deliveries.count("FINAL_Vec2")) break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(100));  // let any (wrongly) late callbacks arrive
  std::lock_guard<std::mutex> lk(mu);
  CHECK_EQ(deliveries["FINAL_Vec2"], 1);
  for (auto& [q, n] : deliveries) CHECK(n == 1);
  // The latest query is the last thing delivered; nothing superseded arrives after it.
  REQUIRE(!order.empty());
  CHECK_EQ(order.back(), std::string("FINAL_Vec2"));
}

TEST(symsearch_engine_after_stop_reports_error_inline) {
  EngineFixture f;
  REQUIRE(f.ok);
  f.ed.semantic().stop();
  bool called = false;
  std::string err;
  f.ed.semantic().workspaceSymbols("Vec2", [&](Confidence, std::vector<SymbolLocation>, const std::string& e) {
    called = true;
    err = e;
  });
  CHECK(called);
  CHECK(!err.empty());
}

TEST(symsearch_find_symbols_returns_before_the_fast_tier_is_computed) {
  EngineFixture f;
  REQUIRE(f.ok);
  // Make the fast tier expensive: a 200k-symbol synthetic index next to the sample project.
  f.ed.semantic().fastIndex().importFiles(makeSynthetic(2000, 100, 3));
  auto& sem = f.ed.semantic();
  sem.fastIndex().setSearchThreads(1);  // sequential scan: the sync cost is well above the call overhead
  double syncMs = 1e9, callMs = 1e9;
  for (int i = 0; i < 3; ++i) {
    auto t = Clock::now();
    auto r = sem.approximateSymbols("n");
    syncMs = std::min(syncMs, msSince(t));
    CHECK(!r.empty());
  }
  int delivered = 0;
  for (int i = 0; i < 5; ++i) {
    auto t = Clock::now();
    f.ed.findSymbols("n", [&](Confidence, const std::vector<SymbolLocation>&, const std::string&) { ++delivered; });
    callMs = std::min(callMs, msSince(t));
    f.ed.waitFor([&] { return delivered > i; }, 20000);
  }
  CHECK_EQ(delivered, 5);
  std::cerr << "    sync fast tier " << syncMs << " ms, findSymbols call " << callMs << " ms\n";
  CHECK(callMs < 5.0);
  CHECK(callMs * 2 < syncMs);
}

TEST(symsearch_engine_rapid_fire_on_a_big_index_skips_stale_queries) {
  EngineFixture f;
  REQUIRE(f.ok);
  f.ed.semantic().fastIndex().importFiles(makeSynthetic(2000, 100, 8));
  f.ed.semantic().fastIndex().setSearchThreads(1);  // each scan takes long enough to be overtaken
  std::mutex mu;
  std::vector<std::string> order;
  const int kQueries = 100;
  for (int i = 0; i < kQueries; ++i) {
    std::string q = i + 1 == kQueries ? "node" : "n" + std::string(1, static_cast<char>('a' + i % 26));
    f.ed.semantic().workspaceSymbols(q, [&, q](Confidence, std::vector<SymbolLocation>, const std::string&) {
      std::lock_guard<std::mutex> lk(mu);
      order.push_back(q);
    });
  }
  for (int i = 0; i < 4000; ++i) {
    {
      std::lock_guard<std::mutex> lk(mu);
      if (!order.empty() && order.back() == "node") break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  std::lock_guard<std::mutex> lk(mu);
  REQUIRE(!order.empty());
  CHECK_EQ(order.back(), std::string("node"));
  CHECK_EQ(std::count(order.begin(), order.end(), std::string("node")), 1);
  CHECK(order.size() < 20u);  // the other 99 were superseded long before they could finish
}

TEST(symsearch_tie_order_follows_add_order_even_after_readd) {
  auto mk = [](const char* path) {
    auto f = std::make_shared<FileSymbols>();
    f->file = path;
    for (const char* scope : {"ns::A", ""}) {
      Symbol s;
      s.name = "tieSymbol";
      s.scope = scope;
      s.line = 1;
      f->symbols.push_back(s);
    }
    return f;
  };
  auto a = mk("/t/a.cpp"), b = mk("/t/b.cpp"), c = mk("/t/c.cpp");
  FastIndex idx;
  idx.importFiles({a, b, c});
  auto order = [&](const char* q) {
    std::string r;
    for (auto& m : idx.search(q, 10)) r += m.file->file.filename().string() + std::to_string(m.index) + " ";
    return r;
  };
  CHECK_EQ(order("tieSymbol"), std::string("a.cpp0 a.cpp1 b.cpp0 b.cpp1 c.cpp0 c.cpp1 "));
  CHECK_EQ(order("ns::tieSymbol"), std::string("a.cpp0 b.cpp0 c.cpp0 "));
  // Re-adding a file appends its refs: it becomes the newest.
  idx.removeFile("/t/a.cpp");
  idx.importFiles({a});
  CHECK_EQ(order("tieSymbol"), std::string("b.cpp0 b.cpp1 c.cpp0 c.cpp1 a.cpp0 a.cpp1 "));
  CHECK_EQ(order("ns::tieSymbol"), std::string("b.cpp0 c.cpp0 a.cpp0 "));
  idx.setSearchThreads(1);
  CHECK_EQ(order("ns::tieSymbol"), std::string("b.cpp0 c.cpp0 a.cpp0 "));
  // A file that only gains a symbol of a new name is found by plain and qualified queries right away.
  auto d = std::make_shared<FileSymbols>();
  d->file = "/t/d.cpp";
  Symbol s;
  s.name = "freshName";
  s.scope = "zz::Q";
  d->symbols.push_back(s);
  idx.importFiles({d});
  CHECK_EQ(order("freshN"), std::string("d.cpp0 "));
  CHECK_EQ(order("zz::Q::fresh"), std::string("d.cpp0 "));
  idx.removeFile("/t/d.cpp");
  CHECK_EQ(order("freshN"), std::string(""));
  CHECK_EQ(order("zz::Q::fresh"), std::string(""));
}
