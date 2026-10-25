#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>

#include "app/Editor.h"
#include "nav/NavigationHistory.h"
#include "test_main.h"

using namespace tf;

namespace {
Location L(const char* f, int line, int col = 0) { return {f, {line, col}}; }
}  // namespace

TEST(nav_back_and_forward_basic) {
  NavigationHistory h;
  CHECK(!h.canGoBack());
  // Jump from a.cpp:10 to b.h:100 (the editor records "from" then "to").
  h.record(L("a.cpp", 10));
  h.record(L("b.h", 100));
  CHECK(h.canGoBack());
  CHECK(!h.canGoForward());
  // Caret has drifted a little within b.h.
  auto back = h.back(L("b.h", 102));
  REQUIRE(back.has_value());
  CHECK(*back == L("a.cpp", 10));
  CHECK(h.canGoForward());
  auto fwd = h.forward(*back);
  REQUIRE(fwd.has_value());
  CHECK(*fwd == L("b.h", 102));  // returns to exactly where we left, not the stale 100
  CHECK(!h.canGoForward());
  CHECK(!h.forward(*fwd).has_value());
}

TEST(nav_multi_hop) {
  NavigationHistory h;
  h.record(L("a", 1));
  h.record(L("b", 1));
  h.record(L("c", 1));
  h.record(L("d", 1));
  auto p = h.back(L("d", 1));
  CHECK(*p == L("c", 1));
  p = h.back(*p);
  CHECK(*p == L("b", 1));
  p = h.back(*p);
  CHECK(*p == L("a", 1));
  CHECK(!h.back(*p).has_value());
  p = h.forward(*p);
  CHECK(*p == L("b", 1));
  p = h.forward(*p);
  CHECK(*p == L("c", 1));
  p = h.forward(*p);
  CHECK(*p == L("d", 1));
  CHECK(!h.canGoForward());
}

TEST(nav_new_jump_truncates_forward_stack) {
  NavigationHistory h;
  h.record(L("a", 1));
  h.record(L("b", 1));
  h.record(L("c", 1));
  auto p = h.back(L("c", 1));  // at b
  p = h.back(*p);              // at a
  CHECK(h.canGoForward());
  h.record(L("z", 1));  // jump somewhere new
  CHECK(!h.canGoForward());
  p = h.back(L("z", 1));
  CHECK(*p == L("a", 1));
}

TEST(nav_coalesces_nearby_entries) {
  NavigationHistory h(NavigationHistory::Options{200, 10});
  h.record(L("a", 10));
  h.record(L("a", 12));  // within 10 lines: merged
  h.record(L("a", 15));
  CHECK_EQ(h.entries().size(), 1u);
  CHECK(h.entries().back().loc == L("a", 15));
  h.record(L("a", 40));
  CHECK_EQ(h.entries().size(), 2u);
}

TEST(nav_caret_moves_record_far_moves_only) {
  NavigationHistory h(NavigationHistory::Options{200, 10});
  h.onCaretMoved(L("a", 0), L("a", 3));  // small: ignored
  CHECK(h.entries().empty());
  h.onCaretMoved(L("a", 3), L("a", 300));  // far: remembers where we were
  CHECK_EQ(h.entries().size(), 1u);
  CHECK(h.entries().back().loc == L("a", 3));
  auto p = h.back(L("a", 300));
  CHECK(*p == L("a", 3));
  p = h.forward(*p);
  CHECK(*p == L("a", 300));
}

TEST(nav_edits_shift_entries) {
  NavigationHistory h;
  h.record(L("a", 10, 4));
  h.record(L("a", 50, 2));
  h.record(L("b", 7));
  // Insert 3 lines at line 20 of a (replace 1 line with 4).
  h.onLinesReplaced("a", 20, 20, 4);
  CHECK(h.entries()[0].loc == L("a", 10, 4));
  CHECK(h.entries()[1].loc == L("a", 53, 2));
  CHECK(h.entries()[2].loc == L("b", 7));
  // Delete lines 48..60 of a: the entry inside the deleted block clamps to 48.
  h.onLinesReplaced("a", 48, 60, 1);
  CHECK(h.entries()[1].loc == L("a", 48, 0));
  // Delete lines 0..4: entry at 10 shifts up by 4.
  h.onLinesReplaced("a", 0, 4, 1);
  CHECK(h.entries()[0].loc == L("a", 6, 4));
}

TEST(nav_rename_and_remove_file) {
  NavigationHistory h;
  h.record(L("a", 1));
  h.record(L("b", 1));
  h.record(L("c", 1));
  h.onFileRenamed("b", "bb");
  CHECK(h.entries()[1].loc == L("bb", 1));
  auto p = h.back(L("c", 1));  // cursor at index 1 (bb)
  CHECK(*p == L("bb", 1));
  h.onFileRemoved("a");
  CHECK_EQ(h.entries().size(), 2u);
  CHECK_EQ(h.cursor(), 0u);
  CHECK(!h.canGoBack());
  CHECK(h.canGoForward());
}

TEST(nav_max_entries) {
  NavigationHistory h(NavigationHistory::Options{3, 1});
  for (int i = 0; i < 10; ++i) h.record(L("a", i * 100));
  CHECK_EQ(h.entries().size(), 3u);
  CHECK(h.entries().front().loc == L("a", 700));
}

TEST(nav_back_from_live_position_far_from_last_entry) {
  NavigationHistory h;
  h.record(L("a", 1));
  h.record(L("b", 1));
  // The user scrolled/arrowed far away from b:1. Like VS Code, Back first
  // returns to the point we arrived at (b:1), then to the origin (a:1)...
  auto p = h.back(L("b", 500));
  CHECK(*p == L("b", 1));
  p = h.back(*p);
  CHECK(*p == L("a", 1));
  // ...and Forward retraces both, ending at the real position we left (b:500).
  p = h.forward(*p);
  CHECK(*p == L("b", 1));
  p = h.forward(*p);
  CHECK(*p == L("b", 500));
  CHECK(!h.canGoForward());
}

namespace {
namespace fs = std::filesystem;

// An Editor over a temp workspace with three 40-line files (no semantic engine: never started).
struct EdFx {
  fs::path dir;
  std::unique_ptr<Editor> ed;
  EdFx() {
    static int n = 0;
    dir = fs::temp_directory_path() /
          ("tf_navfx_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_" +
           std::to_string(n++));
    fs::create_directories(dir);
    for (const char* f : {"a.cpp", "b.cpp", "c.cpp"}) {
      std::ofstream out(dir / f);
      for (int i = 0; i < 40; ++i) out << "int " << f[0] << i << ";\n";
    }
    ed = std::make_unique<Editor>(dir);
  }
  ~EdFx() {
    ed.reset();
    std::error_code ec;
    fs::remove_all(dir, ec);
  }
  Location at(const char* f, int line, int col = 0) const { return {dir / f, {line, col}}; }
};
}  // namespace

TEST(nav_editor_def_def_back_back_forward) {
  EdFx fx;
  Editor& ed = *fx.ed;
  REQUIRE(ed.gotoLocation(fx.at("a.cpp", 0)));
  REQUIRE(ed.gotoLocation(fx.at("b.cpp", 5), nullptr, "foo"));
  REQUIRE(ed.gotoLocation(fx.at("c.cpp", 20), nullptr, "bar"));
  CHECK_EQ(ed.navigation().entries().size(), 3u);
  CHECK(ed.back());
  CHECK(ed.cursor() == fx.at("b.cpp", 5));
  CHECK(ed.back());
  CHECK(ed.cursor() == fx.at("a.cpp", 0));
  CHECK(ed.forward());
  CHECK(ed.cursor() == fx.at("b.cpp", 5));
}

TEST(nav_editor_go_to_history_entry_adds_no_entries) {
  EdFx fx;
  Editor& ed = *fx.ed;
  ed.gotoLocation(fx.at("a.cpp", 0));
  ed.gotoLocation(fx.at("b.cpp", 5), nullptr, "foo");
  ed.gotoLocation(fx.at("c.cpp", 20), nullptr, "bar");
  CHECK(!ed.goToHistoryEntry(3));
  CHECK(ed.goToHistoryEntry(0));  // from the live position
  CHECK(ed.cursor() == fx.at("a.cpp", 0));
  CHECK_EQ(ed.navigation().entries().size(), 3u);
  CHECK(ed.navigation().canGoForward());
  CHECK(ed.goToHistoryEntry(2));  // all the way forward = back at the live position
  CHECK(ed.cursor() == fx.at("c.cpp", 20));
  CHECK(!ed.navigation().canGoForward());
  CHECK(ed.goToHistoryEntry(1));
  CHECK(ed.cursor() == fx.at("b.cpp", 5));
  CHECK(ed.navigation().canGoForward() && ed.navigation().canGoBack());
  CHECK_EQ(ed.navigation().entries().size(), 3u);
  CHECK(ed.forward());  // plain Forward still works afterwards
  CHECK(ed.cursor() == fx.at("c.cpp", 20));
}

TEST(nav_editor_last_edit_location_follows_edits_and_line_shifts) {
  EdFx fx;
  Editor& ed = *fx.ed;
  CHECK(!ed.lastEditLocation().has_value());
  ed.gotoLocation(fx.at("a.cpp", 8));
  ed.moveCursor({8, 0});
  ed.insertText("Z");
  REQUIRE(ed.lastEditLocation().has_value());
  CHECK(*ed.lastEditLocation() == fx.at("a.cpp", 8, 1));
  ed.moveCursor({30, 0});  // moving is not editing
  CHECK(*ed.lastEditLocation() == fx.at("a.cpp", 8, 1));
  ed.insertText("\n");  // multi-line edit: the caret after it
  CHECK(*ed.lastEditLocation() == fx.at("a.cpp", 31, 0));
  ed.moveCursor({8, 1});
  ed.insertText("Q");
  CHECK(*ed.lastEditLocation() == fx.at("a.cpp", 8, 2));
  ed.moveCursor({0, 0});
  CHECK(ed.undo());  // undo is an edit too: it ends where the caret was put back
  CHECK(*ed.lastEditLocation() == ed.cursor());
  CHECK(ed.redo());
  CHECK(*ed.lastEditLocation() == ed.cursor());
  // A change that is not a user edit (external reload) only shifts it: 3 lines inserted above.
  REQUIRE(ed.save(*ed.currentDocument()));
  {
    std::ifstream in(fx.dir / "a.cpp");
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    std::ofstream out(fx.dir / "a.cpp", std::ios::binary);
    out << "x\ny\nz\n" << text;
  }
  REQUIRE(ed.reloadFromDisk(*ed.currentDocument()));
  CHECK(*ed.lastEditLocation() == fx.at("a.cpp", 11, 2));
}
