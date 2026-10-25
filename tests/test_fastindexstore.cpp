#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <thread>

#include "core/Platform.h"
#include "semantic/FastIndex.h"
#include "semantic/FastIndexStore.h"
#include "semantic/SemanticEngine.h"
#include "workspace/Workspace.h"
#include "test_main.h"

using namespace tf;
namespace fs = std::filesystem;

namespace {
struct TempDir {
  fs::path path;
  TempDir() {
    static int n = 0;
    path = fs::temp_directory_path() / ("tenfold_fis_" + std::to_string(tf::currentProcessId()) + "_" + std::to_string(n++));
    fs::remove_all(path);
    fs::create_directories(path);
  }
  ~TempDir() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
};

void write(const fs::path& p, const std::string& text) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary) << text;
}

std::vector<fs::path> makeWorkspace(const fs::path& root, int n) {
  std::vector<fs::path> files;
  for (int i = 0; i < n; ++i) {
    auto p = root / ("src/f" + std::to_string(i) + ".cpp");
    write(p, "#include <vector>\n#include \"x" + std::to_string(i) + ".h\"\nnamespace ns" + std::to_string(i % 3) +
                 " {\nclass Widget" + std::to_string(i) + " { public: void run(); int value; };\n"
                 "template <class T> T helper" + std::to_string(i) + "(T t) { return t; }\n}\n"
                 "void shared_name() {}\n");
    files.push_back(p);
  }
  return files;
}

// Bump mtime by a whole number of seconds so it differs regardless of filesystem timestamp granularity.
void touch(const fs::path& p, int seconds) {
  fs::last_write_time(p, fs::last_write_time(p) + std::chrono::seconds(seconds));
}

std::string describe(const FastIndex::Match& m) {
  return m.file->file.generic_string() + ":" + m.sym().qualified() + ":" + std::to_string(m.sym().line) + ":" +
         std::to_string(m.sym().col) + ":" + symbolKindName(m.sym().kind) + ":" + std::to_string(m.score);
}

std::vector<std::string> describeAll(const std::vector<FastIndex::Match>& ms) {
  std::vector<std::string> out;
  for (auto& m : ms) out.push_back(describe(m));
  std::sort(out.begin(), out.end());
  return out;
}
}  // namespace

TEST(fastindexstore_roundtrip_identical_lookups) {
  TempDir td;
  auto files = makeWorkspace(td.path, 12);
  FastIndex a;
  a.indexFiles(files, 2);
  auto store = td.path / ".tenfold/fastindex.bin";
  std::string err;
  REQUIRE(FastIndexStore::save(store, a.exportFiles(), &err));
  CHECK(!fs::exists(store.string() + ".tmp." + std::to_string(tf::currentProcessId())));

  auto loaded = FastIndexStore::load(store, &err);
  REQUIRE(loaded.has_value());
  CHECK_EQ(loaded->size(), a.fileCount());
  FastIndex b;
  b.importFiles(std::move(*loaded));
  CHECK_EQ(b.fileCount(), a.fileCount());
  CHECK_EQ(b.symbolCount(), a.symbolCount());
  for (const char* name : {"shared_name", "Widget3", "run", "helper7", "ns1", "nothing_here"})
    CHECK(describeAll(a.lookup(name)) == describeAll(b.lookup(name)));
  for (const char* q : {"wid", "ns1::Wid", "hlp", "run", "sn", "zzz"})
    CHECK(describeAll(a.search(q)) == describeAll(b.search(q)));
  auto ia = a.allIncludes(), ib = b.allIncludes();
  std::sort(ia.begin(), ia.end());
  std::sort(ib.begin(), ib.end());
  CHECK(ia == ib);
  auto fa = a.fileSymbols(files[4]), fb = b.fileSymbols(files[4]);
  REQUIRE(fa && fb);
  CHECK_EQ(fa->mtime, fb->mtime);
  CHECK_EQ(fa->size, fb->size);
  CHECK(fa->mtime != 0);
}

TEST(fastindexstore_stale_file_relexed_only) {
  TempDir td;
  auto files = makeWorkspace(td.path, 10);
  auto store = td.path / ".tenfold/fastindex.bin";
  {
    FastIndex cold;
    auto st = cold.indexFilesCached(files, {}, 2);
    CHECK_EQ(st.relexed, size_t(10));
    CHECK_EQ(st.reused, size_t(0));
    REQUIRE(FastIndexStore::save(store, cold.exportFiles()));
  }
  {  // nothing changed
    FastIndex warm;
    auto st = warm.indexFilesCached(files, *FastIndexStore::load(store), 2);
    CHECK_EQ(st.relexed, size_t(0));
    CHECK_EQ(st.reused, size_t(10));
    CHECK_EQ(st.dropped, size_t(0));
    CHECK_EQ(warm.fileCount(), size_t(10));
  }
  {  // only the mtime of one file changes -> exactly that file is re-lexed
    touch(files[2], 5);
    FastIndex warm;
    auto st = warm.indexFilesCached(files, *FastIndexStore::load(store), 2);
    CHECK_EQ(st.relexed, size_t(1));
    CHECK_EQ(st.reused, size_t(9));
    CHECK_EQ(warm.lookup("Widget2").size(), size_t(1));
  }
  {  // changed content, and a new file
    write(files[5], "int brand_new_symbol();\n");
    auto added = td.path / "src/added.cpp";
    write(added, "struct Added {};\n");
    auto list = files;
    list.push_back(added);
    FastIndex warm;
    auto st = warm.indexFilesCached(list, *FastIndexStore::load(store), 2);
    CHECK_EQ(st.relexed, size_t(3));  // touched f2 (store is older than the touch), f5, added
    CHECK_EQ(st.reused, size_t(8));
    CHECK_EQ(warm.fileCount(), size_t(11));
    CHECK_EQ(warm.lookup("brand_new_symbol").size(), size_t(1));
    CHECK_EQ(warm.lookup("Widget5").size(), size_t(0));
    CHECK_EQ(warm.lookup("Added").size(), size_t(1));
    CHECK_EQ(warm.lookup("Widget2").size(), size_t(1));
  }
}

TEST(fastindexstore_deleted_file_dropped) {
  TempDir td;
  auto files = makeWorkspace(td.path, 6);
  auto store = td.path / "fastindex.bin";
  FastIndex cold;
  cold.indexFiles(files, 2);
  REQUIRE(FastIndexStore::save(store, cold.exportFiles()));
  fs::remove(files[1]);
  {  // removed from the workspace list
    auto list = files;
    list.erase(list.begin() + 1);
    FastIndex warm;
    auto st = warm.indexFilesCached(list, *FastIndexStore::load(store), 2);
    CHECK_EQ(st.dropped, size_t(1));
    CHECK_EQ(st.relexed, size_t(0));
    CHECK_EQ(warm.fileCount(), size_t(5));
    CHECK_EQ(warm.lookup("Widget1").size(), size_t(0));
  }
  {  // still listed but gone from disk
    FastIndex warm;
    auto st = warm.indexFilesCached(files, *FastIndexStore::load(store), 2);
    CHECK_EQ(warm.fileCount(), size_t(5));
    CHECK_EQ(warm.lookup("Widget1").size(), size_t(0));
    CHECK_EQ(st.relexed, size_t(0));
  }
}

TEST(fastindexstore_corrupt_truncated_and_version_rejected) {
  TempDir td;
  auto files = makeWorkspace(td.path, 8);
  FastIndex a;
  a.indexFiles(files, 2);
  std::string good = FastIndexStore::serialize(a.exportFiles());
  std::string err;
  REQUIRE(FastIndexStore::deserialize(good.data(), good.size(), &err).has_value());

  // Every truncation point near the header and a spread across the body.
  size_t checked = 0;
  for (size_t cut = 0; cut < good.size(); cut += (cut < 128 ? 1 : 97)) {
    CHECK(!FastIndexStore::deserialize(good.data(), cut, &err).has_value());
    ++checked;
  }
  CHECK(checked > 100);
  CHECK(!FastIndexStore::deserialize(good.data(), good.size() - 1, &err).has_value());

  // Trailing garbage.
  std::string longer = good + "x";
  CHECK(!FastIndexStore::deserialize(longer.data(), longer.size(), &err).has_value());

  // Flipped magic / format version / extractor version.
  for (size_t off : {size_t(0), size_t(8), size_t(12)}) {
    std::string bad = good;
    bad[off] = static_cast<char>(bad[off] ^ 0x5A);
    CHECK(!FastIndexStore::deserialize(bad.data(), bad.size(), &err).has_value());
  }
  {
    std::string bad = good;
    bad[8] = static_cast<char>(FastIndexStore::kFormatVersion + 1);  // plausible "future" version
    CHECK(!FastIndexStore::deserialize(bad.data(), bad.size(), &err).has_value());
    CHECK(err.find("version") != std::string::npos);
  }
  // Single-byte flips anywhere in the payload (checksum) and random garbage must never crash.
  for (size_t i = 48; i < good.size(); i += 37) {
    std::string bad = good;
    bad[i] = static_cast<char>(bad[i] ^ 0xFF);
    CHECK(!FastIndexStore::deserialize(bad.data(), bad.size(), &err).has_value());
  }
  uint32_t seed = 12345;
  for (int round = 0; round < 200; ++round) {
    std::string junk = good;
    for (int k = 0; k < 4; ++k) {
      seed = seed * 1664525u + 1013904223u;
      junk[48 + (seed >> 8) % (junk.size() - 48)] = static_cast<char>(seed >> 24);
    }
    (void)FastIndexStore::deserialize(junk.data(), junk.size(), &err);  // must not crash
  }
  CHECK(!FastIndexStore::deserialize("", 0, &err).has_value());
  CHECK(!FastIndexStore::load(td.path / "does_not_exist.bin", &err).has_value());
}

TEST(fastindexstore_absurd_counts_rejected) {
  TempDir td;
  auto files = makeWorkspace(td.path, 3);
  FastIndex a;
  a.indexFiles(files, 1);
  std::string good = FastIndexStore::serialize(a.exportFiles());
  uint32_t huge = 0xFFFFFFF0u;
  std::string err;
  std::string bad = good;
  std::memcpy(&bad[36], &huge, 4);  // stringCount (checksum is over the payload only, so this reaches the parser)
  CHECK(!FastIndexStore::deserialize(bad.data(), bad.size(), &err).has_value());
  bad = good;
  std::memcpy(&bad[32], &huge, 4);  // fileCount
  CHECK(!FastIndexStore::deserialize(bad.data(), bad.size(), &err).has_value());
}

TEST(fastindexstore_corrupt_file_means_full_rebuild) {
  TempDir td;
  auto files = makeWorkspace(td.path, 6);
  auto store = td.path / ".tenfold/fastindex.bin";
  FastIndex a;
  a.indexFiles(files, 2);
  REQUIRE(FastIndexStore::save(store, a.exportFiles()));
  auto size = fs::file_size(store);
  fs::resize_file(store, size / 2);  // truncated on disk
  std::string err;
  auto loaded = FastIndexStore::load(store, &err);
  CHECK(!loaded.has_value());
  FastIndex b;
  auto st = b.indexFilesCached(files, loaded ? std::move(*loaded) : FastIndexStore::Entries{}, 2);
  CHECK_EQ(st.relexed, size_t(6));
  CHECK_EQ(b.fileCount(), size_t(6));
}

TEST(fastindexstore_generation_tracks_mutation) {
  FastIndex a;
  uint64_t g0 = a.generation();
  a.updateFile("/tmp/zz_gen.cpp", "int gen_symbol();");
  CHECK(a.generation() != g0);
  uint64_t g1 = a.generation();
  (void)a.lookup("gen_symbol");
  CHECK_EQ(a.generation(), g1);
  a.removeFile("/tmp/zz_gen.cpp");
  CHECK(a.generation() != g1);
}

TEST(fastindexstore_engine_warm_start_and_shutdown_save) {
  TempDir td;
  makeWorkspace(td.path, 5);
  auto run = [&](size_t* relexed, size_t* reused, size_t* files) {
    Workspace ws(td.path);
    ws.scan();
    SemanticEngine::Options o;
    o.useClangd = false;
    SemanticEngine eng(ws, o);
    REQUIRE(eng.start());
    REQUIRE(eng.waitForFastIndex(10000));
    auto st = eng.status();
    *relexed = st.fastRelexed;
    *reused = st.fastReused;
    *files = st.fastFiles;
    eng.stop();
  };
  size_t relexed = 0, reused = 0, files = 0;
  run(&relexed, &reused, &files);
  CHECK_EQ(relexed, size_t(5));
  CHECK_EQ(files, size_t(5));
  CHECK(fs::exists(td.path / ".tenfold/fastindex.bin"));
  run(&relexed, &reused, &files);
  CHECK_EQ(relexed, size_t(0));
  CHECK_EQ(reused, size_t(5));
  touch(td.path / "src/f3.cpp", 7);
  fs::remove(td.path / "src/f0.cpp");
  run(&relexed, &reused, &files);
  CHECK_EQ(relexed, size_t(1));
  CHECK_EQ(reused, size_t(3));
  CHECK_EQ(files, size_t(4));
  run(&relexed, &reused, &files);  // the previous run wrote the updated store back at shutdown
  CHECK_EQ(relexed, size_t(0));
  CHECK_EQ(reused, size_t(4));
}
