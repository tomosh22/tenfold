// C2a: the breakpoint store: edit following (same contract as Bookmarks), persistence, Editor wiring.
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>

#include "app/Bookmarks.h"
#include "app/Editor.h"
#include "debug/Breakpoints.h"
#include "test_main.h"

using namespace tf;
namespace fs = std::filesystem;

namespace {

struct Dir {
  fs::path path;
  Dir() {
    static int counter = 0;
    std::error_code cec;
    path = fs::weakly_canonical(fs::temp_directory_path(), cec) /
           ("tenfold_bp_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_" +
            std::to_string(counter++));
    fs::create_directories(path);
  }
  ~Dir() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
};

Breakpoint bp(const fs::path& f, int line, bool enabled = true, std::string cond = {}) {
  Breakpoint b;
  b.file = f;
  b.line = line;
  b.enabled = enabled;
  b.condition = std::move(cond);
  return b;
}

std::string numbered(int n) {
  std::string s;
  for (int i = 0; i < n; ++i) s += (i ? "\n" : "") + std::to_string(i);
  return s;
}

}  // namespace

TEST(bp_toggle_add_remove_enable_condition) {
  Breakpoints b;
  const fs::path f = "/w/a.cpp", g = "/w/b.cpp";
  CHECK(b.empty());
  CHECK(b.toggle(f, 3));
  CHECK(b.has(f, 3));
  CHECK(!b.toggle(f, 3));
  CHECK(b.empty());

  b.add(bp(f, 5));
  b.add(bp(f, 2, false, "i > 1"));
  b.add(bp(g, 9));
  CHECK_EQ(b.count(), static_cast<size_t>(3));
  CHECK((b.lines(f) == std::vector<int>{2, 5}));
  CHECK(b.get(f, 2)->condition == "i > 1");
  CHECK(!b.get(f, 2)->enabled);
  CHECK(b.get(f, 5)->enabled);
  const auto all = b.all();
  REQUIRE(all.size() == 3);
  CHECK(all[0].file == f && all[0].line == 2);  // by path, then line
  CHECK(all[2].file == g);

  CHECK(b.setEnabled(f, 2, true));
  CHECK(b.get(f, 2)->enabled);
  CHECK(b.setCondition(f, 5, "x"));
  CHECK(b.get(f, 5)->condition == "x");
  CHECK(!b.setEnabled(f, 77, true));
  CHECK(!b.setCondition(g, 1, "x"));
  b.add(bp(f, 5, false, "y"));  // add replaces the properties of the one on that line
  CHECK_EQ(b.count(), static_cast<size_t>(3));
  CHECK(b.get(f, 5)->condition == "y");

  CHECK(b.remove(f, 5));
  CHECK(!b.remove(f, 5));
  CHECK((b.lines(f) == std::vector<int>{2}));
  b.clear();
  CHECK(b.empty());
}

TEST(bp_generation_and_change_handler) {
  Breakpoints b;
  int calls = 0;
  b.setChangeHandler([&] { ++calls; });
  const fs::path f = "/w/a.cpp";
  const uint64_t g0 = b.generation();
  b.toggle(f, 1);
  CHECK(b.generation() > g0);
  CHECK_EQ(calls, 1);
  const uint64_t g1 = b.generation();
  b.setEnabled(f, 1, true);  // unchanged
  b.setCondition(f, 1, "");  // unchanged
  b.add(bp(f, 1));           // unchanged
  b.remove(f, 9);            // absent
  b.onLinesReplaced(f, 5, 5, 1);
  b.onLinesReplaced("/w/other.cpp", 0, 0, 3);
  CHECK_EQ(b.generation(), g1);
  CHECK_EQ(calls, 1);
  b.setEnabled(f, 1, false);
  b.setCondition(f, 1, "c");
  b.onLinesReplaced(f, 0, 0, 2);
  CHECK_EQ(calls, 4);
  CHECK(b.generation() > g1);
}

TEST(bp_follow_edits_like_bookmarks) {
  // Every edit shape, applied to Bookmarks and Breakpoints alike: the lines must agree.
  struct Edit {
    int start, end, newCount;
  };
  const std::vector<Edit> edits = {{0, 0, 3}, {2, 2, 1},   {1, 4, 1}, {0, 3, 0},  {6, 6, 2},
                                   {3, 8, 2}, {10, 10, 1}, {4, 4, 4}, {0, 20, 1}, {5, 6, 6}};
  const fs::path f = "/w/a.cpp";
  for (const Edit& e : edits) {
    Bookmarks m;
    Breakpoints b;
    for (int l : {1, 3, 4, 7, 9}) {
      m.toggle(f, l);
      b.toggle(f, l);
    }
    m.onLinesReplaced(f, e.start, e.end, e.newCount);
    b.onLinesReplaced(f, e.start, e.end, e.newCount);
    CHECK(m.lines(f) == b.lines(f));
  }
}

TEST(bp_follow_edits_keeps_properties_and_merges) {
  Breakpoints b;
  const fs::path f = "/w/a.cpp";
  b.add(bp(f, 2, false, "a"));
  b.add(bp(f, 6, true, "b"));
  b.onLinesReplaced(f, 0, 0, 3);  // two lines inserted above
  CHECK((b.lines(f) == std::vector<int>{4, 8}));
  CHECK(!b.get(f, 4)->enabled && b.get(f, 4)->condition == "a");
  CHECK(b.get(f, 8)->condition == "b");
  b.onLinesReplaced(f, 3, 9, 1);  // both inside a replaced block: they collapse onto its first line, the first wins
  CHECK((b.lines(f) == std::vector<int>{3}));
  CHECK(b.get(f, 3)->condition == "a");
  CHECK(b.get(f, 3)->line == 3);
}

TEST(bp_rename_moves_file) {
  Breakpoints b;
  b.add(bp("/w/a.cpp", 2, true, "c"));
  b.add(bp("/w/a.cpp", 4));
  b.onFileRenamed("/w/a.cpp", "/w/new.cpp");
  CHECK(b.lines("/w/a.cpp").empty());
  CHECK((b.lines("/w/new.cpp") == std::vector<int>{2, 4}));
  CHECK(b.get("/w/new.cpp", 2)->file == fs::path("/w/new.cpp"));
  CHECK(b.get("/w/new.cpp", 2)->condition == "c");
}

TEST(bp_persistence_round_trip_relative_paths) {
  Dir d;
  const fs::path root = d.path / "ws";
  fs::create_directories(root / "src");
  const fs::path json = root / ".tenfold" / "breakpoints.json";
  Breakpoints b;
  b.add(bp(root / "src" / "a.cpp", 3, true, "x == \"q\""));
  b.add(bp(root / "src" / "a.cpp", 10, false));
  b.add(bp(d.path / "outside.cpp", 1));
  CHECK(b.dirty());
  std::string err;
  CHECK(b.save(json, root, &err));
  CHECK(!b.dirty());

  std::ifstream in(json);
  std::stringstream ss;
  ss << in.rdbuf();
  const std::string text = ss.str();
  CHECK(text.find("\"version\": 1") != std::string::npos);
  CHECK(text.find("\"src/a.cpp\"") != std::string::npos);  // relative to the root, generic separators
  CHECK(text.find(root.generic_string()) == std::string::npos);

  Breakpoints c;
  const uint64_t g0 = c.generation();
  CHECK(c.load(json, root, &err));
  CHECK(c.generation() > g0);
  CHECK(!c.dirty());
  CHECK_EQ(c.count(), static_cast<size_t>(3));
  CHECK((c.lines(root / "src" / "a.cpp") == std::vector<int>{3, 10}));
  CHECK(c.get(root / "src" / "a.cpp", 3)->condition == "x == \"q\"");
  CHECK(!c.get(root / "src" / "a.cpp", 10)->enabled);
  CHECK(c.has(d.path / "outside.cpp", 1));
}

TEST(bp_load_rejects_bad_files_and_keeps_content) {
  Dir d;
  Breakpoints b;
  b.add(bp("/w/keep.cpp", 1));
  std::string err;
  CHECK(!b.load(d.path / "missing.json", d.path, &err));
  const fs::path bad = d.path / "bad.json";
  auto write = [&](const std::string& t) {
    std::ofstream out(bad, std::ios::binary | std::ios::trunc);
    out << t;
  };
  write("not json");
  CHECK(!b.load(bad, d.path, &err));
  write("{\"version\":1}");
  CHECK(!b.load(bad, d.path, &err));
  write("{\"version\":2,\"breakpoints\":{}}");
  CHECK(!b.load(bad, d.path, &err));
  CHECK(err.find("newer") != std::string::npos);
  CHECK(b.has("/w/keep.cpp", 1));  // nothing changed
  // Tolerates junk entries; defaults for missing fields.
  write(
      "{\"version\":1,\"breakpoints\":{\"a.cpp\":[{\"line\":4},{\"line\":-1},{\"x\":1},7,{\"line\":6,\"enabled\":false}"
      "]}}");
  CHECK(b.load(bad, d.path, &err));
  CHECK((b.lines(d.path / "a.cpp") == std::vector<int>{4, 6}));
  CHECK(b.get(d.path / "a.cpp", 4)->enabled);
  CHECK(!b.get(d.path / "a.cpp", 6)->enabled);
}

TEST(bp_editor_edits_move_and_persist) {
  Dir d;
  {
    std::ofstream out(d.path / "a.cpp", std::ios::binary);
    out << numbered(12);
  }
  Editor ed(d.path);
  Document* doc = ed.open("a.cpp");
  REQUIRE(doc != nullptr);
  ed.gotoLocation({doc->path(), {4, 0}});
  CHECK(ed.toggleBreakpoint());
  ed.gotoLocation({doc->path(), {8, 0}});
  CHECK(ed.toggleBreakpoint());
  CHECK((ed.breakpoints().lines(doc->path()) == std::vector<int>{4, 8}));
  CHECK(fs::exists(d.path / ".tenfold" / "breakpoints.json"));

  ed.setSelections({{{0, 0}, {0, 0}}});
  ed.insertText("x\ny\n");  // two lines above
  CHECK((ed.breakpoints().lines(doc->path()) == std::vector<int>{6, 10}));
  ed.undo();
  CHECK((ed.breakpoints().lines(doc->path()) == std::vector<int>{4, 8}));
  ed.insertText("x\n");
  CHECK(ed.save(*doc));  // save() writes the new lines

  Editor second(d.path);
  second.open("a.cpp");
  CHECK((second.breakpoints().lines(doc->path()) == std::vector<int>{5, 9}));
}

TEST(bp_symlinked_spelling_is_the_same_file_and_persists_relative) {
  Dir d;
  const fs::path real = d.path / "real";
  fs::create_directories(real);
  const fs::path link = d.path / "link";
  std::error_code ec;
  fs::create_directory_symlink(real, link, ec);
  if (ec) return;
  Breakpoints b;
  b.add(bp(link / "a.cpp", 3));
  CHECK(b.has(real / "a.cpp", 3));
  CHECK(!b.toggle(real / "a.cpp", 3));  // toggles the same breakpoint off
  CHECK(b.empty());
  b.add(bp(link / "a.cpp", 4, true, "x"));
  b.onLinesReplaced(real / "a.cpp", 0, 0, 2);
  CHECK((b.lines(link / "a.cpp") == std::vector<int>{5}));
  const fs::path json = real / ".tenfold" / "breakpoints.json";
  CHECK(b.save(json, real));
  std::ifstream in(json);
  std::stringstream ss;
  ss << in.rdbuf();
  CHECK(ss.str().find("\"a.cpp\"") != std::string::npos);  // relative although spelled through the link
  Breakpoints c;
  CHECK(c.load(json, real));
  CHECK(c.has(link / "a.cpp", 5));
}
