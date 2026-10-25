// P1 engine fixes: workspace.json is merged (never clobbered) and written atomically, the scan skips build-output
// directories by exact name (not by prefix), the private clangd config follows the compile-command source, and a cached
// heuristic database is still reported as heuristic.
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>

#include <nlohmann/json.hpp>

#include "app/Editor.h"
#include "test_main.h"
#include "workspace/Workspace.h"

using namespace tf;
namespace fs = std::filesystem;

namespace {

fs::path freshDir(const std::string& name) {
  fs::path d = fs::weakly_canonical(fs::temp_directory_path()) / ("tenfold_p1_" + name);
  std::error_code ec;
  fs::remove_all(d, ec);
  fs::create_directories(d);
  return d;
}

fs::path copyFixture(const std::string& fixture, const std::string& name) {
  fs::path d = freshDir(name);
  fs::copy(fs::path(TENFOLD_SOURCE_DIR) / "tests/fixtures/vs" / fixture, d, fs::copy_options::recursive);
  return d;
}

std::string slurp(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), {});
}
void write(const fs::path& p, const std::string& text) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary) << text;
}
bool contains(const std::string& h, const std::string& n) { return h.find(n) != std::string::npos; }

Editor::Options noClangd() {
  Editor::Options o;
  o.semantic.useClangd = false;
  return o;
}

}  // namespace

TEST(p1_workspace_json_keeps_unknown_keys_when_settings_are_saved) {
  fs::path root = copyFixture("Multi", "wsjson");
  write(root / ".tenfold/workspace.json",
        R"({"vcs": {"provider": "none", "p4Path": "C:/p4/p4.exe"}, "vcs.provider": "git", "debug": {"x": 1},
            "debug.attachAtStartup": false, "futureKey": [1, 2, 3], "vsConfiguration": "Debug", "vsPlatform": "x64"})");
  {
    Workspace ws(root);
    REQUIRE(ws.loadSettings());
    std::string err;
    REQUIRE(ws.setVsConfiguration("Release|x64", &err));  // what vsconfig --set / Build.SelectConfiguration do
  }
  {
    // cl.exe discovery persists msvcToolchain through the same call
    Workspace ws(root);
    REQUIRE(ws.loadSettings());
    ws.settings().msvcToolchain = "C:/VS/cl.exe";
    std::string err;
    REQUIRE(ws.saveSettings(&err));
  }
  auto j = nlohmann::json::parse(slurp(root / ".tenfold/workspace.json"), nullptr, false);
  REQUIRE(j.is_object());
  CHECK_EQ(j["vcs"]["provider"].get<std::string>(), "none");
  CHECK_EQ(j["vcs"]["p4Path"].get<std::string>(), "C:/p4/p4.exe");
  CHECK_EQ(j["vcs.provider"].get<std::string>(), "git");
  CHECK_EQ(j["debug"]["x"].get<int>(), 1);
  CHECK(j["futureKey"].is_array() && j["futureKey"].size() == 3);
  CHECK_EQ(j["debug.attachAtStartup"].get<bool>(), false);  // known key, value kept
  CHECK_EQ(j["vsConfiguration"].get<std::string>(), "Release");
  CHECK_EQ(j["msvcToolchain"].get<std::string>(), "C:/VS/cl.exe");
  CHECK(!fs::exists(root / ".tenfold/workspace.json.tmp"));  // the temporary file was renamed away
  std::error_code ec;
  fs::remove_all(root, ec);
}

TEST(p1_malformed_workspace_json_is_not_overwritten) {
  fs::path root = copyFixture("Multi", "wsjson_bad");
  const std::string bad = "{ \"vcs\": { not json at all";
  write(root / ".tenfold/workspace.json", bad);
  Workspace ws(root);
  std::string err;
  CHECK(!ws.loadSettings(&err));
  CHECK(!ws.setVsConfiguration("Release|x64", &err));
  CHECK(contains(err, "workspace.json"));
  CHECK_EQ(slurp(root / ".tenfold/workspace.json"), bad);
  CHECK(!fs::exists(root / ".tenfold/workspace.json.tmp"));
  write(root / ".tenfold/workspace.json", "[1, 2]");  // valid JSON but not an object
  CHECK(!ws.saveSettings(&err));
  CHECK_EQ(slurp(root / ".tenfold/workspace.json"), std::string("[1, 2]"));
  fs::remove(root / ".tenfold/workspace.json");  // a missing file is simply created
  CHECK(ws.saveSettings(&err));
  CHECK(fs::exists(root / ".tenfold/workspace.json"));
  std::error_code ec;
  fs::remove_all(root, ec);
}

TEST(p1_scan_skips_build_output_names_exactly_and_scans_lookalikes) {
  fs::path root = freshDir("scan");
  for (const char* d : {"output", "objects", "binding", "builder", "outer", "src", "buildtools", "Release", "x64"})
    write(root / d / "f.cpp", "int x;\n");
  for (const char* d : {"build", "Build", "out", "bin", "obj", "build-x64", "cmake-build-debug", "cmake-build", ".git"})
    write(root / d / "g.cpp", "int y;\n");
  Workspace ws(root);
  for (const char* d : {"build", "Build", "out", "bin", "obj", "build-x64", "cmake-build-debug", "cmake-build", ".git"})
    CHECK(ws.isIgnoredDir(root / d));
  for (const char* d : {"output", "objects", "binding", "builder", "outer", "src", "buildtools", "Release", "x64"})
    CHECK(!ws.isIgnoredDir(root / d));
  ws.scan();
  std::set<std::string> dirs;
  for (auto& f : ws.sourceFiles()) dirs.insert(f.parent_path().filename().string());
  CHECK((dirs == std::set<std::string>{"output", "objects", "binding", "builder", "outer", "src", "buildtools",
                                       "Release", "x64"}));
  // A workspace.json saved by an older version carries the old prefix defaults; they are replaced, not honoured.
  write(root / ".tenfold/workspace.json", R"({"ignoreDirPrefixes": ["build", "out", "cmake-build", "bin", "obj"]})");
  Workspace old(root);
  REQUIRE(old.loadSettings());
  CHECK(!old.isIgnoredDir(root / "output"));
  CHECK(old.isIgnoredDir(root / "build-x64"));
  // The lists remain configurable.
  write(root / ".tenfold/workspace.json", R"({"ignoreDirNames": ["output"], "ignoreDirPrefixes": []})");
  Workspace custom(root);
  REQUIRE(custom.loadSettings());
  CHECK(custom.isIgnoredDir(root / "output"));
  CHECK(!custom.isIgnoredDir(root / "build"));
  std::error_code ec;
  fs::remove_all(root, ec);
}

TEST(p1_clangd_config_follows_the_compile_command_source_and_cached_heuristic_stays_heuristic) {
  fs::path root = copyFixture("FirstOnly", "cfg_switch");
  fs::rename(root / "FirstOnly.sln", root / "FirstOnly.sln.off");
  write(root / "inc/lib/thing.h", "#pragma once\nstruct Thing { int x; };\n");
  write(root / "src/a.cpp", "#include \"lib/thing.h\"\nint main() { Thing t; return t.x; }\n");
  write(root / "src/b.cpp", "#include \"lib/thing.h\"\nint f() { return 1; }\n");
  const fs::path cfg = root / ".tenfold/clangd-config/clangd/config.yaml";
  const fs::path a = root / "src/a.cpp";
  auto sourceOf = [&](const std::string& expect, bool expectFlagsInConfig, const char* what) {
    Editor ed(root, noClangd());
    std::string err;
    REQUIRE(ed.start(&err));
    const auto info = ed.compileCommandInfo(a);
    if (info.source != expect) std::cerr << "    " << what << ": source is " << info.source << "\n";
    CHECK_EQ(info.source, expect);
    CHECK_EQ(ed.semantic().status().compdbSource, expect);
    const std::string c = slurp(cfg);
    CHECK(contains(c, "Index:"));
#if !defined(__APPLE__) && !defined(_WIN32)
    CHECK_EQ(contains(c, "CompileFlags:"), expectFlagsInConfig);
    CHECK_EQ(contains(c, "-I"), expectFlagsInConfig);
#else
    (void)expectFlagsInConfig;  // other platforms keep the flags in the commands
#endif
    ed.shutdown();
  };
  sourceOf("heuristic", true, "cold heuristic");
  sourceOf("heuristic", true, "warm heuristic (cached database keeps its label and its shared flags)");
  // The solution appears: the cached heuristic database and its flags must not survive.
  fs::rename(root / "FirstOnly.sln.off", root / "FirstOnly.sln");
  sourceOf("vcxproj", false, "heuristic -> vcxproj");
  sourceOf("vcxproj", false, "warm vcxproj");
  // And back: the stamp of the solution database no longer applies without a solution.
  fs::rename(root / "FirstOnly.sln", root / "FirstOnly.sln.off");
  sourceOf("heuristic", true, "vcxproj -> heuristic");
  // A real compile_commands.json also gets a config without the heuristic flags.
  write(root / "compile_commands.json", "[{\"directory\": \"" + root.generic_string() + "\", \"file\": \"" +
                                            a.generic_string() + "\", \"arguments\": [\"clang++\", \"-c\", \"" +
                                            a.generic_string() + "\"]}]");
  sourceOf("existing", false, "heuristic -> existing");
  std::error_code ec;
  fs::remove_all(root, ec);
}
