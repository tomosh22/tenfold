// C3a: GitProvider against real temporary repositories (skipped without git).
#include <algorithm>
#include <filesystem>
#include <map>

#include "test_main.h"
#include "test_vcs_util.h"
#include "vcs/GitProvider.h"
#include "vcs/LineDiff.h"

using namespace tf;
using tfvcs::TempRepo;
namespace fs = std::filesystem;

namespace {
VcsStatus statusOf(GitProvider& g, const fs::path& f) {
  auto v = g.status({f});
  return v.size() == 1 ? v[0].status : VcsStatus::Unknown;
}
}  // namespace

TEST(git_parse_status_v2) {
  std::string out;
  out += std::string("1 .M N... 100644 100644 100644 aaaa bbbb src/a b.cpp") + '\0';
  out += std::string("1 A. N... 000000 100644 100644 0000 cccc new.txt") + '\0';
  out += std::string("1 .D N... 100644 100644 000000 aaaa aaaa gone.txt") + '\0';
  out += std::string("2 R. N... 100644 100644 100644 aaaa aaaa R100 renamed to.txt") + '\0' + "from name.txt" + '\0';
  out += std::string("u UU N... 100644 100644 100644 100644 a b c clash.txt") + '\0';
  out += std::string("? untracked dir/\xC3\xBCn.txt") + '\0';
  out += std::string("! ignored.log") + '\0';
  auto es = GitProvider::parseStatusV2(out);
  REQUIRE(es.size() == 7);
  CHECK(es[0].path == "src/a b.cpp" && es[0].status == VcsStatus::Modified);
  CHECK(es[1].path == "new.txt" && es[1].status == VcsStatus::Added);
  CHECK(es[2].path == "gone.txt" && es[2].status == VcsStatus::Deleted);
  CHECK(es[3].path == "renamed to.txt" && es[3].origPath == "from name.txt" && es[3].status == VcsStatus::Renamed);
  CHECK(es[4].path == "clash.txt" && es[4].status == VcsStatus::Conflicted);
  CHECK(es[5].path == "untracked dir/\xC3\xBCn.txt" && es[5].status == VcsStatus::Untracked);
  CHECK(es[6].path == "ignored.log" && es[6].status == VcsStatus::Ignored);
  CHECK(GitProvider::parseStatusV2("").empty());
}

TEST(git_detect) {
  SKIP_WITHOUT_GIT();
  TempRepo r;
  REQUIRE(r.ok);
  r.write("sub/dir/x.txt", "x\n");
  GitProvider g;
  CHECK(g.detect(r.dir));
  CHECK(pathsEqual(g.root(), r.dir));
  GitProvider g2;
  CHECK(g2.detect(r.dir / "sub" / "dir"));  // a subdirectory finds the top level
  CHECK(pathsEqual(g2.root(), r.dir));
  GitProvider g3;
  CHECK(g3.detect(r.dir / "sub" / "dir" / "x.txt"));  // so does a file
  CHECK(pathsEqual(g3.root(), r.dir));
  // A directory that is no repository.
  fs::path plain = tfvcs::tempDir("plain");
  GitProvider g4;
  CHECK(!g4.detect(plain));
  CHECK(!g4.lastError().empty());
  CHECK(g4.root().empty());
  CHECK(!g4.detect(plain / "does" / "not" / "exist"));
  GitProvider missing("tenfold-no-such-git-binary");
  CHECK(!missing.detect(r.dir));
  std::error_code ec;
  fs::remove_all(plain, ec);
}

TEST(git_status_kinds) {
  SKIP_WITHOUT_GIT();
  TempRepo r;
  REQUIRE(r.ok);
  r.write(".gitignore", "*.log\nbuild/\n");
  r.write("a.txt", "a1\na2\na3\n");
  r.write("b.txt", "b\n");
  r.write("c.txt", "c line one\nc line two\nc line three\nc line four\n");
  r.write("clean.txt", "clean\n");
  r.write("dir with space/\xC3\xBCn\xC3\xAF.txt", "uni\n");
  r.commitAll("base");

  r.write("a.txt", "a1\nCHANGED\na3\n");          // modified
  fs::remove(r.path("b.txt"));                    // deleted
  REQUIRE(r.git({"mv", "c.txt", "d.txt"}).ok());  // renamed (staged)
  r.write("e.txt", "new and staged\n");
  REQUIRE(r.git({"add", "e.txt"}).ok());  // added
  r.write("f.txt", "untracked\n");        // untracked
  r.write("g.log", "ignored\n");          // ignored
  r.write("build/out.o", "obj\n");        // ignored directory
  r.write("dir with space/\xC3\xBCn\xC3\xAF.txt", "uni changed\n");
  r.write("dir with space/new file.txt", "x\n");  // untracked with a space

  GitProvider g;
  REQUIRE(g.detect(r.dir));
  CHECK(statusOf(g, r.path("a.txt")) == VcsStatus::Modified);
  CHECK(statusOf(g, r.path("b.txt")) == VcsStatus::Deleted);
  CHECK(statusOf(g, r.path("d.txt")) == VcsStatus::Renamed);
  CHECK(statusOf(g, r.path("e.txt")) == VcsStatus::Added);
  CHECK(statusOf(g, r.path("f.txt")) == VcsStatus::Untracked);
  CHECK(statusOf(g, r.path("g.log")) == VcsStatus::Ignored);
  CHECK(statusOf(g, r.path("build/out.o")) == VcsStatus::Ignored);
  CHECK(statusOf(g, r.path("clean.txt")) == VcsStatus::Unmodified);
  CHECK(statusOf(g, r.path("dir with space/\xC3\xBCn\xC3\xAF.txt")) == VcsStatus::Modified);
  CHECK(statusOf(g, r.path("dir with space/new file.txt")) == VcsStatus::Untracked);
  CHECK(statusOf(g, r.path("nope.txt")) == VcsStatus::Unknown);
  CHECK(statusOf(g, fs::temp_directory_path() / "tenfold_outside_the_repo.txt") == VcsStatus::Unknown);

  auto ren = g.status({r.path("d.txt")});
  REQUIRE(ren.size() == 1);
  CHECK(pathsEqual(ren[0].renamedFrom, r.path("c.txt")));

  // Several files at once keep the order of the request.
  auto many = g.status({r.path("f.txt"), r.path("clean.txt"), r.path("a.txt")});
  REQUIRE(many.size() == 3);
  CHECK(many[0].status == VcsStatus::Untracked);
  CHECK(many[1].status == VcsStatus::Unmodified);
  CHECK(many[2].status == VcsStatus::Modified);
  CHECK(pathsEqual(many[2].path, r.path("a.txt")));

  // The whole workspace: only changed files, ignored ones left out.
  std::map<std::string, VcsStatus> all;
  for (auto& fi : g.status({})) all[fs::relative(fi.path, r.dir).generic_string()] = fi.status;
  CHECK(all["a.txt"] == VcsStatus::Modified);
  CHECK(all["b.txt"] == VcsStatus::Deleted);
  CHECK(all["d.txt"] == VcsStatus::Renamed);
  CHECK(all["e.txt"] == VcsStatus::Added);
  CHECK(all["f.txt"] == VcsStatus::Untracked);
  CHECK(all.count("clean.txt") == 0);
  CHECK(all.count("g.log") == 0);
  CHECK(all.count("build/out.o") == 0);
  CHECK(all.count("dir with space/new file.txt") == 1);
  CHECK(all["dir with space/new file.txt"] == VcsStatus::Untracked);
}

TEST(git_status_conflict) {
  SKIP_WITHOUT_GIT();
  TempRepo r;
  REQUIRE(r.ok);
  r.write("c.txt", "base\n");
  r.commitAll("base");
  const std::string branch = [&] {
    std::string b = r.git({"rev-parse", "--abbrev-ref", "HEAD"}).out;
    while (!b.empty() && (b.back() == '\n' || b.back() == '\r')) b.pop_back();
    return b;
  }();
  REQUIRE(r.git({"checkout", "-q", "-b", "other"}).ok());
  r.write("c.txt", "other side\n");
  r.commitAll("other");
  REQUIRE(r.git({"checkout", "-q", branch}).ok());
  r.write("c.txt", "our side\n");
  r.commitAll("ours");
  CHECK(!r.git({"merge", "other"}).ok());  // conflicts
  GitProvider g;
  REQUIRE(g.detect(r.dir));
  CHECK(statusOf(g, r.path("c.txt")) == VcsStatus::Conflicted);
}

TEST(git_base_text) {
  SKIP_WITHOUT_GIT();
  TempRepo r;
  REQUIRE(r.ok);
  GitProvider g;
  REQUIRE(g.detect(r.dir));
  // Empty repository: HEAD does not exist yet. No base, and that is not an error.
  r.write("first.txt", "x\n");
  std::string err = "stale";
  CHECK(!g.baseText(r.path("first.txt"), &err).has_value());
  CHECK(err.empty());

  r.write("a.txt", "one\ntwo\n");
  r.write(
      "sp ace/\xC3\xBCn\xC3\xAF c\xC3\xB6"
      "de.txt",
      "unicode \xE2\x9C\x93\n");
  r.write("bin.dat", std::string("\0\1\2\xFF\n", 5));
  r.write("crlf.txt", "win\r\nline\r\n");
  r.commitAll("base");

  auto a = g.baseText(r.path("a.txt"), &err);
  REQUIRE(a.has_value());
  CHECK_EQ(*a, std::string("one\ntwo\n"));
  CHECK(err.empty());
  r.write("a.txt", "edited\n");  // the base stays the committed text
  CHECK_EQ(*g.baseText(r.path("a.txt")), std::string("one\ntwo\n"));
  auto u = g.baseText(r.path("sp ace/\xC3\xBCn\xC3\xAF c\xC3\xB6"
                             "de.txt"),
                      &err);
  REQUIRE(u.has_value());
  CHECK_EQ(*u, std::string("unicode \xE2\x9C\x93\n"));
  auto bin = g.baseText(r.path("bin.dat"), &err);
  REQUIRE(bin.has_value());
  CHECK_EQ(*bin, std::string("\0\1\2\xFF\n", 5));
  CHECK_EQ(*g.baseText(r.path("crlf.txt")), std::string("win\r\nline\r\n"));

  // Not in HEAD (untracked, staged-new, never existed): nullopt without an error.
  r.write("new.txt", "n\n");
  CHECK(!g.baseText(r.path("new.txt"), &err).has_value());
  CHECK(err.empty());
  REQUIRE(r.git({"add", "new.txt"}).ok());
  CHECK(!g.baseText(r.path("new.txt"), &err).has_value());
  CHECK(err.empty());
  CHECK(!g.baseText(r.path("never.txt"), &err).has_value());
  CHECK(err.empty());

  // Outside the repository: an error.
  fs::path outside = tfvcs::tempDir("outside");
  tfvcs::writeFile(outside / "o.txt", "o\n");
  CHECK(!g.baseText(outside / "o.txt", &err).has_value());
  CHECK(!err.empty());
  CHECK(!g.revert(outside / "o.txt", &err));
  CHECK(!err.empty());
  CHECK(tfvcs::readFile(outside / "o.txt") == "o\n");
  CHECK_EQ(g.status({outside / "o.txt"})[0].status, VcsStatus::Unknown);
  std::error_code ec;
  fs::remove_all(outside, ec);
}

TEST(git_base_compares_independent_of_autocrlf) {
  SKIP_WITHOUT_GIT();
  TempRepo r;
  REQUIRE(r.ok);
  r.write("t.txt", "alpha\nbeta\ngamma\n");  // committed with LF
  r.commitAll("lf");
  GitProvider g;
  REQUIRE(g.detect(r.dir));
  // The working copy has CRLF (what autocrlf=true produces on Windows).
  r.write("t.txt", "alpha\r\nbeta\r\ngamma\r\n");
  auto base = g.baseText(r.path("t.txt"));
  REQUIRE(base.has_value());
  CHECK(diffLines(splitLines(*base), splitLines(r.read("t.txt"))).empty());
  r.write("t.txt", "alpha\r\nBETA\r\ngamma\r\n");
  auto hs = diffLines(splitLines(*base), splitLines(r.read("t.txt")));
  REQUIRE(hs.size() == 1);
  CHECK_EQ(hs[0].baseStart, 1);
  // And a CRLF committed file against an LF buffer.
  r.write("w.txt", "x\r\ny\r\n");
  r.commitAll("crlf");
  auto wb = g.baseText(r.path("w.txt"));
  REQUIRE(wb.has_value());
  CHECK(diffLines(splitLines(*wb), splitLines("x\ny\n")).empty());
}

TEST(git_revert) {
  SKIP_WITHOUT_GIT();
  TempRepo r;
  REQUIRE(r.ok);
  r.write("a.txt", "one\ntwo\n");
  r.write("sp ace/\xC3\xBCn\xC3\xAF.txt", "uni\n");
  r.write("del.txt", "will be deleted\n");
#ifndef _WIN32
  r.write("star*.txt", "glob chars in the name\n");
#endif
  r.commitAll("base");
  GitProvider g;
  REQUIRE(g.detect(r.dir));
  std::string err;

  r.write("a.txt", "changed\n");
  CHECK(statusOf(g, r.path("a.txt")) == VcsStatus::Modified);
  CHECK(g.revert(r.path("a.txt"), &err));
  CHECK(err.empty());
  CHECK_EQ(r.read("a.txt"), std::string("one\ntwo\n"));
  CHECK(statusOf(g, r.path("a.txt")) == VcsStatus::Unmodified);

  // staged AND unstaged changes both go
  r.write("a.txt", "staged\n");
  REQUIRE(r.git({"add", "a.txt"}).ok());
  r.write("a.txt", "staged then edited\n");
  CHECK(g.revert(r.path("a.txt"), &err));
  CHECK_EQ(r.read("a.txt"), std::string("one\ntwo\n"));
  CHECK(statusOf(g, r.path("a.txt")) == VcsStatus::Unmodified);

  // a deleted file comes back
  fs::remove(r.path("del.txt"));
  CHECK(statusOf(g, r.path("del.txt")) == VcsStatus::Deleted);
  CHECK(g.revert(r.path("del.txt"), &err));
  CHECK_EQ(r.read("del.txt"), std::string("will be deleted\n"));

  // spaces, unicode and glob characters in the path
  r.write("sp ace/\xC3\xBCn\xC3\xAF.txt", "edited\n");
  CHECK(g.revert(r.path("sp ace/\xC3\xBCn\xC3\xAF.txt"), &err));
  CHECK_EQ(r.read("sp ace/\xC3\xBCn\xC3\xAF.txt"), std::string("uni\n"));
#ifndef _WIN32  // '*' is not a legal file name character on Windows
  r.write("star*.txt", "edited\n");
  r.write("starX.txt", "must stay as it is\n");
  REQUIRE(r.git({"add", "starX.txt"}).ok());
  r.git({"commit", "-q", "-m", "x"});
  r.write("starX.txt", "edited as well\n");
  CHECK(g.revert(r.path("star*.txt"), &err));
  CHECK_EQ(r.read("star*.txt"), std::string("glob chars in the name\n"));
  CHECK_EQ(r.read("starX.txt"), std::string("edited as well\n"));

#endif

  // an untracked file cannot be reverted: error, file untouched
  r.write("fresh.txt", "untracked\n");
  CHECK(!g.revert(r.path("fresh.txt"), &err));
  CHECK(!err.empty());
  CHECK(!g.lastError().empty());
  CHECK_EQ(r.read("fresh.txt"), std::string("untracked\n"));
}
