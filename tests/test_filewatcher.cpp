// File watching: the raw FileWatcher (events, debounce, recursion) and its
// wiring into the Editor (fast index, workspace lists, navigation history,
// open documents). None of these need clangd except the last one.
#include <atomic>
#include <chrono>
#include <fstream>
#include <mutex>
#include <thread>

#include "app/Editor.h"
#include "core/Platform.h"
#include "test_main.h"
#include "workspace/FileWatcher.h"

using namespace tf;
namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {

struct TempDir {
  fs::path path;
  TempDir() {
    static std::atomic<int> n{0};
    path = fs::temp_directory_path() /
           ("tenfold_fw_" + std::to_string(tf::currentProcessId()) + "_" + std::to_string(n++));
    fs::remove_all(path);
    fs::create_directories(path);
    std::error_code ec;
    path = fs::weakly_canonical(path, ec);
  }
  ~TempDir() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
};

void writeFile(const fs::path& p, const std::string& text) {
  fs::create_directories(p.parent_path());
  std::ofstream out(p, std::ios::binary | std::ios::trunc);
  out << text;
}

template <class Pred>
bool waitUntil(Pred pred, int timeoutMs) {
  auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
  while (std::chrono::steady_clock::now() < deadline) {
    if (pred()) return true;
    std::this_thread::sleep_for(5ms);
  }
  return pred();
}

struct Collector {
  std::mutex mu;
  std::vector<FileWatcher::Event> events;
  FileWatcher::Callback callback() {
    return [this](FileWatcher::Event e) {
      std::lock_guard<std::mutex> lk(mu);
      events.push_back(std::move(e));
    };
  }
  std::vector<FileWatcher::Event> snapshot() {
    std::lock_guard<std::mutex> lk(mu);
    return events;
  }
  size_t countFor(const fs::path& p, FileWatcher::Event::Kind k) {
    size_t n = 0;
    for (auto& e : snapshot())
      if (e.path == p && e.kind == k) ++n;
    return n;
  }
  bool has(const fs::path& p, FileWatcher::Event::Kind k) { return countFor(p, k) > 0; }
};

bool ignoreBuild(const fs::path& d) { return d.filename() == "build" || d.filename() == ".tenfold"; }

Editor::Options fastOnly() {
  Editor::Options o;
  o.semantic.useClangd = false;
  return o;
}

std::string backendName(bool polling) { return polling ? "polling" : "native"; }

// A small C++ workspace in a temp dir, started fast-only.
struct EditorFixture {
  TempDir dir;
  std::unique_ptr<Editor> ed;
  bool ok = false;
  int lat = 1000;  // how long a change may take to show up: tight when event driven, pollMs-based otherwise
  explicit EditorFixture(Editor::Options opt = fastOnly(), bool polling = false) {
    opt.watchForcePolling = polling;
    opt.watchPollMs = 100;
    writeFile(dir.path / "src/a.h", "struct Alpha { int one(); };\n");
    writeFile(dir.path / "src/a.cpp", "#include \"a.h\"\nint Alpha::one() { return 1; }\n");
    ed = std::make_unique<Editor>(dir.path, std::move(opt));
    std::string err;
    ok = ed->start(&err);
    if (!ok) std::cerr << "    editor start: " << err << "\n";
    if (ed->fileWatcher() && !ed->fileWatcher()->eventDriven()) lat = 4000;
  }
  bool symbolAppears(const std::string& name, int ms = -1) {
    if (ms < 0 || lat > ms) ms = lat;
    return ed->waitFor([&] { return !ed->semantic().fastIndex().lookup(name).empty(); }, ms);
  }
  bool symbolGone(const std::string& name, int ms = -1) {
    if (ms < 0 || lat > ms) ms = lat;
    return ed->waitFor([&] { return ed->semantic().fastIndex().lookup(name).empty(); }, ms);
  }
};

bool hasEntryFor(Editor& ed, const fs::path& file) {
  for (auto& e : ed.navigation().entries())
    if (e.loc.file == file) return true;
  return false;
}

}  // namespace

// --- raw FileWatcher -------------------------------------------------------
// Every scenario runs against the native backend and against the polling
// backend (forced, 100 ms interval). Rename pairing and event-driven latency are
// asserted only where the active backend claims them (detectsRenames /
// eventDriven); the polling backend is held to what polling guarantees:
// Created/Modified/Deleted eventually, a rename as Deleted(old) + Created(new).

namespace {

FileWatcher::Options watcherOptions(bool polling) {
  FileWatcher::Options o;
  o.pollMs = 100;
  o.forcePolling = polling;
  return o;
}

int latencyMs(const FileWatcher& w) { return w.eventDriven() ? 1500 : 4000; }

void eventKinds(bool polling) {
  TempDir d;
  writeFile(d.path / "existing.txt", "x");
  Collector c;
  FileWatcher w(watcherOptions(polling));
  REQUIRE(w.start(d.path, ignoreBuild, c.callback()));
  const int lat = latencyMs(w);
  if (polling) {
    CHECK_EQ(w.mode(), std::string("poll"));
    CHECK(!w.eventDriven());
    CHECK(!w.detectsRenames());
  } else {
#if defined(_WIN32)
    CHECK_EQ(w.mode(), std::string("readdirchanges"));
#elif defined(__linux__)
    CHECK_EQ(w.mode(), std::string("inotify"));
#else
    CHECK_EQ(w.mode(), std::string("poll"));
#endif
    CHECK_EQ(w.eventDriven(), w.mode() != "poll");
  }

  writeFile(d.path / "new.cpp", "int x;");
  CHECK(waitUntil([&] { return c.has(d.path / "new.cpp", FileWatcher::Event::Created); }, lat));
  // Created + the write that follows it are one Created event, not Created and Modified.
  CHECK(!c.has(d.path / "new.cpp", FileWatcher::Event::Modified));

  writeFile(d.path / "existing.txt", "changed");
  CHECK(waitUntil([&] { return c.has(d.path / "existing.txt", FileWatcher::Event::Modified); }, lat));

  fs::remove(d.path / "existing.txt");
  CHECK(waitUntil([&] { return c.has(d.path / "existing.txt", FileWatcher::Event::Deleted); }, lat));

  fs::rename(d.path / "new.cpp", d.path / "moved.cpp");
  if (w.detectsRenames()) {
    CHECK(waitUntil([&] { return c.has(d.path / "moved.cpp", FileWatcher::Event::Renamed); }, lat));
    for (auto& e : c.snapshot())
      if (e.kind == FileWatcher::Event::Renamed) CHECK_EQ(e.oldPath.string(), (d.path / "new.cpp").string());
  } else {
    CHECK(waitUntil([&] { return c.has(d.path / "moved.cpp", FileWatcher::Event::Created); }, lat));
    CHECK(waitUntil([&] { return c.has(d.path / "new.cpp", FileWatcher::Event::Deleted); }, lat));
    CHECK(!c.has(d.path / "moved.cpp", FileWatcher::Event::Renamed));
  }

  // A directory that appears after start() is watched, including nested ones.
  fs::create_directories(d.path / "sub/deeper");
  std::this_thread::sleep_for(50ms);
  writeFile(d.path / "sub/deeper/late.h", "struct Late {};");
  CHECK(waitUntil([&] { return c.has(d.path / "sub/deeper/late.h", FileWatcher::Event::Created); }, lat));
  writeFile(d.path / "sub/deeper/late.h", "struct Later {};");
  CHECK(waitUntil([&] { return c.has(d.path / "sub/deeper/late.h", FileWatcher::Event::Modified); }, lat));

  // Ignored directories are not watched.
  fs::create_directories(d.path / "build");
  std::this_thread::sleep_for(50ms);
  writeFile(d.path / "build/out.o", "x");
  std::this_thread::sleep_for(polling ? 800ms : 500ms);
  CHECK(!c.has(d.path / "build/out.o", FileWatcher::Event::Created));
  w.stop();
}

void burstOfWrites(bool polling) {
  TempDir d;
  writeFile(d.path / "burst.cpp", "0");
  Collector c;
  FileWatcher w(watcherOptions(polling));
  REQUIRE(w.start(d.path, nullptr, c.callback()));
  // 40 writes spread over ~120 ms (inside the 200 ms window).
  for (int i = 0; i < 40; ++i) {
    writeFile(d.path / "burst.cpp", std::string(static_cast<size_t>(i) + 1, 'x'));
    std::this_thread::sleep_for(3ms);
  }
  CHECK(waitUntil([&] { return c.countFor(d.path / "burst.cpp", FileWatcher::Event::Modified) >= 1; }, latencyMs(w)));
  std::this_thread::sleep_for(600ms);
  const size_t n = c.countFor(d.path / "burst.cpp", FileWatcher::Event::Modified);
  if (w.eventDriven()) CHECK_EQ(n, 1u);
  else CHECK(n >= 1 && n <= 2);  // scans land mid-burst; at most one extra delivery
  CHECK_EQ(c.snapshot().size(), n);
  w.stop();
}

void createThenDeleteAndStop(bool polling) {
  TempDir d;
  Collector c;
  FileWatcher w(watcherOptions(polling));
  REQUIRE(w.start(d.path, nullptr, c.callback()));
  writeFile(d.path / "tmp.cpp", "x");
  fs::remove(d.path / "tmp.cpp");
  std::this_thread::sleep_for(600ms);
  CHECK_EQ(c.snapshot().size(), 0u);
  auto t0 = std::chrono::steady_clock::now();
  w.stop();
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
  CHECK(ms < (polling ? 400 : 150));
  CHECK(!w.running());
  w.stop();  // idempotent
  // Restartable.
  REQUIRE(w.start(d.path, nullptr, c.callback()));
  writeFile(d.path / "again.cpp", "x");
  CHECK(waitUntil([&] { return c.has(d.path / "again.cpp", FileWatcher::Event::Created); }, latencyMs(w)));
}

void directoryRenameAndRemoval(bool polling) {
  TempDir d;
  writeFile(d.path / "old/x.cpp", "int x;");
  Collector c;
  FileWatcher w(watcherOptions(polling));
  REQUIRE(w.start(d.path, nullptr, c.callback()));
  const int lat = latencyMs(w);
  fs::rename(d.path / "old", d.path / "new");
  if (w.detectsRenames()) {
    CHECK(waitUntil([&] { return c.has(d.path / "new", FileWatcher::Event::Renamed); }, lat));
    for (auto& e : c.snapshot())
      if (e.path == d.path / "new") CHECK(e.isDirectory);
    // The watch followed the directory to its new name.
    writeFile(d.path / "new/x.cpp", "int yyyy;");
    CHECK(waitUntil([&] { return c.has(d.path / "new/x.cpp", FileWatcher::Event::Modified); }, lat));
  } else {
    // No directory events: the files inside are reported instead.
    CHECK(waitUntil([&] { return c.has(d.path / "new/x.cpp", FileWatcher::Event::Created); }, lat));
    CHECK(waitUntil([&] { return c.has(d.path / "old/x.cpp", FileWatcher::Event::Deleted); }, lat));
    writeFile(d.path / "new/x.cpp", "int yyyy;");
    CHECK(waitUntil([&] { return c.has(d.path / "new/x.cpp", FileWatcher::Event::Modified); }, lat));
  }
  fs::remove_all(d.path / "new");
  CHECK(waitUntil([&] { return c.has(d.path / "new/x.cpp", FileWatcher::Event::Deleted); }, lat));
  if (w.eventDriven()) CHECK(waitUntil([&] { return c.has(d.path / "new", FileWatcher::Event::Deleted); }, lat));
  w.stop();
}

}  // namespace

TEST(fw_event_kinds_and_directory_created_after_start) { eventKinds(false); }
TEST(fw_event_kinds_polling) { eventKinds(true); }
TEST(fw_burst_of_writes_is_one_event) { burstOfWrites(false); }
TEST(fw_burst_of_writes_polling) { burstOfWrites(true); }
TEST(fw_create_then_delete_cancels_and_stop_is_prompt) { createThenDeleteAndStop(false); }
TEST(fw_create_then_delete_polling) { createThenDeleteAndStop(true); }
TEST(fw_directory_rename_and_removal) { directoryRenameAndRemoval(false); }
TEST(fw_directory_rename_polling) { directoryRenameAndRemoval(true); }

TEST(fw_polling_mode_detects_changes) {
  // Polling guarantees: create / modify / delete eventually, inside subdirectories too.
  TempDir d;
  writeFile(d.path / "sub/q.cpp", "x");
  Collector c;
  FileWatcher w(watcherOptions(true));
  REQUIRE(w.start(d.path, nullptr, c.callback()));
  CHECK_EQ(w.mode(), std::string("poll"));
  writeFile(d.path / "p.cpp", "x");
  CHECK(waitUntil([&] { return c.has(d.path / "p.cpp", FileWatcher::Event::Created); }, 2000));
  writeFile(d.path / "sub/q.cpp", "changed, longer");
  CHECK(waitUntil([&] { return c.has(d.path / "sub/q.cpp", FileWatcher::Event::Modified); }, 2000));
  fs::remove(d.path / "sub/q.cpp");
  CHECK(waitUntil([&] { return c.has(d.path / "sub/q.cpp", FileWatcher::Event::Deleted); }, 2000));
  w.stop();
}

#if defined(__linux__)  // the inotify watch limit has no equivalent in ReadDirectoryChangesW
TEST(fw_watch_limit_falls_back_to_polling_with_rescan) {
  TempDir d;
  for (int i = 0; i < 6; ++i) writeFile(d.path / ("dir" + std::to_string(i)) / "f.cpp", "x");
  Collector c;
  std::mutex lm;
  std::string logged;
  FileWatcher::Options o;
  o.maxWatches = 3;  // simulates ENOSPC from inotify_add_watch
  o.pollMs = 100;
  o.log = [&](const std::string& m) {
    std::lock_guard<std::mutex> lk(lm);
    logged += m + "\n";
  };
  FileWatcher w(o);
  REQUIRE(w.start(d.path, nullptr, c.callback()));
  CHECK_EQ(w.mode(), std::string("poll"));
  CHECK_EQ(w.watchCount(), 0u);
  {
    std::lock_guard<std::mutex> lk(lm);
    CHECK(logged.find("watch limit") != std::string::npos);
  }
  writeFile(d.path / "dir3/new.cpp", "y");
  CHECK(waitUntil([&] { return c.has(d.path / "dir3/new.cpp", FileWatcher::Event::Created); }, 3000));
  fs::remove(d.path / "dir1/f.cpp");
  CHECK(waitUntil([&] { return c.has(d.path / "dir1/f.cpp", FileWatcher::Event::Deleted); }, 3000));
  w.stop();
}

#endif

static void header_edit_updates_fast_index_within_1s(bool polling) {
  EditorFixture f(fastOnly(), polling);
  REQUIRE(f.ok);
  REQUIRE(f.ed->semantic().waitForFastIndex(5000));
  CHECK(f.ed->semantic().fastIndex().lookup("Brand_new_symbol").empty());
  REQUIRE(f.ed->fileWatcher() != nullptr);
  auto t0 = std::chrono::steady_clock::now();
  writeFile(f.dir.path / "src/a.h", "struct Alpha { int one(); };\nint Brand_new_symbol(int);\n");
  CHECK(f.symbolAppears("Brand_new_symbol", f.lat));
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
  std::cerr << "    modified header visible in lookup after " << ms << " ms\n";
}

static void create_and_delete_file(bool polling) {
  EditorFixture f(fastOnly(), polling);
  REQUIRE(f.ok);
  REQUIRE(f.ed->semantic().waitForFastIndex(5000));
  const fs::path nf = f.dir.path / "src/extra.h";
  writeFile(nf, "struct Extra {};\n");
  CHECK(f.symbolAppears("Extra"));
  CHECK(f.ed->waitFor([&] { return f.ed->workspace().filesUnder(nf).size() == 1; }, f.lat));

  // Navigate there so the history has an entry for the file, then delete it.
  std::string err;
  REQUIRE(f.ed->gotoLocation({nf, {0, 0}}, &err));
  REQUIRE(hasEntryFor(*f.ed, nf));
  f.ed->close(nf);  // files open in a Document keep their (possibly unsaved) index entry
  fs::remove(nf);
  CHECK(f.symbolGone("Extra"));
  CHECK(f.ed->waitFor([&] { return f.ed->workspace().filesUnder(nf).empty() && !hasEntryFor(*f.ed, nf); }, f.lat));
}

static void rename_file_history_follows(bool polling) {
  EditorFixture f(fastOnly(), polling);
  REQUIRE(f.ok);
  REQUIRE(f.ed->semantic().waitForFastIndex(5000));
  REQUIRE(f.ed->fileWatcher() != nullptr);
  const bool renames = f.ed->fileWatcher()->detectsRenames();
  const fs::path from = f.dir.path / "src/a.h", to = f.dir.path / "src/renamed.h";
  std::string err;
  REQUIRE(f.ed->gotoLocation({from, {0, 0}}, &err));
  REQUIRE(hasEntryFor(*f.ed, from));
  f.ed->close(from);
  fs::rename(from, to);
  if (renames) {
    // Rename pairing: the history entry follows the file.
    CHECK(f.ed->waitFor([&] { return hasEntryFor(*f.ed, to) && !hasEntryFor(*f.ed, from); }, f.lat));
  } else {
    // Polling sees delete + create: the old entry is dropped, not followed.
    CHECK(f.ed->waitFor([&] { return !hasEntryFor(*f.ed, from); }, f.lat));
    CHECK(!hasEntryFor(*f.ed, to));
  }
  // Either way the index follows: Alpha now lives in the new file only.
  CHECK(f.ed->waitFor(
      [&] {
        auto m = f.ed->semantic().fastIndex().lookup("Alpha");
        return m.size() == 1 && m[0].file->file == to;
      },
      f.lat));
  CHECK(f.ed->waitFor([&] { return f.ed->workspace().filesUnder(from).empty() && f.ed->workspace().filesUnder(to).size() == 1; },
                      f.lat));
}

static void directory_rename_moves_index_and_history(bool polling) {
  EditorFixture f(fastOnly(), polling);
  REQUIRE(f.ok);
  REQUIRE(f.ed->semantic().waitForFastIndex(5000));
  REQUIRE(f.ed->fileWatcher() != nullptr);
  const bool renames = f.ed->fileWatcher()->detectsRenames();
  const fs::path from = f.dir.path / "src", to = f.dir.path / "source";
  std::string err;
  REQUIRE(f.ed->gotoLocation({from / "a.h", {0, 0}}, &err));
  f.ed->close(from / "a.h");
  fs::rename(from, to);
  if (renames) {
    CHECK(f.ed->waitFor([&] { return hasEntryFor(*f.ed, to / "a.h") && !hasEntryFor(*f.ed, from / "a.h"); }, f.lat));
  } else {
    CHECK(f.ed->waitFor([&] { return !hasEntryFor(*f.ed, from / "a.h"); }, f.lat));
    CHECK(!hasEntryFor(*f.ed, to / "a.h"));
  }
  CHECK(f.ed->waitFor(
      [&] {
        auto m = f.ed->semantic().fastIndex().lookup("Alpha");
        return m.size() == 1 && m[0].file->file == to / "a.h";
      },
      f.lat));
  CHECK(f.ed->waitFor([&] { return f.ed->workspace().filesUnder(to).size() == 2u && f.ed->workspace().filesUnder(from).empty(); },
                      f.lat));
}

static void directory_created_after_start_is_indexed(bool polling) {
  EditorFixture f(fastOnly(), polling);
  REQUIRE(f.ok);
  REQUIRE(f.ed->semantic().waitForFastIndex(5000));
  fs::create_directories(f.dir.path / "lib/inner");
  std::this_thread::sleep_for(30ms);
  writeFile(f.dir.path / "lib/inner/deep.h", "struct DeepThing {};\n");
  CHECK(f.symbolAppears("DeepThing"));
  // A whole tree moved in from outside the workspace is picked up too.
  TempDir outside;
  writeFile(outside.path / "pkg/p.h", "struct FromOutside {};\n");
  fs::rename(outside.path / "pkg", f.dir.path / "pkg");
  CHECK(f.symbolAppears("FromOutside", f.lat));
}

static void external_change_fires_handler_and_reload(bool polling) {
  EditorFixture f(fastOnly(), polling);
  REQUIRE(f.ok);
  REQUIRE(f.ed->semantic().waitForFastIndex(5000));
  const fs::path file = f.dir.path / "src/a.h";
  Document* doc = f.ed->open(file);
  REQUIRE(doc != nullptr);

  std::atomic<int> fired{0};
  Document* firedDoc = nullptr;
  f.ed->onExternalChange([&](Document& d) {
    ++fired;
    firedDoc = &d;
  });

  // Rewriting identical content is not a change.
  writeFile(file, "struct Alpha { int one(); };\n");
  f.ed->waitFor([] { return false; }, 500);
  CHECK_EQ(fired.load(), 0);

  // Our own save is not an external change either.
  doc->insert({1, 0}, "// edited\n");
  CHECK(doc->dirty());
  REQUIRE(f.ed->save(*doc));
  f.ed->waitFor([] { return false; }, 500);
  CHECK_EQ(fired.load(), 0);

  // A genuine external write fires onExternalChange with the document.
  writeFile(file, "struct Alpha { int one(); };\nint Changed_externally(int);\n");
  CHECK(f.ed->waitFor([&] { return fired.load() >= 1; }, f.lat));
  CHECK(firedDoc == doc);
  f.ed->waitFor([] { return false; }, 400);
  CHECK_EQ(fired.load(), 1);  // one notification per distinct on-disk content

  // The UI decides: reload.
  CHECK(!doc->buffer().text().empty());
  REQUIRE(f.ed->reloadFromDisk(*doc));
  CHECK(!doc->dirty());
  CHECK(doc->buffer().text().find("Changed_externally") != std::string::npos);
  CHECK(doc->buffer().text().find("// edited") == std::string::npos);
  // The reloaded text is what we now consider "on disk": no further event.
  f.ed->waitFor([] { return false; }, 300);
  CHECK_EQ(fired.load(), 1);
  CHECK(f.symbolAppears("Changed_externally"));
}

static void reload_from_disk_keeps_history_lines(bool polling) {
  EditorFixture f(fastOnly(), polling);
  REQUIRE(f.ok);
  const fs::path file = f.dir.path / "src/a.cpp";
  writeFile(file, "l0\nl1\nl2\nl3\nl4\nl5\nl6\nl7\nl8\nl9\nl10\nl11\nl12\nl13\nl14\nl15\nl16\nl17\nl18\nl19\nl20\n");
  Document* doc = f.ed->open(file);
  REQUIRE(doc != nullptr);
  std::string err;
  REQUIRE(f.ed->gotoLocation({file, {18, 0}}, &err));
  // Disk gains two lines at the top; the tail is unchanged.
  writeFile(file, "new0\nnew1\nl0\nl1\nl2\nl3\nl4\nl5\nl6\nl7\nl8\nl9\nl10\nl11\nl12\nl13\nl14\nl15\nl16\nl17\nl18\nl19\nl20\n");
  REQUIRE(f.ed->reloadFromDisk(*doc));
  CHECK_EQ(doc->buffer().lineCount(), 24);
  CHECK_EQ(doc->buffer().line(0), std::string("new0"));
  CHECK_EQ(doc->buffer().line(20), std::string("l18"));
  bool shifted = false;
  for (auto& e : f.ed->navigation().entries())
    if (e.loc.file == file && e.loc.pos.line == 20) shifted = true;
  CHECK(shifted);
  // Deleting everything after a prefix, and appending, also round-trip.
  writeFile(file, "new0\nnew1\n");
  REQUIRE(f.ed->reloadFromDisk(*doc));
  CHECK_EQ(doc->buffer().text(), std::string("new0\nnew1\n"));
  writeFile(file, "new0\nnew1\nmore\nstuff");
  REQUIRE(f.ed->reloadFromDisk(*doc));
  CHECK_EQ(doc->buffer().text(), std::string("new0\nnew1\nmore\nstuff"));
  writeFile(file, "");
  REQUIRE(f.ed->reloadFromDisk(*doc));
  CHECK_EQ(doc->buffer().text(), std::string(""));
}

static void dirty_document_is_not_clobbered_in_index(bool polling) {
  EditorFixture f(fastOnly(), polling);
  REQUIRE(f.ok);
  REQUIRE(f.ed->semantic().waitForFastIndex(5000));
  const fs::path file = f.dir.path / "src/a.h";
  Document* doc = f.ed->open(file);
  REQUIRE(doc != nullptr);
  doc->insert({1, 0}, "int Unsaved_symbol(int);\n");
  CHECK(f.symbolAppears("Unsaved_symbol", 200));
  std::atomic<int> fired{0};
  f.ed->onExternalChange([&](Document&) { ++fired; });
  writeFile(file, "struct Alpha { int one(); };\nint Disk_symbol(int);\n");
  CHECK(f.ed->waitFor([&] { return fired.load() == 1; }, f.lat));
  // The unsaved buffer stays authoritative for the index.
  CHECK(!f.ed->semantic().fastIndex().lookup("Unsaved_symbol").empty());
  CHECK(f.ed->semantic().fastIndex().lookup("Disk_symbol").empty());
}


TEST(watch_header_edit_updates_fast_index_within_1s) { header_edit_updates_fast_index_within_1s(false); }
TEST(watch_header_edit_updates_fast_index_within_1s_polling) { header_edit_updates_fast_index_within_1s(true); }
TEST(watch_create_and_delete_file) { create_and_delete_file(false); }
TEST(watch_create_and_delete_file_polling) { create_and_delete_file(true); }
TEST(watch_rename_file_history_follows) { rename_file_history_follows(false); }
TEST(watch_rename_file_history_follows_polling) { rename_file_history_follows(true); }
TEST(watch_directory_rename_moves_index_and_history) { directory_rename_moves_index_and_history(false); }
TEST(watch_directory_rename_moves_index_and_history_polling) { directory_rename_moves_index_and_history(true); }
TEST(watch_directory_created_after_start_is_indexed) { directory_created_after_start_is_indexed(false); }
TEST(watch_directory_created_after_start_is_indexed_polling) { directory_created_after_start_is_indexed(true); }
TEST(watch_external_change_fires_handler_and_reload) { external_change_fires_handler_and_reload(false); }
TEST(watch_external_change_fires_handler_and_reload_polling) { external_change_fires_handler_and_reload(true); }
TEST(watch_reload_from_disk_keeps_history_lines) { reload_from_disk_keeps_history_lines(false); }
TEST(watch_reload_from_disk_keeps_history_lines_polling) { reload_from_disk_keeps_history_lines(true); }
TEST(watch_dirty_document_is_not_clobbered_in_index) { dirty_document_is_not_clobbered_in_index(false); }
TEST(watch_dirty_document_is_not_clobbered_in_index_polling) { dirty_document_is_not_clobbered_in_index(true); }

TEST(watch_can_be_disabled_in_workspace_json) {
  TempDir d;
  writeFile(d.path / "a.h", "struct A {};\n");
  writeFile(d.path / ".tenfold/workspace.json", "{\"watchFiles\": false}");
  Editor ed(d.path, fastOnly());
  REQUIRE(ed.start());
  CHECK(!ed.workspace().settings().watchFiles);
  CHECK(ed.fileWatcher() == nullptr);
  REQUIRE(ed.semantic().waitForFastIndex(5000));
  writeFile(d.path / "b.h", "struct Never_seen {};\n");
  ed.waitFor([] { return false; }, 500);
  CHECK(ed.semantic().fastIndex().lookup("Never_seen").empty());
}

TEST(watch_enabled_by_default) {
  Workspace ws(fs::temp_directory_path());
  CHECK(ws.settings().watchFiles);
}

TEST(watch_compile_commands_location_change_restarts_clangd_only_then) {
  if (findExecutable("clangd").empty()) {
    std::cerr << "    (skipped: clangd not found)\n";
    return;
  }
  std::mutex mu;
  int starts = 0;
  Editor::Options o;
  o.semantic.clangdBudgetMs = 150;
  o.log = [&](const std::string& m) {
    if (m.rfind("clangd started", 0) == 0) {
      std::lock_guard<std::mutex> lk(mu);
      ++starts;
    }
  };
  EditorFixture f(std::move(o));
  REQUIRE(f.ok);
  auto startCount = [&] {
    std::lock_guard<std::mutex> lk(mu);
    return starts;
  };
  CHECK_EQ(startCount(), 1);
  const fs::path before = f.ed->semantic().status().compdbPath;
  CHECK_EQ(before.parent_path().string(), (f.dir.path / ".tenfold").string());

  // A real compile_commands.json appears at the root: the location changes.
  writeFile(f.dir.path / "compile_commands.json",
            "[{\"directory\":\"" + f.dir.path.generic_string() + "\",\"file\":\"" + (f.dir.path / "src/a.cpp").generic_string() +
                "\",\"arguments\":[\"clang++\",\"-std=c++20\",\"-c\",\"" + (f.dir.path / "src/a.cpp").generic_string() +
                "\"]}]");
  CHECK(f.ed->waitFor([&] { return startCount() == 2; }, 20000));
  CHECK_EQ(f.ed->semantic().status().compdbPath.parent_path().string(), f.dir.path.string());
  CHECK(f.ed->semantic().status().clangdInitialized);

  // Editing the database in place: clangd re-reads it itself, no restart.
  writeFile(f.dir.path / "compile_commands.json",
            "[{\"directory\":\"" + f.dir.path.generic_string() + "\",\"file\":\"" + (f.dir.path / "src/a.cpp").generic_string() +
                "\",\"arguments\":[\"clang++\",\"-std=c++17\",\"-c\",\"" + (f.dir.path / "src/a.cpp").generic_string() +
                "\"]}]");
  CHECK(f.ed->waitFor([&] { return f.ed->semantic().compileCommands().size() == 1 &&
                                   f.ed->semantic().compileCommands()[0].arguments.size() > 1 &&
                                   f.ed->semantic().compileCommands()[0].arguments[1] == "-std=c++17"; }, 5000));
  f.ed->waitFor([] { return false; }, 600);
  CHECK_EQ(startCount(), 2);
}
