// Visual Studio solution settings (C5): what MSBuild would hand to cl.exe, per file and per configuration.
// Fixtures: tests/fixtures/vs/{Multi,FirstOnly,WinPaths,Sample}. Evaluation is string-level and hermetic (the
// process environment is not consulted unless a test asks for it), so Windows spellings work on any host.
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <set>

#include "app/Editor.h"
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
int indexOf(const std::vector<std::string>& v, const std::string& s) {
  auto it = std::find(v.begin(), v.end(), s);
  return it == v.end() ? -1 : static_cast<int>(it - v.begin());
}
int countOf(const std::vector<std::string>& v, const std::string& s) {
  return static_cast<int>(std::count(v.begin(), v.end(), s));
}
bool hasPrefix(const std::vector<std::string>& v, const std::string& prefix) {
  return std::any_of(v.begin(), v.end(), [&](const std::string& a) { return a.rfind(prefix, 0) == 0; });
}
const CompileCommand* forFile(const std::vector<CompileCommand>& cs, const std::string& suffix) {
  for (auto& c : cs)
    if (c.file.generic_string().ends_with(suffix)) return &c;
  return nullptr;
}
std::string canon(const fs::path& p) { return fs::weakly_canonical(p).generic_string(); }

compdb::VcxprojOptions hermetic(const std::string& cfg, const std::string& plat) {
  compdb::VcxprojOptions o;
  o.configuration = cfg;
  o.platform = plat;
  o.useProcessEnvironment = false;
  o.environment = {{"ENV_TEST_ROOT", "C:\\EnvRoot"}};
  return o;
}

std::vector<CompileCommand> multi(const std::string& cfg, const std::string& plat, std::string* err = nullptr) {
  std::string e;
  auto cmds = compdb::fromSolution(kVs / "Multi/Multi.sln", hermetic(cfg, plat), &e);
  if (err) *err = e;
  return cmds;
}

const CompileCommand& need(const std::vector<CompileCommand>& cmds, const std::string& suffix) {
  static const CompileCommand empty;
  const CompileCommand* c = forFile(cmds, suffix);
  if (!c) {
    tftest::fail(__FILE__, __LINE__, "no compile command for " + suffix);
    return empty;
  }
  return *c;
}

fs::path freshDir(const std::string& name) {
  fs::path d = fs::weakly_canonical(fs::temp_directory_path()) / ("tenfold_vs_" + name);
  std::error_code ec;
  fs::remove_all(d, ec);
  fs::create_directories(d);
  return d;
}

fs::path copyFixture(const std::string& fixture, const std::string& name) {
  fs::path d = freshDir(name);
  fs::copy(kVs / fixture, d, fs::copy_options::recursive);
  return d;
}

void setEnv(const char* k, const char* v) {
#ifdef _WIN32
  _putenv_s(k, v);
#else
  setenv(k, v, 1);
#endif
}

std::string readFile(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), {});
}

}  // namespace

// ---------------------------------------------------------------------------
// Solution: configurations and the solution -> project configuration mapping
// ---------------------------------------------------------------------------

TEST(vs_solution_lists_configurations_and_mapping) {
  std::string err;
  auto info = compdb::readSolution(kVs / "Multi/Multi.sln", &err);
  CHECK_EQ(err, "");
  REQUIRE(info.configurations.size() == 4u);
  CHECK_EQ(info.configurations[0].name(), "Debug|x64");
  CHECK_EQ(info.configurations[1].name(), "Debug|x86");
  CHECK_EQ(info.configurations[2].name(), "Release|x64");
  CHECK_EQ(info.configurations[3].name(), "Release|x86");
  CHECK(info.hasProjectConfigurationMap);
  REQUIRE(info.projects.size() == 5u);
  CHECK_EQ(info.projects[0].name, "App");
  CHECK(info.projects[0].isVcxproj());
  CHECK(!info.projects[4].isVcxproj());  // Managed.csproj
  CHECK_EQ(compdb::solutionConfigurations(kVs / "Multi/Multi.sln").size(), 4u);

  const auto& app = info.projects[0];
  auto m = info.mapping(app, {"Debug", "x86"});
  REQUIRE(m.has_value());
  CHECK_EQ(m->configuration, "Debug");
  CHECK_EQ(m->platform, "Win32");  // solution "x86" is the project's "Win32"
  CHECK(m->build);
  // Tests is mapped (ActiveCfg) in Release but has no Build.0: not built there.
  auto t = info.mapping(info.projects[2], {"Release", "x64"});
  REQUIRE(t.has_value());
  CHECK(!t->build);
  CHECK(info.mapping(info.projects[2], {"Debug", "x64"})->build);
  // Tools has no entries for x86 at all.
  CHECK(!info.mapping(info.projects[3], {"Debug", "x86"}).has_value());
  // Case-insensitive lookup.
  CHECK(info.mapping(app, {"debug", "X64"}).has_value());
}

TEST(vs_solution_skips_projects_that_are_not_built) {
  auto files = [](const std::vector<CompileCommand>& cmds) {
    std::set<std::string> projects;
    for (auto& c : cmds) projects.insert(c.directory.filename().string());
    return projects;
  };
  std::string err;
  auto dx64 = multi("Debug", "x64", &err);
  CHECK_EQ(err, "");
  CHECK(files(dx64) == (std::set<std::string>{"App", "Lib", "Tests", "Tools"}));
  auto rx64 = multi("Release", "x64");
  CHECK(files(rx64) == (std::set<std::string>{"App", "Lib", "Tools"}));  // Tests: ActiveCfg without Build.0
  auto dx86 = multi("Debug", "x86");
  CHECK(files(dx86) == (std::set<std::string>{"App", "Lib", "Tests"}));  // Tools: not in the x86 configurations
  // The mapped project platform is what the project is evaluated with.
  const auto& lib = need(dx86, "Lib/lib.cpp");
  CHECK(has(lib.arguments, "-m32"));
}

TEST(vs_active_configuration_defaults) {
  using compdb::pickSolutionConfiguration;
  std::vector<compdb::SolutionConfiguration> a = {{"Release", "Win32"}, {"Debug", "x64"}, {"Debug", "Win32"}};
  bool exact = false;
  CHECK_EQ(pickSolutionConfiguration(a, "Debug", "Win32", &exact).name(), "Debug|Win32");
  CHECK(exact);
  CHECK_EQ(pickSolutionConfiguration(a, "release", "WIN32").name(), "Release|Win32");  // case-insensitive
  CHECK_EQ(pickSolutionConfiguration(a, "Debug", "x64").name(), "Debug|x64");
  CHECK_EQ(pickSolutionConfiguration(a, "Profile", "x64", &exact).name(), "Debug|x64");  // Debug|x64 when present
  CHECK(!exact);
  std::vector<compdb::SolutionConfiguration> b = {{"Release", "Win32"}, {"Debug", "Win32"}};
  CHECK_EQ(pickSolutionConfiguration(b, "Debug", "x64").name(), "Release|Win32");  // else the first one
  CHECK_EQ(pickSolutionConfiguration({}, "Debug", "x64").name(), "Debug|x64");     // no list: as requested
  CHECK_EQ(compdb::parseSolutionConfiguration("Release|Any CPU").platform, "Any CPU");
  CHECK_EQ(compdb::parseSolutionConfiguration("Release").platform, "");

  // fromSolution falls back the same way: FirstOnly has no Debug|x64, so the first entry (Release|Win32) is built.
  auto cmds = compdb::fromSolution(kVs / "FirstOnly/FirstOnly.sln", hermetic("Debug", "x64"));
  REQUIRE(cmds.size() == 1u);
  CHECK(has(cmds[0].arguments, "/DCFG_Release_Win32"));
  auto dbg = compdb::fromSolution(kVs / "FirstOnly/FirstOnly.sln", hermetic("Debug", "Win32"));
  REQUIRE(dbg.size() == 1u);
  CHECK(has(dbg[0].arguments, "/DCFG_Debug_Win32"));
  // A solution without a configuration section (hand written) uses the request as is.
  auto legacy = compdb::fromSolution(kVs / "Sample.sln", hermetic("Release", "x64"));
  CHECK_EQ(legacy.size(), 3u);
}

// ---------------------------------------------------------------------------
// Defines
// ---------------------------------------------------------------------------

TEST(vs_defines_undefines_and_implicit_defines) {
  auto cmds = multi("Debug", "x64");
  const auto& a = need(cmds, "App/src/main.cpp").arguments;
  // As written: project, property sheet (conditional import), Directory.Build.props/targets, inheritance.
  for (const char* d : {"/DAPP", "/D_CONSOLE", "/DFROM_COMMON_PROPS", "/DSHEET_DEFINE", "/DDBG64", "/DFROM_DIRPROPS",
                        "/DFROM_DIRTARGETS"})
    CHECK(has(a, d));
  // Implicit: x64 Unicode, /MDd.
  for (const char* d : {"/D_WIN32", "/D_WIN64", "/D_DEBUG", "/D_DLL", "/D_MT", "/DUNICODE", "/D_UNICODE"})
    CHECK(has(a, d));
  CHECK(!has(a, "/D_MBCS"));
  CHECK(!has(a, "/D_WINDOWS"));  // only what the project lists is added: App lists _CONSOLE
  CHECK(!has(a, "/DNDEBUG"));
  CHECK(!hasPrefix(a, "/DREG="));  // $(Registry:...) cannot be read: the define is dropped, not half-written
  // /U comes after every /D, keeps the inherited names.
  CHECK(has(a, "/UDBG64_UNDEF"));
  CHECK(has(a, "/UFROM_COMMON_PROPS_UNDEF"));
  int lastD = 0, firstU = 1 << 30;
  for (size_t i = 0; i < a.size(); ++i) {
    if (a[i].rfind("/D", 0) == 0) lastD = static_cast<int>(i);
    if (a[i].rfind("/U", 0) == 0) firstU = std::min(firstU, static_cast<int>(i));
  }
  CHECK(lastD < firstU);
  // A define written by the project and also implied is listed once.
  CHECK_EQ(countOf(compdb::fromSolution(kVs / "Sample.sln", hermetic("Debug", "x64"))[0].arguments, "/D_DEBUG"), 1);

  auto rel = multi("Release", "x64");
  const auto& r = need(rel, "App/src/main.cpp").arguments;
  CHECK(has(r, "/DNDEBUG"));
  CHECK(has(r, "/D_MT"));  // /MT: static runtime, no _DLL and no _DEBUG
  CHECK(!has(r, "/D_DLL"));
  CHECK(!has(r, "/D_DEBUG"));
  CHECK(!has(r, "/DSHEET_DEFINE"));  // DebugX64.props is not imported for Release|x64
  CHECK(!has(r, "/DDBG64"));
  CHECK(has(r, "/MT"));

  // Win32 / MultiByte / /MTd
  auto x86 = multi("Debug", "x86");
  const auto& w = need(x86, "App/src/main.cpp").arguments;
  CHECK(has(w, "/D_WIN32"));
  CHECK(!has(w, "/D_WIN64"));
  CHECK(has(w, "/D_MBCS"));
  CHECK(!has(w, "/DUNICODE"));
  CHECK(has(w, "/D_DEBUG") && has(w, "/D_MT") && !has(w, "/D_DLL"));
  CHECK(has(w, "/MTd"));

  // Opt out of the implicit ones.
  auto o = hermetic("Debug", "x64");
  o.implicitDefines = false;
  auto plain = compdb::fromSolution(kVs / "Multi/Multi.sln", o);
  const auto& p = need(plain, "App/src/main.cpp").arguments;
  CHECK(!has(p, "/D_WIN32") && !has(p, "/DUNICODE") && !has(p, "/D_DLL"));
  CHECK(has(p, "/DAPP"));
}

TEST(vs_runtime_library_defaults_from_use_debug_libraries) {
  // Lib sets no RuntimeLibrary: the toolset derives it from UseDebugLibraries.
  auto d = multi("Debug", "x64");
  const auto& a = need(d, "Lib/lib.cpp").arguments;
  CHECK(has(a, "/MDd") && has(a, "/D_DEBUG") && has(a, "/D_DLL") && has(a, "/D_MT"));
  CHECK(!hasPrefix(a, "/DUNICODE") && !has(a, "/D_MBCS"));  // CharacterSet NotSet
  auto r = multi("Release", "x64");
  const auto& b = need(r, "Lib/lib.cpp").arguments;
  CHECK(has(b, "/MD") && has(b, "/D_DLL") && !has(b, "/D_DEBUG"));
  CHECK(has(b, "/DWIN32") && has(b, "/D_WIN32"));  // both: the project's WIN32 and the implicit _WIN32
}

// ---------------------------------------------------------------------------
// Options
// ---------------------------------------------------------------------------

TEST(vs_compiler_options_per_file_and_configuration) {
  auto cmds = multi("Debug", "x64");
  const fs::path app = fs::weakly_canonical(kVs / "Multi/App");
  const fs::path multiDir = fs::weakly_canonical(kVs / "Multi");
  const auto& m = need(cmds, "App/src/main.cpp").arguments;
  CHECK_EQ(m[0], "clang-cl");
  CHECK(has(m, "/std:c++20"));
  CHECK(has(m, "/EHa"));  // ExceptionHandling=Async
  CHECK(!has(m, "/EHsc"));
  CHECK(has(m, "/permissive-"));
  CHECK(has(m, "/Zc:wchar_t-"));
  CHECK(!has(m, "/GR-"));  // RuntimeTypeInfo=true
  CHECK(has(m, "/MDd"));
  CHECK(has(m, "-m64"));
  CHECK_EQ(m[m.size() - 3], "/TP");
  CHECK_EQ(m[m.size() - 2], "/c");
  CHECK_EQ(m.back(), (app / "src/main.cpp").generic_string());
  // Include directories: project, relative, drive letter, property-sheet and Directory.Build.props ones, in order.
  const std::vector<std::string> incs = {
      "/I" + (app / "include").generic_string(),
      "/I" + (multiDir / "ext/rel").generic_string(),
      "/IC:/SDK/include",
      "/I" + (app / "probe").generic_string(),  // $(MSBuildThisFileDirectory) restored
      "/I" + (multiDir / "ext/inc").generic_string(),
      "/I" + (multiDir / "gen").generic_string(),  // [MSBuild]::EnsureTrailingSlash
      "/I" + (multiDir / "props/sheet").generic_string(),
      "/I" + (multiDir / "shared").generic_string()};
  int prev = -1;
  for (auto& i : incs) {
    int at = indexOf(m, i);
    CHECK(at > prev);
    prev = at;
  }
  // IncludePath (environment variable as property) after /I, ExternalIncludePath as system directory.
  CHECK(has(m, "/IC:/EnvRoot/envinc"));
  CHECK(indexOf(m, "/IC:/EnvRoot/envinc") > indexOf(m, "/I" + (multiDir / "shared").generic_string()));
  CHECK(has(m, "/imsvc" + (multiDir / "ext/external").generic_string()));
  CHECK(has(m, "-Wsystem-headers"));  // ExternalWarningLevel=Level2
  // Nothing unresolved leaks into an argument.
  for (auto& arg : m) {
    CHECK(arg.find("$(") == std::string::npos);
    CHECK(arg.find("%(") == std::string::npos);
    CHECK(arg.find('\x01') == std::string::npos);
    CHECK(arg.find("HKEY") == std::string::npos);
    CHECK(arg.find("bogus") == std::string::npos);
    CHECK(arg.find("VC_IncludePath") == std::string::npos);
  }
  // Precompiled header: parse with the header forced in.
  const std::string pch = (app / "src/pch.h").generic_string();
  CHECK(has(m, "/FI" + pch));
  CHECK(!hasPrefix(m, "/Yu") && !hasPrefix(m, "/Yc"));         // see ARCHITECTURE.md: /Yu would hide code from clangd
  CHECK(indexOf(m, "/FI" + pch) < indexOf(m, "/FIforced.h"));  // pch first, then ForcedIncludeFiles in order
  CHECK(has(m, "/bigobj"));
  CHECK(has(m, "/FIC:\\Win Inc\\opt.h"));  // AdditionalOptions: backslashes are literal, quotes group

  // pch.cpp creates the header; sources outside src/ find it through the include directories.
  const auto& pc = need(cmds, "App/src/pch.cpp").arguments;
  CHECK(has(pc, "/FI" + pch));
  const auto& gen = need(cmds, "App/gen/a.cpp").arguments;
  CHECK(has(gen, "/FI" + (app / "include/pch.h").generic_string()));
  CHECK(forFile(cmds, "App/gen/b.cpp") != nullptr);
  CHECK(forFile(cmds, "App/gen/skip.txt") == nullptr);  // wildcard matched only .cpp
  CHECK(forFile(cmds, "App/src/pch.h") == nullptr);     // ClInclude is not compiled

  // Per-file metadata overrides the definition group and keeps the inherited values.
  const auto& sp = need(cmds, "App/src/special.cpp").arguments;
  CHECK(has(sp, "/std:c++14"));
  CHECK(!has(sp, "/std:c++20"));
  CHECK(has(sp, "/DSPECIAL"));
  CHECK(!has(sp, "/DSPECIAL_RELEASE"));  // conditional metadata for Release only
  CHECK(has(sp, "/DAPP"));               // %(PreprocessorDefinitions) of the group
  CHECK(has(sp, "/GR-"));
  CHECK(!hasPrefix(sp, "/FI" + (app / "src/pch.h").generic_string()));  // PrecompiledHeader NotUsing
  CHECK(!has(m, "/DSPECIAL"));

  // CompileAs, and C files.
  const auto& c = need(cmds, "App/src/plain.c").arguments;
  CHECK(has(c, "/TC"));
  CHECK(has(c, "/std:c17"));  // LanguageStandard_C
  CHECK(!has(c, "/std:c++20") && !has(c, "/EHa") && !has(c, "/GR-") && !has(c, "/Zc:wchar_t-"));
  CHECK(has(c, "/DPLAIN_C"));
  CHECK(has(c, "/permissive-"));
  const auto& ca = need(cmds, "App/src/c_as_cpp.c").arguments;  // .c compiled as C++
  CHECK(has(ca, "/TP") && has(ca, "/std:c++20"));
  const auto& ac = need(cmds, "App/src/cpp_as_c.cpp").arguments;  // .cpp compiled as C
  CHECK(has(ac, "/TC") && has(ac, "/std:c17") && !has(ac, "/TP"));

  // ExcludedFromBuild per file and per configuration; items with their own Condition.
  CHECK(forFile(cmds, "App/src/legacy.cpp") == nullptr);        // excluded in Debug|x64
  CHECK(forFile(cmds, "App/src/release_only.cpp") == nullptr);  // excluded in Debug
  CHECK(forFile(cmds, "App/src/x64_only.cpp") != nullptr);
  auto rel = multi("Release", "x64");
  CHECK(forFile(rel, "App/src/legacy.cpp") != nullptr);
  CHECK(forFile(rel, "App/src/release_only.cpp") != nullptr);
  const auto& rm = need(rel, "App/src/main.cpp").arguments;
  CHECK(has(rm, "/EHs"));  // SyncCThrow
  CHECK(has(rm, "/MT"));
  CHECK(has(need(rel, "App/src/special.cpp").arguments, "/DSPECIAL_RELEASE"));
  auto x86 = multi("Debug", "x86");
  CHECK(forFile(x86, "App/src/legacy.cpp") != nullptr);  // only Debug|x64 excludes it
  CHECK(forFile(x86, "App/src/x64_only.cpp") == nullptr);
  const auto& xm = need(x86, "App/src/main.cpp").arguments;
  CHECK(!hasPrefix(xm, "/EH"));  // ExceptionHandling=false
  CHECK(has(xm, "/GR-"));
  CHECK(has(xm, "-m32"));
}

TEST(vs_language_standards) {
  // Every LanguageStandard value maps to the matching clang-cl switch.
  struct Row {
    const char* meta;
    const char* flag;
  };
  const fs::path dir = freshDir("standards");
  const Row rows[] = {{"stdcpp14", "/std:c++14"},     {"stdcpp17", "/std:c++17"},         {"stdcpp20", "/std:c++20"},
                      {"stdcpp23", "/std:c++latest"}, {"stdcpplatest", "/std:c++latest"}, {"Default", "/std:c++17"}};
  for (auto& r : rows) {
    std::ofstream(dir / "p.vcxproj") << "<Project><ItemDefinitionGroup><ClCompile><LanguageStandard>" << r.meta
                                     << "</LanguageStandard></ClCompile></ItemDefinitionGroup>"
                                     << "<ItemGroup><ClCompile Include=\"a.cpp\"/><ClCompile Include=\"b.c\"/>"
                                     << "</ItemGroup></Project>";
    auto cmds = compdb::fromVcxproj(dir / "p.vcxproj", hermetic("Debug", "x64"));
    REQUIRE(cmds.size() == 2u);
    CHECK(has(cmds[0].arguments, r.flag));
    CHECK(!hasPrefix(cmds[1].arguments, "/std:c++"));
  }
  const char* cstd[][2] = {{"stdc11", "/std:c11"}, {"stdc17", "/std:c17"}, {"stdclatest", "/std:clatest"}};
  for (auto& r : cstd) {
    std::ofstream(dir / "p.vcxproj") << "<Project><ItemDefinitionGroup><ClCompile><LanguageStandard_C>" << r[0]
                                     << "</LanguageStandard_C></ClCompile></ItemDefinitionGroup>"
                                     << "<ItemGroup><ClCompile Include=\"b.c\"/></ItemGroup></Project>";
    auto cmds = compdb::fromVcxproj(dir / "p.vcxproj", hermetic("Debug", "x64"));
    REQUIRE(cmds.size() == 1u);
    CHECK(has(cmds[0].arguments, r[1]));
  }
  fs::remove_all(dir);
}

// ---------------------------------------------------------------------------
// Property sheets, inheritance, environment
// ---------------------------------------------------------------------------

TEST(vs_directory_build_props_and_targets) {
  auto cmds = multi("Debug", "x64");
  const fs::path multiDir = fs::weakly_canonical(kVs / "Multi");
  // Tools/Directory.Build.props (nearest) imports the parent one through GetPathOfFileAbove.
  const auto& t = need(cmds, "Tools/tool.c").arguments;
  CHECK(has(t, "/DFROM_TOOLS_DIRPROPS"));
  CHECK(has(t, "/DFROM_DIRPROPS"));
  CHECK(has(t, "/DFROM_DIRTARGETS"));
  CHECK(has(t, "/I" + (multiDir / "shared").generic_string()));
  CHECK(has(t, "/DTOOLS") && has(t, "/D_WINDOWS"));  // listed by the project, so present
  CHECK(has(t, "/D_MBCS"));
  CHECK(has(t, "/std:c11") && has(t, "/TC"));
  // A project below Multi/ without its own file walks up to Multi/Directory.Build.props.
  CHECK(has(need(cmds, "Tests/tests.cpp").arguments, "/DFROM_DIRPROPS"));
  CHECK(!has(need(cmds, "Tests/tests.cpp").arguments, "/DFROM_TOOLS_DIRPROPS"));
  // Order follows the evaluation: what a later "X;%(Y)" adds comes before what it inherits.
  const auto& a = need(cmds, "App/src/main.cpp").arguments;
  CHECK(indexOf(a, "/DAPP") < indexOf(a, "/DFROM_COMMON_PROPS"));
  CHECK(indexOf(a, "/DFROM_COMMON_PROPS") < indexOf(a, "/DFROM_DIRPROPS"));
  // Opt out.
  auto o = hermetic("Debug", "x64");
  o.directoryBuildFiles = false;
  auto off = compdb::fromSolution(kVs / "Multi/Multi.sln", o);
  CHECK(!has(need(off, "Tools/tool.c").arguments, "/DFROM_DIRPROPS"));
  CHECK(!has(need(off, "Tools/tool.c").arguments, "/DFROM_DIRTARGETS"));
  // Not found anywhere: a project outside any Directory.Build.props.
  auto sample = compdb::fromVcxproj(kVs / "Opts/Opts.vcxproj", hermetic("Debug", "x64"));
  REQUIRE(sample.size() == 1u);
  CHECK(!has(sample[0].arguments, "/DFROM_DIRPROPS"));
}

TEST(vs_environment_variables_are_properties) {
  const fs::path multiDir = fs::weakly_canonical(kVs / "Multi");
  // Injected environment.
  auto cmds = multi("Debug", "x64");
  CHECK(has(need(cmds, "App/src/main.cpp").arguments, "/IC:/EnvRoot/envinc"));
  // Not set: the entry is dropped rather than turned into "/envinc".
  auto o = hermetic("Debug", "x64");
  o.environment.clear();
  auto none = compdb::fromSolution(kVs / "Multi/Multi.sln", o);
  for (auto& a : need(none, "App/src/main.cpp").arguments) CHECK(a.find("envinc") == std::string::npos);
  // The real process environment, when allowed.
  setEnv("ENV_TEST_ROOT", "/from/process");
  o.useProcessEnvironment = true;
  auto proc = compdb::fromSolution(kVs / "Multi/Multi.sln", o);
  CHECK(has(need(proc, "App/src/main.cpp").arguments, "/I/from/process/envinc"));
  o.environment = {{"ENV_TEST_ROOT", "D:\\Injected"}};  // injected wins
  auto inj = compdb::fromSolution(kVs / "Multi/Multi.sln", o);
  CHECK(has(need(inj, "App/src/main.cpp").arguments, "/ID:/Injected/envinc"));
  setEnv("ENV_TEST_ROOT", "");
  (void)multiDir;
}

TEST(vs_property_expansion_functions_and_unresolvable_macros) {
  std::map<std::string, std::string> p{{"Configuration", "Debug"}, {"MyDir", "C:\\x\\y"}, {"Empty", ""}};
  CHECK_EQ(compdb::expandProperties("$(configuration)", p), "Debug");  // names are case-insensitive
  CHECK_EQ(compdb::expandProperties("$([MSBuild]::EnsureTrailingSlash('$(MyDir)'))inc", p), "C:\\x\\y/inc");
  CHECK_EQ(compdb::expandProperties("$([MSBuild]::EnsureTrailingSlash('a/'))", p), "a/");
  CHECK_EQ(compdb::expandProperties("a$(Registry:HKEY_LOCAL_MACHINE\\SOFTWARE\\X@Y)b", p), "ab");
  CHECK_EQ(compdb::expandProperties("a$(Configuration.Replace('D','d'))b", p), "ab");
  CHECK_EQ(compdb::expandProperties("a$(Unknown)b$(Empty)c", p), "abc");
  CHECK_EQ(compdb::expandProperties("$([System.IO.Path]::Combine('$(MyDir)', 'z'))", p), "C:\\x\\y/z");
  CHECK(compdb::evaluateCondition("'$(Unknown)' == ''", p, "."));
  CHECK(compdb::evaluateCondition("'$(CONFIGURATION)' == 'debug'", p, "."));
  CHECK(compdb::evaluateCondition("Exists('$(MyDir)')", {{"MyDir", TENFOLD_SOURCE_DIR}}, "."));
  CHECK(!compdb::evaluateCondition("Exists('$(Unknown)')", p, "."));

  // In a project: a macro that cannot be resolved removes its entry only.
  const fs::path dir = freshDir("macros");
  std::ofstream(dir / "p.vcxproj")
      << "<Project><PropertyGroup><Mine>$(Nope)\\inc</Mine></PropertyGroup>"
         "<ItemDefinitionGroup><ClCompile><AdditionalIncludeDirectories>"
         "$(VC_IncludePath);$(WindowsSDK_IncludePath);$(Mine);$(VCToolsInstallDir)include;$(ProjectDir)ok;"
         "%(RootDir)%(Directory)gen;%(AdditionalIncludeDirectories)"
         "</AdditionalIncludeDirectories><PreprocessorDefinitions>A=$(Nope);B;%(PreprocessorDefinitions)"
         "</PreprocessorDefinitions><AdditionalOptions>/x $(Nope)y /z</AdditionalOptions></ClCompile>"
         "</ItemDefinitionGroup><ItemGroup><ClCompile Include=\"$(Nope)a.cpp\"/><ClCompile Include=\"b.cpp\"/>"
         "</ItemGroup></Project>";
  auto cmds = compdb::fromVcxproj(dir / "p.vcxproj", hermetic("Debug", "x64"));
  REQUIRE(cmds.size() == 1u);  // the first item's file name is unresolvable: dropped
  const auto& a = cmds[0].arguments;
  CHECK(has(a, "/I" + canon(dir) + "/ok"));
  CHECK_EQ(std::count_if(a.begin(), a.end(), [](const std::string& s) { return s.rfind("/I", 0) == 0; }), 1);
  CHECK(has(a, "/DB"));
  CHECK(!hasPrefix(a, "/DA="));
  CHECK(has(a, "/x") && has(a, "/z"));
  CHECK(!hasPrefix(a, "/y"));
  fs::remove_all(dir);
}

TEST(vs_choose_when_otherwise_and_import_conditions) {
  const fs::path dir = freshDir("choose");
  std::ofstream(dir / "p.vcxproj")
      << "<Project><PropertyGroup><Mode>b</Mode></PropertyGroup>"
         "<Choose><When Condition=\"'$(Mode)'=='a'\"><PropertyGroup><V>A</V></PropertyGroup></When>"
         "<When Condition=\"'$(Mode)'=='b'\"><PropertyGroup><V>B</V></PropertyGroup></When>"
         "<Otherwise><PropertyGroup><V>O</V></PropertyGroup></Otherwise></Choose>"
         "<Import Project=\"missing.props\" Condition=\"Exists('missing.props')\"/>"
         "<Import Project=\"ext_$(V).props\" Condition=\"Exists('ext_$(V).props')\"/>"
         "<ItemDefinitionGroup><ClCompile><PreprocessorDefinitions>V_$(V);%(PreprocessorDefinitions)"
         "</PreprocessorDefinitions></ClCompile></ItemDefinitionGroup>"
         "<PropertyGroup><V>late</V></PropertyGroup>"  // item definitions see the final value
         "<ItemGroup><ClCompile Include=\"a.cpp\"/></ItemGroup></Project>";
  std::ofstream(dir / "ext_B.props") << "<Project><ItemDefinitionGroup><ClCompile><PreprocessorDefinitions>"
                                        "FROM_EXT;%(PreprocessorDefinitions)</PreprocessorDefinitions>"
                                        "</ClCompile></ItemDefinitionGroup></Project>";
  auto cmds = compdb::fromVcxproj(dir / "p.vcxproj", hermetic("Debug", "x64"));
  REQUIRE(cmds.size() == 1u);
  CHECK(has(cmds[0].arguments, "/DV_late"));    // MSBuild evaluates properties before item definitions
  CHECK(has(cmds[0].arguments, "/DFROM_EXT"));  // the import was chosen while V was still "B"
  fs::remove_all(dir);
}

// ---------------------------------------------------------------------------
// Windows spellings, string level
// ---------------------------------------------------------------------------

TEST(vs_windows_paths_are_handled_as_strings) {
  std::string err;
  auto cmds = compdb::fromVcxproj(kVs / "WinPaths/WinPaths.vcxproj", hermetic("Debug", "x64"), &err);
  CHECK_EQ(err, "");
  REQUIRE(cmds.size() == 3u);
  const std::string proj = canon(kVs / "WinPaths");
  const std::string vs = canon(kVs);
  // Sources: a drive-letter path stays as written (not joined to the project dir), relative ones are normalised.
  CHECK_EQ(cmds[0].file.generic_string(), "C:/src/abs.cpp");
  CHECK_EQ(cmds[1].file.generic_string(), proj + "/src/rel.cpp");
  CHECK_EQ(cmds[2].file.generic_string(), vs + "/shared/outside.cpp");
  const auto& a = cmds[1].arguments;
  CHECK(has(a, "/IC:/SDK/include"));
  CHECK(has(a, "/ID:/inc"));  // D:\other\..\inc
  CHECK(has(a, "/I//server/share/inc"));
  CHECK(has(a, "/I" + vs + "/rel/inc"));
  CHECK(has(a, "/I" + proj + "/here"));
  CHECK(has(a, "/IC:/Program Files (x86)/Sdk/include"));  // $(SdkRoot) with a trailing backslash
  CHECK(has(a, "/FIC:/fi/forced.h"));
  CHECK(has(a, "/FIrel/fi.h"));
  CHECK(has(a, "/FIC:\\Program Files\\x\\y.h"));  // AdditionalOptions keep their backslashes
  CHECK(has(a, "/I") && has(a, "C:\\a\\b"));
  CHECK(has(a, "/DNAME=a b"));
  CHECK_EQ(a.back(), cmds[1].file.generic_string());
}

TEST(vs_solution_file_is_read_with_crlf_bom_and_backslashes) {
  const fs::path dir = freshDir("sln");
  std::ofstream f(dir / "S.sln", std::ios::binary);
  f << "\xEF\xBB\xBF\r\nMicrosoft Visual Studio Solution File, Format Version 12.00\r\n"
       "Project(\"{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}\") = \"A\", \"sub\\dir\\..\\A.vcxproj\", "
       "\"{abcdef00-0000-0000-0000-000000000001}\"\r\nEndProject\r\n"
       "Global\r\n\tGlobalSection(SolutionConfigurationPlatforms) = preSolution\r\n"
       "\t\tDebug|Any CPU = Debug|Any CPU\r\n\tEndGlobalSection\r\n"
       "\tGlobalSection(ProjectConfigurationPlatforms) = postSolution\r\n"
       "\t\t{ABCDEF00-0000-0000-0000-000000000001}.Debug|Any CPU.ActiveCfg = Debug|x64\r\n"
       "\t\t{ABCDEF00-0000-0000-0000-000000000001}.Debug|Any CPU.Build.0 = Debug|x64\r\n"
       "\t\t{ABCDEF00-0000-0000-0000-000000000001}.Debug|Any CPU.Deploy.0 = Debug|x64\r\n"
       "\tEndGlobalSection\r\nEndGlobal\r\n";
  f.close();
  auto info = compdb::readSolution(dir / "S.sln");
  REQUIRE(info.projects.size() == 1u);
  CHECK_EQ(info.projects[0].guid, "{ABCDEF00-0000-0000-0000-000000000001}");
  CHECK_EQ(info.projects[0].path.generic_string(), (dir / "sub/A.vcxproj").generic_string());
  REQUIRE(info.configurations.size() == 1u);
  CHECK_EQ(info.configurations[0].platform, "Any CPU");
  auto m = info.mapping(info.projects[0], info.configurations[0]);
  REQUIRE(m.has_value());
  CHECK(m->build);
  CHECK_EQ(m->platform, "x64");
  fs::remove_all(dir);
}

// ---------------------------------------------------------------------------
// Workspace / Editor configuration API
// ---------------------------------------------------------------------------

TEST(vs_workspace_lists_and_switches_configurations) {
  fs::path root = copyFixture("Multi", "ws_api");
  {
    Workspace ws(root);
    auto list = ws.vsConfigurations();
    REQUIRE(list.size() == 4u);
    CHECK_EQ(ws.activeVsConfiguration().name(), "Debug|x64");  // default setting, listed by the solution
    std::string err;
    CHECK(!ws.setVsConfiguration("Bogus|x64", &err));
    CHECK(err.find("Debug|x64") != std::string::npos && err.find("Release|x86") != std::string::npos);
    CHECK_EQ(ws.settings().vsConfiguration, "Debug");   // unchanged
    CHECK(ws.setVsConfiguration("release|X86", &err));  // case-insensitive, stored as the solution spells it
    CHECK_EQ(ws.settings().vsConfiguration, "Release");
    CHECK_EQ(ws.settings().vsPlatform, "x86");
    CHECK(ws.setVsConfiguration("Release", &err));  // bare name: that configuration's first platform
    CHECK_EQ(ws.activeVsConfiguration().name(), "Release|x64");
  }
  {
    Workspace ws(root);  // persisted in .tenfold/workspace.json
    CHECK(ws.loadSettings());
    CHECK_EQ(ws.activeVsConfiguration().name(), "Release|x64");
  }
  // Backward compatibility: a workspace.json with only the old two keys.
  std::ofstream(root / ".tenfold/workspace.json") << R"({"vsConfiguration": "Debug", "vsPlatform": "x86"})";
  {
    Workspace ws(root);
    CHECK(ws.loadSettings());
    CHECK_EQ(ws.activeVsConfiguration().name(), "Debug|x86");
  }
  // A setting the solution does not list falls back to Debug|x64, else the first configuration.
  std::ofstream(root / ".tenfold/workspace.json") << R"({"vsConfiguration": "Profile", "vsPlatform": "ARM64"})";
  {
    Workspace ws(root);
    CHECK(ws.loadSettings());
    CHECK_EQ(ws.activeVsConfiguration().name(), "Debug|x64");
  }
  fs::remove_all(root);
  root = copyFixture("FirstOnly", "ws_first");
  {
    Workspace ws(root);
    CHECK_EQ(ws.activeVsConfiguration().name(), "Release|Win32");
  }
  fs::remove_all(root);
}

TEST(vs_editor_switches_configuration_end_to_end) {
  fs::path root = copyFixture("Multi", "editor_switch");
  Editor::Options o;
  o.semantic.useClangd = false;
  std::mutex mu;
  std::vector<std::string> log;
  o.log = [&](const std::string& m) {
    std::lock_guard<std::mutex> lk(mu);
    log.push_back(m);
  };
  {
    Editor ed(root, o);
    std::string err;
    REQUIRE(ed.start(&err));
    CHECK_EQ(ed.activeVsConfiguration().name(), "Debug|x64");
    CHECK_EQ(ed.vsConfigurations().size(), 4u);
    auto st = ed.semantic().status();
    CHECK_EQ(st.compdbSource, "vcxproj");
    auto before = ed.semantic().compileCommands();
    const auto& b = need(*before, "App/src/main.cpp").arguments;
    CHECK(has(b, "/DSHEET_DEFINE") && has(b, "/MDd") && has(b, "-m64"));
    CHECK(forFile(*before, "Tests/tests.cpp") != nullptr);

    CHECK(!ed.selectVsConfiguration("Nope|x64", &err));
    CHECK(!err.empty());
    CHECK_EQ(ed.semantic().compileCommands().get(), before.get());  // an unknown name changes nothing

    bool applied = false;
    CHECK(ed.selectVsConfiguration("Release|x64", &err, [&] { applied = true; }));
    CHECK(ed.waitFor([&] { return applied; }, 30000));
    auto after = ed.semantic().compileCommands();
    CHECK(after.get() != before.get());
    const auto& r = need(*after, "App/src/main.cpp").arguments;
    CHECK(has(r, "/DNDEBUG") && has(r, "/MT"));
    CHECK(!has(r, "/DSHEET_DEFINE"));
    CHECK(forFile(*after, "Tests/tests.cpp") == nullptr);  // not built in Release
    CHECK_EQ(ed.activeVsConfiguration().name(), "Release|x64");
    CHECK(readFile(root / ".tenfold/workspace.json").find("\"Release\"") != std::string::npos);

    // x86 maps the projects to Win32 and drops Tools.
    applied = false;
    CHECK(ed.selectVsConfiguration("Debug|x86", &err, [&] { applied = true; }));
    CHECK(ed.waitFor([&] { return applied; }, 30000));
    auto x86 = ed.semantic().compileCommands();
    CHECK(has(need(*x86, "Lib/lib.cpp").arguments, "-m32"));
    CHECK(forFile(*x86, "Tools/tool.c") == nullptr);
  }
  // A new session reads the persisted choice; a hand-edited workspace.json invalidates the cached database.
  {
    Editor ed(root, o);
    std::string err;
    REQUIRE(ed.start(&err));
    CHECK_EQ(ed.activeVsConfiguration().name(), "Debug|x86");
    CHECK(has(need(*ed.semantic().compileCommands(), "Lib/lib.cpp").arguments, "-m32"));
  }
  std::ofstream(root / ".tenfold/workspace.json") << R"({"vsConfiguration": "Release", "vsPlatform": "x64"})";
  {
    Editor ed(root, o);
    std::string err;
    REQUIRE(ed.start(&err));
    auto cmds = ed.semantic().compileCommands();
    CHECK(has(need(*cmds, "App/src/main.cpp").arguments, "/DNDEBUG"));
    CHECK_EQ(ed.semantic().status().compdbSource, "vcxproj");
  }
  fs::remove_all(root);
}

TEST(vs_clangd_sees_define_from_property_sheet_for_the_active_configuration) {
  if (findExecutable("clangd").empty()) {
    std::cerr << "    (skipped: clangd not found)\n";
    return;
  }
  fs::path root = copyFixture("Multi", "clangd_switch");
  {
    // The fixture forces a Windows-only header through AdditionalOptions; a missing forced include is a fatal error
    // that would stop clangd before it reaches the #error under test.
    std::string text = readFile(root / "App/App.vcxproj");
    const std::string forced = "/FI\"C:\\Win Inc\\opt.h\" ";
    size_t at = text.find(forced);
    REQUIRE(at != std::string::npos);
    text.erase(at, forced.size());
    std::ofstream(root / "App/App.vcxproj", std::ios::binary) << text;
  }
  Editor::Options o;
  o.semantic.useClangd = true;
  o.semantic.clangdBudgetMs = 150;
  o.log = [](const std::string& m) {
    if (std::getenv("TENFOLD_TEST_VERBOSE")) std::cerr << "    [engine] " << m.substr(0, 400) << "\n";
  };
  Editor ed(root, o);
  std::string err;
  REQUIRE(ed.start(&err));
  const fs::path main = root / "App/src/main.cpp";
  REQUIRE(ed.open(main, &err) != nullptr);
  auto errorSeen = [&] {
    for (auto& d : ed.diagnostics(main))
      if (d.message.find("SHEET_DEFINE_MISSING") != std::string::npos) return true;
    return false;
  };
  // Debug|x64: DebugX64.props defines SHEET_DEFINE, so the #ifndef branch with the #error is inactive.
  CHECK(ed.semantic().waitForFile(main, 90000));
  ed.waitFor([] { return false; }, 1500);  // let diagnostics arrive
  CHECK(!errorSeen());

  // Release|x64 does not import the sheet: the #error branch becomes active.
  bool applied = false;
  CHECK(ed.selectVsConfiguration("Release|x64", &err, [&] { applied = true; }));
  CHECK(ed.waitFor([&] { return applied; }, 60000));
  const bool sawError = ed.waitFor(errorSeen, 90000);
  CHECK(sawError);
  if (!sawError) {
    for (auto& d : ed.diagnostics(main))
      std::cerr << "    diag " << d.range.start.line + 1 << ": " << d.message << "\n";
    for (auto& a : need(*ed.semantic().compileCommands(), "App/src/main.cpp").arguments)
      std::cerr << "    " << a << "\n";
  }

  // And back.
  applied = false;
  CHECK(ed.selectVsConfiguration("Debug|x64", &err, [&] { applied = true; }));
  CHECK(ed.waitFor([&] { return applied; }, 60000));
  CHECK(ed.waitFor([&] { return !errorSeen(); }, 90000));
  ed.shutdown();  // clangd must be gone before its workspace (and index cache) is removed
  std::error_code ec;
  fs::remove_all(root, ec);  // best effort: never throw from test cleanup
}

// ---------------------------------------------------------------------------
// Benchmark: a generated 50-project solution
// ---------------------------------------------------------------------------

TEST(vs_benchmark_fifty_project_solution) {
  const fs::path dir = freshDir("bench50");
  const int projects = 50, filesPerProject = 20;
  {
    std::ofstream(dir / "Directory.Build.props")
        << "<Project><ItemDefinitionGroup><ClCompile>"
           "<PreprocessorDefinitions>ALL;%(PreprocessorDefinitions)"
           "</PreprocessorDefinitions></ClCompile></ItemDefinitionGroup></Project>";
    std::ofstream(dir / "common.props")
        << "<Project><PropertyGroup><Common>$(MSBuildThisFileDirectory)common</Common>"
           "</PropertyGroup><ItemDefinitionGroup><ClCompile>"
           "<AdditionalIncludeDirectories>$(Common);%(AdditionalIncludeDirectories)"
           "</AdditionalIncludeDirectories></ClCompile></ItemDefinitionGroup></Project>";
    std::ofstream sln(dir / "Bench.sln");
    std::string cfg, map;
    sln << "Microsoft Visual Studio Solution File, Format Version 12.00\n";
    for (int i = 0; i < projects; ++i) {
      std::string n = "P" + std::to_string(i), g = "{00000000-0000-0000-0000-" +
                                                   std::string(12 - std::to_string(i).size(), '0') + std::to_string(i) +
                                                   "}";
      sln << "Project(\"{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}\") = \"" << n << "\", \"" << n << "\\" << n
          << ".vcxproj\", \"" << g << "\"\nEndProject\n";
      for (const char* c : {"Debug|x64", "Release|x64"})
        map += "\t\t" + g + "." + c + ".ActiveCfg = " + c + "\n\t\t" + g + "." + c + ".Build.0 = " + c + "\n";
      fs::create_directories(dir / n);
      std::ofstream p(dir / n / (n + ".vcxproj"));
      p << "<Project>\n";
      for (const char* c : {"Debug", "Release"})
        p << "<PropertyGroup Condition=\"'$(Configuration)'=='" << c << "'\"><UseDebugLibraries>"
          << (std::string(c) == "Debug" ? "true" : "false") << "</UseDebugLibraries></PropertyGroup>\n";
      p << "<Import Project=\"..\\common.props\"/>\n<ItemDefinitionGroup><ClCompile>"
           "<PreprocessorDefinitions>"
        << n
        << ";%(PreprocessorDefinitions)</PreprocessorDefinitions><AdditionalIncludeDirectories>$(ProjectDir)include;"
           "$(SolutionDir)lib"
        << i
        << ";%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>"
           "<LanguageStandard>stdcpp20</LanguageStandard></ClCompile></ItemDefinitionGroup>\n<ItemGroup>\n";
      for (int f = 0; f < filesPerProject; ++f) {
        p << "<ClCompile Include=\"src\\f" << f << ".cpp\">"
          << (f % 5 == 0 ? "<PreprocessorDefinitions>FILE_" + std::to_string(f) +
                               ";%(PreprocessorDefinitions)</PreprocessorDefinitions>"
                         : "")
          << "</ClCompile>\n";
      }
      p << "</ItemGroup></Project>\n";
    }
    sln << "Global\n\tGlobalSection(SolutionConfigurationPlatforms) = preSolution\n\t\tDebug|x64 = Debug|x64\n"
           "\t\tRelease|x64 = Release|x64\n\tEndGlobalSection\n\tGlobalSection(ProjectConfigurationPlatforms) = "
           "postSolution\n"
        << map << "\tEndGlobalSection\nEndGlobal\n";
  }
  std::vector<double> ms;
  size_t count = 0;
  for (int run = 0; run < 5; ++run) {
    auto t0 = std::chrono::steady_clock::now();
    auto cmds = compdb::fromSolution(dir / "Bench.sln", hermetic("Debug", "x64"));
    ms.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    count = cmds.size();
  }
  std::sort(ms.begin(), ms.end());
  std::cerr << "    bench: " << projects << " projects / " << count << " files evaluated in median " << ms[2]
            << " ms (min " << ms[0] << ", max " << ms[4] << ")\n";
  CHECK_EQ(count, static_cast<size_t>(projects * filesPerProject));
  CHECK(ms[2] < 5000);
  fs::remove_all(dir);
}
