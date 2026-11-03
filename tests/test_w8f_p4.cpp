// W8f: the Perforce history / p4v helpers of PerforceProvider and VcsManager, against the fake `p4`.
#include <filesystem>
#include <iostream>
#include <memory>

#include "app/Editor.h"
#include "test_main.h"
#include "test_w8f_p4util.h"
#include "vcs/PerforceProvider.h"
#include "vcs/VcsManager.h"

using namespace tf;
namespace fs = std::filesystem;

namespace {
constexpr int kWaitMs = 15000;

std::unique_ptr<PerforceProvider> detected(const w8f::P4Ws& w) {
  PerforceProvider::clearDetectCache();
  auto p = std::make_unique<PerforceProvider>(TENFOLD_FAKE_P4, 3000, 20000);
  if (!p->detect(w.ws)) std::cerr << "    detect failed: " << p->lastError() << "\n";
  return p;
}

bool logHas(w8f::P4Ws& w, const std::string& needle) {
  for (const std::string& l : w.log())
    if (l.find(needle) != std::string::npos) return true;
  return false;
}
}  // namespace

TEST(w8f_parse_filelog) {
  const std::string out =
      "... depotFile //depot/a.txt\n"
      "... rev0 7\n... change0 70\n... action0 integrate\n... time0 1700000000\n... user0 u7\n... client0 c7\n"
      "... desc0 First line\nsecond line\n\nafter a blank line\n"
      "... rev1 6\n... change1 60\n... action1 add\n... time1 1600000000\n... user1 u6\n... client1 c6\n"
      "... desc1 Old\n... type1 text\n";
  const auto revs = PerforceProvider::parseFilelog(out);
  REQUIRE(revs.size() == static_cast<size_t>(2));
  CHECK_EQ(revs[0].rev, 7);
  CHECK_EQ(revs[0].change, 70);
  CHECK_EQ(revs[0].action, std::string("integrate"));
  CHECK_EQ(revs[0].user, std::string("u7"));
  CHECK_EQ(revs[0].time, 1700000000LL);
  CHECK_EQ(revs[0].description, std::string("First line"));
  CHECK_EQ(revs[1].rev, 6);
  CHECK_EQ(revs[1].description, std::string("Old"));
  CHECK_EQ(revs[1].type, std::string("text"));
  CHECK(PerforceProvider::parseFilelog("").empty());
}

TEST(w8f_provider_filelog_print_and_p4v_target) {
  w8f::P4Ws w;
  w.depotFile("src/a.txt");
  w.save();
  auto p = detected(w);
  std::string err;
  const auto revs = p->filelog(w.ws / "src/a.txt", &err);
  CHECK(err.empty());
  REQUIRE(revs.size() == static_cast<size_t>(3));
  CHECK_EQ(revs[0].rev, 3);
  CHECK_EQ(revs[0].user, std::string("carol"));
  CHECK_EQ(revs[0].description, std::string("Third change"));
  CHECK_EQ(revs[2].action, std::string("add"));
  CHECK(logHas(w, "filelog"));
  // a file that is not in the depot: empty list, no error
  tfvcs::writeFile(w.ws / "new.txt", "x\n");
  CHECK(p->filelog(w.ws / "new.txt", &err).empty());
  CHECK(err.empty());

  auto t2 = p->printRevision(w.ws / "src/a.txt", 2, &err);
  REQUIRE(t2.has_value());
  CHECK_EQ(*t2, std::string("second\n"));
  CHECK(logHas(w, "print -q"));
  CHECK(logHas(w, "#2"));

  PerforceProvider::P4vTarget t;
  REQUIRE(p->p4vTarget(w.ws / "src/a.txt", t, &err));
  CHECK_EQ(t.user, std::string("tester"));
  CHECK_EQ(t.client, std::string("fake-client"));
  CHECK_EQ(t.port, std::string("fake-server:1666"));
  CHECK_EQ(t.depotPath, std::string("//depot/src/a.txt"));
}

TEST(w8f_manager_perforce_helpers_run_off_the_ui_thread) {
  w8f::P4Ws w;
  w.depotFile("a.txt");
  w.save();
  PerforceProvider::clearDetectCache();
  Editor::Options o;
  o.vcs.provider = "perforce";
  o.vcs.p4Path = TENFOLD_FAKE_P4;
  Editor ed(w.ws, o);
  Document* d = ed.open(w.ws / "a.txt");
  REQUIRE(d != nullptr);
  REQUIRE(ed.waitFor([&] { return ed.vcs().active(); }, kWaitMs));

  bool got = false, ok = false;
  VcsManager::Revisions revs;
  std::string error;
  ed.vcs().perforceHistory(w.ws / "a.txt", [&](bool o2, VcsManager::Revisions r, const std::string& e) {
    got = true;
    ok = o2;
    revs = std::move(r);
    error = e;
  });
  REQUIRE(ed.waitFor([&] { return got; }, kWaitMs));
  CHECK(ok);
  CHECK_EQ(revs.size(), static_cast<size_t>(3));

  got = false;
  std::string text;
  ed.vcs().perforceRevisionText(w.ws / "a.txt", 1, [&](bool o2, std::string t, const std::string& e) {
    got = true;
    ok = o2;
    text = std::move(t);
    error = e;
  });
  REQUIRE(ed.waitFor([&] { return got; }, kWaitMs));
  CHECK(ok);
  CHECK_EQ(text, std::string("first\n"));

  got = false;
  PerforceProvider::P4vTarget tg;
  ed.vcs().perforceP4vTarget(w.ws / "a.txt", [&](bool o2, PerforceProvider::P4vTarget t, const std::string& e) {
    got = true;
    ok = o2;
    tg = std::move(t);
    error = e;
  });
  REQUIRE(ed.waitFor([&] { return got; }, kWaitMs));
  CHECK(ok);
  CHECK_EQ(tg.depotPath, std::string("//depot/a.txt"));
}

TEST(w8f_manager_helpers_refuse_other_providers) {
  tfvcs::TempRepo repo;
  if (!tfvcs::gitAvailable()) return;
  repo.write("a.txt", "x\n");
  repo.commitAll();
  Editor::Options o;
  o.vcs.provider = "git";
  Editor ed(repo.dir, o);
  REQUIRE(ed.open(repo.path("a.txt")) != nullptr);
  REQUIRE(ed.waitFor([&] { return ed.vcs().active(); }, kWaitMs));
  bool got = false, ok = true;
  std::string error;
  ed.vcs().perforceHistory(repo.path("a.txt"), [&](bool o2, VcsManager::Revisions, const std::string& e) {
    got = true;
    ok = o2;
    error = e;
  });
  REQUIRE(ed.waitFor([&] { return got; }, kWaitMs));
  CHECK(!ok);
  CHECK(error.find("not under Perforce") != std::string::npos);
}
