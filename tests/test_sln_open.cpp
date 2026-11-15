// SLN-OPEN engine side (docs/VS_BEHAVIOUR.md section 16): what a command-line / dialog / drop argument means
// (resolveWorkspaceTarget), the chosen solution of a workspace (Workspace::Settings::solution, workspace.json key
// "solution") and that it alone drives compile flags, the build command and Editor::compileCommandInfo even when the
// folder holds several solutions.
#include <chrono>
#include <fstream>

#include "app/Editor.h"
#include "build/BuildRunner.h"
#include "core/PathUtil.h"
#include "test_main.h"
#include "workspace/Workspace.h"

using namespace tf;
namespace fs = std::filesystem;

namespace {

struct Dir {
  fs::path path;
  Dir() {
    static int n = 0;
    path = fs::weakly_canonical(fs::temp_directory_path()) /
           ("tf_slnopen_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_" +
            std::to_string(n++));
    std::error_code ec;
    fs::remove_all(path, ec);
    fs::create_directories(path);
  }
  ~Dir() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
};

void write(const fs::path& p, const std::string& text) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary) << text;
}
std::string slurp(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), {});
}

// A one-project solution whose project compiles <name>.cpp with the define NAME_DEF.
void makeSolution(const fs::path& dir, const std::string& name) {
  write(dir / (name + ".sln"),
        "\nMicrosoft Visual Studio Solution File, Format Version 12.00\n"
        "Project(\"{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}\") = \"" +
            name + "\", \"" + name +
            ".vcxproj\", \"{CCCCCCCC-0000-0000-0000-000000000001}\"\nEndProject\nGlobal\n"
            "\tGlobalSection(SolutionConfigurationPlatforms) = preSolution\n"
            "\t\tDebug|x64 = Debug|x64\n\tEndGlobalSection\n"
            "\tGlobalSection(ProjectConfigurationPlatforms) = postSolution\n"
            "\t\t{CCCCCCCC-0000-0000-0000-000000000001}.Debug|x64.ActiveCfg = Debug|x64\n"
            "\t\t{CCCCCCCC-0000-0000-0000-000000000001}.Debug|x64.Build.0 = Debug|x64\n"
            "\tEndGlobalSection\nEndGlobal\n");
  write(dir / (name + ".vcxproj"),
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<Project DefaultTargets=\"Build\" xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">\n"
        "  <ItemGroup Label=\"ProjectConfigurations\">\n"
        "    <ProjectConfiguration Include=\"Debug|x64\"><Configuration>Debug</Configuration>"
        "<Platform>x64</Platform></ProjectConfiguration>\n"
        "  </ItemGroup>\n"
        "  <ItemDefinitionGroup><ClCompile><PreprocessorDefinitions>" +
            name +
            "_DEF;%(PreprocessorDefinitions)</PreprocessorDefinitions></ClCompile></ItemDefinitionGroup>\n"
            "  <ItemGroup><ClCompile Include=\"" +
            name + ".cpp\" /></ItemGroup>\n</Project>\n");
  write(dir / (name + ".cpp"), "int " + name + "_value = 1;\n");
}

Editor::Options quiet() {
  Editor::Options o;
  o.semantic.useClangd = false;
  o.semantic.persistFastIndex = false;
  o.vcs.enabled = false;
  return o;
}

}  // namespace

TEST(sln_open_isSolutionFile) {
  CHECK(isSolutionFile("a/Game.sln"));
  CHECK(isSolutionFile("a/Game.SLN"));
  CHECK(isSolutionFile("Game.slnx"));
  CHECK(isSolutionFile("Game.SlnX"));
  CHECK(!isSolutionFile("Game.vcxproj"));
  CHECK(!isSolutionFile("Game.slnf"));
  CHECK(!isSolutionFile("sln"));
}

TEST(sln_open_resolveTarget) {
  Dir d;
  makeSolution(d.path, "Game");
  write(d.path / "Game.slnx", "<Solution />\n");
  write(d.path / "sub" / "CMakeLists.txt", "project(x)\n");

  WorkspaceTarget t = resolveWorkspaceTarget(d.path / "Game.sln");
  CHECK_EQ(t.root, d.path);
  CHECK_EQ(t.solution, d.path / "Game.sln");
  CHECK_EQ(t.file, d.path / "Game.sln");

  t = resolveWorkspaceTarget(d.path / "Game.slnx");
  CHECK_EQ(t.root, d.path);
  CHECK_EQ(t.solution, d.path / "Game.slnx");

  t = resolveWorkspaceTarget(d.path / "Game.vcxproj");  // a project: its folder, no solution choice
  CHECK_EQ(t.root, d.path);
  CHECK(t.solution.empty());
  CHECK_EQ(t.file, d.path / "Game.vcxproj");

  t = resolveWorkspaceTarget(d.path / "sub" / "CMakeLists.txt");
  CHECK_EQ(t.root, d.path / "sub");
  CHECK(t.solution.empty());

  t = resolveWorkspaceTarget(d.path);  // a folder, as before
  CHECK_EQ(t.root, d.path);
  CHECK(t.solution.empty() && t.file.empty());

  t = resolveWorkspaceTarget(d.path / "sub" / ".." / "." / "");  // trailing slash and dots
  CHECK_EQ(t.root, d.path);

  t = resolveWorkspaceTarget(d.path / "NoSuch.sln");  // a missing path is a folder (the open then fails later)
  CHECK(t.solution.empty());
}

TEST(sln_open_chosenSolutionDrivesTheWorkspace) {
  Dir d;
  makeSolution(d.path, "Alpha");
  makeSolution(d.path, "Beta");
  Workspace ws(d.path);
  CHECK_EQ(ws.solutionFiles().size(), size_t(2));  // no choice: both, as before
  CHECK(ws.chosenSolution().empty());

  std::string err;
  CHECK(ws.setSolution(d.path / "Beta.sln", &err));
  CHECK(ws.solutionFiles() == std::vector<fs::path>{d.path / "Beta.sln"});
  CHECK_EQ(ws.chosenSolution(), d.path / "Beta.sln");
  CHECK_EQ(ws.settings().solution, "Beta.sln");  // relative to the root, generic separators
  CHECK(slurp(d.path / ".tenfold" / "workspace.json").find("\"solution\": \"Beta.sln\"") != std::string::npos);

  Workspace again(d.path);  // reopening the folder later uses it
  again.loadSettings();
  CHECK(again.solutionFiles() == std::vector<fs::path>{d.path / "Beta.sln"});

  // The build and the compile-command report follow it too.
  BuildCommand c;
  CHECK(detectBuildCommand(again, {}, &c, &err));
  CHECK_EQ(c.exe, "msbuild");
  CHECK_EQ(fs::path(c.args.at(0)).filename(), "Beta.sln");

  // Choosing the same solution again does not rewrite the file.
  const auto before = fs::last_write_time(d.path / ".tenfold" / "workspace.json");
  CHECK(again.setSolution(d.path / "Beta.sln", &err));
  CHECK(fs::last_write_time(d.path / ".tenfold" / "workspace.json") == before);

  // A choice that no longer exists falls back to every solution of the folder.
  fs::remove(d.path / "Beta.sln");
  CHECK(again.solutionFiles() == std::vector<fs::path>{d.path / "Alpha.sln"});
  CHECK(again.chosenSolution().empty());

  // Forgetting the choice removes the key.
  CHECK(again.setSolution({}, &err));
  CHECK(slurp(d.path / ".tenfold" / "workspace.json").find("\"solution\"") == std::string::npos);
}

TEST(sln_open_setSolutionRejectsWhatIsNoSolution) {
  Dir d;
  makeSolution(d.path, "Alpha");
  write(d.path / "notes.txt", "x");
  Workspace ws(d.path);
  std::string err;
  CHECK(!ws.setSolution(d.path / "Missing.sln", &err));
  CHECK(!err.empty());
  CHECK(!ws.setSolution(d.path / "notes.txt", &err));
  CHECK(ws.settings().solution.empty());
  write(d.path / "New.slnx", "<Solution />\n");
  CHECK(ws.setSolution("New.slnx", &err));  // relative to the root; .slnx is accepted
  CHECK_EQ(ws.chosenSolution(), d.path / "New.slnx");
  CHECK(ws.solutionFiles() == std::vector<fs::path>{d.path / "New.slnx"});
}

TEST(sln_open_solutionOutsideRootIsStoredAbsolute) {
  Dir d;
  fs::create_directories(d.path / "root");
  makeSolution(d.path / "elsewhere", "Far");
  Workspace ws(d.path / "root");
  std::string err;
  CHECK(ws.setSolution(d.path / "elsewhere" / "Far.sln", &err));
  CHECK_EQ(fs::path(ws.settings().solution), d.path / "elsewhere" / "Far.sln");
  CHECK_EQ(ws.chosenSolution(), d.path / "elsewhere" / "Far.sln");
}

TEST(sln_open_workspaceJsonKeepsOtherKeys) {
  Dir d;
  makeSolution(d.path, "Alpha");
  write(d.path / ".tenfold" / "workspace.json", "{\"vcs\": {\"provider\": \"none\"}, \"custom\": 7}");
  Workspace ws(d.path);
  ws.loadSettings();
  std::string err;
  CHECK(ws.setSolution(d.path / "Alpha.sln", &err));
  const std::string json = slurp(d.path / ".tenfold" / "workspace.json");
  CHECK(json.find("\"custom\": 7") != std::string::npos);
  CHECK(json.find("\"provider\": \"none\"") != std::string::npos);
  CHECK(json.find("\"solution\": \"Alpha.sln\"") != std::string::npos);
}

TEST(sln_open_editorOptionChoosesAndPersists) {
  Dir d;
  makeSolution(d.path, "Alpha");
  makeSolution(d.path, "Beta");
  {
    Editor::Options o = quiet();
    o.solution = d.path / "Beta.sln";
    Editor ed(d.path, o);
    CHECK(ed.workspace().solutionFiles() == std::vector<fs::path>{d.path / "Beta.sln"});
    ed.start();
    auto info = ed.compileCommandInfo(d.path / "Beta.cpp");
    CHECK_EQ(info.solution, d.path / "Beta.sln");
    // The database was generated from Beta alone: its flags, not Alpha's.
    auto cmds = ed.semantic().compileCommands();
    bool beta = false, alpha = false;
    for (const CompileCommand& c : *cmds)
      for (const std::string& a : c.arguments) {
        beta = beta || a.find("Beta_DEF") != std::string::npos;
        alpha = alpha || a.find("Alpha_DEF") != std::string::npos;
      }
    CHECK(beta);
    CHECK(!alpha);
    ed.shutdown();
  }
  {
    Editor ed(d.path, quiet());  // no option: the stored choice
    CHECK_EQ(ed.workspace().chosenSolution(), d.path / "Beta.sln");
  }
  {
    Editor::Options o = quiet();
    o.solution = d.path / "Alpha.sln";  // a different one on the command line wins and is stored
    Editor ed(d.path, o);
    ed.start();
    CHECK_EQ(ed.compileCommandInfo(d.path / "Alpha.cpp").solution, d.path / "Alpha.sln");
    auto cmds = ed.semantic().compileCommands();
    bool alpha = false, beta = false;
    for (const CompileCommand& c : *cmds)
      for (const std::string& a : c.arguments) {
        alpha = alpha || a.find("Alpha_DEF") != std::string::npos;
        beta = beta || a.find("Beta_DEF") != std::string::npos;
      }
    CHECK(alpha);  // not served from Beta's cached database
    CHECK(!beta);
    ed.shutdown();
  }
  Workspace ws(d.path);
  ws.loadSettings();
  CHECK_EQ(ws.settings().solution, "Alpha.sln");
}
