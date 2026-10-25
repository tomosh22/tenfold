// C2a: DebugSession against FakeDebugger: worker-thread queueing, state delivery through pump(), two-way breakpoint
// sync, follow-current-statement, error propagation and shutdown with a blocked backend call.
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <thread>

#include "app/Editor.h"
#include "debug/DebugSession.h"
#include "debug/FakeDebugger.h"
#include "test_main.h"

using namespace tf;
namespace fs = std::filesystem;

namespace {

std::string numbered(int n) {
  std::string s;
  for (int i = 0; i < n; ++i) s += (i ? "\n" : "") + std::to_string(i);
  return s;
}

struct Fx {
  fs::path dir;
  std::unique_ptr<Editor> ed;
  Document* doc = nullptr;
  std::shared_ptr<FakeDebugger::State> fake = std::make_shared<FakeDebugger::State>();

  explicit Fx(bool install = true) {
    static int counter = 0;
    std::error_code cec;
    dir = fs::weakly_canonical(fs::temp_directory_path(), cec) /
          ("tenfold_dbg_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_" +
           std::to_string(counter++));
    fs::create_directories(dir);
    {
      std::ofstream out(dir / "a.cpp", std::ios::binary);
      out << numbered(40);
    }
    {
      std::ofstream out(dir / "b.cpp", std::ios::binary);
      out << numbered(40);
    }
    ed = std::make_unique<Editor>(dir);
    doc = ed->open("a.cpp");
    if (doc) ed->gotoLocation({doc->path(), {0, 0}});
    if (install) {
      ed->debug().setPollIntervals(2, 2);
      ed->debug().setBackend(std::make_unique<FakeDebugger>(fake));
    }
  }
  ~Fx() {
    ed.reset();
    std::error_code ec;
    fs::remove_all(dir, ec);
  }
  fs::path file(const char* n = "a.cpp") const { return dir / n; }
  bool until(const std::function<bool()>& pred, int ms = 3000) { return ed->waitFor(pred, ms); }
  bool untilMode(DebugMode m) {
    return until([&] { return ed->debug().mode() == m; });
  }
  // Pump for a while so queued polls and posts settle (negative checks).
  void settle(int ms = 60) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
      ed->pump();
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
  }
  bool connectAndWait() {
    ed->debug().connect();
    return untilMode(DebugMode::Design);
  }
  static std::vector<int> fakeLines(const Fx& f, const char* name = "a.cpp") {
    std::vector<int> v;
    for (const Breakpoint& b : f.fake->breakpoints())
      if (b.file.filename() == name) v.push_back(b.line);
    std::sort(v.begin(), v.end());
    return v;
  }
};

}  // namespace

TEST(dbg_no_backend_reports_it) {
  Fx f(false);
  CHECK(!f.ed->debug().hasBackend());
  CHECK(f.ed->debug().status().find("no debugger backend on this platform") != std::string::npos);
  CHECK(!f.ed->debug().connect());
  CHECK(!f.ed->debug().start());
  CHECK(f.ed->debug().lastError().find("no debugger backend") != std::string::npos);
  CHECK(f.ed->debug().mode() == DebugMode::Disconnected);
  // Breakpoints still work without a backend.
  f.ed->gotoLocation({f.doc->path(), {3, 0}});
  CHECK(f.ed->toggleBreakpoint());
}

TEST(dbg_factory_creates_backend_on_connect) {
  Fx f(false);
  int made = 0;
  f.ed->setDebuggerFactory([&]() -> std::unique_ptr<Debugger> {
    ++made;
    return std::make_unique<FakeDebugger>(f.fake);
  });
  CHECK(f.ed->debug().hasBackend());
  CHECK_EQ(made, 0);  // lazily
  f.ed->debug().setPollIntervals(2, 2);
  CHECK(f.connectAndWait());
  CHECK_EQ(made, 1);
  CHECK(f.ed->debug().status().find("fake") != std::string::npos);
  // A factory that yields nothing is an error, not a crash.
  Fx g(false);
  g.ed->setDebuggerFactory([]() -> std::unique_ptr<Debugger> { return nullptr; });
  CHECK(!g.ed->debug().connect());
}

TEST(dbg_commands_run_in_order_on_the_worker_thread) {
  Fx f;
  CHECK(f.ed->debug().connect());
  CHECK(f.ed->debug().start());
  CHECK(f.ed->debug().breakAll());
  CHECK(f.ed->debug().stepOver());
  CHECK(f.ed->debug().stepInto());
  CHECK(f.ed->debug().stepOut());
  CHECK(f.ed->debug().runToCursor(f.file(), 7));
  CHECK(f.ed->debug().restart());
  CHECK(f.ed->debug().startWithoutDebugging());
  CHECK(f.ed->debug().stop());
  CHECK(f.fake->waitUntil([](const FakeDebugger::State& s) { return s.count("stop") == 1; }, 3000));
  const std::vector<std::string> expect = {"connect",     "start",    "breakAll",
                                           "stepOver",    "stepInto", "stepOut",
                                           "runToCursor", "restart",  "startWithoutDebugging",
                                           "stop"};
  CHECK(f.fake->callNames() == expect);
  const auto me = std::this_thread::get_id();
  const auto calls = f.fake->calls();
  REQUIRE(!calls.empty());
  for (const auto& c : calls) CHECK(c.thread != me);
  for (const auto& c : calls) CHECK(c.thread == calls.front().thread);  // one worker thread
  CHECK(f.fake->callsOf("runToCursor")[0].detail == f.file().generic_string() + ":7");
  CHECK(f.untilMode(DebugMode::Design));
}

TEST(dbg_commands_are_asynchronous_and_do_not_block_the_caller) {
  Fx f;
  CHECK(f.connectAndWait());
  f.fake->blockOn("stepOver");
  const auto t0 = std::chrono::steady_clock::now();
  CHECK(f.ed->debug().stepOver());
  CHECK(f.ed->debug().stepInto());  // queued behind the blocked call
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
  CHECK(ms < 500);
  CHECK(f.fake->waitUntil([](const FakeDebugger::State& s) { return s.blockedNow() == 1; }, 3000));
  f.settle(30);
  CHECK_EQ(f.fake->count("stepInto"), static_cast<size_t>(0));
  f.fake->release();
  CHECK(f.fake->waitUntil([](const FakeDebugger::State& s) { return s.count("stepInto") == 1; }, 3000));
}

TEST(dbg_state_arrives_via_pump_only) {
  Fx f;
  CHECK(f.ed->debug().connect());
  // The worker connects, but the main thread sees nothing before it pumps.
  CHECK(f.fake->waitUntil([](const FakeDebugger::State& s) { return s.count("connect") == 1; }, 3000));
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  CHECK(f.ed->debug().mode() == DebugMode::Disconnected);
  CHECK(!f.ed->debug().connected());
  const uint64_t g0 = f.ed->debug().generation();
  CHECK(f.untilMode(DebugMode::Design));
  CHECK(f.ed->debug().connected());
  CHECK(f.ed->debug().generation() > g0);

  f.fake->setMode(DebugMode::Running);
  CHECK(f.untilMode(DebugMode::Running));
  CHECK(!f.ed->debug().currentStatement());
  f.fake->setMode(DebugMode::Break, DebugLocation{f.file(), 9});
  CHECK(f.untilMode(DebugMode::Break));
  REQUIRE(f.ed->debug().currentStatement().has_value());
  CHECK_EQ(f.ed->debug().currentStatement()->line, 9);
  const std::string st = f.ed->debug().status();
  CHECK(st.find("Break") != std::string::npos);
  CHECK(st.find("a.cpp:10") != std::string::npos);  // shown 1-based
  f.fake->setMode(DebugMode::Running);
  CHECK(f.untilMode(DebugMode::Running));
  CHECK(!f.ed->debug().currentStatement());  // only meaningful in Break
}

TEST(dbg_connect_passes_workspace_info) {
  Fx f;
  std::ofstream(f.dir / "proj.sln") << "";
  f.ed->debug().setSolutionPath(f.dir / "proj.sln");
  CHECK(f.connectAndWait());
  const DebugWorkspaceInfo w = f.fake->workspace();
  CHECK(w.root == f.dir);
  CHECK(w.solution == f.dir / "proj.sln");
}

TEST(dbg_follow_current_statement_lands_in_navigation_history) {
  Fx f;
  CHECK(f.connectAndWait());
  f.ed->gotoLocation({f.file(), {2, 0}});
  f.fake->setBreakStatement({f.file("b.cpp"), 17});
  CHECK(f.ed->debug().breakAll());
  CHECK(f.untilMode(DebugMode::Break));
  CHECK(f.until([&] { return f.ed->cursor().pos.line == 17; }));
  CHECK(f.ed->cursor().file.filename() == "b.cpp");
  CHECK(f.ed->currentDocument() != nullptr && f.ed->currentDocument()->path().filename() == "b.cpp");
  // The jump went through gotoLocation: Back returns to where we were.
  CHECK(f.ed->back());
  CHECK(f.ed->cursor().file.filename() == "a.cpp");
  CHECK_EQ(f.ed->cursor().pos.line, 2);
  CHECK(f.ed->forward());
  CHECK_EQ(f.ed->cursor().pos.line, 17);

  // A step to another statement jumps again; staying put does not move the user's caret.
  f.ed->gotoLocation({f.file(), {30, 0}});
  f.fake->setBreakStatement({f.file("b.cpp"), 18});
  CHECK(f.ed->debug().stepOver());
  CHECK(f.until([&] { return f.ed->cursor().pos.line == 18; }));
  f.ed->gotoLocation({f.file(), {5, 0}});
  f.settle(40);  // the same statement is still current: no more jumps
  CHECK_EQ(f.ed->cursor().pos.line, 5);
}

TEST(dbg_follow_can_be_switched_off) {
  Fx f;
  CHECK(f.connectAndWait());
  f.ed->debug().followCurrentStatement = false;
  f.fake->setBreakStatement({f.file("b.cpp"), 17});
  CHECK(f.ed->debug().breakAll());
  CHECK(f.untilMode(DebugMode::Break));
  f.settle(30);
  CHECK(f.ed->cursor().file.filename() == "a.cpp");
  CHECK_EQ(f.ed->cursor().pos.line, 0);
  REQUIRE(f.ed->debug().currentStatement().has_value());  // still exposed for the UI
  CHECK_EQ(f.ed->debug().currentStatement()->line, 17);
}

TEST(dbg_follow_unopenable_file_is_an_error_not_a_crash) {
  Fx f;
  CHECK(f.connectAndWait());
  f.fake->setBreakStatement({f.dir / "nope.cpp", 3});
  f.ed->debug().breakAll();
  CHECK(f.untilMode(DebugMode::Break));
  CHECK(f.until([&] { return !f.ed->debug().lastError().empty(); }));
  CHECK(f.ed->debug().lastError().find("current statement") != std::string::npos);
}

TEST(dbg_error_propagation) {
  Fx f;
  CHECK(f.connectAndWait());
  f.fake->failNext("stepOver", "no process");
  DebugResult got;
  bool called = false;
  CHECK(f.ed->debug().stepOver([&](const DebugResult& r) {
    got = r;
    called = true;
  }));
  CHECK(f.until([&] { return called; }));
  CHECK(!got.ok);
  CHECK_EQ(got.error, std::string("no process"));
  CHECK_EQ(f.ed->debug().lastError(), std::string("stepOver: no process"));
  CHECK(f.ed->debug().status().find("no process") != std::string::npos);
  // The next successful command clears it.
  const uint64_t g = f.ed->debug().generation();
  called = false;
  f.ed->debug().start([&](const DebugResult& r) {
    got = r;
    called = true;
  });
  CHECK(f.until([&] { return called; }));
  CHECK(got.ok);
  CHECK(f.ed->debug().lastError().empty());
  CHECK(f.ed->debug().generation() > g);

  // Backend-side asynchronous errors and poll failures surface too.
  f.fake->setLastError("symbols not loaded");
  CHECK(f.until([&] { return f.ed->debug().lastError() == "symbols not loaded"; }));
  f.ed->debug().clearError();
  f.fake->failNext("poll", "rpc gone", -1);
  CHECK(f.until([&] { return f.ed->debug().lastError() == "poll: rpc gone"; }));
  f.fake->failNext("poll", "", 0);
}

TEST(dbg_failed_connect_leaves_session_disconnected_and_retryable) {
  Fx f;
  f.fake->failNext("connect", "VS not installed");
  bool called = false;
  f.ed->debug().connect([&](const DebugResult& r) { called = !r.ok; });
  CHECK(f.until([&] { return called; }));
  CHECK(f.ed->debug().lastError() == "connect: VS not installed");
  CHECK(!f.ed->debug().connected());
  CHECK(f.ed->debug().mode() == DebugMode::Disconnected);
  CHECK(f.connectAndWait());  // second attempt works
}

TEST(dbg_commands_connect_implicitly) {
  Fx f;
  CHECK(f.ed->debug().start());  // F5 without an explicit connect
  CHECK(f.untilMode(DebugMode::Running));
  CHECK((f.fake->callNames() == std::vector<std::string>{"connect", "start"}));
}

TEST(dbg_disconnect_and_reconnect) {
  Fx f;
  CHECK(f.connectAndWait());
  CHECK(f.ed->debug().disconnect());
  CHECK(f.ed->debug().mode() == DebugMode::Disconnected);
  CHECK(f.until([&] { return f.fake->count("disconnect") == 1; }));
  f.settle(30);
  CHECK(f.ed->debug().mode() == DebugMode::Disconnected);  // no stale snapshot revives it
  CHECK(f.connectAndWait());
}

// ---- breakpoint sync ------------------------------------------------------------------------------------------

TEST(dbg_tenfold_edits_push_to_the_backend) {
  Fx f;
  CHECK(f.connectAndWait());
  f.ed->gotoLocation({f.file(), {5, 0}});
  f.ed->toggleBreakpoint();
  CHECK(f.fake->waitUntil([](const FakeDebugger::State& s) { return s.breakpoints().size() == 1; }, 3000));
  CHECK((Fx::fakeLines(f) == std::vector<int>{5}));
  f.ed->breakpoints().setCondition(f.file(), 5, "i == 3");
  f.ed->breakpoints().setEnabled(f.file(), 5, false);
  CHECK(f.fake->waitUntil(
      [](const FakeDebugger::State& s) {
        auto v = s.breakpoints();
        return v.size() == 1 && v[0].condition == "i == 3" && !v[0].enabled;
      },
      3000));
  f.ed->toggleBreakpoint();
  CHECK(f.fake->waitUntil([](const FakeDebugger::State& s) { return s.breakpoints().empty(); }, 3000));
  CHECK(f.ed->breakpoints().empty());
}

TEST(dbg_connect_pushes_existing_breakpoints_and_adopts_backend_ones) {
  Fx f;
  f.ed->breakpoints().add({f.file(), 4, true, "x"});
  f.ed->breakpoints().add({f.file("b.cpp"), 9, true, {}});
  f.fake->injectBreakpoint({f.file(), 20, true, "y"});  // set in the debugger earlier
  CHECK(f.connectAndWait());
  CHECK(f.fake->waitUntil([](const FakeDebugger::State& s) { return s.breakpoints().size() == 3; }, 3000));
  CHECK(f.until([&] { return f.ed->breakpoints().count() == 3; }));
  CHECK((f.ed->breakpoints().lines(f.file()) == std::vector<int>{4, 20}));
  CHECK(f.ed->breakpoints().get(f.file(), 20)->condition == "y");
  CHECK(f.ed->breakpoints().has(f.file("b.cpp"), 9));
  // Nothing pushed back for the adopted one.
  f.settle(40);
  CHECK_EQ(f.fake->count("setBreakpoint"), static_cast<size_t>(2));
}

TEST(dbg_backend_side_changes_update_tenfold_without_echo) {
  Fx f;
  CHECK(f.connectAndWait());
  f.fake->injectBreakpoint({f.file(), 12, true, {}});
  CHECK(f.until([&] { return f.ed->breakpoints().has(f.file(), 12); }));
  f.fake->injectBreakpoint({f.file(), 12, false, "n > 2"});  // changed in the debugger
  CHECK(f.until([&] {
    auto b = f.ed->breakpoints().get(f.file(), 12);
    return b && !b->enabled && b->condition == "n > 2";
  }));
  f.fake->injectBreakpoint({f.file("b.cpp"), 3, true, {}});
  CHECK(f.until([&] { return f.ed->breakpoints().count() == 2; }));
  f.fake->injectRemoveBreakpoint(f.file(), 12);  // deleted in the debugger
  CHECK(f.until([&] { return !f.ed->breakpoints().has(f.file(), 12); }));
  CHECK(f.ed->breakpoints().has(f.file("b.cpp"), 3));
  // Persisted after adoption.
  Editor other(f.dir);
  other.open("a.cpp");
  CHECK(other.breakpoints().has(f.file("b.cpp"), 3));
  // The tenfold side never pushed any of it back, however many polls passed.
  f.settle(60);
  CHECK_EQ(f.fake->count("setBreakpoint"), static_cast<size_t>(0));
  CHECK_EQ(f.fake->count("removeBreakpoint"), static_cast<size_t>(0));
}

TEST(dbg_poll_that_overtakes_a_push_does_not_undo_it) {
  Fx f;
  CHECK(f.connectAndWait());
  // Blocking setBreakpoint keeps the push queued while polls (before it) have already been posted.
  f.fake->blockOn("setBreakpoint");
  f.ed->gotoLocation({f.file(), {6, 0}});
  f.ed->toggleBreakpoint();
  CHECK(f.fake->waitUntil([](const FakeDebugger::State& s) { return s.blockedNow() == 1; }, 3000));
  f.settle(30);  // posted snapshots from before the push are processed meanwhile
  CHECK(f.ed->breakpoints().has(f.file(), 6));
  f.fake->release();
  CHECK(f.fake->waitUntil([](const FakeDebugger::State& s) { return s.breakpoints().size() == 1; }, 3000));
  f.settle(40);
  CHECK(f.ed->breakpoints().has(f.file(), 6));
}

TEST(dbg_rapid_toggling_converges) {
  Fx f;
  CHECK(f.connectAndWait());
  f.ed->debug().setPollIntervals(1, 1);
  unsigned seed = 12345;
  for (int i = 0; i < 300; ++i) {
    seed = seed * 1103515245u + 12345u;
    const int line = static_cast<int>((seed >> 16) % 8);
    if (i % 3 == 0) f.ed->breakpoints().toggle(f.file(), line);
    else if (i % 3 == 1) f.ed->breakpoints().add({f.file("b.cpp"), line, (i & 1) == 0, std::to_string(i)});
    else f.ed->breakpoints().remove(f.file("b.cpp"), line);
    if (i % 10 == 0) f.ed->pump();
  }
  // Both sides settle on the same set.
  auto same = [&] {
    auto mine = f.ed->breakpoints().all();
    auto theirs = f.fake->breakpoints();
    if (mine.size() != theirs.size()) return false;
    for (const Breakpoint& b : mine) {
      bool found = false;
      for (const Breakpoint& t : theirs)
        if (t.file == b.file && t.line == b.line && t.enabled == b.enabled && t.condition == b.condition) found = true;
      if (!found) return false;
    }
    return true;
  };
  CHECK(f.until(same, 5000));
  f.settle(60);
  CHECK(same());
}

TEST(dbg_edits_move_breakpoints_and_resync_the_backend) {
  Fx f;
  CHECK(f.connectAndWait());
  f.ed->breakpoints().add({f.file(), 5, true, "c"});
  f.ed->breakpoints().add({f.file(), 9, true, {}});
  CHECK(f.fake->waitUntil([](const FakeDebugger::State& s) { return s.breakpoints().size() == 2; }, 3000));
  f.ed->gotoLocation({f.file(), {0, 0}});
  f.ed->insertText("new\n");  // one line above both
  CHECK((f.ed->breakpoints().lines(f.file()) == std::vector<int>{6, 10}));
  CHECK(
      f.fake->waitUntil([&](const FakeDebugger::State&) { return Fx::fakeLines(f) == std::vector<int>{6, 10}; }, 3000));
  auto bps = f.fake->breakpoints();
  for (const Breakpoint& b : bps)
    if (b.line == 6) CHECK(b.condition == "c");  // properties travel with the breakpoint
  // Nothing was lost to the backend echoing a stale list.
  f.settle(40);
  CHECK((f.ed->breakpoints().lines(f.file()) == std::vector<int>{6, 10}));
  // Removing the lines they sit on collapses them onto one line.
  f.ed->deleteRange({{5, 0}, {11, 0}});
  CHECK_EQ(f.ed->breakpoints().count(), static_cast<size_t>(1));
  CHECK(f.fake->waitUntil([&](const FakeDebugger::State&) { return Fx::fakeLines(f) == std::vector<int>{5}; }, 3000));
  CHECK((f.ed->breakpoints().lines(f.file()) == std::vector<int>{5}));
}

TEST(dbg_failed_breakpoint_push_keeps_the_breakpoint_and_reports) {
  Fx f;
  CHECK(f.connectAndWait());
  f.fake->failNext("setBreakpoint", "cannot bind", 1);
  f.ed->gotoLocation({f.file(), {8, 0}});
  f.ed->toggleBreakpoint();
  CHECK(f.until([&] { return f.ed->debug().lastError() == "setBreakpoint: cannot bind"; }));
  f.settle(60);  // polls that do not list it must not delete it from tenfold
  CHECK(f.ed->breakpoints().has(f.file(), 8));
  // A later change retries the push.
  f.ed->breakpoints().setCondition(f.file(), 8, "k");
  CHECK(f.fake->waitUntil([](const FakeDebugger::State& s) { return s.breakpoints().size() == 1; }, 3000));
}

TEST(dbg_backend_going_away_does_not_wipe_breakpoints) {
  Fx f;
  f.ed->breakpoints().add({f.file(), 4, true, {}});
  CHECK(f.connectAndWait());
  CHECK(f.fake->waitUntil([](const FakeDebugger::State& s) { return s.breakpoints().size() == 1; }, 3000));
  f.fake->setMode(DebugMode::Disconnected);  // the debugger instance was closed
  f.fake->injectRemoveBreakpoint(f.file(), 4);
  CHECK(f.untilMode(DebugMode::Disconnected));
  f.settle(30);
  CHECK(f.ed->breakpoints().has(f.file(), 4));
}

// ---- shutdown -------------------------------------------------------------------------------------------------

TEST(dbg_shutdown_with_a_blocked_call_does_not_hang) {
  auto fake = std::make_shared<FakeDebugger::State>();
  const auto t0 = std::chrono::steady_clock::now();
  {
    Fx f(false);
    f.ed->debug().setPollIntervals(2, 2);
    f.ed->debug().setBackend(std::make_unique<FakeDebugger>(f.fake));
    fake = f.fake;
    CHECK(f.connectAndWait());
    f.fake->blockOn("stepOver", 30000);
    CHECK(f.ed->debug().stepOver());
    CHECK(f.fake->waitUntil([](const FakeDebugger::State& s) { return s.blockedNow() == 1; }, 3000));
  }  // ~Editor: cancel() wakes the call, the worker is joined
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
  CHECK(ms < 2500);
  CHECK(fake->cancelCalled());
  CHECK(fake->destroyed());  // the backend was destroyed (on its worker thread)
  CHECK_EQ(fake->blockedNow(), 0);
}

TEST(dbg_shutdown_abandons_a_worker_that_ignores_cancel) {
  auto fake = std::make_shared<FakeDebugger::State>();
  fake->ignoreCancel(true);
  const auto t0 = std::chrono::steady_clock::now();
  {
    Fx f(false);
    f.ed->debug().setPollIntervals(2, 2);
    f.ed->debug().setShutdownTimeout(150);
    f.fake = fake;
    f.ed->debug().setBackend(std::make_unique<FakeDebugger>(fake));
    CHECK(f.connectAndWait());
    fake->blockOn("stepOver", 30000);
    CHECK(f.ed->debug().stepOver());
    CHECK(f.ed->debug().stepInto());  // queued; dropped on shutdown
    CHECK(fake->waitUntil([](const FakeDebugger::State& s) { return s.blockedNow() == 1; }, 3000));
  }  // returns after the timeout although the call is still blocked
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
  CHECK(ms < 2500);
  CHECK(!fake->destroyed());
  CHECK_EQ(fake->blockedNow(), 1);
  fake->release();  // the abandoned worker finishes by itself and tidies up
  CHECK(fake->waitUntil([](const FakeDebugger::State& s) { return s.destroyed(); }, 3000));
  CHECK_EQ(fake->count("stepInto"), static_cast<size_t>(0));
}

TEST(dbg_shutdown_disconnects_the_backend_and_posts_nothing_more) {
  Fx f;
  CHECK(f.connectAndWait());
  f.fake->setMode(DebugMode::Running);
  CHECK(f.untilMode(DebugMode::Running));
  f.ed->shutdown();
  CHECK(f.fake->count("disconnect") == 1);
  CHECK(f.fake->destroyed());
  const uint64_t g = f.ed->debug().generation();
  f.fake->setMode(DebugMode::Break, DebugLocation{f.file(), 3});
  f.settle(30);
  CHECK_EQ(f.ed->debug().generation(), g);
  CHECK(!f.ed->debug().start());  // no backend left (the test installed it directly, no factory)
}

TEST(dbg_session_destroyed_with_pending_posts_is_safe) {
  Fx f;
  CHECK(f.connectAndWait());
  f.fake->setMode(DebugMode::Running);
  CHECK(f.fake->waitUntil([](const FakeDebugger::State& s) { return s.count("poll") > 3; }, 3000));
  f.ed->debug().shutdown();  // posts already queued on the editor refer to the dead worker: they must be ignored
  f.ed->pump();
  CHECK(f.ed->debug().mode() != DebugMode::Running);
}

// Regression: the same file spelled differently (macOS /var -> /private/var, Windows 8.3 names) is one file.
TEST(dbg_breakpoint_paths_are_canonical_across_spellings) {
  Fx f;
  const fs::path link = f.dir.parent_path() / (f.dir.filename().string() + "_link");
  std::error_code ec;
  fs::create_directory_symlink(f.dir, link, ec);
  if (ec) return;  // no symlink privilege (Windows without developer mode)
  struct Cleanup {
    fs::path p;
    ~Cleanup() {
      std::error_code e;
      fs::remove(p, e);
    }
  } cleanup{link};
  const fs::path viaLink = link / "a.cpp";
  CHECK(f.connectAndWait());
  CHECK(f.ed->debug().connect());
  f.ed->breakpoints().add({viaLink, 5, true, {}});
  CHECK(f.ed->breakpoints().has(f.file(), 5));  // either spelling finds it
  CHECK(f.ed->breakpoints().has(viaLink, 5));
  f.ed->breakpoints().add({f.file(), 5, true, "c"});  // updates, does not duplicate
  CHECK_EQ(f.ed->breakpoints().count(), static_cast<size_t>(1));
  CHECK(f.fake->waitUntil(
      [](const FakeDebugger::State& s) {
        auto v = s.breakpoints();
        return v.size() == 1 && v[0].condition == "c";
      },
      3000));
  const size_t setsBefore = f.fake->count("setBreakpoint");
  // The backend reports it under the other spelling: still the same breakpoint, nothing is added or pushed back.
  f.fake->injectBreakpoint({viaLink, 5, true, "d"});
  CHECK(f.until([&] {
    auto b = f.ed->breakpoints().get(f.file(), 5);
    return b && b->condition == "d";
  }));
  CHECK_EQ(f.ed->breakpoints().count(), static_cast<size_t>(1));
  CHECK_EQ(f.fake->breakpoints().size(), static_cast<size_t>(1));
  f.settle(40);
  CHECK_EQ(f.fake->count("setBreakpoint"), setsBefore);
  CHECK_EQ(f.fake->count("removeBreakpoint"), static_cast<size_t>(0));
  // Removing via the other spelling works and reaches the backend.
  CHECK(f.ed->breakpoints().remove(viaLink, 5));
  CHECK(f.fake->waitUntil([](const FakeDebugger::State& s) { return s.breakpoints().empty(); }, 3000));
  // Edits to the document (opened under the canonical spelling) move a breakpoint added via the link.
  f.ed->breakpoints().add({viaLink, 9, true, {}});
  f.ed->gotoLocation({f.file(), {0, 0}});
  f.ed->insertText("x\n");
  CHECK((f.ed->breakpoints().lines(viaLink) == std::vector<int>{10}));
  CHECK(f.fake->waitUntil([&](const FakeDebugger::State&) { return Fx::fakeLines(f) == std::vector<int>{10}; }, 3000));
}
