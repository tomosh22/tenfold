// W9-FIX-vs: Visual Studio configuration handling.
//   * one compile database and one clangd background index per solution configuration (P-15),
//   * a project with an ActiveCfg mapping but no Build.0 is evaluated with the mapped configuration (P-16),
//   * the host standard library fallback of .sln mode where no MSVC toolchain exists (Linux, macOS).
#include <algorithm>
#include <fstream>
#include <iostream>
#include <set>
#include <thread>

#include "app/Editor.h"
#include "core/PathUtil.h"
#include "semantic/CompileCommands.h"
#include "test_main.h"
#include "workspace/Workspace.h"

using namespace tf;
namespace fs = std::filesystem;

namespace {

const fs::path kVs = fs::path(TENFOLD_SOURCE_DIR) / "tests/fixtures/vs";

bool has(const std::vector<std::string>& v, const std::string& s) {
  return std::find(v.begin(), v.end(), s) != v.end();
}
bool hasSuffix(const std::vector<std::string>& v, const std::string& suffix) {
  return std::any_of(v.begin(), v.end(), [&](const std::string& a) { return a.ends_with(suffix); });
}
bool hasPrefix(const std::vector<std::string>& v, const std::string& prefix) {
  return std::any_of(v.begin(), v.end(), [&](const std::string& a) { return a.rfind(prefix, 0) == 0; });
}

fs::path freshDir(const std::string& name) {
  fs::path d = fs::weakly_canonical(fs::temp_directory_path()) / ("tenfold_vsc_" + name);
  std::error_code ec;
  fs::remove_all(d, ec);
  fs::create_directories(d);
  return d;
}

void write(const fs::path& p, const std::string& text) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary) << text;
}

std::string slurp(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), {});
}

compdb::VcxprojOptions hermetic(const std::string& cfg, const std::string& plat) {
  compdb::VcxprojOptions o;
  o.configuration = cfg;
  o.platform = plat;
  o.useProcessEnvironment = false;
  return o;
}

bool clangdAvailable() { return !findExecutable("clangd").empty(); }

// A two-configuration solution: Debug|x64 and Release|x64, with `cfg.h` found in inc_<Configuration>/ (like the
// vs-configs corpus) and a std-free main.cpp that calls cfg_value().
struct Solution {
  fs::path root;
  explicit Solution(const std::string& name) : root(freshDir(name)) {
    write(root / "app.sln",
          "\nMicrosoft Visual Studio Solution File, Format Version 12.00\n"
          "Project(\"{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}\") = \"app\", \"app.vcxproj\", "
          "\"{CCCCCCCC-0000-0000-0000-000000000001}\"\nEndProject\nGlobal\n"
          "\tGlobalSection(SolutionConfigurationPlatforms) = preSolution\n"
          "\t\tDebug|x64 = Debug|x64\n\t\tRelease|x64 = Release|x64\n\tEndGlobalSection\n"
          "\tGlobalSection(ProjectConfigurationPlatforms) = postSolution\n"
          "\t\t{CCCCCCCC-0000-0000-0000-000000000001}.Debug|x64.ActiveCfg = Debug|x64\n"
          "\t\t{CCCCCCCC-0000-0000-0000-000000000001}.Debug|x64.Build.0 = Debug|x64\n"
          "\t\t{CCCCCCCC-0000-0000-0000-000000000001}.Release|x64.ActiveCfg = Release|x64\n"
          "\t\t{CCCCCCCC-0000-0000-0000-000000000001}.Release|x64.Build.0 = Release|x64\n"
          "\tEndGlobalSection\nEndGlobal\n");
    write(root / "app.vcxproj",
          "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
          "<Project DefaultTargets=\"Build\" xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">\n"
          "  <ItemGroup Label=\"ProjectConfigurations\">\n"
          "    <ProjectConfiguration Include=\"Debug|x64\"><Configuration>Debug</Configuration>"
          "<Platform>x64</Platform></ProjectConfiguration>\n"
          "    <ProjectConfiguration Include=\"Release|x64\"><Configuration>Release</Configuration>"
          "<Platform>x64</Platform></ProjectConfiguration>\n"
          "  </ItemGroup>\n"
          "  <Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.Default.props\" />\n"
          "  <Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.props\" />\n"
          "  <ItemDefinitionGroup><ClCompile>\n"
          "    <AdditionalIncludeDirectories>$(ProjectDir)inc_$(Configuration);%(AdditionalIncludeDirectories)"
          "</AdditionalIncludeDirectories>\n"
          "  </ClCompile></ItemDefinitionGroup>\n"
          "  <ItemGroup><ClCompile Include=\"main.cpp\" /></ItemGroup>\n"
          "  <Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.targets\" />\n"
          "</Project>\n");
    write(root / "inc_Debug/cfg.h", "#pragma once\ninline int cfg_value() { return 1; }\n");
    write(root / "inc_Release/cfg.h", "#pragma once\ninline int cfg_value() { return 2; }\n");
    write(root / "main.cpp", "#include \"cfg.h\"\nint main() { return cfg_value(); }\n");
  }
  ~Solution() {
    std::error_code ec;
    fs::remove_all(root, ec);
  }
};

fs::path dbDir(Editor& ed) { return ed.semantic().status().compdbPath.parent_path(); }

bool switchTo(Editor& ed, const std::string& name) {
  bool applied = false;
  std::string err;
  if (!ed.selectVsConfiguration(name, &err, [&] { applied = true; })) return false;
  return ed.waitFor([&] { return applied; }, 60000);
}

Editor::Options noClangd() {
  Editor::Options o;
  o.semantic.useClangd = false;
  return o;
}

}  // namespace

// ---------------------------------------------------------------------------
// P-16: ActiveCfg without Build.0
// ---------------------------------------------------------------------------

TEST(vs_activecfg_without_build_is_evaluated_with_the_mapped_configuration) {
  fs::path root = freshDir("p16");
  write(root / "p.sln",
        "\nMicrosoft Visual Studio Solution File, Format Version 12.00\n"
        "Project(\"{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}\") = \"main\", \"main/main.vcxproj\", "
        "\"{AAAAAAAA-0000-0000-0000-000000000001}\"\nEndProject\n"
        "Project(\"{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}\") = \"plugin\", \"plugin/plugin.vcxproj\", "
        "\"{AAAAAAAA-0000-0000-0000-000000000002}\"\nEndProject\nGlobal\n"
        "\tGlobalSection(SolutionConfigurationPlatforms) = preSolution\n"
        "\t\tDebug|x64 = Debug|x64\n\t\tDebug|x86 = Debug|x86\n\tEndGlobalSection\n"
        "\tGlobalSection(ProjectConfigurationPlatforms) = postSolution\n"
        "\t\t{AAAAAAAA-0000-0000-0000-000000000001}.Debug|x64.ActiveCfg = Debug|x64\n"
        "\t\t{AAAAAAAA-0000-0000-0000-000000000001}.Debug|x64.Build.0 = Debug|x64\n"
        "\t\t{AAAAAAAA-0000-0000-0000-000000000001}.Debug|x86.ActiveCfg = Debug|Win32\n"
        "\t\t{AAAAAAAA-0000-0000-0000-000000000001}.Debug|x86.Build.0 = Debug|Win32\n"
        "\t\t{AAAAAAAA-0000-0000-0000-000000000002}.Debug|x64.ActiveCfg = Debug|x64\n"
        "\t\t{AAAAAAAA-0000-0000-0000-000000000002}.Debug|x64.Build.0 = Debug|x64\n"
        "\t\t{AAAAAAAA-0000-0000-0000-000000000002}.Debug|x86.ActiveCfg = Debug|x64\n"  // mapped, never built
        "\tEndGlobalSection\nEndGlobal\n");
  const std::string head =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
      "<Project DefaultTargets=\"Build\" xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">\n"
      "  <ItemGroup Label=\"ProjectConfigurations\">\n"
      "    <ProjectConfiguration Include=\"Debug|x64\"><Configuration>Debug</Configuration><Platform>x64</Platform>"
      "</ProjectConfiguration>\n"
      "    <ProjectConfiguration Include=\"Debug|Win32\"><Configuration>Debug</Configuration><Platform>Win32</Platform>"
      "</ProjectConfiguration>\n  </ItemGroup>\n";
  write(root / "main/main.vcxproj",
        head +
            "  <ItemDefinitionGroup><ClCompile><PreprocessorDefinitions>MAIN_FLAG;"
            "%(PreprocessorDefinitions)</PreprocessorDefinitions></ClCompile>"
            "</ItemDefinitionGroup>\n  <ItemGroup><ClCompile Include=\"main.cpp\" /></ItemGroup>\n</Project>\n");
  write(root / "plugin/plugin.vcxproj",
        head +
            "  <ItemDefinitionGroup Condition=\"'$(Platform)'=='x64'\"><ClCompile><PreprocessorDefinitions>"
            "PLUGIN_X64;%(PreprocessorDefinitions)</PreprocessorDefinitions></ClCompile></ItemDefinitionGroup>\n"
            "  <ItemDefinitionGroup Condition=\"'$(Platform)'=='Win32'\"><ClCompile><PreprocessorDefinitions>"
            "PLUGIN_WIN32;%(PreprocessorDefinitions)</PreprocessorDefinitions></ClCompile></ItemDefinitionGroup>\n"
            "  <ItemGroup><ClCompile Include=\"plugin.cpp\" /></ItemGroup>\n</Project>\n");
  write(root / "main/main.cpp", "int main() { return 0; }\n");
  write(root / "plugin/plugin.cpp", "int plugin() { return 0; }\n");

  std::string err;
  auto cmds = compdb::fromSolution(root / "p.sln", hermetic("Debug", "x86"), &err);
  const CompileCommand* plugin = nullptr;
  const CompileCommand* mainCmd = nullptr;
  for (auto& c : cmds) {
    if (c.file.filename() == "plugin.cpp") plugin = &c;
    if (c.file.filename() == "main.cpp") mainCmd = &c;
  }
  REQUIRE(plugin != nullptr);  // the plugin is mapped to Debug|x64 but not built in Debug|x86
  REQUIRE(mainCmd != nullptr);
  CHECK(has(plugin->arguments, "/DPLUGIN_X64"));  // its mapped configuration, not the solution's x86
  CHECK(!has(plugin->arguments, "/DPLUGIN_WIN32"));
  CHECK(has(plugin->arguments, "-m64"));
  CHECK(has(mainCmd->arguments, "-m32"));  // the built project keeps its own mapping (Win32)

  // The solution API still says what is built.
  auto info = compdb::readSolution(root / "p.sln");
  auto m = info.mapping(info.projects[1], {"Debug", "x86"});
  REQUIRE(m.has_value());
  CHECK(!m->build);
  CHECK_EQ(m->platform, "x64");

  // No mapping at all (not even ActiveCfg) still means "not in this configuration".
  std::string sln = slurp(root / "p.sln");
  const std::string drop = "\t\t{AAAAAAAA-0000-0000-0000-000000000002}.Debug|x86.ActiveCfg = Debug|x64\n";
  sln.erase(sln.find(drop), drop.size());
  write(root / "p.sln", sln);
  bool sawPlugin = false;
  for (auto& c : compdb::fromSolution(root / "p.sln", hermetic("Debug", "x86")))
    sawPlugin = sawPlugin || c.file.filename() == "plugin.cpp";
  CHECK(!sawPlugin);
  std::error_code ec;
  fs::remove_all(root, ec);
}

// ---------------------------------------------------------------------------
// Host standard library fallback
// ---------------------------------------------------------------------------

TEST(vs_host_fallback_rewrites_a_clang_cl_command_for_the_host) {
  const std::vector<std::string> in = {"clang-cl", "/std:c++20", "/EHsc", "/nologo", "-m32", "/IC:/inc",
                                       "/DFOO=1",  "/FI/f.h",    "/MDd",  "/TP",     "/c",   "/x/a.cpp"};
  auto out = compdb::toHostStdlibArgs(in, "x86_64-pc-linux-gnu", {"-isysroot", "/sdk"});
  REQUIRE(!out.empty());
  CHECK_EQ(out[0], "clang-cl");
  CHECK_EQ(out[1], "--target=x86_64-pc-linux-gnu");
  CHECK_EQ(out[2], "-isysroot");
  CHECK_EQ(out[3], "/sdk");
  CHECK(has(out, "-fms-extensions"));
  CHECK(has(out, "/D_MSC_VER=1933"));
  CHECK(has(out, "/clang:-std=c++20"));  // cl-mode /std: is ignored for a non-Windows target
  CHECK(!has(out, "/std:c++20"));
  CHECK(!has(out, "-m32"));
  for (const char* kept : {"/EHsc", "/IC:/inc", "/DFOO=1", "/FI/f.h", "/MDd", "/TP", "/c", "/x/a.cpp"})
    CHECK(has(out, kept));
  CHECK_EQ(out.back(), "/x/a.cpp");

  // The project's own _MSC_VER and a Windows ARM64 target are respected / dropped.
  auto own = compdb::toHostStdlibArgs(
      {"clang-cl", "/D_MSC_VER=1900", "--target=aarch64-pc-windows-msvc", "/std:c11", "/std:c++latest", "/c", "/a.c"},
      "aarch64-unknown-linux-gnu");
  CHECK(has(own, "/D_MSC_VER=1900"));
  CHECK(!hasPrefix(own, "/D_MSC_VER=1933"));
  CHECK(!has(own, "--target=aarch64-pc-windows-msvc"));
  CHECK(has(own, "/clang:-std=c11"));
  CHECK(has(own, "/clang:-std=c++2b"));
  CHECK(compdb::toHostStdlibArgs({}, "x").empty());
}

TEST(vs_host_fallback_is_opt_in_and_keeps_the_project_flags) {
  auto plain = compdb::fromSolution(kVs / "Multi/Multi.sln", hermetic("Debug", "x64"));
  auto hostOpt = hermetic("Debug", "x64");
  hostOpt.hostTarget = "x86_64-pc-linux-gnu";
  auto host = compdb::fromSolution(kVs / "Multi/Multi.sln", hostOpt);
  REQUIRE(!plain.empty());
  REQUIRE(plain.size() == host.size());
  for (auto& c : plain) {  // the MSVC form is untouched
    CHECK(!hasPrefix(c.arguments, "--target="));
    CHECK(!has(c.arguments, "-fms-extensions"));
  }
  for (size_t i = 0; i < host.size(); ++i) {
    const auto& a = host[i].arguments;
    CHECK_EQ(a[0], "clang-cl");
    CHECK(has(a, "--target=x86_64-pc-linux-gnu"));
    CHECK(!has(a, "-m64"));
    CHECK(!hasPrefix(a, "/std:"));
    // everything else the MSVC command had (defines, include dirs, forced includes) is still there
    for (auto& flag : plain[i].arguments) {
      if (flag == "-m64" || flag.rfind("/std:", 0) == 0) continue;
      CHECK(has(a, flag));
    }
  }
#ifdef _WIN32
  CHECK(compdb::hostTargetTriple().empty());
#endif
}

#ifndef _WIN32
TEST(vs_engine_uses_the_host_standard_library_without_msvc_and_logs_it_once) {
  Solution sln("hostfallback");
  int logged = 0;
  Editor::Options o = noClangd();
  o.semantic.forceHostStdlibFallback = true;
  o.log = [&](const std::string& m) {
    if (m.find("no MSVC toolchain") != std::string::npos) ++logged;
  };
  Editor ed(sln.root, std::move(o));
  std::string err;
  REQUIRE(ed.start(&err));
  auto info = ed.compileCommandInfo(sln.root / "main.cpp");
  CHECK_EQ(info.source, "vcxproj");
  REQUIRE(info.listed);
  CHECK(hasPrefix(info.argv, "--target="));
  CHECK(has(info.argv, "-fms-extensions"));
  CHECK(hasSuffix(info.argv, "/inc_Debug"));
  CHECK(!hasPrefix(info.argv, "/std:"));
  REQUIRE(switchTo(ed, "Release|x64"));
  REQUIRE(switchTo(ed, "Debug|x64"));
  CHECK_EQ(logged, 1);
  // The cache is keyed by the mode too: the stamp says which toolchain made the commands.
  const std::string stamp = slurp(dbDir(ed) / "compile_commands.stamp");
  CHECK(stamp.find("host:") != std::string::npos);
}
#endif

// ---------------------------------------------------------------------------
// P-15: one database and one index per configuration
// ---------------------------------------------------------------------------

TEST(vs_engine_keeps_a_database_directory_and_an_index_per_configuration) {
  Solution sln("perconfig");
  // What an earlier version left behind: the shared database, its stamp and the shared background index.
  const fs::path tf = sln.root / ".tenfold";
  write(tf / "compile_commands.json", "[]");
  write(tf / "compile_commands.stamp", "vcxproj\nDebug|x64\n");
  write(tf / ".cache/clangd/index/old.idx", "x");
  {
    Editor ed(sln.root, noClangd());
    std::string err;
    REQUIRE(ed.start(&err));
    const fs::path dbg = dbDir(ed);
    CHECK_EQ(dbg.parent_path().string(), (tf / "vs").string());
    CHECK(!fs::exists(tf / "compile_commands.json"));
    CHECK(!fs::exists(tf / "compile_commands.stamp"));
    CHECK(!fs::exists(tf / ".cache/clangd/index"));
    CHECK_EQ(ed.semantic().status().compdbSource, "vcxproj");

    // clangd keeps its background index in <compile-commands-dir>/.cache/clangd/index: plant a shard there.
    write(dbg / ".cache/clangd/index/marker.idx", "debug");

    REQUIRE(switchTo(ed, "Release|x64"));
    const fs::path rel = dbDir(ed);
    CHECK(rel != dbg);
    CHECK_EQ(rel.parent_path().string(), (tf / "vs").string());
    CHECK(fs::exists(rel / "compile_commands.json"));
    CHECK(!fs::exists(rel / ".cache/clangd/index/marker.idx"));  // a new configuration starts without foreign shards
    CHECK_EQ(slurp(dbg / ".cache/clangd/index/marker.idx"), "debug");  // and the old one keeps its own
    auto info = ed.compileCommandInfo(sln.root / "main.cpp");
    CHECK(hasSuffix(info.argv, "/inc_Release"));

    REQUIRE(switchTo(ed, "Debug|x64"));  // back: same directory, the shard is still there
    CHECK_EQ(dbDir(ed).string(), dbg.string());
    CHECK_EQ(slurp(dbg / ".cache/clangd/index/marker.idx"), "debug");
    info = ed.compileCommandInfo(sln.root / "main.cpp");
    CHECK(hasSuffix(info.argv, "/inc_Debug"));
  }
  // A warm start (no reconfigure) reuses the cached database of the active configuration.
  {
    Editor ed(sln.root, noClangd());
    std::string err;
    REQUIRE(ed.start(&err));
    CHECK(ed.semantic().status().compdbPath.parent_path().filename().string().rfind("Debug_x64-", 0) == 0);
    CHECK(ed.semantic().compileCommands()->size() == 1u);
  }
}

TEST(vs_engine_bounds_the_number_of_configuration_directories) {
  fs::path root = freshDir("prune");
  fs::copy(kVs / "Multi", root, fs::copy_options::recursive);
  Editor::Options o = noClangd();
  o.semantic.maxVsConfigIndexes = 2;
  Editor ed(root, std::move(o));
  std::string err;
  REQUIRE(ed.start(&err));
  const fs::path first = dbDir(ed);  // Debug|x64
  // The least recently used directory is the one that goes: make "first" clearly the oldest.
  fs::last_write_time(first / "compile_commands.stamp", fs::file_time_type::clock::now() - std::chrono::hours(2));
  REQUIRE(switchTo(ed, "Release|x64"));
  const fs::path second = dbDir(ed);
  CHECK(fs::exists(first));  // two are allowed
  fs::last_write_time(second / "compile_commands.stamp", fs::file_time_type::clock::now() - std::chrono::hours(1));
  REQUIRE(switchTo(ed, "Debug|x86"));
  const fs::path third = dbDir(ed);
  CHECK(!fs::exists(first));  // dropped: the oldest
  CHECK(fs::exists(second));
  CHECK(fs::exists(third));
  size_t dirs = 0;
  for (auto& e : fs::directory_iterator(root / ".tenfold/vs")) dirs += e.is_directory() ? 1 : 0;
  CHECK_EQ(dirs, size_t(2));
  std::error_code ec;
  fs::remove_all(root, ec);
}

TEST(vs_configuration_directories_differ_for_names_that_sanitise_alike) {
  // "Debug|x64" and "Debug_x64|x64" both read Debug_x64... on disk; the hash of the exact name keeps them apart.
  Solution sln("names");
  std::string text = slurp(sln.root / "app.sln");
  text.replace(text.find("\t\tRelease|x64 = Release|x64\n"), 28, "\t\tDebug_x64|x64 = Debug_x64|x64\n");
  text.erase(text.find("\tGlobalSection(ProjectConfigurationPlatforms)"));  // no map: every project, same name
  write(sln.root / "app.sln", text + "EndGlobal\n");
  Editor ed(sln.root, noClangd());
  std::string err;
  REQUIRE(ed.start(&err));
  const std::string a = dbDir(ed).filename().string();
  REQUIRE(switchTo(ed, "Debug_x64|x64"));
  const std::string b = dbDir(ed).filename().string();
  CHECK(a != b);
  CHECK(a.rfind("Debug_x64-", 0) == 0);
  CHECK(b.rfind("Debug_x64_x64-", 0) == 0);
}

// ---------------------------------------------------------------------------
// Include reachability (which open headers belong to a configuration)
// ---------------------------------------------------------------------------

TEST(vs_files_reachable_from_follows_includes_and_include_directories) {
  fs::path root = freshDir("reach");
  write(root / "src/a.cpp", "");
  write(root / "src/local.h", "");
  write(root / "inc_a/cfg.h", "");
  write(root / "inc_a/deep.h", "");
  write(root / "inc_b/cfg.h", "");
  write(root / "inc_b/other.h", "");
  write(root / "forced.h", "");
  CompileCommand c;
  c.directory = root / "src";
  c.file = root / "src/a.cpp";
  c.arguments = {"clang-cl", "/I../inc_a", "/FI" + (root / "forced.h").generic_string(), "/c", c.file.generic_string()};
  std::map<std::string, std::vector<std::string>> inc = {{pathKey(root / "src/a.cpp"), {"local.h", "cfg.h"}},
                                                         {pathKey(root / "inc_a/cfg.h"), {"deep.h", "missing.h"}}};
  auto includesOf = [&](const fs::path& f) {
    auto it = inc.find(pathKey(f));
    return it == inc.end() ? std::vector<std::string>() : it->second;
  };
  std::set<std::string> targets;
  for (const char* p :
       {"src/a.cpp", "src/local.h", "inc_a/cfg.h", "inc_a/deep.h", "inc_b/cfg.h", "inc_b/other.h", "forced.h"})
    targets.insert(pathKey(root / p));
  auto got = compdb::filesReachableFrom({c}, includesOf, targets);
  CHECK(got.count(pathKey(root / "src/a.cpp")));
  CHECK(got.count(pathKey(root / "src/local.h")));   // next to the includer
  CHECK(got.count(pathKey(root / "inc_a/cfg.h")));   // through a relative -I
  CHECK(got.count(pathKey(root / "inc_a/deep.h")));  // transitively
  CHECK(got.count(pathKey(root / "forced.h")));
  CHECK(!got.count(pathKey(root / "inc_b/cfg.h")));  // the other configuration's header
  CHECK(!got.count(pathKey(root / "inc_b/other.h")));
  CHECK(compdb::filesReachableFrom({c}, includesOf, {}).empty());
  std::error_code ec;
  fs::remove_all(root, ec);
}

// ---------------------------------------------------------------------------
// With clangd
// ---------------------------------------------------------------------------

TEST(vs_clangd_refs_after_a_switch_see_only_the_active_configuration) {
  if (!clangdAvailable()) {
    std::cerr << "    (skipped: clangd not found)\n";
    return;
  }
  Solution sln("refs");
  const fs::path dbgH = sln.root / "inc_Debug/cfg.h", relH = sln.root / "inc_Release/cfg.h",
                 main = sln.root / "main.cpp";
  Editor::Options o;
  o.semantic.clangdBudgetMs = 150;
  Editor ed(sln.root, std::move(o));
  std::string err;
  REQUIRE(ed.start(&err));
  REQUIRE(ed.open(main) != nullptr);
  REQUIRE(ed.open(dbgH) != nullptr);  // an open header of the Debug configuration
  CHECK(ed.semantic().waitForFile(main, 90000));
  ed.semantic().waitForClangdIndex(60000);

  auto refsAt = [&](const fs::path& file, int line, int col) {
    std::vector<SymbolLocation> out;
    bool done = false;
    ed.gotoLocation({file, {line, col}});
    ed.findReferences([&](Confidence c, const std::vector<SymbolLocation>& l, const std::string&) {
      if (c != Confidence::Authoritative) return;
      out = l;
      done = true;
    });
    ed.waitFor([&] { return done; }, 60000);
    return out;
  };
  auto mentions = [](const std::vector<SymbolLocation>& l, const fs::path& f) {
    return std::any_of(l.begin(), l.end(), [&](const SymbolLocation& s) { return pathsEqual(s.loc.file, f); });
  };

  REQUIRE(switchTo(ed, "Release|x64"));
  REQUIRE(ed.open(main) != nullptr);
  CHECK(ed.semantic().waitForFile(main, 90000));
  ed.semantic().waitForClangdIndex(60000);
  REQUIRE(ed.open(relH) != nullptr);
  CHECK(ed.semantic().waitForFile(relH, 90000));
  auto refs = refsAt(relH, 1, 11);  // cfg_value in inc_Release/cfg.h
  CHECK(mentions(refs, relH));
  CHECK(mentions(refs, main));
  CHECK(!mentions(refs, dbgH));  // not the Debug declaration: neither from the old index nor from the open document
  // The Debug header stays open for the user; editing it hands it back to clangd (it is not lost).
  Document* dd = ed.document(dbgH);
  REQUIRE(dd != nullptr);
  ed.gotoLocation({dbgH, {1, 0}});
  ed.insertText("// edited\n");
  CHECK(ed.document(dbgH)->buffer().text().find("// edited") != std::string::npos);
}

#ifndef _WIN32
TEST(vs_clangd_resolves_the_standard_library_in_sln_mode_off_windows) {
  if (!clangdAvailable()) {
    std::cerr << "    (skipped: clangd not found)\n";
    return;
  }
  Solution sln("stdlib");
  write(sln.root / "main.cpp",
        "#include <string>\n#include <vector>\n#include \"cfg.h\"\n"
        "#ifndef _MSC_VER\n#error msvc branch lost\n#endif\n"
        "int main() { std::vector<std::string> v; v.push_back(\"a\"); return cfg_value() + static_cast<int>(v.size()); "
        "}\n");
  Editor::Options o;
  o.semantic.clangdBudgetMs = 150;
  Editor ed(sln.root, std::move(o));
  std::string err;
  REQUIRE(ed.start(&err));
  const fs::path main = sln.root / "main.cpp";
  REQUIRE(ed.open(main) != nullptr);
  CHECK(ed.semantic().waitForFile(main, 120000));
  bool published = ed.waitFor([&] { return true; }, 1500);
  (void)published;
  for (auto& d : ed.diagnostics(main)) {
    std::cerr << "    diag " << d.range.start.line + 1 << ": " << d.message << "\n";
    CHECK(d.severity != 1);  // no 'vector' file not found, no #error
  }
}
#endif
