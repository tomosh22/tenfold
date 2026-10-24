#include <algorithm>
#include <fstream>

#include "core/Document.h"
#include "core/PathUtil.h"
#include "nav/NavigationHistory.h"
#include "semantic/CompileCommands.h"
#include "test_main.h"
#include "workspace/Workspace.h"

using namespace tf;
namespace fs = std::filesystem;

namespace {
const PathStyle W = PathStyle::Windows;
const PathStyle P = PathStyle::Posix;

fs::path freshDir(const char* name) {
  fs::path d = fs::temp_directory_path() / (std::string("tenfold_paths_") + name);
  std::error_code ec;
  fs::remove_all(d, ec);
  fs::create_directories(d);
  return d;
}
void touch(const fs::path& p) {
  fs::create_directories(p.parent_path());
  std::ofstream(p) << "x";
}
bool has(const std::vector<std::string>& v, const std::string& s) { return std::find(v.begin(), v.end(), s) != v.end(); }
}  // namespace

TEST(uri_windows_drive_roundtrip) {
  CHECK_EQ(pathToUri("C:\\Users\\me\\x.cpp", W), "file:///C:/Users/me/x.cpp");
  CHECK_EQ(pathToUri("C:/Users/me/x.cpp", W), "file:///C:/Users/me/x.cpp");
  CHECK_EQ(uriToPathString("file:///C:/Users/me/x.cpp", W), "C:\\Users\\me\\x.cpp");
  // clangd style: lower-case drive, escaped colon
  CHECK_EQ(uriToPathString("file:///c%3A/Users/me/x.cpp", W), "c:\\Users\\me\\x.cpp");
  CHECK_EQ(uriToPathString(pathToUri("D:\\a\\b.h", W), W), "D:\\a\\b.h");
}

TEST(uri_windows_unc_roundtrip) {
  CHECK_EQ(pathToUri("\\\\server\\share\\dir\\x.cpp", W), "file://server/share/dir/x.cpp");
  CHECK_EQ(uriToPathString("file://server/share/dir/x.cpp", W), "\\\\server\\share\\dir\\x.cpp");
  CHECK_EQ(uriToPathString(pathToUri("//srv/s/a b.cpp", W), W), "\\\\srv\\s\\a b.cpp");
}

TEST(uri_spaces_and_percent) {
  CHECK_EQ(pathToUri("C:\\My Project\\100%\\a#b.cpp", W), "file:///C:/My%20Project/100%25/a%23b.cpp");
  CHECK_EQ(uriToPathString("file:///C:/My%20Project/100%25/a%23b.cpp", W), "C:\\My Project\\100%\\a#b.cpp");
  CHECK_EQ(pathToUri("/home/u/my dir/x.cpp", P), "file:///home/u/my%20dir/x.cpp");
  CHECK_EQ(uriToPathString("file:///home/u/my%20dir/x.cpp", P), "/home/u/my dir/x.cpp");
  CHECK_EQ(uriToPathString(pathToUri("/a/ü/é%.cpp", P), P), "/a/ü/é%.cpp");
  // malformed escapes are left alone rather than throwing
  CHECK_EQ(uriToPathString("file:///a/%zz/b", P), "/a/%zz/b");
}

TEST(uri_posix_native_wrappers) {
  CHECK_EQ(pathToUri(fs::path("/tmp/a b/c.cpp")), "file:///tmp/a%20b/c.cpp");
  CHECK_EQ(uriToPath("file:///tmp/a%20b/c.cpp").generic_string(), "/tmp/a b/c.cpp");
}

TEST(path_key_windows_rules) {
  CHECK(pathsEqual("C:\\Foo\\Bar.CPP", "c:/foo/bar.cpp", W));
  CHECK(pathsEqual("C:\\Foo\\\\Bar", "C:/foo/bar/", W));
  CHECK(pathsEqual("\\\\Server\\Share\\x", "//server/share/X", W));
  CHECK(!pathsEqual("C:\\foo", "C:\\bar", W));
  CHECK(!pathsEqual("\\\\server\\share", "/server/share", W));
  CHECK_EQ(pathKey("C:\\", W), "c:/");
  CHECK_EQ(pathKey("\\\\Srv\\S\\", W), "//srv/s");
  // Posix is exact
  CHECK(!pathsEqual("/a/B", "/a/b", P));
  CHECK(!pathsEqual("/a/b", "/a\\b", P));
  CHECK(pathsEqual("/a/b", "/a/b", P));
}

TEST(exe_split_path_list) {
  auto w = splitPathList("C:\\a;C:\\b dir;;D:\\c;", ';');
  REQUIRE(w.size() == 3u);
  CHECK_EQ(w[1], "C:\\b dir");
  auto p = splitPathList("/usr/bin::/opt/bin", ':');
  REQUIRE(p.size() == 2u);
  CHECK_EQ(p[1], "/opt/bin");
  CHECK(splitPathList("", ':').empty());
}

TEST(exe_candidates_windows_rules) {
  auto c = executableCandidates("clangd", true);
  REQUIRE(c.size() == 3u);
  CHECK_EQ(c[0], "clangd.exe");
  CHECK_EQ(c[1], "clangd.cmd");
  CHECK_EQ(c[2], "clangd.bat");
  CHECK_EQ(executableCandidates("clangd.EXE", true).size(), 1u);
  CHECK_EQ(executableCandidates("tool.bat", true)[0], "tool.bat");
  auto p = executableCandidates("clangd", false);
  REQUIRE(p.size() == 1u);
  CHECK_EQ(p[0], "clangd");
}

TEST(exe_find_in_dirs_windows_and_order) {
  fs::path d = freshDir("exe");
  touch(d / "one" / "tool.cmd");
  touch(d / "two" / "tool.exe");
  std::vector<std::string> dirs = {(d / "missing").string(), (d / "one").string(), (d / "two").string()};
  CHECK_EQ(findExecutableIn("tool", dirs, true), (d / "one" / "tool.cmd").string());
  CHECK_EQ(findExecutableIn("tool", dirs, false), "");  // posix: bare name only
  std::vector<std::string> bundledFirst = {(d / "two").string(), (d / "one").string()};
  CHECK_EQ(findExecutableIn("tool", bundledFirst, true), (d / "two" / "tool.exe").string());
  CHECK(!bundledBinDir().empty() ? bundledBinDir().filename() == "bin" : true);
  fs::remove_all(d);
}

TEST(msvc_cl_via_vctoolsinstalldir) {
  fs::path d = freshDir("vct");
  fs::path tools = d / "VC" / "Tools" / "MSVC" / "14.40.33807";
  touch(tools / "bin" / "Hostx64" / "x64" / "cl.exe");
  compdb::MsvcProbe probe;
  probe.vcToolsInstallDir = tools.string() + "/";
  auto cl = compdb::discoverCl("", probe);
  REQUIRE(cl.has_value());
  CHECK_EQ(cl->generic_string(), (tools / "bin/Hostx64/x64/cl.exe").generic_string());
  CHECK_EQ(compdb::queryDriverArg(*cl), "--query-driver=" + (tools / "bin/Hostx64/x64").generic_string() + "/**");
  fs::remove_all(d);
}

TEST(msvc_cl_via_vswhere_picks_newest) {
  fs::path d = freshDir("vsw");
  touch(d / "VC/Tools/MSVC/14.9.1/bin/Hostx64/x64/cl.exe");
  touch(d / "VC/Tools/MSVC/14.40.33807/bin/Hostx64/x64/cl.exe");
  touch(d / "VC/Tools/MSVC/14.41.0/bin/Hostx86/x86/cl.exe");  // no x64 host: skipped
  compdb::MsvcProbe probe;
  probe.vswhere = [&] { return d.string() + "\r\n"; };
  auto cl = compdb::discoverCl("", probe);
  REQUIRE(cl.has_value());
  CHECK(cl->generic_string().find("14.40.33807") != std::string::npos);
  // Nothing found anywhere
  compdb::MsvcProbe none;
  none.vswhere = [] { return std::string(); };
  CHECK(!compdb::discoverCl("", none).has_value());
  fs::remove_all(d);
}

TEST(msvc_configured_toolchain_skips_discovery) {
  compdb::MsvcProbe probe;
  bool called = false;
  probe.vswhere = [&] { called = true; return std::string("/nope"); };
  auto cl = compdb::discoverCl("/opt/msvc/bin/Hostx64/x64/cl.exe", probe);
  REQUIRE(cl.has_value());
  CHECK_EQ(cl->generic_string(), "/opt/msvc/bin/Hostx64/x64/cl.exe");
  CHECK(!called);
}

TEST(msvc_toolchain_setting_roundtrip_and_args) {
  fs::path d = freshDir("ws");
  {
    Workspace ws(d);
    CHECK_EQ(ws.settings().vsConfiguration, "Debug");
    CHECK_EQ(ws.settings().vsPlatform, "x64");
    CHECK(ws.settings().msvcToolchain.empty());
    ws.settings().msvcToolchain = "/tc/bin/Hostx64/x64/cl.exe";
    ws.settings().vsConfiguration = "Release";
    ws.settings().vsPlatform = "ARM64";
    CHECK(ws.saveSettings());
  }
  Workspace ws2(d);
  CHECK(ws2.loadSettings());
  CHECK_EQ(ws2.settings().msvcToolchain, "/tc/bin/Hostx64/x64/cl.exe");
  CHECK_EQ(ws2.settings().vsConfiguration, "Release");
  CHECK_EQ(ws2.settings().vsPlatform, "ARM64");
  auto cl = compdb::discoverCl(ws2.settings().msvcToolchain, {});
  REQUIRE(cl.has_value());
  auto args = compdb::msvcClangdArgs(*cl);
  REQUIRE(args.size() == 1u);
  CHECK_EQ(args[0], "--query-driver=/tc/bin/Hostx64/x64/**");
  CHECK(compdb::msvcClangdArgs({}).empty());
  fs::remove_all(d);
}

TEST(vcxproj_additional_options_fixture) {
  fs::path proj = fs::path(TENFOLD_SOURCE_DIR) / "tests/fixtures/vs/Opts/Opts.vcxproj";
  std::string err;
  auto cmds = compdb::fromVcxproj(proj, {}, &err);
  CHECK_EQ(err, "");
  REQUIRE(cmds.size() == 1u);
  auto& a = cmds[0].arguments;
  CHECK_EQ(a[0], "clang-cl");
  CHECK(has(a, "/Zc:__cplusplus"));
  CHECK(has(a, "/permissive-"));
  CHECK(has(a, "/std:c++latest"));
  CHECK(!has(a, "%(AdditionalOptions)"));
}
