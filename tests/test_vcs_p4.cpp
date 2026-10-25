// C3b: the Perforce provider and its auto-checkout, against the fake `p4` (tests/fake_p4_main.cpp). There is no
// real Perforce server in the test environment: these tests check our use of p4's command line and tagged output
// as the fake emulates it.
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <thread>

#include "app/Editor.h"
#include "core/Document.h"
#include "lsp/JsonRpc.h"
#include "test_main.h"
#include "test_vcs_util.h"
#include "vcs/GitProvider.h"
#include "vcs/LineDiff.h"
#include "vcs/PerforceProvider.h"
#include "vcs/VcsManager.h"

using namespace tf;
namespace fs = std::filesystem;

namespace {

constexpr int kWaitMs = 15000;

void setReadOnly(const fs::path& p, bool ro) {
  std::error_code ec;
  const auto w = fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write;
  if (ro) fs::permissions(p, w, fs::perm_options::remove, ec);
  else fs::permissions(p, fs::perms::owner_write, fs::perm_options::add, ec);
}

bool isWritable(const fs::path& p) {
  std::error_code ec;
  return (fs::status(p, ec).permissions() & fs::perms::owner_write) != fs::perms::none;
}

// A workspace directory plus the fake server's state file.
struct P4Fx {
  fs::path ws = tfvcs::tempDir("p4ws");
  fs::path stateDir = tfvcs::tempDir("p4state");
  fs::path stateFile = stateDir / "state.json";
  Json state;

  P4Fx() {
    PerforceProvider::clearDetectCache();
    state = {{"clientRoot", ws.generic_string()}, {"files", Json::object()}, {"log", Json::array()}};
    save();
    tfvcs::setEnvVar("FAKE_P4_STATE", stateFile.string().c_str());
  }
  ~P4Fx() {
    std::error_code ec;
    fs::remove_all(ws, ec);
    fs::remove_all(stateDir, ec);
  }
  P4Fx(const P4Fx&) = delete;
  P4Fx& operator=(const P4Fx&) = delete;

  void save() const {
    std::ofstream o(stateFile, std::ios::trunc);
    o << state.dump(2);
  }
  void load() {
    std::ifstream in(stateFile);
    state = Json::parse(in, nullptr, false);
  }
  // A depot file at its have revision: on disk with `content`, read-only like a synced file.
  Json& depotFile(const std::string& rel, const std::string& content = "base\n", const std::string& type = "text") {
    tfvcs::writeFile(path(rel), content);
    setReadOnly(path(rel), true);
    state["files"][rel] = {
        {"headRev", 3}, {"haveRev", 3}, {"type", type}, {"headAction", "edit"}, {"content", content}};
    return state["files"][rel];
  }
  fs::path path(const std::string& rel) const { return ws / rel; }
  std::vector<std::string> log() {
    load();
    return state["log"].get<std::vector<std::string>>();
  }
  int count(const std::string& needle) {
    int n = 0;
    for (auto& l : log())
      if (l.find(needle) != std::string::npos) ++n;
    return n;
  }
  std::unique_ptr<PerforceProvider> provider(int detectMs = 3000) const {
    return std::make_unique<PerforceProvider>(TENFOLD_FAKE_P4, detectMs, 20000);
  }
  std::unique_ptr<PerforceProvider> detected() {
    auto p = provider();
    if (!p->detect(ws)) std::cerr << "    detect failed: " << p->lastError() << "\n";
    return p;
  }
};

VcsStatus statusOf(PerforceProvider& p, const fs::path& f) {
  auto v = p.status({f});
  return v.size() == 1 ? v[0].status : VcsStatus::Unknown;
}

}  // namespace

TEST(p4_tagged_parsing_classify_and_escape) {
  auto recs = PerforceProvider::parseTagged(
      "... depotFile //depot/a b.txt\r\n... clientFile /ws/a b.txt\n... unresolved\n\n... depotFile //depot/c\n");
  REQUIRE(recs.size() == static_cast<size_t>(2));
  CHECK_EQ(recs[0]["depotFile"], std::string("//depot/a b.txt"));
  CHECK(recs[0].count("unresolved") == 1);
  CHECK_EQ(recs[1]["depotFile"], std::string("//depot/c"));
  CHECK_EQ(PerforceProvider::escapePath("/w/a@b#c*d%e.txt"), std::string("/w/a%40b%23c%2Ad%25e.txt"));
  CHECK(PerforceProvider::isBinaryType("binary+F"));
  CHECK(PerforceProvider::isBinaryType("utf16"));
  CHECK(!PerforceProvider::isBinaryType("text+x"));
  CHECK(!PerforceProvider::isBinaryType("unicode"));
  using R = PerforceProvider::Record;
  CHECK(PerforceProvider::classify(R{{"depotFile", "d"}, {"haveRev", "1"}}, true, false) ==
        VcsStatus::ReadOnlyNotCheckedOut);
  CHECK(PerforceProvider::classify(R{{"depotFile", "d"}, {"haveRev", "1"}}, true, true) == VcsStatus::Unmodified);
  CHECK(PerforceProvider::classify(R{{"depotFile", "d"}, {"action", "edit"}, {"unresolved", ""}}, true, true) ==
        VcsStatus::Conflicted);
  CHECK(PerforceProvider::classify(R{{"depotFile", "d"}, {"action", "integrate"}}, true, true) ==
        VcsStatus::CheckedOut);
  CHECK(PerforceProvider::classify(R{{"depotFile", "d"}, {"action", "move/delete"}}, false, false) ==
        VcsStatus::Deleted);
}

TEST(p4_detect_failures_are_fast_and_cached) {
  P4Fx fx;
  {
    auto p = fx.provider();
    CHECK(p->detect(fx.ws));
    CHECK(tfvcs::readFile(fx.stateFile).find("-F %clientRoot% info") != std::string::npos);
    CHECK(fx.count("-ztag -F %clientRoot% info") == 1);
    CHECK(fx.count("where") == 1);
    CHECK(p->root() == fx.ws);
    CHECK(p->lastError().empty());
    // a sub directory of the client is fine as well
    CHECK(fx.provider()->detect(fx.ws));
  }
  {  // no p4 at all
    PerforceProvider missing("tenfold-no-such-p4-executable");
    const auto t0 = std::chrono::steady_clock::now();
    CHECK(!missing.detect(fx.ws));
    CHECK(!missing.lastError().empty());
    CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(2));
  }
  {  // server down: fails immediately, with p4's message
    P4Fx down;
    down.state["serverDown"] = true;
    down.save();
    auto p = down.provider();
    const auto t0 = std::chrono::steady_clock::now();
    CHECK(!p->detect(down.ws));
    CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(2));
    CHECK(p->lastError().find("Connect to server failed") != std::string::npos);
    // the negative result is cached: a second probe does not start p4 again
    const int runs = down.count("info");
    CHECK(!down.provider()->detect(down.ws));
    CHECK_EQ(down.count("info"), runs);
    PerforceProvider::clearDetectCache();
    CHECK(!down.provider()->detect(down.ws));
    CHECK_EQ(down.count("info"), runs + 1);
  }
  {  // a hung server is cut off by the detect timeout
    P4Fx hung;
    hung.state["hangMs"] = 8000;
    hung.save();
    auto p = hung.provider(/*detectMs=*/300);
    const auto t0 = std::chrono::steady_clock::now();
    CHECK(!p->detect(hung.ws));
    CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(4));
    CHECK(p->lastError().find("timed out") != std::string::npos);
  }
  {  // a client exists but does not map the directory
    P4Fx out;
    out.state["notUnderClient"] = true;
    out.save();
    CHECK(!out.provider()->detect(out.ws));
  }
  {  // no client at all (clientRoot "null")
    P4Fx nc;
    nc.state["noClient"] = true;
    nc.save();
    CHECK(!nc.provider()->detect(nc.ws));
  }
}

TEST(p4_status_kinds) {
  P4Fx fx;
  fx.depotFile("synced.txt");  // read-only, not opened
  fx.depotFile("writable.txt");
  setReadOnly(fx.path("writable.txt"), false);  // not opened but writable
  fx.depotFile("edit.txt")["action"] = "edit";
  setReadOnly(fx.path("edit.txt"), false);
  fx.depotFile("integ.txt")["action"] = "integrate";
  fx.depotFile("del.txt")["action"] = "delete";
  fx.depotFile("conflict.txt")["action"] = "edit";
  fx.state["files"]["conflict.txt"]["unresolved"] = true;
  fx.depotFile("old name.txt")["action"] = "move/delete";
  fx.depotFile("new name.txt");
  fx.state["files"]["new name.txt"].erase("haveRev");
  fx.state["files"]["new name.txt"]["inDepot"] = false;
  fx.state["files"]["new name.txt"]["action"] = "move/add";
  fx.state["files"]["new name.txt"]["movedFile"] = "//depot/old name.txt";
  setReadOnly(fx.path("new name.txt"), false);
  tfvcs::writeFile(fx.path("added.txt"), "new\n");
  fx.state["files"]["added.txt"] = {{"inDepot", false}, {"action", "add"}, {"content", ""}};
  tfvcs::writeFile(fx.path("untracked.txt"), "x\n");
  fx.save();

  auto p = fx.detected();
  REQUIRE(p->root() == fx.ws);
  std::string wsName = p->name();
  CHECK_EQ(wsName, std::string("perforce"));
  CHECK(statusOf(*p, fx.path("synced.txt")) == VcsStatus::ReadOnlyNotCheckedOut);
  CHECK(statusOf(*p, fx.path("writable.txt")) == VcsStatus::Unmodified);
  CHECK(statusOf(*p, fx.path("edit.txt")) == VcsStatus::CheckedOut);
  CHECK(statusOf(*p, fx.path("integ.txt")) == VcsStatus::CheckedOut);
  CHECK(statusOf(*p, fx.path("del.txt")) == VcsStatus::Deleted);
  CHECK(statusOf(*p, fx.path("conflict.txt")) == VcsStatus::Conflicted);
  CHECK(statusOf(*p, fx.path("added.txt")) == VcsStatus::Added);
  CHECK(statusOf(*p, fx.path("untracked.txt")) == VcsStatus::Untracked);
  CHECK(statusOf(*p, fx.path("nothere.txt")) == VcsStatus::Unknown);
  CHECK(statusOf(*p, fx.ws.parent_path() / "outside.txt") == VcsStatus::Unknown);
  auto ren = p->status({fx.path("new name.txt")});
  REQUIRE(ren.size() == static_cast<size_t>(1));
  CHECK(ren[0].status == VcsStatus::Renamed);
  CHECK(ren[0].renamedFrom == fx.path("old name.txt"));

  // a batch: one entry per input, in order, ONE fstat call
  const int before = fx.count("fstat");
  auto batch = p->status({fx.path("edit.txt"), fx.path("synced.txt"), fx.path("untracked.txt"), fx.path("del.txt")});
  REQUIRE(batch.size() == static_cast<size_t>(4));
  CHECK(batch[0].status == VcsStatus::CheckedOut);
  CHECK(batch[1].status == VcsStatus::ReadOnlyNotCheckedOut);
  CHECK(batch[2].status == VcsStatus::Untracked);
  CHECK(batch[3].status == VcsStatus::Deleted);
  CHECK(batch[3].path == fx.path("del.txt"));
  CHECK_EQ(fx.count("fstat") - before, 1);

  // workspace-wide: the opened files only
  auto all = p->status({});
  std::map<std::string, VcsStatus> byName;
  for (auto& f : all) byName[f.path.filename().string()] = f.status;
  CHECK_EQ(byName.size(), static_cast<size_t>(7));  // edit integ del conflict old name, new name, added
  CHECK(byName["edit.txt"] == VcsStatus::CheckedOut);
  CHECK(byName["conflict.txt"] == VcsStatus::Conflicted);
  CHECK(byName["new name.txt"] == VcsStatus::Renamed);
  CHECK(byName["old name.txt"] == VcsStatus::Deleted);
  CHECK(byName["added.txt"] == VcsStatus::Added);
  CHECK(byName.count("synced.txt") == 0);
  CHECK(byName.count("untracked.txt") == 0);

  // server goes away: Unknown with the message, no crash
  fx.load();
  fx.state["serverDown"] = true;
  fx.save();
  CHECK(statusOf(*p, fx.path("synced.txt")) == VcsStatus::Unknown);
  CHECK(!p->lastError().empty());
  CHECK(p->status({}).empty());
}

TEST(p4_base_text) {
  P4Fx fx;
  fx.depotFile("a.txt", "one\r\ntwo\r\n");
  fx.depotFile("edited.txt", "committed\n")["action"] = "edit";
  setReadOnly(fx.path("edited.txt"), false);
  tfvcs::writeFile(fx.path("edited.txt"), "local change\n");
  fx.depotFile("pic.bin", "\x01\x02", "binary+F");
  fx.depotFile("wide.txt", "w\0i\0", "utf16");
  tfvcs::writeFile(fx.path("added.txt"), "new\n");
  fx.state["files"]["added.txt"] = {{"inDepot", false}, {"action", "add"}};
  tfvcs::writeFile(fx.path("untracked.txt"), "u\n");
  fx.save();
  auto p = fx.detected();
  std::string err = "x";
  auto t = p->baseText(fx.path("a.txt"), &err);
  REQUIRE(t.has_value());
  CHECK_EQ(*t, std::string("one\r\ntwo\r\n"));  // bytes as stored; the diff splits lines like the Git provider
  CHECK(err.empty());
  CHECK(fx.count("print -q " + fx.path("a.txt").generic_string() + "#have") == 1);
  t = p->baseText(fx.path("edited.txt"), &err);
  REQUIRE(t.has_value());
  CHECK_EQ(*t, std::string("committed\n"));  // the have revision, not the working copy
  // no committed version: no text, no error
  CHECK(!p->baseText(fx.path("added.txt"), &err).has_value());
  CHECK(err.empty());
  CHECK(!p->baseText(fx.path("untracked.txt"), &err).has_value());
  CHECK(err.empty());
  // binary and utf16 types: no textual base, and print is not even run
  const int prints = fx.count("print");
  CHECK(!p->baseText(fx.path("pic.bin"), &err).has_value());
  CHECK(err.empty());
  CHECK(!p->baseText(fx.path("wide.txt"), &err).has_value());
  CHECK(err.empty());
  CHECK_EQ(fx.count("print"), prints);
  // server down: nullopt WITH an error
  fx.load();
  fx.state["serverDown"] = true;
  fx.save();
  CHECK(!p->baseText(fx.path("a.txt"), &err).has_value());
  CHECK(!err.empty());
}

TEST(p4_revert) {
  P4Fx fx;
  fx.depotFile("a.txt", "committed\n");
  fx.state["files"]["a.txt"]["action"] = "edit";
  setReadOnly(fx.path("a.txt"), false);
  tfvcs::writeFile(fx.path("a.txt"), "edited\n");
  fx.depotFile("closed.txt", "c\n");
  fx.save();
  auto p = fx.detected();
  std::string err;
  CHECK(p->revert(fx.path("a.txt"), &err));
  CHECK(err.empty());
  CHECK_EQ(tfvcs::readFile(fx.path("a.txt")), std::string("committed\n"));
  CHECK(!isWritable(fx.path("a.txt")));  // p4 makes it read-only again
  CHECK(statusOf(*p, fx.path("a.txt")) == VcsStatus::ReadOnlyNotCheckedOut);
  // not opened: refused with a clear message, nothing run
  const int reverts = fx.count("revert");
  CHECK(!p->revert(fx.path("closed.txt"), &err));
  CHECK(err.find("not opened") != std::string::npos);
  CHECK(!p->lastError().empty());
  CHECK_EQ(fx.count("revert"), reverts);
  CHECK(!p->revert(fx.path("never.txt"), &err));
}

TEST(p4_before_first_edit) {
  P4Fx fx;
  fx.depotFile("a.txt", "committed\n");
  fx.depotFile("opened.txt", "o\n")["action"] = "edit";
  setReadOnly(fx.path("opened.txt"), false);
  fx.depotFile("locked.txt", "l\n")["lockedBy"] = "alice@other";
  fx.depotFile("hard.txt", "h\n");
  fx.state["files"]["hard.txt"]["lockedBy"] = "bob@other";
  fx.state["files"]["hard.txt"]["editExit"] = 1;
  tfvcs::writeFile(fx.path("untracked.txt"), "u\n");
  fx.save();
  auto p = fx.detected();
  std::string err;

  REQUIRE(!isWritable(fx.path("a.txt")));
  CHECK(p->beforeFirstEdit(fx.path("a.txt"), &err));
  CHECK(err.empty());
  CHECK(isWritable(fx.path("a.txt")));  // p4 (the fake) made it writable
  CHECK(statusOf(*p, fx.path("a.txt")) == VcsStatus::CheckedOut);
  CHECK_EQ(fx.count("edit " + fx.path("a.txt").generic_string()), 1);
  // already opened: no second p4 edit
  CHECK(p->beforeFirstEdit(fx.path("a.txt"), &err));
  CHECK(p->beforeFirstEdit(fx.path("opened.txt"), &err));
  CHECK_EQ(fx.count(" edit "), 1);
  // not in the depot: nothing happens, nothing is added
  CHECK(p->beforeFirstEdit(fx.path("untracked.txt"), &err));
  CHECK(p->beforeFirstEdit(fx.ws.parent_path() / "outside.txt", &err));
  CHECK_EQ(fx.count(" edit "), 1);
  CHECK_EQ(fx.count(" add "), 0);
  // exclusively locked by someone else (a warning with exit code 0, and a hard error): false with p4's message
  CHECK(!p->beforeFirstEdit(fx.path("locked.txt"), &err));
  CHECK(err.find("exclusive") != std::string::npos);
  CHECK(err.find("alice@other") != std::string::npos);
  CHECK(!isWritable(fx.path("locked.txt")));
  CHECK(!p->beforeFirstEdit(fx.path("hard.txt"), &err));
  CHECK(err.find("bob@other") != std::string::npos);
  CHECK(!p->lastError().empty());
  // server down
  fx.depotFile("late.txt");
  fx.state["serverDown"] = true;
  fx.save();
  CHECK(!p->beforeFirstEdit(fx.path("late.txt"), &err));
  CHECK(err.find("Connect to server failed") != std::string::npos);
  CHECK(!isWritable(fx.path("late.txt")));
  // the explicit add
  fx.load();
  fx.state["serverDown"] = false;
  fx.save();
  CHECK(p->add(fx.path("untracked.txt"), &err));
  CHECK(statusOf(*p, fx.path("untracked.txt")) == VcsStatus::Added);
}

namespace {

struct EdFx {
  P4Fx fx;
  std::unique_ptr<Editor> ed;
  std::vector<std::pair<fs::path, std::string>> errors;

  explicit EdFx(std::function<void(P4Fx&)> prepare = {}) {
    fx.depotFile("a.txt", "line 0\nline 1\nline 2\n");
    fx.depotFile("b.txt", "b\n");
    if (prepare) prepare(fx);
    fx.save();
    Editor::Options o;
    o.vcs.debounceMs = 40;
    o.vcs.statusDebounceMs = 100;
    o.vcs.provider = "perforce";
    o.vcs.p4Path = TENFOLD_FAKE_P4;
    ed = std::make_unique<Editor>(fx.ws, o);
    ed->vcs().onError([this](const fs::path& f, const std::string& m) { errors.emplace_back(f, m); });
  }
  Document* open(const std::string& rel) {
    Document* d = ed->open(fx.path(rel));
    if (d) ed->activateDocument(*d);
    return d;
  }
  bool waitBase(const std::string& rel) {
    return ed->waitFor([&] { return ed->vcs().hasBase(fx.path(rel)) && ed->vcs().hunksVersion(fx.path(rel)) != 0; },
                       kWaitMs);
  }
};

}  // namespace

TEST(p4_manager_auto_checkout_on_first_edit) {
  EdFx e;
  Document* d = e.open("a.txt");
  REQUIRE(d != nullptr);
  REQUIRE(e.waitBase("a.txt"));
  CHECK_EQ(e.ed->vcs().providerName(), std::string("perforce"));
  CHECK(e.ed->vcs().fileStatus(e.fx.path("a.txt")) == VcsStatus::ReadOnlyNotCheckedOut);
  CHECK(!isWritable(e.fx.path("a.txt")));

  d->replace({{1, 0}, {1, 6}}, "CHANGED");
  d->replace({{0, 0}, {0, 4}}, "LINE");  // a second edit must not check out again
  CHECK(e.ed->waitFor([&] { return e.ed->vcs().stats().beforeFirstEditCalls == 1; }, kWaitMs));
  CHECK(
      e.ed->waitFor([&] { return e.ed->vcs().fileInfo(e.fx.path("a.txt")).status == VcsStatus::CheckedOut; }, kWaitMs));
  CHECK(isWritable(e.fx.path("a.txt")));
  CHECK(e.errors.empty());
  d->replace({{2, 0}, {2, 4}}, "LINE");
  CHECK(e.ed->waitFor([&] { return e.ed->vcs().hunks(e.fx.path("a.txt")).size() == 1; }, kWaitMs));
  CHECK_EQ(e.ed->vcs().stats().beforeFirstEditCalls, 1);
  CHECK_EQ(e.fx.count(" edit "), 1);
  // the save now works and the diff base (have revision) is unchanged
  std::string err;
  CHECK(e.ed->save(*d, &err));
  CHECK_EQ(tfvcs::readFile(e.fx.path("a.txt")), std::string("LINE 0\nCHANGED\nLINE 2\n"));
  CHECK(e.ed->vcs().baseLines(e.fx.path("a.txt"))->at(0) == "line 0");
}

TEST(p4_manager_checkout_failure_keeps_the_edit_and_reports) {
  EdFx e([](P4Fx& f) { f.state["files"]["a.txt"]["lockedBy"] = "alice@other"; });
  Document* d = e.open("a.txt");
  REQUIRE(d != nullptr);
  REQUIRE(e.waitBase("a.txt"));
  d->replace({{0, 0}, {0, 4}}, "LINE");
  REQUIRE(e.ed->waitFor([&] { return !e.errors.empty(); }, kWaitMs));
  CHECK(e.errors[0].first == e.fx.path("a.txt"));
  CHECK(e.errors[0].second.find("alice@other") != std::string::npos);
  CHECK(d->dirty());
  CHECK_EQ(d->buffer().line(0), std::string("LINE 0"));  // the edit stays in the buffer
  CHECK(!isWritable(e.fx.path("a.txt")));
  // saving reports the read-only state, clearly, and keeps the buffer dirty
  std::string err;
  CHECK(!e.ed->save(*d, &err));
  CHECK(err.find("read-only") != std::string::npos);
  CHECK(err.find("a.txt") != std::string::npos);
  CHECK(err.find("Perforce") != std::string::npos);
  CHECK(d->dirty());
  CHECK_EQ(tfvcs::readFile(e.fx.path("a.txt")), std::string("line 0\nline 1\nline 2\n"));
  // once the lock is gone a new edit is not retried by itself (one call per document); p4 edit by hand works
  CHECK_EQ(e.ed->vcs().stats().beforeFirstEditCalls, 1);
}

TEST(p4_manager_server_down_at_detect_means_no_vcs) {
  EdFx e([](P4Fx& f) { f.state["serverDown"] = true; });
  Document* d = e.open("a.txt");
  REQUIRE(d != nullptr);
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  e.ed->pump();
  CHECK(!e.ed->vcs().active());
  d->replace({{0, 0}, {0, 4}}, "LINE");  // editing still works, nothing is checked out
  e.ed->pump();
  CHECK(!isWritable(e.fx.path("a.txt")));
  CHECK(e.errors.empty());
}

TEST(save_to_read_only_file_reports_it) {
  // Without any version control: the message names the file and the cause.
  auto dir = tfvcs::tempDir("ro");
  tfvcs::writeFile(dir / "r.txt", "x\n");
  setReadOnly(dir / "r.txt", true);
  Editor::Options o;
  o.vcs.provider = "none";
  Editor ed(dir, o);
  Document* d = ed.open(dir / "r.txt");
  REQUIRE(d != nullptr);
  d->insert({0, 0}, "y");
  std::string err;
  CHECK(!ed.save(*d, &err));
  CHECK(err.find("r.txt") != std::string::npos);
  CHECK(err.find("read-only") != std::string::npos);
  CHECK(d->dirty());
  CHECK_EQ(tfvcs::readFile(dir / "r.txt"), std::string("x\n"));
  setReadOnly(dir / "r.txt", false);
  err.clear();
  CHECK(ed.save(*d, &err));
  CHECK_EQ(tfvcs::readFile(dir / "r.txt"), std::string("yx\n"));
  ed.shutdown();
  std::error_code ec;
  fs::remove_all(dir, ec);
}

namespace {

// A directory that is both a git repository and a Perforce client.
struct BothFx {
  tfvcs::TempRepo repo;
  P4Fx fx;
  BothFx() {
    repo.write("a.txt", "a\n");
    repo.commitAll("base");
    fx.state["clientRoot"] = repo.dir.generic_string();
    fx.state["files"]["a.txt"] = {{"headRev", 1}, {"haveRev", 1}, {"type", "text"}, {"content", "a\n"}};
    fx.save();
  }
  // Provider name chosen for the workspace ("" = none), through Editor::Options or workspace.json.
  std::string choose(const std::string& optionProvider, const std::string& jsonProvider = {}, bool expectNone = false) {
    PerforceProvider::clearDetectCache();
    if (!jsonProvider.empty())
      repo.write(".tenfold/workspace.json",
                 Json{{"vcs", {{"provider", jsonProvider}, {"p4Path", TENFOLD_FAKE_P4}}}}.dump());
    Editor::Options o;
    o.vcs.statusDebounceMs = 100;
    o.vcs.provider = optionProvider;
    if (jsonProvider.empty()) o.vcs.p4Path = TENFOLD_FAKE_P4;
    Editor ed(repo.dir, o);
    Document* d = ed.open(repo.path("a.txt"));
    std::string name;
    if (d) {
      ed.waitFor([&] { return ed.vcs().active(); }, expectNone || optionProvider == "none" ? 800 : kWaitMs);
      name = ed.vcs().providerName();
    }
    ed.shutdown();
    return name;
  }
};

}  // namespace

TEST(vcs_provider_selection) {
  SKIP_WITHOUT_GIT();
  {
    BothFx b;
    CHECK_EQ(b.choose("auto"), std::string("perforce"));  // both detect: Perforce wins
    CHECK_EQ(b.choose("perforce"), std::string("perforce"));
    CHECK_EQ(b.choose("git"), std::string("git"));
    CHECK_EQ(b.choose("none"), std::string());
  }
  {  // workspace.json {"vcs": {"provider": ...}} is read by the Editor
    BothFx b;
    CHECK_EQ(b.choose("auto", "git"), std::string("git"));
    CHECK_EQ(b.choose("auto", "perforce"), std::string("perforce"));
    CHECK_EQ(b.choose("auto", "none", true), std::string());
    CHECK_EQ(b.choose("auto", "auto"), std::string("perforce"));
  }
  {  // the p4 server is down: auto falls back to Git
    BothFx b;
    b.fx.state["serverDown"] = true;
    b.fx.save();
    CHECK_EQ(b.choose("auto"), std::string("git"));
  }
  {  // forced Perforce on a directory the client does not map: no version control
    BothFx b;
    b.fx.state["notUnderClient"] = true;
    b.fx.save();
    CHECK_EQ(b.choose("perforce", "", true), std::string());
    CHECK_EQ(b.choose("auto"), std::string("git"));
  }
  CHECK(VcsManager::providersFor("none").empty());
  CHECK_EQ(VcsManager::providersFor("auto").size(), static_cast<size_t>(2));
  CHECK_EQ(VcsManager::providersFor("git").size(), static_cast<size_t>(1));
  CHECK(VcsManager::providersFor("git")[0]()->name() == "git");
  CHECK(VcsManager::providersFor("perforce")[0]()->name() == "perforce");
  CHECK(VcsManager::defaultProviders()[0]()->name() == "perforce");
}

TEST(p4_base_text_line_endings_do_not_matter) {
  // The real p4 prints text files with the client's LineEnd setting (CRLF on many Windows clients): the provider
  // hands the bytes over as printed and the diff ignores the ending, in both directions.
  P4Fx fx;
  fx.depotFile("crlf.txt", "a\r\nb\r\nc\r\n");
  fx.depotFile("lf.txt", "a\nb\nc\n");
  fx.save();
  auto p = fx.detected();
  auto crlf = p->baseText(fx.path("crlf.txt"));
  auto lf = p->baseText(fx.path("lf.txt"));
  REQUIRE(crlf.has_value());
  REQUIRE(lf.has_value());
  CHECK_EQ(*crlf, std::string("a\r\nb\r\nc\r\n"));  // exactly what the fake printed (no CRT translation)
  CHECK_EQ(*lf, std::string("a\nb\nc\n"));
  CHECK(splitLines(*crlf) == splitLines(std::string("a\nb\nc\n")));      // LF buffer against a CRLF base
  CHECK(splitLines(*lf) == splitLines(std::string("a\r\nb\r\nc\r\n")));  // CRLF buffer against an LF base
  CHECK(diffLines(splitLines(*crlf), splitLines(std::string("a\nb\nc\n"))).empty());
  CHECK(diffLines(splitLines(*lf), splitLines(std::string("a\r\nb\r\nc\r\n"))).empty());
  CHECK_EQ(diffLines(splitLines(*crlf), splitLines(std::string("a\nX\nc\n"))).size(), static_cast<size_t>(1));
}
