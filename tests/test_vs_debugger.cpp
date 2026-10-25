#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>

#include "debug/VsDteUtil.h"
#include "test_main.h"

#ifdef _WIN32
#include <chrono>
#include <thread>

#include "debug/ComDispatch_win32.h"
#include "debug/VsDebugger_win32.h"
#endif

using namespace tf;

// ---- pure helpers: run everywhere ----

TEST(vsdte_moniker_parse) {
  auto m = parseDteMoniker("!VisualStudio.DTE.17.0:12345");
  REQUIRE(m.has_value());
  CHECK_EQ(m->versionText, std::string("17.0"));
  CHECK_EQ(m->pid, 12345u);
  CHECK((m->version == std::vector<int>{17, 0}));
  CHECK(parseDteMoniker("!visualstudio.dte.16.0:7").has_value());
  CHECK(parseDteMoniker("!VisualStudio.DTE.8.0:1").has_value());
  CHECK(!parseDteMoniker("!VisualStudio.DTE.17.0").has_value());
  CHECK(!parseDteMoniker("!VisualStudio.DTE.17.0:").has_value());
  CHECK(!parseDteMoniker("!VisualStudio.DTE.17.0:abc").has_value());
  CHECK(!parseDteMoniker("!VisualStudio.DTE.17.0:0").has_value());
  CHECK(!parseDteMoniker("!VisualStudio.DTE.:5").has_value());
  CHECK(!parseDteMoniker("!VisualStudio.DTE.x.y:5").has_value());
  CHECK(!parseDteMoniker("!VisualStudio.Solution.17.0:5").has_value());
  CHECK(!parseDteMoniker("!VisualStudio.DTE.17.0:99999999999").has_value());
  CHECK(!parseDteMoniker("").has_value());
  CHECK(!parseDteMoniker("!VisualStudio.DTE.").has_value());
}

TEST(vsdte_version_ordering) {
  std::vector<DteMoniker> v;
  for (const char* s : {"!VisualStudio.DTE.16.0:100", "!VisualStudio.DTE.17.0:5", "!VisualStudio.DTE.9.0:900",
                        "!VisualStudio.DTE.17.0:50", "!VisualStudio.DTE.10.0:1"}) {
    auto m = parseDteMoniker(s);
    REQUIRE(m.has_value());
    v.push_back(*m);
  }
  sortDteNewestFirst(v);
  CHECK_EQ(v[0].pid, 50u);  // 17.0, higher pid first
  CHECK_EQ(v[1].pid, 5u);
  CHECK_EQ(v[2].pid, 100u);  // 16.0
  CHECK_EQ(v[3].pid, 1u);    // 10.0 before 9.0: numeric, not textual
  CHECK_EQ(v[4].pid, 900u);
  CHECK(dteNewer(*parseDteMoniker("!VisualStudio.DTE.17.10:1"), *parseDteMoniker("!VisualStudio.DTE.17.9:1")));
  CHECK(!dteNewer(*parseDteMoniker("!VisualStudio.DTE.17.0:1"), *parseDteMoniker("!VisualStudio.DTE.17.0:1")));
}

TEST(vsdte_solution_path_normalisation) {
  CHECK_EQ(normalizeSolutionPath("C:/Work/App/App.sln"), std::string("c:\\work\\app\\app.sln"));
  CHECK_EQ(normalizeSolutionPath("C:\\Work\\\\App\\.\\Sub\\..\\App.SLN"), std::string("c:\\work\\app\\app.sln"));
  CHECK_EQ(normalizeSolutionPath("\\\\?\\C:\\Work\\App.sln"), std::string("c:\\work\\app.sln"));
  CHECK_EQ(normalizeSolutionPath("\\\\?\\UNC\\Srv\\Share\\a.sln"), std::string("\\\\srv\\share\\a.sln"));
  CHECK_EQ(normalizeSolutionPath("C:\\Work\\App\\"), std::string("c:\\work\\app"));
  CHECK_EQ(normalizeSolutionPath("C:\\..\\x.sln"), std::string("c:\\x.sln"));
  CHECK(solutionPathsEqual("c:\\work\\a.sln", "C:/WORK/./a.sln"));
  CHECK(!solutionPathsEqual("c:\\work\\a.sln", "c:\\work\\b.sln"));
  CHECK(!solutionPathsEqual("", ""));  // an empty name never matches
  CHECK(!solutionPathsEqual("c:\\work\\a.sln", ""));
}

TEST(vsdte_line_conversion) {
  CHECK_EQ(toVsLine(0), 1);
  CHECK_EQ(toVsLine(41), 42);
  CHECK_EQ(fromVsLine(1), 0);
  CHECK_EQ(fromVsLine(42), 41);
  CHECK_EQ(fromVsLine(0), 0);  // never negative
  CHECK_EQ(fromVsLine(-3), 0);
}

TEST(vsdte_mode_mapping) {
  CHECK(modeFromDte(1) == DebugMode::Design);
  CHECK(modeFromDte(2) == DebugMode::Break);
  CHECK(modeFromDte(3) == DebugMode::Running);
  CHECK(modeFromDte(0) == DebugMode::Design);
  CHECK(modeFromDte(99) == DebugMode::Design);
}

TEST(vsdte_hresult_classes) {
  CHECK(isServerBusyHr(static_cast<long>(0x80010001u)));  // RPC_E_CALL_REJECTED
  CHECK(isServerBusyHr(static_cast<long>(0x8001010Au)));  // RPC_E_SERVERCALL_RETRYLATER
  CHECK(!isServerBusyHr(0));
  CHECK(isServerDeadHr(static_cast<long>(0x80010108u)));  // RPC_E_DISCONNECTED
  CHECK(isServerDeadHr(static_cast<long>(0x800706BAu)));  // RPC_S_SERVER_UNAVAILABLE
  CHECK(!isServerDeadHr(static_cast<long>(0x80010001u)));
  CHECK(!isServerDeadHr(0));
  CHECK_EQ(describeHr("Go", static_cast<long>(0x80010108u), "gone"), std::string("Go failed (0x80010108): gone"));
  CHECK_EQ(describeHr("Go", static_cast<long>(0x80004005u)), std::string("Go failed (0x80004005)"));
}

TEST(vsdte_retry_policy) {
  VsBusyPolicy p;
  CHECK_EQ(p.commandMs, 15000);
  CHECK_EQ(p.pollMs, 1000);
  CHECK(p.pollMs < p.commandMs);
  // retry later while inside the window
  CHECK_EQ(retryDelayMs(true, 0, 15000, false, false), 100L);
  CHECK_EQ(retryDelayMs(true, 14999, 15000, false, false), 100L);
  CHECK_EQ(retryDelayMs(true, 15000, 15000, false, false), -1L);
  // a poll window of 1 s gives up after 1 s even though commands would still retry
  CHECK_EQ(retryDelayMs(true, 999, static_cast<unsigned long>(p.pollMs), false, false), 100L);
  CHECK_EQ(retryDelayMs(true, 1000, static_cast<unsigned long>(p.pollMs), false, false), -1L);
  // the poll's overall deadline, cancel() and "rejected, not retry-later" end it at once
  CHECK_EQ(retryDelayMs(true, 10, 15000, true, false), -1L);
  CHECK_EQ(retryDelayMs(true, 10, 15000, false, true), -1L);
  CHECK_EQ(retryDelayMs(false, 10, 15000, false, false), -1L);
}

TEST(vsdte_vswhere_output) {
  CHECK_EQ(firstOutputLine("C:\\VS\\Common7\\IDE\\devenv.exe\r\n"), std::string("C:\\VS\\Common7\\IDE\\devenv.exe"));
  CHECK_EQ(firstOutputLine("\r\n  \r\n  x y  \r\nz"), std::string("x y"));
  CHECK_EQ(firstOutputLine(""), std::string());
  CHECK_EQ(firstOutputLine("\n\n"), std::string());
}

TEST(vsdte_default_open_in_editor_unsupported) {
  struct Plain : Debugger {
    std::string name() const override { return "plain"; }
    DebugResult connect(const DebugWorkspaceInfo&) override { return {}; }
    DebugResult disconnect() override { return {}; }
    DebugResult start() override { return {}; }
    DebugResult startWithoutDebugging() override { return {}; }
    DebugResult stop() override { return {}; }
    DebugResult restart() override { return {}; }
    DebugResult breakAll() override { return {}; }
    DebugResult stepOver() override { return {}; }
    DebugResult stepInto() override { return {}; }
    DebugResult stepOut() override { return {}; }
    DebugResult runToCursor(const std::filesystem::path&, int) override { return {}; }
    DebugResult setBreakpoint(const Breakpoint&) override { return {}; }
    DebugResult removeBreakpoint(const std::filesystem::path&, int) override { return {}; }
    DebugResult poll(DebugSnapshot&) override { return {}; }
  } d;
  CHECK(!d.openInEditor("x.cpp", 0, 0).ok);
}

// ---- Windows only: COM wrappers, then the real Visual Studio ----

#ifdef _WIN32

namespace {
struct ComApartment {
  ComApartment() { hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); }
  ~ComApartment() {
    if (SUCCEEDED(hr)) CoUninitialize();
  }
  HRESULT hr;
};
}  // namespace

TEST(vsdte_com_bstr_variant_roundtrip) {
  const std::string text = "h\xC3\xA9llo \xE4\xB8\xAD\xE6\x96\x87 \xF0\x9F\x99\x82";  // accents, CJK, a surrogate pair
  com::Bstr b(text);
  CHECK_EQ(b.toUtf8(), text);
  com::Bstr copy = b;
  CHECK(copy.get() != b.get());
  CHECK_EQ(copy.toUtf8(), text);
  com::Variant v = com::Variant::fromString(text);
  std::string s;
  CHECK(v.toString(s));
  CHECK_EQ(s, text);
  com::Variant v2 = v;  // deep copy
  CHECK(v2.toString(s));
  CHECK_EQ(s, text);
  long n = 0;
  CHECK(com::Variant::fromLong(-7).toLong(n));
  CHECK_EQ(n, -7L);
  bool bv = false;
  CHECK(com::Variant::fromBool(true).toBool(bv));
  CHECK(bv);
  CHECK(com::Variant::fromBool(false).toBool(bv));
  CHECK(!bv);
  CHECK(com::Variant::fromLong(12).toString(s));
  CHECK_EQ(s, std::string("12"));
  CHECK(!com::Variant().toLong(n));
  CHECK(com::Variant::missing().type() == VT_ERROR);
  CHECK(!com::Variant().toDispatch());
  CHECK_EQ(com::narrow(com::widen("")), std::string());
}

// Late binding against a scripting object that ships with Windows: property get / put, method call with arguments.
TEST(vsdte_com_dispatch_scripting_dictionary) {
  ComApartment com;
  if (FAILED(com.hr)) return;
  CLSID clsid;
  IDispatch* raw = nullptr;
  if (FAILED(CLSIDFromProgID(L"Scripting.Dictionary", &clsid)) ||
      FAILED(CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, IID_IDispatch, reinterpret_cast<void**>(&raw)))) {
    std::cerr << "    (Scripting.Dictionary unavailable: skipped)\n";
    return;
  }
  com::Dispatch dict(raw, false);
  // CompareMode can only change while the dictionary is empty
  CHECK(SUCCEEDED(dict.putProperty(L"CompareMode", com::Variant::fromLong(1))));
  long cm = -1;
  CHECK(SUCCEEDED(dict.getLong(L"CompareMode", cm)));
  CHECK_EQ(cm, 1L);
  CHECK(SUCCEEDED(dict.call(L"Add", {com::Variant::fromString("alpha"), com::Variant::fromLong(5)}, nullptr)));
  CHECK(
      SUCCEEDED(dict.call(L"Add", {com::Variant::fromString("beta"), com::Variant::fromString("x\xC3\xA9")}, nullptr)));
  long count = 0;
  CHECK(SUCCEEDED(dict.getLong(L"Count", count)));
  CHECK_EQ(count, 2L);
  com::Variant item;
  CHECK(SUCCEEDED(dict.getProperty(L"Item", {com::Variant::fromString("alpha")}, item)));
  long n = 0;
  CHECK(item.toLong(n));
  CHECK_EQ(n, 5L);
  // a duplicate key makes the server raise: the failure surfaces as an HRESULT
  std::string detail;
  HRESULT hr = dict.call(L"Add", {com::Variant::fromString("alpha"), com::Variant::fromLong(1)}, nullptr, &detail);
  CHECK(FAILED(hr));
  CHECK(FAILED(dict.getLong(L"NoSuchMember", count)));
}

namespace {

namespace fs = std::filesystem;

void writeFile(const fs::path& p, const std::string& text) {
  std::ofstream f(p, std::ios::binary);
  f << text;
}

// Closes the Visual Studio this test launched, whatever happens.
struct QuitGuard {
  com::Dispatch dte;
  ~QuitGuard() {
    if (!dte) return;
    com::Dispatch sol;
    if (SUCCEEDED(dte.getObject(L"Solution", sol))) sol.putProperty(L"Saved", com::Variant::fromBool(true));
    dte.call(L"Quit", {}, nullptr);
  }
};

bool waitFor(Debugger& d, std::chrono::seconds timeout, const std::function<bool(const DebugSnapshot&)>& pred,
             DebugSnapshot& last) {
  const auto end = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < end) {
    DebugSnapshot s;
    if (d.poll(s)) {
      last = s;
      if (pred(s)) return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
  }
  return false;
}

}  // namespace

// Needs a real Visual Studio 2022 with the C++ workload: runs only with TENFOLD_VS_DTE_TEST=1 (the CI job
// windows-vs-debugger). Launches devenv for a throw-away console solution and drives it through the backend.
TEST(vsdte_integration) {
  const char* flag = std::getenv("TENFOLD_VS_DTE_TEST");
  if (!flag || std::string(flag) != "1") {
    std::cerr << "    TENFOLD_VS_DTE_TEST=1 not set: skipping the Visual Studio integration test\n";
    return;
  }
  namespace fs = std::filesystem;
  const fs::path root =
      fs::weakly_canonical(fs::temp_directory_path()) / ("tenfold_vsdte_" + std::to_string(GetCurrentProcessId()));
  std::error_code ec;
  fs::remove_all(root, ec);
  fs::create_directories(root);
  const fs::path sln = root / "Tiny.sln";
  const fs::path proj = root / "Tiny.vcxproj";
  const fs::path src = root / "main.cpp";
  writeFile(src,
            "#include <cstdio>\n"                   // 1
            "\n"                                    // 2
            "int main() {\n"                        // 3
            "  int total = 0;\n"                    // 4
            "  for (int i = 0; i < 5; ++i) {\n"     // 5
            "    total += i;\n"                     // 6
            "    std::printf(\"%d\\n\", total);\n"  // 7
            "  }\n"                                 // 8
            "  return total == 10 ? 0 : 1;\n"       // 9
            "}\n");                                 // 10
  writeFile(sln,
            "\nMicrosoft Visual Studio Solution File, Format Version 12.00\n"
            "# Visual Studio Version 17\n"
            "VisualStudioVersion = 17.0.31903.59\n"
            "MinimumVisualStudioVersion = 10.0.40219.1\n"
            "Project(\"{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}\") = \"Tiny\", \"Tiny.vcxproj\", "
            "\"{5B1F0A52-3C4D-4E0B-9A3C-0F1E2D3C4B5A}\"\n"
            "EndProject\n"
            "Global\n"
            "\tGlobalSection(SolutionConfigurationPlatforms) = preSolution\n"
            "\t\tDebug|x64 = Debug|x64\n"
            "\tEndGlobalSection\n"
            "\tGlobalSection(ProjectConfigurationPlatforms) = postSolution\n"
            "\t\t{5B1F0A52-3C4D-4E0B-9A3C-0F1E2D3C4B5A}.Debug|x64.ActiveCfg = Debug|x64\n"
            "\t\t{5B1F0A52-3C4D-4E0B-9A3C-0F1E2D3C4B5A}.Debug|x64.Build.0 = Debug|x64\n"
            "\tEndGlobalSection\n"
            "EndGlobal\n");
  writeFile(
      proj,
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
      "<Project DefaultTargets=\"Build\" xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">\n"
      "  <ItemGroup Label=\"ProjectConfigurations\">\n"
      "    <ProjectConfiguration Include=\"Debug|x64\"><Configuration>Debug</Configuration><Platform>x64</Platform>"
      "</ProjectConfiguration>\n"
      "  </ItemGroup>\n"
      "  <PropertyGroup Label=\"Globals\">\n"
      "    <ProjectGuid>{5B1F0A52-3C4D-4E0B-9A3C-0F1E2D3C4B5A}</ProjectGuid>\n"
      "    <Keyword>Win32Proj</Keyword>\n"
      "    <RootNamespace>Tiny</RootNamespace>\n"
      "  </PropertyGroup>\n"
      "  <Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.Default.props\" />\n"
      "  <PropertyGroup Condition=\"'$(Configuration)|$(Platform)'=='Debug|x64'\" Label=\"Configuration\">\n"
      "    <ConfigurationType>Application</ConfigurationType>\n"
      "    <UseDebugLibraries>true</UseDebugLibraries>\n"
      "    <PlatformToolset>v143</PlatformToolset>\n"
      "    <CharacterSet>Unicode</CharacterSet>\n"
      "  </PropertyGroup>\n"
      "  <Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.props\" />\n"
      "  <ItemDefinitionGroup Condition=\"'$(Configuration)|$(Platform)'=='Debug|x64'\">\n"
      "    <ClCompile><WarningLevel>Level3</WarningLevel><Optimization>Disabled</Optimization></ClCompile>\n"
      "    <Link><SubSystem>Console</SubSystem></Link>\n"
      "  </ItemDefinitionGroup>\n"
      "  <ItemGroup><ClCompile Include=\"main.cpp\" /></ItemGroup>\n"
      "  <Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.targets\" />\n"
      "</Project>\n");

  auto dbg = createVisualStudioDebugger();
  DebugWorkspaceInfo ws{root, sln};
  DebugResult r = dbg->connect(ws);  // launches devenv (up to 90 s)
  // Whatever happens next, close the devenv this test started.
  QuitGuard guard;
  for (auto& inst : com::enumerateDteInstances()) {
    std::string name;
    com::Dispatch s;
    if (SUCCEEDED(inst.dte.getObject(L"Solution", s)) && SUCCEEDED(s.getString(L"FullName", name)) &&
        solutionPathsEqual(name, sln.string()))
      guard.dte = inst.dte;
  }
  if (!r) std::cerr << "    connect: " << r.error << "\n";
  REQUIRE(r.ok);
  REQUIRE(static_cast<bool>(guard.dte));

  DebugSnapshot snap;
  REQUIRE(dbg->poll(snap).ok);
  CHECK(snap.mode == DebugMode::Design);

  // a breakpoint on "total += i;" (0-based line 5)
  Breakpoint bp;
  bp.file = src;
  bp.line = 5;
  r = dbg->setBreakpoint(bp);
  if (!r) std::cerr << "    setBreakpoint: " << r.error << "\n";
  REQUIRE(r.ok);
  CHECK(dbg->setBreakpoint(bp).ok);  // idempotent
  REQUIRE(dbg->poll(snap).ok);
  int seen = 0;
  for (const Breakpoint& b : snap.breakpoints)
    if (b.line == 5 && solutionPathsEqual(b.file.string(), src.string())) ++seen;
  CHECK_EQ(seen, 1);

  r = dbg->start();  // builds (per the VS settings), then runs to the breakpoint
  if (!r) std::cerr << "    start: " << r.error << "\n";
  REQUIRE(r.ok);
  auto atBreak = [&](const DebugSnapshot& s, int line) {
    return s.mode == DebugMode::Break && s.currentStatement && s.currentStatement->line == line &&
           solutionPathsEqual(s.currentStatement->file.string(), src.string());
  };
  bool hit = waitFor(*dbg, std::chrono::seconds(240), [&](const DebugSnapshot& s) { return atBreak(s, 5); }, snap);
  if (!hit)
    std::cerr << "    never reached Break at line 5; last mode " << toString(snap.mode) << " error '" << snap.lastError
              << "'\n";
  REQUIRE(hit);

  r = dbg->stepOver();
  if (!r) std::cerr << "    stepOver: " << r.error << "\n";
  REQUIRE(r.ok);
  hit = waitFor(*dbg, std::chrono::seconds(60), [&](const DebugSnapshot& s) { return atBreak(s, 6); }, snap);
  CHECK(hit);

  // A breakpoint set from the "Visual Studio side" shows up in poll().
  {
    com::Dispatch d, bps;
    REQUIRE(SUCCEEDED(guard.dte.getObject(L"Debugger", d)));
    REQUIRE(SUCCEEDED(d.getObject(L"Breakpoints", bps)));
    HRESULT hr = bps.call(
        L"Add", {com::Variant::missing(), com::Variant::fromString(src.string()), com::Variant::fromLong(9)}, nullptr);
    CHECK(SUCCEEDED(hr));
    bool seenVs = waitFor(
        *dbg, std::chrono::seconds(30),
        [&](const DebugSnapshot& s) {
          for (const Breakpoint& b : s.breakpoints)
            if (b.line == 8 && solutionPathsEqual(b.file.string(), src.string())) return true;
          return false;
        },
        snap);
    CHECK(seenVs);
  }

  // removal is idempotent
  CHECK(dbg->removeBreakpoint(src, 5).ok);
  CHECK(dbg->removeBreakpoint(src, 5).ok);

  r = dbg->stop();
  if (!r) std::cerr << "    stop: " << r.error << "\n";
  CHECK(r.ok);
  CHECK(waitFor(
      *dbg, std::chrono::seconds(60), [](const DebugSnapshot& s) { return s.mode == DebugMode::Design; }, snap));
  CHECK(dbg->disconnect().ok);
  REQUIRE(dbg->poll(snap).ok);
  CHECK(snap.mode == DebugMode::Disconnected);
  // Scope exit: QuitGuard closes devenv first, then the backend (and its COM apartment) goes away.
}

#endif  // _WIN32
