// C3a: VcsManager through the Editor: detection, diff hunks that follow edits, debounce, stale results, revert.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>

#include "app/Editor.h"
#include "core/Document.h"
#include "test_main.h"
#include "test_vcs_util.h"
#include "vcs/GitProvider.h"
#include "vcs/VcsManager.h"
#include "workspace/Workspace.h"

using namespace tf;
using tfvcs::TempRepo;
namespace fs = std::filesystem;

namespace {

constexpr int kWaitMs = 15000;

std::string numbered(int n, const char* prefix = "line ") {
  std::string s;
  for (int i = 0; i < n; ++i) s += prefix + std::to_string(i) + "\n";
  return s;
}

// A committed repository with a.txt (10 lines) and an Editor on it.
struct Fx {
  TempRepo repo;
  std::unique_ptr<Editor> ed;
  fs::path a;

  explicit Fx(int debounceMs = 40, std::function<void(Editor::Options&)> tweak = {}, const std::string& text = {})
      : repo() {
    repo.write("a.txt", text.empty() ? numbered(10) : text);
    repo.write("b.txt", "other\n");
    repo.commitAll("base");
    a = repo.path("a.txt");
    Editor::Options o;
    o.vcs.debounceMs = debounceMs;
    o.vcs.statusDebounceMs = 100;
    if (tweak) tweak(o);
    ed = std::make_unique<Editor>(repo.dir, o);
  }
  Document* open(const std::string& rel = "a.txt") {
    Document* d = ed->open(repo.path(rel));
    if (d) ed->activateDocument(*d);
    return d;
  }
  bool waitBase(const fs::path& p) {
    return ed->waitFor([&] { return ed->vcs().hasBase(p) && ed->vcs().hunksVersion(p) != 0; }, kWaitMs);
  }
  bool waitHunks(const fs::path& p, size_t n) {
    return ed->waitFor([&] { return ed->vcs().hunks(p).size() == n && ed->vcs().hunksVersion(p) != 0; }, kWaitMs);
  }
};

}  // namespace

TEST(vcs_manager_detects_git_and_loads_base) {
  SKIP_WITHOUT_GIT();
  Fx fx;
  Document* d = fx.open();
  REQUIRE(d != nullptr);
  CHECK(fx.waitBase(fx.a));
  CHECK(fx.ed->vcs().active());
  CHECK_EQ(fx.ed->vcs().providerName(), std::string("git"));
  CHECK(pathsEqual(fx.ed->vcs().root(), fx.repo.dir));
  CHECK(fx.ed->vcs().hunks(fx.a).empty());
  CHECK(fx.ed->vcs().fileStatus(fx.a) == VcsStatus::Unmodified);
  CHECK_EQ(fx.ed->vcs().baseLines(fx.a)->size(), static_cast<size_t>(11));  // 10 lines + the empty one after the \n
  CHECK(fx.ed->vcs().fileStatus(fx.repo.dir.parent_path() / "outside.txt") == VcsStatus::Unknown);
  CHECK(fx.ed->vcs().generation() > 0);
}

TEST(vcs_manager_hunks_follow_edits) {
  SKIP_WITHOUT_GIT();
  Fx fx;
  Document* d = fx.open();
  REQUIRE(d != nullptr);
  REQUIRE(fx.waitBase(fx.a));
  const uint64_t gen0 = fx.ed->vcs().generation();

  d->replace({{2, 0}, {2, 6}}, "changed");  // line 2 modified
  REQUIRE(fx.waitHunks(fx.a, 1));
  auto hs = fx.ed->vcs().hunks(fx.a);
  CHECK(hs[0] == (DiffHunk{2, 1, 2, 1, HunkKind::Modified}));
  CHECK_EQ(fx.ed->vcs().hunksVersion(fx.a), d->version());
  CHECK(fx.ed->vcs().generation() > gen0);
  CHECK(fx.ed->vcs().fileStatus(fx.a) == VcsStatus::Modified);  // unsaved changes count
  CHECK(fx.ed->vcs().hunkBaseLines(fx.a, 0) == std::vector<std::string>{"line 2"});

  d->insert({6, 0}, "inserted\n");
  REQUIRE(fx.waitHunks(fx.a, 2));
  CHECK(fx.ed->vcs().hunks(fx.a)[1] == (DiffHunk{6, 0, 6, 1, HunkKind::Added}));

  d->erase({{8, 0}, {9, 0}});  // delete a line (the base line 7 now "line 7")
  REQUIRE(fx.waitHunks(fx.a, 3));
  CHECK(fx.ed->vcs().hunks(fx.a)[2].kind == HunkKind::Deleted);

  // Undo everything: no hunks any more.
  while (d->undo()) {}
  REQUIRE(fx.waitHunks(fx.a, 0));
  CHECK(fx.ed->vcs().fileStatus(fx.a) == VcsStatus::Unmodified);
}

TEST(vcs_manager_status_after_save_and_crlf_buffer) {
  SKIP_WITHOUT_GIT();
  Fx fx;
  Document* d = fx.open();
  REQUIRE(d != nullptr);
  REQUIRE(fx.waitBase(fx.a));
  d->replace({{0, 0}, {0, 4}}, "LINE");
  REQUIRE(fx.waitHunks(fx.a, 1));
  std::string err;
  REQUIRE(fx.ed->save(*d, &err));
  // The provider now reports Modified from disk.
  CHECK(fx.ed->waitFor([&] { return fx.ed->vcs().fileInfo(fx.a).status == VcsStatus::Modified; }, kWaitMs));
  CHECK_EQ(fx.ed->vcs().hunks(fx.a).size(), static_cast<size_t>(1));
  // Switching the buffer to CRLF changes no line content: the diff is line based and ignores the ending.
  fx.ed->setLineEnding(*d, LineEnding::CRLF);
  d->replace({{0, 0}, {0, 4}}, "line");  // back to the committed first line
  REQUIRE(fx.waitHunks(fx.a, 0));
}

TEST(vcs_manager_debounces_edits) {
  SKIP_WITHOUT_GIT();
  Fx fx(/*debounceMs=*/400);
  Document* d = fx.open();
  REQUIRE(d != nullptr);
  REQUIRE(fx.waitBase(fx.a));
  const int before = fx.ed->vcs().stats().diffsComputed;
  for (int i = 0; i < 40; ++i) d->insert({0, 0}, "x");  // a burst of keystrokes
  REQUIRE(fx.waitHunks(fx.a, 1));
  std::this_thread::sleep_for(std::chrono::milliseconds(600));  // a second recompute would have happened by now
  fx.ed->pump();
  const int computed = fx.ed->vcs().stats().diffsComputed - before;
  CHECK(computed >= 1);
  CHECK(computed <= 2);  // 40 edits, one (at most two on a very slow machine) diffs
  CHECK_EQ(fx.ed->vcs().hunksVersion(fx.a), d->version());
}

TEST(vcs_manager_drops_stale_results) {
  SKIP_WITHOUT_GIT();
  std::atomic<int> calls{0};
  std::atomic<bool> entered{false};
  std::mutex mu;
  std::condition_variable cv;
  bool release = false;
  Fx fx(40, [&](Editor::Options& o) {
    o.vcs.diffHook = [&] {
      if (calls++ != 0) return;  // only the first diff is held back
      entered = true;
      std::unique_lock<std::mutex> lk(mu);
      cv.wait(lk, [&] { return release; });
    };
  });
  Document* d = fx.open();
  REQUIRE(d != nullptr);
  for (int i = 0; i < 15000 && !entered; ++i) {  // the base result must be pumped before the first diff starts
    fx.ed->pump();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  REQUIRE(entered.load());               // diff #1 (of the unchanged text) is running
  d->replace({{1, 0}, {1, 4}}, "EDIT");  // the document moves on meanwhile
  {
    std::lock_guard<std::mutex> lk(mu);
    release = true;
  }
  cv.notify_all();
  REQUIRE(fx.waitHunks(fx.a, 1));  // the recompute for the edit arrives
  CHECK(fx.ed->vcs().hunks(fx.a)[0] == (DiffHunk{1, 1, 1, 1, HunkKind::Modified}));
  CHECK_EQ(fx.ed->vcs().stats().staleDropped, 1);  // the first result was thrown away, not shown
  CHECK_EQ(fx.ed->vcs().hunksVersion(fx.a), d->version());
}

TEST(vcs_manager_revert_hunk_is_one_undo_step) {
  SKIP_WITHOUT_GIT();
  Fx fx;
  Document* d = fx.open();
  REQUIRE(d != nullptr);
  REQUIRE(fx.waitBase(fx.a));
  d->replace({{1, 0}, {1, 6}}, "one\ntwo\nthree");  // hunk 0: 1 line -> 3 lines
  d->insert({6, 0}, "added\n");                     // hunk 1
  d->erase({{9, 0}, {10, 0}});                      // hunk 2: a deleted line (coordinates after the insert shifted)
  REQUIRE(fx.waitHunks(fx.a, 3));
  const std::string edited = d->buffer().text();

  std::string err;
  // Revert the middle hunk (current document: carets follow through Editor::replaceRange).
  REQUIRE(fx.ed->vcs().revertHunk(fx.a, 1, &err));
  CHECK_EQ(fx.ed->vcs().hunks(fx.a).size(), static_cast<size_t>(2));  // exact at once, no waiting
  CHECK_EQ(fx.ed->vcs().hunksVersion(fx.a), d->version());
  CHECK(d->buffer().text().find("added") == std::string::npos);
  // One undo step brings exactly that hunk back.
  REQUIRE(d->undo());
  CHECK_EQ(d->buffer().text(), edited);

  // Revert all hunks, always the first; the buffer ends up equal to the committed text.
  for (int i = 0; i < 3; ++i) REQUIRE(fx.ed->vcs().revertHunk(fx.a, 0, &err));
  CHECK_EQ(d->buffer().text(), numbered(10));
  CHECK(fx.ed->vcs().hunks(fx.a).empty());
  CHECK(!fx.ed->vcs().revertHunk(fx.a, 0, &err));  // no such change any more
  CHECK(!err.empty());
  // Each of them was one step: three undos restore the edited text.
  for (int i = 0; i < 3; ++i) REQUIRE(d->undo());
  CHECK_EQ(d->buffer().text(), edited);

  // A document that is not the current one is reverted through the Document directly, also in one step.
  Document* b = fx.ed->open(fx.repo.path("b.txt"));
  REQUIRE(b != nullptr);
  fx.ed->activateDocument(*b);
  REQUIRE(fx.waitHunks(fx.a, 3));
  REQUIRE(fx.ed->vcs().revertHunk(fx.a, 2, &err));
  CHECK_EQ(fx.ed->vcs().hunks(fx.a).size(), static_cast<size_t>(2));
  REQUIRE(d->undo());
  CHECK_EQ(d->buffer().text(), edited);
}

TEST(vcs_manager_revert_hunk_deleted_at_end_and_stale_hunks) {
  SKIP_WITHOUT_GIT();
  Fx fx;
  Document* d = fx.open();
  REQUIRE(d != nullptr);
  REQUIRE(fx.waitBase(fx.a));
  // Remove the last two real lines, then revert without waiting for the debounced diff: stale hunks are recomputed.
  d->erase({{8, 0}, {10, 0}});
  std::string err;
  REQUIRE(fx.ed->vcs().revertHunk(fx.a, 0, &err));
  CHECK_EQ(d->buffer().text(), numbered(10));
}

TEST(vcs_manager_before_first_edit_runs_once_off_thread) {
  SKIP_WITHOUT_GIT();
  struct Shared {
    std::atomic<int> calls{0};
    std::atomic<bool> offMain{false};
    std::atomic<bool> fail{false};
  };
  auto sh = std::make_shared<Shared>();
  const std::thread::id mainId = std::this_thread::get_id();
  class Fake : public VcsProvider {
   public:
    Fake(std::shared_ptr<Shared> s, std::thread::id main) : s_(std::move(s)), main_(main) {}
    std::string name() const override { return "fake"; }
    bool detect(const fs::path& p) override {
      root_ = p;
      return true;
    }
    fs::path root() const override { return root_; }
    std::vector<VcsFileInfo> status(const std::vector<fs::path>& files) override {
      std::vector<VcsFileInfo> v;
      for (auto& f : files) v.push_back({f, VcsStatus::Unmodified, {}});
      return v;
    }
    std::optional<std::string> baseText(const fs::path&, std::string*) override { return std::string("a\nb\n"); }
    bool revert(const fs::path&, std::string*) override { return true; }
    bool beforeFirstEdit(const fs::path&, std::string* err) override {
      ++s_->calls;
      if (std::this_thread::get_id() != main_) s_->offMain = true;
      std::this_thread::sleep_for(std::chrono::milliseconds(300));  // a slow p4 edit
      if (s_->fail) {
        if (err) *err = "p4 edit failed";
        return false;
      }
      return true;
    }

   private:
    std::shared_ptr<Shared> s_;
    std::thread::id main_;
    fs::path root_;
  };
  fs::path dir = tfvcs::tempDir("fake");
  tfvcs::writeFile(dir / "f.txt", "a\nb\n");
  Editor::Options o;
  o.vcs.debounceMs = 30;
  o.vcs.providers = {[sh, mainId] { return std::make_unique<Fake>(sh, mainId); }};
  Editor ed(dir, o);
  Document* d = ed.open(dir / "f.txt");
  REQUIRE(d != nullptr);
  std::string lastMsg;
  ed.vcs().onError([&](const fs::path&, const std::string& m) { lastMsg = m; });
  REQUIRE(ed.waitFor([&] { return ed.vcs().hasBase(d->path()); }, kWaitMs));
  CHECK_EQ(sh->calls.load(), 0);  // opening is not editing

  auto t0 = std::chrono::steady_clock::now();
  d->insert({0, 0}, "x");  // first edit: the document becomes dirty
  const auto editMs =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
  CHECK(editMs < 150);  // the 300 ms hook did not run on this thread
  d->insert({0, 0}, "y");
  d->insert({1, 0}, "z");
  REQUIRE(ed.waitFor([&] { return sh->calls.load() >= 1; }, kWaitMs));
  std::string err;
  REQUIRE(ed.save(*d, &err));
  d->insert({0, 0}, "w");  // dirty again after the save: still the same document, still once
  std::this_thread::sleep_for(std::chrono::milliseconds(700));
  ed.pump();
  CHECK_EQ(sh->calls.load(), 1);
  CHECK(sh->offMain.load());
  CHECK_EQ(ed.vcs().stats().beforeFirstEditCalls, 1);
  CHECK(lastMsg.empty());

  // An external reload is not an edit; and after it the next real edit is a first edit again.
  tfvcs::writeFile(dir / "f.txt", "changed outside\n");
  REQUIRE(ed.reloadFromDisk(*d, &err));
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  ed.pump();
  CHECK_EQ(sh->calls.load(), 1);
  sh->fail = true;
  d->insert({0, 0}, "v");
  REQUIRE(ed.waitFor([&] { return sh->calls.load() == 2; }, kWaitMs));
  REQUIRE(ed.waitFor([&] { return !lastMsg.empty(); }, kWaitMs));  // failure reported, edit kept
  CHECK_EQ(lastMsg, std::string("p4 edit failed"));
  CHECK_EQ(d->buffer().line(0).substr(0, 1), std::string("v"));
  std::error_code ec;
  ed.vcs().shutdown();
  fs::remove_all(dir, ec);
}

TEST(vcs_manager_revert_file_reloads_document) {
  SKIP_WITHOUT_GIT();
  Fx fx;
  Document* d = fx.open();
  REQUIRE(d != nullptr);
  REQUIRE(fx.waitBase(fx.a));
  int externalChanges = 0;
  fx.ed->onExternalChange([&](Document&) { ++externalChanges; });
  d->replace({{3, 0}, {3, 4}}, "EDIT");
  std::string err;
  REQUIRE(fx.ed->save(*d, &err));
  d->insert({0, 0}, "unsaved ");
  REQUIRE(fx.waitHunks(fx.a, 2));

  bool done = false, ok = false;
  std::string msg;
  fx.ed->vcs().revertFile(fx.a, [&](bool good, const std::string& e) {
    done = true;
    ok = good;
    msg = e;
  });
  REQUIRE(fx.ed->waitFor([&] { return done; }, kWaitMs));
  CHECK(ok);
  CHECK(msg.empty());
  CHECK_EQ(d->buffer().text(), numbered(10));
  CHECK_EQ(fx.repo.read("a.txt"), numbered(10));
  CHECK(!d->dirty());
  CHECK(!d->canUndo());  // like an external reload
  REQUIRE(fx.waitHunks(fx.a, 0));
  CHECK(fx.ed->waitFor([&] { return fx.ed->vcs().fileInfo(fx.a).status == VcsStatus::Unmodified; }, kWaitMs));
  fx.ed->pump();
  CHECK_EQ(externalChanges, 0);

  // A failing revert reports and keeps the buffer.
  fx.repo.write("u.txt", "untracked\n");
  Document* u = fx.ed->open(fx.repo.path("u.txt"));
  REQUIRE(u != nullptr);
  u->insert({0, 0}, "e");
  done = false;
  fx.ed->vcs().revertFile(fx.repo.path("u.txt"), [&](bool good, const std::string& e) {
    done = true;
    ok = good;
    msg = e;
  });
  REQUIRE(fx.ed->waitFor([&] { return done; }, kWaitMs));
  CHECK(!ok);
  CHECK(!msg.empty());
  CHECK(u->dirty());
}

TEST(vcs_manager_no_repository) {
  SKIP_WITHOUT_GIT();
  fs::path dir = tfvcs::tempDir("norepo");
  tfvcs::writeFile(dir / "f.txt", "a\nb\n");
  {
    Editor ed(dir);
    Document* d = ed.open(dir / "f.txt");
    REQUIRE(d != nullptr);
    d->insert({0, 0}, "x");
    ed.waitFor([] { return false; }, 300);
    CHECK(!ed.vcs().active());
    CHECK(ed.vcs().fileStatus(dir / "f.txt") == VcsStatus::Unknown);
    CHECK(ed.vcs().hunks(dir / "f.txt").empty());
    CHECK(!ed.vcs().hasBase(dir / "f.txt"));
    std::string err;
    CHECK(!ed.vcs().revertHunk(dir / "f.txt", 0, &err));
    bool done = false, ok = true;
    ed.vcs().revertFile(dir / "f.txt", [&](bool good, const std::string&) {
      done = true;
      ok = good;
    });
    CHECK(ed.waitFor([&] { return done; }, kWaitMs));
    CHECK(!ok);
  }
  // Disabled: nothing starts at all.
  {
    Editor::Options o;
    o.vcs.enabled = false;
    Editor ed(dir, o);
    CHECK(ed.open(dir / "f.txt") != nullptr);
    ed.waitFor([] { return false; }, 100);
    CHECK(!ed.vcs().active());
  }
  std::error_code ec;
  fs::remove_all(dir, ec);
}

TEST(vcs_manager_untracked_and_new_files) {
  SKIP_WITHOUT_GIT();
  Fx fx;
  fx.repo.write("new.txt", "brand new\nfile\n");
  fx.repo.write("staged.txt", "staged new\nfile\n");
  REQUIRE(fx.repo.git({"add", "staged.txt"}).ok());
  Document* u = fx.open("new.txt");
  Document* s = fx.open("staged.txt");
  REQUIRE(u != nullptr);
  REQUIRE(s != nullptr);
  const fs::path pu = fx.repo.path("new.txt"), ps = fx.repo.path("staged.txt");
  CHECK(fx.ed->waitFor([&] { return fx.ed->vcs().fileStatus(pu) == VcsStatus::Untracked; }, kWaitMs));
  CHECK(fx.ed->waitFor([&] { return fx.ed->vcs().fileStatus(ps) == VcsStatus::Added; }, kWaitMs));
  CHECK(!fx.ed->vcs().hasBase(pu));  // untracked: nothing to compare with, no marks
  CHECK(fx.ed->vcs().hunks(pu).empty());
  REQUIRE(fx.waitBase(ps));  // added to the index: the whole file is new
  auto hs = fx.ed->vcs().hunks(ps);
  REQUIRE(hs.size() == 1);
  CHECK(hs[0] == (DiffHunk{0, 0, 0, 3, HunkKind::Added}));
}

TEST(vcs_manager_refresh_status_sees_outside_changes) {
  SKIP_WITHOUT_GIT();
  Fx fx;
  Document* d = fx.open();
  REQUIRE(d != nullptr);
  REQUIRE(fx.waitBase(fx.a));
  fx.repo.write("b.txt", "other changed\n");
  fx.repo.write("fresh.txt", "x\n");
  fx.ed->vcs().refreshStatus();
  CHECK(fx.ed->waitFor([&] { return fx.ed->vcs().fileStatus(fx.repo.path("b.txt")) == VcsStatus::Modified; }, kWaitMs));
  CHECK(fx.ed->vcs().fileStatus(fx.repo.path("fresh.txt")) == VcsStatus::Untracked);
  CHECK(fx.ed->vcs().fileStatus(fx.a) == VcsStatus::Unmodified);

  // A commit made elsewhere moves the base of an open document.
  d->replace({{0, 0}, {0, 4}}, "LINE");
  std::string err;
  REQUIRE(fx.ed->save(*d, &err));
  REQUIRE(fx.waitHunks(fx.a, 1));
  fx.repo.git({"add", "a.txt"});
  REQUIRE(fx.repo.git({"commit", "-q", "-m", "second"}).ok());
  fx.ed->vcs().refreshStatus();
  CHECK(fx.ed->waitFor([&] { return fx.ed->vcs().hunks(fx.a).empty() && fx.ed->vcs().hunksVersion(fx.a) != 0; },
                       kWaitMs));
  CHECK(fx.ed->waitFor([&] { return fx.ed->vcs().fileStatus(fx.a) == VcsStatus::Unmodified; }, kWaitMs));
}

TEST(vcs_manager_file_watcher_refreshes_status) {
  SKIP_WITHOUT_GIT();
  if (findExecutable("clangd").empty()) {  // Editor::start() needs clangd; the watcher only runs after start()
    std::cerr << "    SKIPPED: clangd not found on PATH\n";
    return;
  }
  Fx fx(40, [](Editor::Options& o) {
    o.watchForcePolling = true;
    o.watchPollMs = 100;
  });
  std::string err;
  REQUIRE(fx.ed->start(&err));
  Document* d = fx.open();
  REQUIRE(d != nullptr);
  REQUIRE(fx.waitBase(fx.a));
  fx.repo.write("b.txt", "other, edited by a build tool\n");
  CHECK(fx.ed->waitFor([&] { return fx.ed->vcs().fileStatus(fx.repo.path("b.txt")) == VcsStatus::Modified; }, 20000));
  fx.ed->shutdown();
}

TEST(vcs_manager_closed_document_drops_results) {
  SKIP_WITHOUT_GIT();
  Fx fx;
  Document* d = fx.open();
  REQUIRE(d != nullptr);
  d->insert({0, 0}, "x");
  fx.ed->close(fx.a);  // results for it may still be in flight
  fx.ed->waitFor([] { return false; }, 200);
  CHECK(!fx.ed->vcs().hasBase(fx.a));
  CHECK(fx.ed->vcs().hunks(fx.a).empty());
}

// Benchmark: what the main thread pays for a 100k line file (Release numbers go into the task report).
TEST(vcs_manager_bench_main_thread_100k) {
  SKIP_WITHOUT_GIT();
  std::string text;
  text.reserve(4000000);
  for (int i = 0; i < 100000; ++i)
    text += "    int value_" + std::to_string(i) + " = compute(" + std::to_string(i * 7) + ");\n";
  Fx fx(150, {}, text);
  Document* d = fx.open();
  REQUIRE(d != nullptr);
  REQUIRE(fx.waitBase(fx.a));
  using Clock = std::chrono::steady_clock;
  auto ms = [](Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
  };
  // One edit; then pump like a UI frame loop and record the longest single pump (the stall the main thread sees).
  auto editAndWait = [&](int line, double& replaceMs, double& maxPumpMs, double& latencyMs) {
    const auto t0 = Clock::now();
    d->replace({{line, 4}, {line, 7}}, "x" + std::to_string(line));
    replaceMs = ms(t0, Clock::now());
    maxPumpMs = 0;
    while (fx.ed->vcs().hunksVersion(fx.a) != d->version() && ms(t0, Clock::now()) < kWaitMs) {
      const auto p0 = Clock::now();
      fx.ed->pump();
      maxPumpMs = std::max(maxPumpMs, ms(p0, Clock::now()));
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    latencyMs = ms(t0, Clock::now());
  };
  double replaceMs = 0, pumpMs = 0, latencyMs = 0, worstReplace = 0, worstPump = 0;
  for (int i = 0; i < 5; ++i) {
    editAndWait(50000 + 7 * i, replaceMs, pumpMs, latencyMs);
    worstReplace = std::max(worstReplace, replaceMs);
    worstPump = std::max(worstPump, pumpMs);
  }
  CHECK_EQ(fx.ed->vcs().hunks(fx.a).size(), static_cast<size_t>(5));
  std::cout << "BENCH vcs 100k lines: Document::replace incl. VCS notification max " << worstReplace
            << " ms; longest single pump " << worstPump << " ms; last edit -> hunks visible " << latencyMs
            << " ms (150 ms debounce included)\n";
#ifdef NDEBUG  // timing is only meaningful in an optimised, unsanitised build
  CHECK(worstReplace < 50);
#endif
}
