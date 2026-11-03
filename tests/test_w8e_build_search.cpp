// W8e: RebuildActiveWorkspace / BuildCurrentFile command lines per build system (with fake build tools: the builds
// that run print GCC-style messages through `cmake -E echo`) and replace in files over open and closed files.
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "app/Editor.h"
#include "build/BuildRunner.h"
#include "core/PathUtil.h"
#include "core/Platform.h"
#include "search/ReplaceInFiles.h"
#include "search/TextSearch.h"
#include "test_main.h"

using namespace tf;
namespace fs = std::filesystem;

namespace {

struct TempDir {
  fs::path path;
  TempDir() {
    static int n = 0;
    path = fs::temp_directory_path() /
           ("tenfold_w8e_" + std::to_string(tf::currentProcessId()) + "_" + std::to_string(n++));
    fs::remove_all(path);
    fs::create_directories(path);
    std::error_code ec;
    path = fs::canonical(path, ec);
  }
  ~TempDir() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
};

void writeFile(const fs::path& p, const std::string& text) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary) << text;
}

std::string readFile(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

Editor::Options quietOptions() {
  Editor::Options o;
  o.semantic.useClangd = false;
  return o;
}

BuildOverrides kind(BuildOverrides::Kind k, const fs::path& file = {}) {
  BuildOverrides o;
  o.kind = k;
  o.file = file;
  return o;
}

bool has(const std::vector<std::string>& v, const std::string& s) {
  return std::find(v.begin(), v.end(), s) != v.end();
}

std::string sampleSolution(const std::string& projectMapping = "Debug|x64") {
  return "Microsoft Visual Studio Solution File, Format Version 12.00\n"
         "Project(\"{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}\") = \"App\", \"App\\App.vcxproj\", "
         "\"{11111111-2222-3333-4444-555555555555}\"\nEndProject\n"
         "Global\n\tGlobalSection(SolutionConfigurationPlatforms) = preSolution\n\t\tDebug|x64 = Debug|x64\n"
         "\tEndGlobalSection\n\tGlobalSection(ProjectConfigurationPlatforms) = postSolution\n"
         "\t\t{11111111-2222-3333-4444-555555555555}.Debug|x64.ActiveCfg = " +
         projectMapping + "\n\t\t{11111111-2222-3333-4444-555555555555}.Debug|x64.Build.0 = " + projectMapping +
         "\n\tEndGlobalSection\nEndGlobal\n";
}

const char* kVcxproj =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<Project "
    "xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">\n"
    "  <ItemGroup>\n    <ClCompile Include=\"src\\main.cpp\" />\n    <ClCompile Include=\"util.cpp\" />\n  "
    "</ItemGroup>\n"
    "</Project>\n";

}  // namespace

// ---------------------------------------------------------------------------
// RebuildActiveWorkspace
// ---------------------------------------------------------------------------

TEST(w8e_rebuild_command_per_build_system) {
  TempDir td;
  Workspace ws(td.path);
  BuildCommand c;
  std::string err;
  CHECK(!detectBuildCommand(ws, kind(BuildOverrides::Kind::Rebuild), &c, &err));
  CHECK(!err.empty());

  writeFile(td.path / "Makefile", "all:\n");  // make clean, then make
  REQUIRE(detectBuildCommand(ws, kind(BuildOverrides::Kind::Rebuild), &c, &err));
  CHECK(c.exe.empty());
  CHECK(c.shell.starts_with("make clean && make -j"));
  CHECK_EQ(c.cwd, td.path);
  CHECK_EQ(c.display, c.shell);

  writeFile(td.path / "CMakeLists.txt", "project(x)\n");  // cmake --build --clean-first
  ws.settings().vsConfiguration = "Release";
  ws.settings().buildDir = "out-dir";
  REQUIRE(detectBuildCommand(ws, kind(BuildOverrides::Kind::Rebuild), &c, &err));
  CHECK_EQ(c.exe, "cmake");
  REQUIRE(c.args.size() == 5);
  CHECK_EQ(c.args[0], "--build");
  CHECK_EQ(fs::path(c.args[1]), td.path / "out-dir");
  CHECK_EQ(c.args[2], "--config");
  CHECK_EQ(c.args[3], "Release");
  CHECK_EQ(c.args[4], "--clean-first");

  writeFile(td.path / "App.sln", "");  // msbuild /t:Rebuild with the active configuration and platform
  ws.settings().vsPlatform = "ARM64";
  REQUIRE(detectBuildCommand(ws, kind(BuildOverrides::Kind::Rebuild), &c, &err));
  CHECK_EQ(c.exe, "msbuild");
  REQUIRE(c.args.size() == 5);
  CHECK_EQ(fs::path(c.args[0]).filename(), "App.sln");
  CHECK_EQ(c.args[1], "/t:Rebuild");
  CHECK_EQ(c.args[2], "/m");
  CHECK_EQ(c.args[3], "/p:Configuration=Release");
  CHECK_EQ(c.args[4], "/p:Platform=ARM64");
  BuildOverrides o = kind(BuildOverrides::Kind::Rebuild);
  o.configuration = "Debug";  // an explicit configuration wins
  REQUIRE(detectBuildCommand(ws, o, &c, &err));
  CHECK_EQ(c.args[3], "/p:Configuration=Debug");
  // The plain build is unchanged.
  REQUIRE(detectBuildCommand(ws, {}, &c, &err));
  CHECK(!has(c.args, "/t:Rebuild"));
}

TEST(w8e_rebuild_custom_command) {
  TempDir td;
  writeFile(td.path / "Makefile", "all:\n");
  Workspace ws(td.path);
  BuildCommand c;
  std::string err;
  // A custom build command cannot be cleaned: a rebuild needs its own command.
  ws.settings().buildCommand = "ninja -C out";
  CHECK(!detectBuildCommand(ws, kind(BuildOverrides::Kind::Rebuild), &c, &err));
  CHECK(err.find("rebuildCommand") != std::string::npos);
  ws.settings().rebuildCommand = "ninja -C out -t clean && ninja -C out";
  REQUIRE(detectBuildCommand(ws, kind(BuildOverrides::Kind::Rebuild), &c, &err));
  CHECK_EQ(c.shell, "ninja -C out -t clean && ninja -C out");
  CHECK_EQ(c.cwd, td.path);
  BuildOverrides o = kind(BuildOverrides::Kind::Rebuild);
  o.rebuildCommand = "custom-rebuild.sh";
  REQUIRE(detectBuildCommand(ws, o, &c, &err));
  CHECK_EQ(c.shell, "custom-rebuild.sh");
  // The setting alone also overrides the detected system.
  ws.settings().buildCommand.clear();
  REQUIRE(detectBuildCommand(ws, kind(BuildOverrides::Kind::Rebuild), &c, &err));
  CHECK_EQ(c.shell, "ninja -C out -t clean && ninja -C out");

  Workspace saved(td.path);
  saved.settings().rebuildCommand = "make fresh";
  REQUIRE(saved.saveSettings());
  Workspace loaded(td.path);
  REQUIRE(loaded.loadSettings());
  CHECK_EQ(loaded.settings().rebuildCommand, "make fresh");
}

// ---------------------------------------------------------------------------
// BuildCurrentFile
// ---------------------------------------------------------------------------

TEST(w8e_build_file_msbuild_single_file_compile) {
  TempDir td;
  writeFile(td.path / "App.sln", sampleSolution());
  writeFile(td.path / "App" / "App.vcxproj", kVcxproj);
  writeFile(td.path / "App" / "src" / "main.cpp", "int main() {}\n");
  writeFile(td.path / "App" / "util.cpp", "int u;\n");
  writeFile(td.path / "App" / "other.cpp", "int o;\n");
  Workspace ws(td.path);
  ws.settings().vsConfiguration = "Debug";
  ws.settings().vsPlatform = "x64";
  BuildCommand c;
  std::string err;
  const fs::path main = td.path / "App" / "src" / "main.cpp";
  REQUIRE(detectBuildCommand(ws, kind(BuildOverrides::Kind::CurrentFile, main), &c, &err));
  CHECK_EQ(c.exe, "msbuild");
  REQUIRE(c.args.size() == 5);
  CHECK_EQ(fs::path(c.args[0]).filename(), "App.vcxproj");
  CHECK_EQ(c.args[1], "/t:ClCompile");
  CHECK_EQ(c.args[2], "/p:SelectedFiles=" + main.string());
  CHECK_EQ(c.args[3], "/p:Configuration=Debug");
  CHECK_EQ(c.args[4], "/p:Platform=x64");
  CHECK_EQ(c.cwd, td.path);

  // Backslash-relative includes resolve like MSBuild's; a relative file name is taken from the workspace root.
  REQUIRE(detectBuildCommand(ws, kind(BuildOverrides::Kind::CurrentFile, td.path / "App" / "util.cpp"), &c, &err));
  CHECK_EQ(c.args[2], "/p:SelectedFiles=" + (td.path / "App" / "util.cpp").string());
  // A file no project compiles is an error, not a build of the whole solution.
  CHECK(!detectBuildCommand(ws, kind(BuildOverrides::Kind::CurrentFile, td.path / "App" / "other.cpp"), &c, &err));
  CHECK(err.find("other.cpp") != std::string::npos);
  CHECK(!detectBuildCommand(ws, kind(BuildOverrides::Kind::CurrentFile), &c, &err));  // no file
  // A custom build command does not apply to one file.
  ws.settings().buildCommand = "echo custom";
  REQUIRE(detectBuildCommand(ws, kind(BuildOverrides::Kind::CurrentFile, main), &c, &err));
  CHECK_EQ(c.exe, "msbuild");
}

TEST(w8e_build_file_msbuild_maps_solution_configuration) {
  TempDir td;
  writeFile(td.path / "App.sln", sampleSolution("Debug|Win32"));  // the solution's x64 is the project's Win32
  writeFile(td.path / "App" / "App.vcxproj", kVcxproj);
  Workspace ws(td.path);
  BuildCommand c;
  std::string err;
  REQUIRE(detectBuildCommand(ws, kind(BuildOverrides::Kind::CurrentFile, td.path / "App" / "util.cpp"), &c, &err));
  CHECK_EQ(c.args[3], "/p:Configuration=Debug");
  CHECK_EQ(c.args[4], "/p:Platform=Win32");
}

TEST(w8e_build_file_lone_vcxproj) {
  TempDir td;
  writeFile(td.path / "Lib.vcxproj", kVcxproj);
  Workspace ws(td.path);
  ws.settings().vsConfiguration = "Release";
  BuildCommand c;
  std::string err;
  REQUIRE(detectBuildCommand(ws, kind(BuildOverrides::Kind::CurrentFile, td.path / "util.cpp"), &c, &err));
  CHECK_EQ(c.exe, "msbuild");
  CHECK_EQ(fs::path(c.args[0]).filename(), "Lib.vcxproj");
  CHECK_EQ(c.args[3], "/p:Configuration=Release");
}

TEST(w8e_build_file_compile_database) {
  TempDir td;
  writeFile(td.path / "CMakeLists.txt", "project(x)\n");
  writeFile(td.path / "src" / "a.cpp", "int a;\n");
  writeFile(td.path / "src" / "b.cpp", "int b;\n");
  writeFile(td.path / "src" / "c.cpp", "int c;\n");
  const fs::path bld = td.path / "build";
  fs::create_directories(bld);
  std::vector<CompileCommand> cmds;
  cmds.push_back(
      {bld,
       td.path / "src" / "a.cpp",
       {"/usr/bin/c++", "-Isrc", "-c", (td.path / "src" / "a.cpp").string(), "-o", "CMakeFiles/x.dir/src/a.cpp.o"}});
  cmds.push_back({bld, td.path / "src" / "b.cpp", {"clang++", "-c", "../src/b.cpp", "-o", "b.o"}});
  REQUIRE(CompileDatabase::save(cmds, bld / "compile_commands.json"));
  Workspace ws(td.path);
  BuildCommand c;
  std::string err;

  // No build.ninja: the entry's own command line, run in its directory (a real compile, not -fsyntax-only).
  REQUIRE(detectBuildCommand(ws, kind(BuildOverrides::Kind::CurrentFile, td.path / "src" / "b.cpp"), &c, &err));
  CHECK_EQ(c.exe, "clang++");
  CHECK_EQ(c.cwd, bld);
  REQUIRE(c.args.size() == 4);
  CHECK_EQ(c.args[2], "-o");
  CHECK_EQ(c.args[3], "b.o");
  CHECK(!has(c.args, "-fsyntax-only"));

  // A Ninja build directory: build the object target.
  writeFile(bld / "build.ninja", "# ninja\n");
  REQUIRE(detectBuildCommand(ws, kind(BuildOverrides::Kind::CurrentFile, td.path / "src" / "a.cpp"), &c, &err));
  CHECK_EQ(c.exe, "ninja");
  REQUIRE(c.args.size() == 3);
  CHECK_EQ(c.args[0], "-C");
  CHECK_EQ(fs::path(c.args[1]), bld);
  CHECK_EQ(c.args[2], "CMakeFiles/x.dir/src/a.cpp.o");

  // Not in the database.
  CHECK(!detectBuildCommand(ws, kind(BuildOverrides::Kind::CurrentFile, td.path / "src" / "c.cpp"), &c, &err));
  CHECK(err.find("c.cpp") != std::string::npos);
}

TEST(w8e_build_file_no_compile_database) {
  TempDir td;
  writeFile(td.path / "a.cpp", "int a;\n");
  Workspace ws(td.path);
  BuildCommand c;
  std::string err;
  CHECK(!detectBuildCommand(ws, kind(BuildOverrides::Kind::CurrentFile, td.path / "a.cpp"), &c, &err));
  CHECK(!err.empty());
}

TEST(w8e_build_file_results_reach_the_editor_messages) {
  TempDir td;
  writeFile(td.path / "src" / "a.cpp", "int a;\nint bad_name;\n");
  const fs::path bld = td.path / "build";
  fs::create_directories(bld);
  // A fake compiler: cmake -E echo prints a GCC-style error that is relative to the entry's directory.
  std::vector<CompileCommand> cmds;
  cmds.push_back({bld, td.path / "src" / "a.cpp", {"cmake", "-E", "echo", "../src/a.cpp:2:5: error: bad"}});
  REQUIRE(CompileDatabase::save(cmds, bld / "compile_commands.json"));
  Editor ed(td.path, quietOptions());
  REQUIRE(ed.start());
  bool done = false;
  std::string err;
  REQUIRE(ed.build(
      kind(BuildOverrides::Kind::CurrentFile, td.path / "src" / "a.cpp"), {}, [&](const BuildResult&) { done = true; },
      &err));
  REQUIRE(ed.waitFor([&] { return done; }, 20000));
  REQUIRE(ed.buildMessages().size() == 1);
  CHECK_EQ(ed.buildMessages()[0].line, 2);
  const auto m = ed.nextBuildMessage();
  REQUIRE(m.has_value());
  CHECK_EQ(pathKey(ed.cursor().file), pathKey(td.path / "src" / "a.cpp"));
  CHECK_EQ(ed.cursor().pos.line, 1);
  CHECK_EQ(ed.buildDiagnostics(td.path / "src" / "a.cpp").size(), size_t(1));
}

// ---------------------------------------------------------------------------
// Replace in files
// ---------------------------------------------------------------------------

namespace {
std::vector<SearchMatch> findAll(const fs::path& root, const std::string& query, bool cs = true) {
  SearchInput in;
  in.root = root;
  for (auto& e : fs::recursive_directory_iterator(root))
    if (e.is_regular_file() && e.path().extension() == ".cpp") in.files.push_back(e.path());
  SearchOptions o;
  o.query = query;
  o.caseSensitive = cs;
  std::vector<SearchMatch> out;
  TextSearch::run(std::move(in), o, &out);
  return out;
}
}  // namespace

TEST(w8e_replace_matches_in_text) {
  const std::string text = "alpha beta alpha\r\nno\r\nalpha\n";
  SearchInput in;
  in.root = "r";
  in.files = {fs::path("r") / "t"};
  in.overrides[pathKey("t", nativePathStyle())] = text;
  SearchOptions o;
  o.query = "alpha";
  std::vector<SearchMatch> ms;
  TextSearch::run(std::move(in), o, &ms);
  REQUIRE(ms.size() == 3);
  ReplaceCounts n;
  // Order does not matter; line endings (CRLF and LF) are kept.
  std::reverse(ms.begin(), ms.end());
  CHECK_EQ(replaceMatchesInText(text, ms, "X", &n), std::string("X beta X\r\nno\r\nX\n"));
  CHECK_EQ(n.replaced, size_t(3));
  CHECK_EQ(n.stale, size_t(0));
  // An empty replacement deletes.
  CHECK_EQ(replaceMatchesInText(text, ms, ""), std::string(" beta \r\nno\r\n\n"));
  // A longer or shorter replacement does not disturb the later matches of the same line.
  CHECK_EQ(replaceMatchesInText(text, ms, "longer text"),
           std::string("longer text beta longer text\r\nno\r\nlonger text\n"));

  // The text changed since the search: a changed line is left alone (all its matches are stale), the others apply.
  ReplaceCounts n2;
  const std::string changed = "alphA beta alpha\r\nno\r\nalpha\n";
  CHECK_EQ(replaceMatchesInText(changed, ms, "X", &n2), std::string("alphA beta alpha\r\nno\r\nX\n"));
  CHECK_EQ(n2.replaced, size_t(1));
  CHECK_EQ(n2.stale, size_t(2));
  // Fewer lines than before: nothing out of range is touched.
  ReplaceCounts n3;
  CHECK_EQ(replaceMatchesInText("alpha beta alpha\r\n", ms, "X", &n3), std::string("X beta X\r\n"));
  CHECK_EQ(n3.replaced, size_t(2));
  CHECK_EQ(n3.stale, size_t(1));
}

TEST(w8e_replace_matches_in_file_writes_atomically) {
  TempDir td;
  writeFile(td.path / "f.cpp", "one two one\n");
  const auto ms = findAll(td.path, "one");
  REQUIRE(ms.size() == 2);
  ReplaceCounts n;
  std::string err;
  REQUIRE(replaceMatchesInFile(td.path / "f.cpp", ms, "1", &n, &err));
  CHECK_EQ(readFile(td.path / "f.cpp"), std::string("1 two 1\n"));
  CHECK_EQ(n.replaced, size_t(2));
  CHECK(!fs::exists(td.path / "f.cpp.tenfold-replace"));
  // Applying the same (now stale) matches again changes nothing.
  ReplaceCounts again;
  REQUIRE(replaceMatchesInFile(td.path / "f.cpp", ms, "1", &again, &err));
  CHECK_EQ(again.replaced, size_t(0));
  CHECK_EQ(again.stale, size_t(2));
  CHECK_EQ(readFile(td.path / "f.cpp"), std::string("1 two 1\n"));
  CHECK(!replaceMatchesInFile(td.path / "missing.cpp", ms, "1", nullptr, &err));
  CHECK(!err.empty());
}

TEST(w8e_replace_in_files_open_documents_and_closed_files) {
  TempDir td;
  writeFile(td.path / "a.cpp", "int foo = 1;\nint bar = foo + foo;\n");
  writeFile(td.path / "b.cpp", "foo();\r\nnothing\r\nfoo();\r\n");
  writeFile(td.path / "sub" / "c.cpp", "// foo here\n");
  Editor ed(td.path, quietOptions());
  REQUIRE(ed.start());
  std::string err;
  Document* a = ed.open(td.path / "a.cpp", &err);
  REQUIRE(a != nullptr);

  const std::vector<SearchMatch> ms = findAll(td.path, "foo");
  REQUIRE(ms.size() == 6);
  const std::string aBefore = a->buffer().text();

  const Editor::ReplaceInFilesResult r = ed.replaceInFiles(ms, "baz");
  CHECK(r.errors.empty());
  CHECK_EQ(r.replaced, size_t(6));
  CHECK_EQ(r.stale, size_t(0));
  CHECK_EQ(r.openDocuments, size_t(1));
  CHECK_EQ(r.filesWritten, size_t(2));

  // The open document is edited in place (unsaved), the disk copy is untouched until it is saved.
  CHECK_EQ(a->buffer().text(), std::string("int baz = 1;\nint bar = baz + baz;\n"));
  CHECK(a->dirty());
  CHECK_EQ(readFile(td.path / "a.cpp"), aBefore);
  // The closed files are written, CRLF kept.
  CHECK_EQ(readFile(td.path / "b.cpp"), std::string("baz();\r\nnothing\r\nbaz();\r\n"));
  CHECK_EQ(readFile(td.path / "sub" / "c.cpp"), std::string("// baz here\n"));
  CHECK(ed.document(td.path / "b.cpp") == nullptr);  // not opened by the replacement

  // One undo step for the whole document (three matches), one redo.
  ed.activateDocument(*a);
  CHECK(ed.undo());
  CHECK_EQ(a->buffer().text(), aBefore);
  CHECK(ed.redo());
  CHECK_EQ(a->buffer().text(), std::string("int baz = 1;\nint bar = baz + baz;\n"));

  // Saving the document writes the replaced text.
  REQUIRE(ed.save(*a));
  CHECK_EQ(readFile(td.path / "a.cpp"), std::string("int baz = 1;\nint bar = baz + baz;\n"));
}

TEST(w8e_replace_in_files_skips_stale_matches_and_subsets) {
  TempDir td;
  writeFile(td.path / "a.cpp", "foo\nfoo\nfoo\n");
  writeFile(td.path / "b.cpp", "foo bar\nfoo\n");
  Editor ed(td.path, quietOptions());
  REQUIRE(ed.start());
  Document* a = ed.open(td.path / "a.cpp");
  REQUIRE(a != nullptr);
  std::vector<SearchMatch> ms = findAll(td.path, "foo");
  REQUIRE(ms.size() == 5);

  // The selected ones only: the first match of every file stays out.
  std::vector<SearchMatch> chosen;
  for (const SearchMatch& m : ms)
    if (m.line > 0) chosen.push_back(m);
  REQUIRE(chosen.size() == 3);

  // The open document changed after the search: its first remaining line no longer matches.
  ed.activateDocument(*a);
  ed.setSelections({Selection{{1, 0}, {1, 3}}});
  ed.insertText("fox");
  const auto r = ed.replaceInFiles(chosen, "X");
  CHECK_EQ(r.replaced, size_t(2));
  CHECK_EQ(r.stale, size_t(1));
  CHECK_EQ(a->buffer().text(), std::string("foo\nfox\nX\n"));
  CHECK_EQ(readFile(td.path / "b.cpp"), std::string("foo bar\nX\n"));
}
