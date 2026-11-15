// SLN-FILES (docs/ARCHITECTURE.md "Workspace kinds", docs/VS_BEHAVIOUR.md section 18; user decision of 2026-11-15): a
// solution workspace contains exactly the files of its projects wherever they are on disk, plus the folders of the
// AdditionalWorkspaceFolders setting; a folder workspace is unchanged. These tests cover the file list, the setting,
// the file watcher's extra roots, the fast index (the headers the workspace includes, the persisted store with files
// outside the root) and the search scope. No test needs clangd.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <set>
#include <thread>

#include "app/Editor.h"
#include "core/PathUtil.h"
#include "core/Platform.h"
#include "semantic/FastIndexStore.h"
#include "test_main.h"
#include "workspace/FileWatcher.h"
#include "workspace/Workspace.h"

using namespace tf;
namespace fs = std::filesystem;

namespace {

struct Base {
  fs::path path;  // <base>/Game (the solution folder), <base>/Engine and <base>/Third live next to it
  Base() {
    static std::atomic<int> n{0};
    path =
        fs::temp_directory_path() / ("tf_slnfiles_" + std::to_string(currentProcessId()) + "_" + std::to_string(n++));
    std::error_code ec;
    fs::remove_all(path, ec);
    fs::create_directories(path);
    path = fs::weakly_canonical(path, ec);
  }
  ~Base() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
  fs::path game() const { return path / "Game"; }
  fs::path engine() const { return path / "Engine"; }
  fs::path third() const { return path / "Third"; }
};

void write(const fs::path& p, const std::string& text) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary | std::ios::trunc) << text;
}

const char* kCpp = "{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}";

std::string project(const std::string& name, const std::string& guid) {
  return std::string("Project(\"") + kCpp + "\") = \"" + name + "\", \"" + name + "\", \"" + guid + "\"\nEndProject\n";
}

std::string solutionText(const std::vector<std::pair<std::string, std::string>>& projects,
                         const std::string& extra = {}) {
  std::string s = "\nMicrosoft Visual Studio Solution File, Format Version 12.00\n";
  int n = 1;
  for (auto& [name, rel] : projects) {
    const std::string guid = "{CCCCCCCC-0000-0000-0000-00000000000" + std::to_string(n++) + "}";
    s += std::string("Project(\"") + kCpp + "\") = \"" + name + "\", \"" + rel + "\", \"" + guid + "\"\nEndProject\n";
  }
  s += extra;
  s += "Global\n\tGlobalSection(SolutionConfigurationPlatforms) = preSolution\n\t\tDebug|x64 = Debug|x64\n"
       "\t\tRelease|x64 = Release|x64\n\tEndGlobalSection\nEndGlobal\n";
  return s;
}

std::string vcxproj(const std::string& items, const std::string& head = {}) {
  return "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
         "<Project DefaultTargets=\"Build\" xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">\n"
         "  <ItemGroup Label=\"ProjectConfigurations\">\n"
         "    <ProjectConfiguration Include=\"Debug|x64\"><Configuration>Debug</Configuration>"
         "<Platform>x64</Platform></ProjectConfiguration>\n"
         "    <ProjectConfiguration Include=\"Release|x64\"><Configuration>Release</Configuration>"
         "<Platform>x64</Platform></ProjectConfiguration>\n"
         "  </ItemGroup>\n" +
         head + "  <ItemGroup>\n" + items + "  </ItemGroup>\n</Project>\n";
}

// <base>/Game/Game.sln (projects Game.vcxproj and ..\Engine\Engine.vcxproj), a loose file next to the project, a
// third-party header no project lists. Game includes engine.h and third.h.
void makeGameAndEngine(const Base& b) {
  write(b.game() / "Game.sln", solutionText({{"Game", "Game.vcxproj"}, {"Engine", "..\\Engine\\Engine.vcxproj"}},
                                            "Project(\"{2150E333-8FDC-42A3-9474-1A3956D46DE8}\") = \"Items\", "
                                            "\"Items\", \"{DDDDDDDD-0000-0000-0000-000000000003}\"\n"
                                            "\tProjectSection(SolutionItems) = preProject\n"
                                            "\t\t.editorconfig = .editorconfig\n"
                                            "\t\tdocs\\notes.md = docs\\notes.md\n"
                                            "\tEndProjectSection\nEndProject\n"));
  write(b.game() / "Game.vcxproj",
        vcxproj("    <ClCompile Include=\"src\\main.cpp\" />\n"
                "    <ClCompile Include=\"gen\\*.cpp\" />\n"
                "    <ClCompile Include=\"src\\skipped.cpp\"><ExcludedFromBuild>true</ExcludedFromBuild></ClCompile>\n"
                "    <ClInclude Include=\"src\\game.h\" />\n"
                "    <None Include=\"notes.txt\" />\n"
                "    <Text Include=\"readme.md\" />\n"
                "    <ResourceCompile Include=\"game.rc\" />\n"
                "    <CustomBuild Include=\"shader.hlsl\" />\n"
                "  </ItemGroup>\n  <ItemGroup Condition=\"'$(Configuration)'=='Release'\">\n"
                "    <ClCompile Include=\"src\\release_only.cpp\" />\n",
                "  <Import Project=\"common.props\" />\n"
                "  <ItemDefinitionGroup><ClCompile><AdditionalIncludeDirectories>..\\Engine\\include;..\\Third\\inc;"
                "%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories></ClCompile></ItemDefinitionGroup>\n"));
  write(b.game() / "common.props",
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<Project xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">\n"
        "  <ItemGroup><ClInclude Include=\"$(MSBuildThisFileDirectory)src\\from_props.h\" /></ItemGroup>\n"
        "</Project>\n");
  write(b.game() / "src/main.cpp",
        "#include \"game.h\"\n#include \"engine.h\"\n#include \"third.h\"\nint main() { return engine_api() + "
        "third_api(); }\n");
  write(b.game() / "src/game.h", "int game_api();\n");
  write(b.game() / "src/skipped.cpp", "int skipped_value;\n");
  write(b.game() / "src/release_only.cpp", "int release_only_value;\n");
  write(b.game() / "src/from_props.h", "int from_props_value;\n");
  write(b.game() / "gen/g1.cpp", "int gen_one;\n");
  write(b.game() / "gen/g2.cpp", "int gen_two;\n");
  write(b.game() / "notes.txt", "notes\n");
  write(b.game() / "readme.md", "readme\n");
  write(b.game() / "game.rc", "rc\n");
  write(b.game() / "shader.hlsl", "hlsl\n");
  write(b.game() / ".editorconfig", "root = true\n");
  write(b.game() / "docs/notes.md", "notes\n");
  write(b.game() / "loose/loose.cpp", "int loose_value;\n");  // in no project
  write(b.engine() / "Engine.vcxproj",
        vcxproj("    <ClCompile Include=\"src\\Engine.cpp\" />\n    <ClInclude Include=\"include\\engine.h\" />\n"));
  write(b.engine() / "include/engine.h", "int engine_api();\n");
  write(b.engine() / "src/Engine.cpp", "#include \"engine.h\"\nint engine_api() { return 1; }\n");
  write(b.engine() / "src/unlisted.cpp", "int engine_unlisted;\n");  // next to the project's files, listed by nothing
  write(b.third() / "inc/third.h", "int third_api();\n");
}

Editor::Options quiet() {
  Editor::Options o;
  o.semantic.useClangd = false;
  o.semantic.persistFastIndex = false;
  o.vcs.enabled = false;
  return o;
}

std::set<std::string> keys(const Workspace& ws, const fs::path& base) {
  std::set<std::string> out;
  for (const fs::path& f : ws.allFiles()) out.insert(pathKeyUtf8(f.lexically_relative(base)));
  return out;
}
bool has(const std::set<std::string>& s, const std::string& k) { return s.count(k) != 0; }

}  // namespace

TEST(slnfiles_kind_follows_the_compile_flag_precedence) {
  Base b;
  makeGameAndEngine(b);
  {
    Workspace ws(b.game());
    ws.scan();
    CHECK(ws.isSolutionWorkspace());
  }
  {  // a compile_commands.json of the user's wins over the solution, as it does for the compile flags
    write(b.game() / "compile_commands.json", "[]\n");
    Workspace ws(b.game());
    ws.scan();
    CHECK(!ws.isSolutionWorkspace());
    CHECK(has(keys(ws, b.path), "Game/loose/loose.cpp"));  // a folder workspace: every file under the root
    fs::remove(b.game() / "compile_commands.json");
  }
  {  // a CMake project wins too, unless it is switched off
    write(b.game() / "CMakeLists.txt", "project(x)\n");
    Workspace ws(b.game());
    ws.scan();
    CHECK(!ws.isSolutionWorkspace());
    ws.settings().runCMake = false;
    ws.scan();
    CHECK(ws.isSolutionWorkspace());
    fs::remove(b.game() / "CMakeLists.txt");
  }
  {  // the compile database tenfold generates itself in .tenfold is not "the user's"
    write(b.game() / ".tenfold" / "compile_commands.json", "[]\n");
    Workspace ws(b.game());
    ws.scan();
    CHECK(ws.isSolutionWorkspace());
  }
  {  // no solution: a folder workspace
    write(b.path / "Plain" / "a.cpp", "int a;\n");
    Workspace ws(b.path / "Plain");
    ws.scan();
    CHECK(!ws.isSolutionWorkspace());
    CHECK_EQ(ws.sourceFiles().size(), size_t{1});
  }
}

TEST(slnfiles_the_file_list_is_the_files_of_the_projects_wherever_they_are) {
  Base b;
  makeGameAndEngine(b);
  Workspace ws(b.game());
  ws.scan();
  REQUIRE(ws.isSolutionWorkspace());
  const auto k = keys(ws, b.path);
  // every item type, whatever the extension: ClCompile, ClInclude, None, Text, ResourceCompile, CustomBuild
  for (const char* f :
       {"Game/src/main.cpp", "Game/src/game.h", "Game/notes.txt", "Game/readme.md", "Game/game.rc", "Game/shader.hlsl"})
    CHECK(has(k, f));
  CHECK(has(k, "Game/gen/g1.cpp"));  // a wildcard item
  CHECK(has(k, "Game/gen/g2.cpp"));
  CHECK(has(k, "Game/src/skipped.cpp"));       // excluded from build: still listed, as Visual Studio lists it
  CHECK(has(k, "Game/src/release_only.cpp"));  // an item of one configuration only: all configurations count
  CHECK(has(k, "Game/src/from_props.h"));      // an item of an imported .props
  CHECK(has(k, "Game/Game.vcxproj"));          // the project files themselves, even outside the root
  CHECK(has(k, "Engine/Engine.vcxproj"));
  CHECK(has(k, "Engine/src/Engine.cpp"));  // a project outside the solution folder
  CHECK(has(k, "Engine/include/engine.h"));
  CHECK(has(k, "Game/.editorconfig"));  // solution items
  CHECK(has(k, "Game/docs/notes.md"));
  // not the solution file, not the property sheet, not what no project lists
  CHECK(!has(k, "Game/Game.sln"));
  CHECK(!has(k, "Game/common.props"));
  CHECK(!has(k, "Game/loose/loose.cpp"));
  CHECK(!has(k, "Engine/src/unlisted.cpp"));
  CHECK(!has(k, "Third/inc/third.h"));
  // by kind: sources, headers, everything else under others
  const auto isIn = [&](const std::vector<fs::path>& list, const char* rel) {
    return std::find(list.begin(), list.end(), b.path / rel) != list.end();
  };
  CHECK(isIn(ws.sourceFiles(), "Engine/src/Engine.cpp"));
  CHECK(isIn(ws.headerFiles(), "Engine/include/engine.h"));
  CHECK(isIn(ws.otherFiles(), "Game/shader.hlsl"));
  CHECK(isIn(ws.otherFiles(), "Engine/Engine.vcxproj"));
  // sorted and without duplicates
  CHECK(std::is_sorted(ws.sourceFiles().begin(), ws.sourceFiles().end()));
  CHECK_EQ(std::set<fs::path>(ws.sourceFiles().begin(), ws.sourceFiles().end()).size(), ws.sourceFiles().size());
  // what the explorer's folder view and Show All Files list is unchanged: the loose file is there
  const auto ex = ws.explorerFiles();
  CHECK(std::find(ex.begin(), ex.end(), b.game() / "loose/loose.cpp") != ex.end());
  CHECK(std::find(ex.begin(), ex.end(), b.game() / "src/main.cpp") != ex.end());
  CHECK(std::find(ex.begin(), ex.end(), b.engine() / "src/Engine.cpp") !=
        ex.end());  // outside files too (the tree skips them)
}

TEST(slnfiles_shared_items_and_missing_files) {
  Base b;
  write(b.game() / "S.sln", solutionText({{"App", "App.vcxproj"}, {"Shared", "..\\Shared\\Shared.vcxitems"}}));
  write(b.game() / "App.vcxproj",
        vcxproj("    <ClCompile Include=\"app.cpp\" />\n    <ClCompile Include=\"not_yet.cpp\" />\n",
                "  <Import Project=\"..\\Shared\\Shared.vcxitems\" Label=\"Shared\" />\n"));
  write(b.game() / "app.cpp", "int app;\n");
  write(b.path / "Shared/Shared.vcxitems",
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<Project xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">\n"
        "  <ItemGroup><ClCompile Include=\"$(MSBuildThisFileDirectory)shared.cpp\" />"
        "<ClInclude Include=\"$(MSBuildThisFileDirectory)shared.h\" /></ItemGroup>\n</Project>\n");
  write(b.path / "Shared/shared.cpp", "int shared;\n");
  write(b.path / "Shared/shared.h", "int shared();\n");
  Workspace ws(b.game());
  ws.scan();
  REQUIRE(ws.isSolutionWorkspace());
  const auto k = keys(ws, b.path);
  CHECK(has(k, "Shared/shared.cpp"));
  CHECK(has(k, "Shared/shared.h"));
  CHECK(has(k, "Shared/Shared.vcxitems"));
  CHECK(!has(k, "Game/not_yet.cpp"));            // listed by the project but not on disk: not a file yet
  CHECK(ws.isMember(b.game() / "not_yet.cpp"));  // ... and a workspace file the moment it is created
  CHECK(!ws.isMember(b.game() / "other.cpp"));
  CHECK(ws.noteFileAdded(b.game() / "not_yet.cpp"));
  CHECK(has(keys(ws, b.path), "Game/not_yet.cpp"));
  ws.noteFileAdded(b.game() / "other.cpp");  // not a project item: loose
  CHECK(!has(keys(ws, b.path), "Game/other.cpp"));
  const auto ex = ws.explorerFiles();
  CHECK(std::find(ex.begin(), ex.end(), b.game() / "other.cpp") != ex.end());
}

TEST(slnfiles_additional_workspace_folders) {
  Base b;
  makeGameAndEngine(b);
  write(b.third() / "lib/a.h", "int lib_a();\n");
  write(b.third() / "lib/deep/b.cpp", "int lib_b;\n");
  write(b.third() / "lib/deep/c.png", "png\n");        // an extension the workspace does not list
  write(b.third() / "lib/.git/x.h", "int hidden;\n");  // an ignored directory
  write(b.third() / "lib/build/y.h", "int built;\n");  // another one (build output)
  write(b.game() / "extra/e.cpp", "int extra_e;\n");   // a folder below the root
  write(b.game() / "extra/sub/f.h", "int extra_f;\n");
  write(b.game() / "flat/g.cpp", "int flat_g;\n");
  write(b.game() / "flat/sub/h.cpp", "int flat_h;\n");
  Workspace ws(b.game());
  // 10x's value: comma separated wildcard paths, backslashes, relative to the solution folder
  ws.settings().additionalWorkspaceFolders = splitFolderList("..\\Third\\lib\\**\\*.*, extra, flat\\*.cpp");
  ws.scan();
  REQUIRE(ws.isSolutionWorkspace());
  const auto k = keys(ws, b.path);
  CHECK(has(k, "Third/lib/a.h"));
  CHECK(has(k, "Third/lib/deep/b.cpp"));
  CHECK(!has(k, "Third/lib/deep/c.png"));
  CHECK(!has(k, "Third/lib/.git/x.h"));
  CHECK(!has(k, "Third/lib/build/y.h"));
  CHECK(has(k, "Game/extra/e.cpp"));  // a bare folder: everything below it
  CHECK(has(k, "Game/extra/sub/f.h"));
  CHECK(has(k, "Game/flat/g.cpp"));
  CHECK(!has(k, "Game/flat/sub/h.cpp"));               // no ** : the folder only
  CHECK(!has(k, "Third/inc/third.h"));                 // outside every rule
  CHECK(ws.isMember(b.third() / "lib/deep/new.cpp"));  // a file the rule covers, not created yet
  CHECK(!ws.isMember(b.third() / "inc/third.h"));
  // the watcher has to look at the folders outside the root, and the project directory
  std::set<std::string> roots;
  for (auto& w : ws.extraWatchRoots())
    roots.insert(pathKeyUtf8(w.dir.lexically_relative(b.path)) + (w.recursive ? "/**" : ""));
  CHECK(has(roots, "Engine/**"));
  CHECK(has(roots, "Third/lib/**"));
  CHECK(!has(roots, "Game/**"));  // below the root: the root's own watch covers it
  // a folder workspace ignores the setting
  write(b.game() / "compile_commands.json", "[]\n");
  ws.scan();
  CHECK(!ws.isSolutionWorkspace());
  CHECK(!has(keys(ws, b.path), "Third/lib/a.h"));
  CHECK(has(keys(ws, b.path), "Game/loose/loose.cpp"));
}

TEST(slnfiles_additional_workspace_folders_setting_in_workspace_json) {
  Base b;
  makeGameAndEngine(b);
  write(b.game() / ".tenfold/workspace.json",
        R"({"AdditionalWorkspaceFolders": "..\\Third\\inc\\*.h, extra", "cppStandard": "c++17"})");
  {
    Workspace ws(b.game());
    REQUIRE(ws.loadSettings());
    CHECK_EQ(ws.settings().additionalWorkspaceFolders.size(), size_t{2});
    CHECK_EQ(ws.settings().additionalWorkspaceFolders[0], std::string("..\\Third\\inc\\*.h"));
    ws.scan();
    CHECK(has(keys(ws, b.path), "Third/inc/third.h"));
    CHECK(ws.saveSettings());  // our spelling is an array and the 10x spelling is gone; other keys stay
  }
  const std::string text = [&] {
    std::ifstream in(b.game() / ".tenfold/workspace.json");
    return std::string(std::istreambuf_iterator<char>(in), {});
  }();
  CHECK(text.find("\"additionalWorkspaceFolders\"") != std::string::npos);
  CHECK(text.find("AdditionalWorkspaceFolders") == std::string::npos);
  Workspace again(b.game());
  REQUIRE(again.loadSettings());
  CHECK_EQ(again.settings().additionalWorkspaceFolders.size(), size_t{2});
  // an array form works too, a value of the wrong type is ignored
  write(b.game() / ".tenfold/workspace.json", R"({"additionalWorkspaceFolders": ["a\\**\\*.*", 5, "b"]})");
  Workspace arr(b.game());
  REQUIRE(arr.loadSettings());
  CHECK_EQ(arr.settings().additionalWorkspaceFolders.size(), size_t{2});
  write(b.game() / ".tenfold/workspace.json", R"({"additionalWorkspaceFolders": 7})");
  Workspace bad(b.game());
  REQUIRE(bad.loadSettings());
  CHECK(bad.settings().additionalWorkspaceFolders.empty());
}

TEST(slnfiles_folder_rules_with_windows_paths) {
  const fs::path root = "/work/Game";
  auto never = [](const std::string&) { return false; };
  auto ign = [](const std::string& n) { return n == ".git" || n == "build"; };
  {  // MyFolder\**\*.* : recursive, every name
    FolderRule r = parseFolderRule("MyFolder\\**\\*.*", root, PathStyle::Windows);
    REQUIRE(r.valid);
    CHECK_EQ(r.base, fs::path("/work/Game/MyFolder"));
    CHECK(r.recursive);
    CHECK(r.matches("/work/Game/MyFolder/a.h", never, PathStyle::Windows));
    CHECK(r.matches("/work/Game/MyFolder/x/y/z.noext", never, PathStyle::Windows));
    CHECK(r.matches("/work/Game/myfolder/A.H", never, PathStyle::Windows));  // Windows: case-insensitive
    CHECK(!r.matches("/work/Game/myfolder/A.H", never, PathStyle::Posix));
    CHECK(!r.matches("/work/Game/Other/a.h", never, PathStyle::Windows));
    CHECK(!r.matches("/work/Game/MyFolder", never, PathStyle::Windows));
    CHECK(!r.matches("/work/Game/MyFolder/.git/a.h", ign, PathStyle::Windows));  // an ignored directory on the way
  }
  {  // a file-name pattern, no recursion
    FolderRule r = parseFolderRule("..\\Third\\lib\\*.h", root, PathStyle::Windows);
    REQUIRE(r.valid);
    CHECK_EQ(r.base, fs::path("/work/Third/lib"));
    CHECK(!r.recursive);
    CHECK(r.matches("/work/Third/lib/a.h", never, PathStyle::Windows));
    CHECK(!r.matches("/work/Third/lib/a.cpp", never, PathStyle::Windows));
    CHECK(!r.matches("/work/Third/lib/sub/a.h", never, PathStyle::Windows));
  }
  {  // ** in the middle, ? and * inside names
    FolderRule r = parseFolderRule("src/**/gen_??.c*", root, PathStyle::Posix);
    REQUIRE(r.valid);
    CHECK(r.recursive);
    CHECK(r.matches("/work/Game/src/gen_01.cpp", never, PathStyle::Posix));
    CHECK(r.matches("/work/Game/src/a/b/gen_zz.c", never, PathStyle::Posix));
    CHECK(!r.matches("/work/Game/src/gen_001.cpp", never, PathStyle::Posix));
  }
  {  // a wildcard folder name, and an absolute drive-letter path (kept as written)
    FolderRule r = parseFolderRule("Mod*\\inc\\*.*", root, PathStyle::Windows);
    CHECK_EQ(r.base, fs::path("/work/Game"));
    CHECK(r.matches("/work/Game/ModA/inc/x.h", never, PathStyle::Windows));
    CHECK(!r.matches("/work/Game/ModA/src/x.h", never, PathStyle::Windows));
    FolderRule d = parseFolderRule("D:\\Libs\\vendor\\**\\*.h", root, PathStyle::Windows);
    REQUIRE(d.valid);
    CHECK_EQ(pathKeyUtf8(d.base), std::string("D:/Libs/vendor"));
    CHECK(d.matches(fs::path("D:/Libs/vendor/a/b.H"), never, PathStyle::Windows));
    CHECK(!d.matches(fs::path("E:/Libs/vendor/a/b.h"), never, PathStyle::Windows));
    FolderRule bare = parseFolderRule("C:\\", root, PathStyle::Windows);  // the drive root as a folder: everything
    CHECK(bare.valid);
  }
  CHECK(!parseFolderRule("  ", root).valid);
  CHECK(splitFolderList(" a , ,b\\**\\*.* ,").size() == 2);
}

TEST(slnfiles_project_file_change_rereads_the_list) {
  Base b;
  makeGameAndEngine(b);
  Editor::Options o = quiet();
  o.watchPollMs = 100;
  Editor ed(b.game(), o);
  REQUIRE(ed.start());
  REQUIRE(ed.workspace().isSolutionWorkspace());
  auto listed = [&](const char* rel) {
    const auto k = keys(ed.workspace(), b.path);
    return has(k, rel);
  };
  CHECK(!listed("Engine/src/unlisted.cpp"));
  // the project (outside the root) lists one more file
  write(b.engine() / "Engine.vcxproj",
        vcxproj("    <ClCompile Include=\"src\\Engine.cpp\" />\n    <ClInclude Include=\"include\\engine.h\" />\n"
                "    <ClCompile Include=\"src\\unlisted.cpp\" />\n"));
  CHECK(ed.waitFor([&] { return listed("Engine/src/unlisted.cpp"); }, 6000));
  CHECK(ed.waitFor([&] { return !ed.semantic().fastIndex().lookup("engine_unlisted").empty(); }, 6000));
  // an item leaves: it is dropped from the list and from the index
  write(b.engine() / "Engine.vcxproj",
        vcxproj("    <ClCompile Include=\"src\\Engine.cpp\" />\n    <ClInclude Include=\"include\\engine.h\" />\n"));
  CHECK(ed.waitFor([&] { return !listed("Engine/src/unlisted.cpp"); }, 6000));
  CHECK(ed.waitFor([&] { return ed.semantic().fastIndex().lookup("engine_unlisted").empty(); }, 6000));
  // the imported property sheet changes: the files it contributes follow
  write(b.game() / "src/more.h", "int more_value;\n");
  write(b.game() / "common.props",
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<Project "
        "xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">\n"
        "  <ItemGroup><ClInclude Include=\"$(MSBuildThisFileDirectory)src\\more.h\" /></ItemGroup>\n</Project>\n");
  CHECK(ed.waitFor([&] { return listed("Game/src/more.h") && !listed("Game/src/from_props.h"); }, 6000));
  // a new project directory outside the root is watched from then on
  write(b.path / "Late/Late.vcxproj", vcxproj("    <ClCompile Include=\"late.cpp\" />\n"));
  write(b.path / "Late/late.cpp", "int late_value;\n");
  write(b.game() / "Game.sln",
        solutionText(
            {{"Game", "Game.vcxproj"}, {"Engine", "..\\Engine\\Engine.vcxproj"}, {"Late", "..\\Late\\Late.vcxproj"}}));
  CHECK(ed.waitFor([&] { return listed("Late/late.cpp"); }, 6000));
  CHECK(ed.waitFor([&] { return !ed.semantic().fastIndex().lookup("late_value").empty(); }, 6000));
  ed.waitFor([] { return false; }, 600);                     // the new directory is picked up by the watcher
  write(b.path / "Late/late.cpp", "int late_value_two;\n");  // an edit in the new outside directory is noticed
  CHECK(ed.waitFor([&] { return !ed.semantic().fastIndex().lookup("late_value_two").empty(); }, 6000));
}

TEST(slnfiles_editor_follows_files_outside_the_root) {
  Base b;
  makeGameAndEngine(b);
  Editor::Options o = quiet();
  o.watchPollMs = 100;
  Editor ed(b.game(), o);
  REQUIRE(ed.start());
  REQUIRE(ed.semantic().waitForFastIndex(10000));
  const fs::path engine = b.engine() / "src/Engine.cpp";
  CHECK(!ed.semantic().fastIndex().lookup("engine_api").empty());
  // an external edit of a file outside the root reaches the index
  write(engine, "#include \"engine.h\"\nint engine_api() { return 1; }\nint engine_added_symbol;\n");
  CHECK(ed.waitFor([&] { return !ed.semantic().fastIndex().lookup("engine_added_symbol").empty(); }, 6000));
  // a file nobody lists, in a directory that is watched, is not a workspace file and not indexed
  write(b.engine() / "src/stray.cpp", "int engine_stray;\n");
  write(b.game() / "loose/stray2.cpp", "int loose_stray;\n");
  ed.waitFor([] { return false; }, 800);
  const auto k = keys(ed.workspace(), b.path);
  CHECK(!has(k, "Engine/src/stray.cpp"));
  CHECK(!has(k, "Game/loose/stray2.cpp"));
  CHECK(ed.semantic().fastIndex().lookup("engine_stray").empty());
  CHECK(ed.semantic().fastIndex().lookup("loose_stray").empty());
  // ... but the explorer's folder view sees the loose file below the root
  const auto ex = ed.workspace().explorerFiles();
  CHECK(std::find(ex.begin(), ex.end(), b.game() / "loose/stray2.cpp") != ex.end());
  // deleting a project's file outside the root removes it
  fs::remove(b.engine() / "include/engine.h");
  CHECK(ed.waitFor([&] { return !has(keys(ed.workspace(), b.path), "Engine/include/engine.h"); }, 6000));
  // and creating it again brings it back (it is still a project item)
  write(b.engine() / "include/engine.h", "int engine_api();\nint engine_back;\n");
  CHECK(ed.waitFor([&] { return has(keys(ed.workspace(), b.path), "Engine/include/engine.h"); }, 6000));
  CHECK(ed.waitFor([&] { return !ed.semantic().fastIndex().lookup("engine_back").empty(); }, 6000));
}

TEST(slnfiles_fast_index_holds_the_headers_the_workspace_includes) {
  Base b;
  makeGameAndEngine(b);
  Editor ed(b.game(), quiet());
  REQUIRE(ed.start());
  REQUIRE(ed.semantic().waitForFastIndex(10000));
  FastIndex& fast = ed.semantic().fastIndex();
  // third.h is in no project and below no workspace folder: not a workspace file ...
  CHECK(!has(keys(ed.workspace(), b.path), "Third/inc/third.h"));
  // ... but the fast tier knows it (go to definition of third_api() from main.cpp resolves into it)
  CHECK(fast.fileSymbols(b.third() / "inc/third.h") != nullptr);
  CHECK(!fast.lookup("third_api").empty());
  CHECK(fast.search("third_api", 10).empty());  // symbol search lists workspace files only
  CHECK(!fast.search("engine_api", 10).empty());
  auto reach = fast.reachableFrom(b.game() / "src/main.cpp", {}, {b.engine() / "include", b.third() / "inc"});
  REQUIRE(reach != nullptr);
  CHECK(reach->count(FastIndex::pathKey(b.third() / "inc/third.h")) != 0);
  // the loose file is not indexed
  CHECK(fast.lookup("loose_value").empty());
  CHECK_EQ(ed.semantic().status().fastIncludeOnly, size_t{1});
  // the caps: no headers beyond the limit
  Editor::Options capped = quiet();
  capped.semantic.maxIncludeIndexFiles = 0;
  Editor ed0(b.game(), capped);
  REQUIRE(ed0.start());
  REQUIRE(ed0.semantic().waitForFastIndex(10000));
  CHECK(ed0.semantic().fastIndex().lookup("third_api").empty());
  // the system directories are not looked up
  write(b.path / "Windows Kits/10/Include/um/winlike.h", "int winlike_api();\n");
  write(b.game() / "src/game.h", "#include <winlike.h>\nint game_api();\n");
  write(
      b.game() / "Game.vcxproj",
      vcxproj("    <ClCompile Include=\"src\\main.cpp\" />\n    <ClInclude Include=\"src\\game.h\" />\n",
              "  <ItemDefinitionGroup><ClCompile><AdditionalIncludeDirectories>..\\Engine\\include;..\\Third\\inc;"
              "..\\Windows Kits\\10\\Include\\um</AdditionalIncludeDirectories></ClCompile></ItemDefinitionGroup>\n"));
  Editor ed2(b.game(), quiet());
  REQUIRE(ed2.start());
  REQUIRE(ed2.semantic().waitForFastIndex(10000));
  CHECK(ed2.semantic().fastIndex().lookup("winlike_api").empty());
  CHECK(!ed2.semantic().fastIndex().lookup("third_api").empty());
}

TEST(slnfiles_fast_index_store_round_trip_with_files_outside_the_root) {
  Base b;
  makeGameAndEngine(b);
  Editor::Options o = quiet();
  o.semantic.persistFastIndex = true;
  size_t files = 0;
  {
    Editor ed(b.game(), o);
    REQUIRE(ed.start());
    REQUIRE(ed.semantic().waitForFastIndex(10000));
    files = ed.semantic().fastIndex().fileCount();
    CHECK(files > 8);
    CHECK(ed.semantic().status().fastRelexed > 0);
  }  // shutdown saves the store
  CHECK(fs::is_regular_file(b.game() / ".tenfold/fastindex.bin"));
  {
    Editor ed(b.game(), o);
    REQUIRE(ed.start());
    REQUIRE(ed.semantic().waitForFastIndex(10000));
    // every entry, the ones outside the root and the included header too, came from the store
    CHECK_EQ(ed.semantic().fastIndex().fileCount(), files);
    CHECK_EQ(ed.semantic().status().fastRelexed, size_t{0});
    CHECK(ed.semantic().status().fastReused > 0);
    CHECK(!ed.semantic().fastIndex().lookup("engine_api").empty());
    CHECK(!ed.semantic().fastIndex().lookup("third_api").empty());
    CHECK(ed.semantic().fastIndex().search("third_api", 5).empty());
  }
  // a header that changed outside the root is read again at the next start
  write(b.third() / "inc/third.h", "int third_api();\nint third_more();\n");
  {
    Editor ed(b.game(), o);
    REQUIRE(ed.start());
    REQUIRE(ed.semantic().waitForFastIndex(10000));
    CHECK(!ed.semantic().fastIndex().lookup("third_more").empty());
  }
}

TEST(slnfiles_store_keys_files_outside_the_root_by_absolute_path) {
  // The store keeps paths as given: absolute, so a file outside the workspace root, another drive and a case variant
  // are separate entries that survive a round trip unchanged (the format needs no new version for them).
  FastIndexStore::Entries in;
  for (const char* p : {"/work/Game/src/a.cpp", "/work/Engine/src/e.cpp", "C:/Libs/Engine/Src/E.cpp",
                        "D:/Third Party/inc/t.h", "//server/share/x/y.h"}) {
    auto fs = std::make_shared<FileSymbols>();
    fs->file = pathFromUtf8(p);
    extractFile("int f_" + std::to_string(in.size()) + "(int);\n#include \"z.h\"\n", *fs);
    fs->mtime = 123 + static_cast<int64_t>(in.size());
    fs->size = 7;
    in.push_back(fs);
  }
  const std::string bytes = FastIndexStore::serialize(in);
  std::string err;
  auto back = FastIndexStore::deserialize(bytes.data(), bytes.size(), &err);
  REQUIRE(back.has_value());
  CHECK_EQ(back->size(), in.size());
  for (size_t i = 0; i < in.size() && i < back->size(); ++i) {
    CHECK_EQ(pathKeyUtf8((*back)[i]->file), pathKeyUtf8(in[i]->file));
    CHECK_EQ((*back)[i]->mtime, in[i]->mtime);
    CHECK_EQ((*back)[i]->symbols.size(), in[i]->symbols.size());
  }
}

TEST(slnfiles_find_in_files_scope_is_the_file_list) {
  Base b;
  makeGameAndEngine(b);
  write(b.game() / "loose/loose.cpp", "int scope_token;\n");
  write(b.engine() / "src/unlisted.cpp", "int scope_token;\n");
  write(b.third() / "inc/third.h", "int third_api(); // scope_token\n");
  write(b.engine() / "src/Engine.cpp", "#include \"engine.h\"\nint engine_api() { return scope_token; }\n");
  write(b.game() / "notes.txt", "scope_token in a text file\n");
  Editor ed(b.game(), quiet());
  REQUIRE(ed.start());
  auto run = [&](bool others, const std::vector<std::string>& include = {}) {
    std::vector<SearchMatch> got;
    bool done = false;
    SearchOptions so;
    so.includeOtherFiles = others;
    so.includeGlobs = include;
    ed.findInWorkspace(
        "scope_token", so, [&](const std::vector<SearchMatch>& m) { got.insert(got.end(), m.begin(), m.end()); },
        [&](const SearchSummary&) { done = true; });
    ed.waitFor([&] { return done; }, 10000);
    std::set<std::string> files;
    for (auto& m : got) files.insert(pathKeyUtf8(m.file.lexically_relative(b.path)));
    return files;
  };
  auto code = run(false);
  CHECK(has(code, "Engine/src/Engine.cpp"));  // a project outside the solution folder is searched
  CHECK(!has(code, "Game/loose/loose.cpp"));
  CHECK(!has(code, "Engine/src/unlisted.cpp"));
  CHECK(!has(code, "Third/inc/third.h"));
  CHECK(!has(code, "Game/notes.txt"));
  auto all = run(true);
  CHECK(has(all, "Game/notes.txt"));
  // globs see an outside file by its path relative to the root
  CHECK(has(run(false, {"../Engine/**"}), "Engine/src/Engine.cpp"));
  CHECK(run(false, {"src/*.cpp"}).count("Engine/src/Engine.cpp") == 0);
  // an unsaved buffer of an outside file is what gets searched
  Document* d = ed.open(b.engine() / "src/Engine.cpp");
  REQUIRE(d != nullptr);
  d->replace({{1, 0}, {1, 33}}, "int engine_api() { return 0; }");
  auto edited = run(false);
  CHECK(!has(edited, "Engine/src/Engine.cpp"));
}

TEST(slnfiles_file_watcher_extra_roots) {
  for (bool polling : {false, true}) {
    Base b;
    write(b.path / "main/a.txt", "a");
    write(b.path / "rec/deep/r.txt", "r");
    write(b.path / "flat/f.txt", "f");
    write(b.path / "flat/sub/s.txt", "s");
    FileWatcher::Options wo;
    wo.forcePolling = polling;
    wo.pollMs = 80;
    wo.debounceMs = 60;
    FileWatcher w(wo);
    std::mutex mu;
    std::vector<FileWatcher::Event> evs;
    auto seen = [&](const fs::path& p) {
      std::lock_guard<std::mutex> lk(mu);
      for (auto& e : evs)
        if (e.path == p) return true;
      return false;
    };
    auto waitSeen = [&](const fs::path& p, int ms = 4000) {
      auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
      while (std::chrono::steady_clock::now() < end) {
        if (seen(p)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      return seen(p);
    };
    w.addRoot(b.path / "rec", true);  // before start()
    REQUIRE(w.start(b.path / "main", {}, [&](FileWatcher::Event e) {
      std::lock_guard<std::mutex> lk(mu);
      evs.push_back(std::move(e));
    }));
    w.addRoot(b.path / "flat", false);  // while running
    std::this_thread::sleep_for(std::chrono::milliseconds(polling ? 250 : 150));
    write(b.path / "main/a.txt", "a2");
    CHECK(waitSeen(b.path / "main/a.txt"));
    write(b.path / "rec/deep/r.txt", "r2");  // below a recursive extra root
    CHECK(waitSeen(b.path / "rec/deep/r.txt"));
    write(b.path / "rec/deep/new/n.txt", "n");  // a directory that appears later is followed
    CHECK(waitSeen(b.path / "rec/deep/new/n.txt"));
    write(b.path / "flat/f.txt", "f2");
    CHECK(waitSeen(b.path / "flat/f.txt"));
    write(b.path / "flat/sub/s.txt", "s2");  // not recursive: nothing from below
    std::this_thread::sleep_for(std::chrono::milliseconds(polling ? 400 : 250));
    CHECK(!seen(b.path / "flat/sub/s.txt"));
    // a root that is removed is no longer watched
    w.removeRoot(b.path / "rec");
    std::this_thread::sleep_for(std::chrono::milliseconds(polling ? 300 : 200));
    write(b.path / "rec/deep/r.txt", "r3");
    write(b.path / "rec/after.txt", "x");
    std::this_thread::sleep_for(std::chrono::milliseconds(polling ? 500 : 300));
    CHECK(!seen(b.path / "rec/after.txt"));
    CHECK_EQ(w.rootCount(), size_t{1});
    w.stop();
  }
}

TEST(slnfiles_paths_are_compared_whatever_their_separators) {
  // The explorer and the watcher hand over paths spelled with '/' (pathFromUtf8 of a generic key) while the root is
  // native ('\' on Windows): equal paths must stay equal (the Windows root-directory element "\" vs "/" once made
  // every file look like it was outside the root).
  Base b;
  makeGameAndEngine(b);
  Workspace ws(b.game());
  ws.scan();
  REQUIRE(ws.isSolutionWorkspace());
  const fs::path fresh = pathFromUtf8(pathKeyUtf8(b.game()) + "/loose/fresh.cpp");
  write(fresh, "int fresh;\n");
  ws.noteFileAdded(fresh);
  const auto ex = ws.explorerFiles();
  CHECK(std::find(ex.begin(), ex.end(), fresh) != ex.end());
  CHECK(!has(keys(ws, b.path), "Game/loose/fresh.cpp"));  // loose: not a workspace file
  // a project directory outside the root, found through a '/' spelling, is not "below the root"
  std::set<std::string> roots;
  for (auto& w : ws.extraWatchRoots()) roots.insert(pathKeyUtf8(w.dir.lexically_relative(b.path)));
  CHECK(has(roots, "Engine"));
  CHECK(!has(roots, "Game"));
  FolderRule r = parseFolderRule(pathKeyUtf8(b.third()) + "/inc/**/*.h", b.game());
  CHECK(r.matches(b.third() / "inc" / "x" / "t.h", [](const std::string&) { return false; }));
  CHECK(r.matches(pathFromUtf8(pathKeyUtf8(b.third()) + "/inc/t.h"), [](const std::string&) { return false; }));
}
