// SLNX: .slnx (Visual Studio's XML solution file) is understood wherever a .sln is: the parser (configurations and
// their defaults, solution folders, project -> configuration rules with wildcards, Build=false, paths, malformed
// input), the compile commands (identical to the equivalent .sln), the configuration switch, the workspace's solution
// list, the build command and the CLI. Rules and sources: docs/ARCHITECTURE.md "Solution files (.sln and .slnx)".
#include <algorithm>
#include <fstream>
#include <map>
#include <set>

#include "app/Editor.h"
#include "build/BuildRunner.h"
#include "core/PathUtil.h"
#include "semantic/CompileCommands.h"
#include "test_main.h"
#include "workspace/Workspace.h"

using namespace tf;
namespace fs = std::filesystem;

namespace {

const fs::path kPair = fs::path(TENFOLD_SOURCE_DIR) / "tests/fixtures/slnx/Pair";

fs::path freshDir(const std::string& name) {
  fs::path d = fs::weakly_canonical(fs::temp_directory_path()) / ("tenfold_slnx_" + name);
  std::error_code ec;
  fs::remove_all(d, ec);
  fs::create_directories(d);
  return d;
}

void write(const fs::path& p, const std::string& text) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary) << text;
}

struct Dir {
  fs::path root;
  explicit Dir(const std::string& name) : root(freshDir(name)) {}
  ~Dir() {
    std::error_code ec;
    fs::remove_all(root, ec);
  }
  // Writes `<root>/s.slnx` and reads it back.
  compdb::SolutionInfo read(const std::string& xml, std::string* err = nullptr) const {
    write(root / "s.slnx", xml);
    return compdb::readSolution(root / "s.slnx", err);
  }
};

const compdb::SolutionProject* project(const compdb::SolutionInfo& i, const std::string& name) {
  for (auto& p : i.projects)
    if (p.name == name) return &p;
  return nullptr;
}

// "config|platform" the solution maps `name` to in `cfg` ("" when unmapped), with "!" appended when it is not built.
std::string mapped(const compdb::SolutionInfo& i, const std::string& name, const std::string& cfg) {
  const auto* p = project(i, name);
  if (!p) return "no such project";
  auto m = i.mapping(*p, compdb::parseSolutionConfiguration(cfg));
  if (!m) return "";
  return m->configuration + "|" + m->platform + (m->build ? "" : "!");
}

std::string names(const std::vector<compdb::SolutionConfiguration>& v) {
  std::string out;
  for (auto& c : v) out += (out.empty() ? "" : ", ") + c.name();
  return out;
}

compdb::VcxprojOptions hermetic(const std::string& cfg, const std::string& plat) {
  compdb::VcxprojOptions o;
  o.configuration = cfg;
  o.platform = plat;
  o.useProcessEnvironment = false;
  return o;
}

Editor::Options noClangd() {
  Editor::Options o;
  o.semantic.useClangd = false;
  return o;
}

bool switchTo(Editor& ed, const std::string& name) {
  bool applied = false;
  std::string err;
  if (!ed.selectVsConfiguration(name, &err, [&] { applied = true; })) return false;
  return ed.waitFor([&] { return applied; }, 60000);
}

}  // namespace

// ---------------------------------------------------------------------------
// Configurations and their defaults
// ---------------------------------------------------------------------------

TEST(slnx_without_configurations_has_debug_and_release_for_any_cpu) {
  Dir d("defaults");
  std::string err;
  auto i = d.read("<Solution><Project Path=\"a/a.vcxproj\" /></Solution>", &err);
  CHECK_EQ(err, "");
  CHECK_EQ(names(i.configurations), "Debug|Any CPU, Release|Any CPU");
  CHECK(i.isSlnx);
  CHECK(i.hasProjectConfigurationMap);
  // A native project's platform for "Any CPU" is x64 (Visual Studio's built-in C++ rule); the build type is the same.
  CHECK_EQ(mapped(i, "a", "Debug|Any CPU"), "Debug|x64");
  CHECK_EQ(mapped(i, "a", "release|any cpu"), "Release|x64");
}

TEST(slnx_platforms_only_gives_debug_and_release_for_each_platform) {
  Dir d("platforms");
  auto i = d.read(
      "<Solution><Configurations><Platform Name=\"x64\" /><Platform Name=\"x86\" /><Platform Name=\"ARM64\" />"
      "</Configurations><Project Path=\"a.vcxproj\" /></Solution>");
  CHECK_EQ(names(i.configurations), "Debug|x64, Debug|x86, Debug|ARM64, Release|x64, Release|x86, Release|ARM64");
  CHECK_EQ(mapped(i, "a", "Debug|x64"), "Debug|x64");
  CHECK_EQ(mapped(i, "a", "Debug|x86"), "Debug|Win32");  // the built-in rule: solution x86 is the project's Win32
  CHECK_EQ(mapped(i, "a", "Release|ARM64"), "Release|ARM64");
}

TEST(slnx_build_types_only_gives_any_cpu_and_custom_names_are_kept) {
  Dir d("buildtypes");
  auto i = d.read(
      "<Solution><Configurations><BuildType Name=\"Debug\" /><BuildType Name=\"Profile\" /><BuildType Name=\"debug\" />"
      "</Configurations><Project Path=\"a.vcxproj\" /></Solution>");
  CHECK_EQ(names(i.configurations), "Debug|Any CPU, Profile|Any CPU");  // case-insensitive duplicates collapse
  CHECK_EQ(mapped(i, "a", "Profile|Any CPU"), "Profile|x64");
}

TEST(slnx_a_solution_without_projects_has_no_configurations) {
  Dir d("empty");
  std::string err;
  auto i = d.read("<Solution />", &err);
  CHECK_EQ(err, "");
  CHECK(i.configurations.empty());
  CHECK(i.projects.empty());
  auto j = d.read("<Solution><Configurations><Platform Name=\"x64\" /></Configurations></Solution>");
  CHECK_EQ(names(j.configurations), "");  // BuildTypes stay empty without a project: no Debug|x64
}

TEST(slnx_solution_configuration_order_is_build_type_major) {
  Dir d("order");
  auto i = d.read(
      "<Solution><Configurations><BuildType Name=\"Release\" /><BuildType Name=\"Debug\" /><Platform Name=\"x86\" />"
      "<Platform Name=\"x64\" /></Configurations><Project Path=\"a.vcxproj\" /></Solution>");
  CHECK_EQ(names(i.configurations), "Release|x86, Release|x64, Debug|x86, Debug|x64");
  auto pick = compdb::pickSolutionConfiguration(i.configurations, "Debug", "x64");
  CHECK_EQ(pick.name(), "Debug|x64");
  CHECK_EQ(compdb::pickSolutionConfiguration(i.configurations, "Nope", "x64").name(), "Debug|x64");
}

// ---------------------------------------------------------------------------
// Solution folders, solution items, project paths
// ---------------------------------------------------------------------------

TEST(slnx_folders_nest_by_path_and_hold_projects_and_files) {
  Dir d("folders");
  std::string err;
  auto i = d.read(
      "<Solution>\n"
      "  <Folder Name=\"/Libs/Core/\"><File Path=\"docs/core.md\" /><Project Path=\"core/core.vcxproj\" /></Folder>\n"
      "  <Folder Name=\"/Solution Items/\"><File Path=\"..\\readme.txt\" /></Folder>\n"
      "  <Project Path=\"app/app.vcxproj\" />\n"
      "</Solution>",
      &err);
  CHECK_EQ(err, "");
  const auto* libs = project(i, "Libs");  // the parent folder exists although only /Libs/Core/ was declared
  const auto* core = project(i, "Core");
  const auto* items = project(i, "Solution Items");
  REQUIRE(libs != nullptr);
  REQUIRE(core != nullptr);
  REQUIRE(items != nullptr);
  CHECK_EQ(libs->typeGuid, "{2150E333-8FDC-42A3-9474-1A3956D46DE8}");
  CHECK_EQ(i.nestedIn[core->guid], libs->guid);
  CHECK(i.nestedIn.count(libs->guid) == 0);  // a top level folder
  const auto* coreProj = project(i, "core");
  REQUIRE(coreProj != nullptr);
  CHECK_EQ(i.nestedIn[coreProj->guid], core->guid);
  CHECK(i.nestedIn.count(project(i, "app")->guid) == 0);
  REQUIRE(i.folderItems[core->guid].size() == 1u);
  CHECK_EQ(i.folderItems[core->guid][0], "docs/core.md");
  CHECK_EQ(i.folderItems[items->guid][0], "..\\readme.txt");
  // Folders carry no configuration mapping and are no projects to evaluate.
  CHECK_EQ(compdb::projectsInSolution(d.root / "s.slnx").size(), 2u);
  CHECK_EQ(project(i, "app")->path, d.root / "app/app.vcxproj");
}

TEST(slnx_folder_names_must_start_and_end_with_a_slash) {
  Dir d("badfolder");
  std::string err;
  auto i = d.read("<Solution><Folder Name=\"No/Start/\" /><Folder Name=\"/Fine/\" /></Solution>", &err);
  CHECK(err.find("invalid folder name") != std::string::npos);
  CHECK(project(i, "Fine") != nullptr);
  CHECK(project(i, "Start") == nullptr);
}

TEST(slnx_project_paths_are_relative_to_the_file_and_take_both_separators) {
  Dir d("paths");
  std::string err;
  auto i = d.read(
      "<Solution>\n"
      "  <Project Path=\"folder/forward.vcxproj\" />\n"
      "  <Project Path=\"folder\\back.vcxproj\" />\n"
      "  <Project Path=\"folder\\inner/mixed.vcxproj\" />\n"
      "  <Project Path=\"../up/Up.vcxproj\" DisplayName=\"Shown\" />\n"
      "  <Project Path=\"https://example.com/site/\" Type=\"Website\" DisplayName=\"Web\" />\n"
      "</Solution>",
      &err);
  CHECK_EQ(err, "");
  CHECK_EQ(project(i, "forward")->path, d.root / "folder/forward.vcxproj");
  CHECK_EQ(project(i, "back")->path, d.root / "folder/back.vcxproj");
  CHECK_EQ(project(i, "mixed")->path, d.root / "folder/inner/mixed.vcxproj");
  CHECK_EQ(project(i, "Shown")->path, (d.root / "../up/Up.vcxproj").lexically_normal());  // DisplayName is the name
  CHECK(project(i, "Web") != nullptr);
  CHECK(!project(i, "Web")->isVcxproj());
  CHECK_EQ(compdb::projectsInSolution(d.root / "s.slnx").size(), 4u);
}

TEST(slnx_project_id_and_type_are_kept_and_default_startup_goes_first) {
  Dir d("ids");
  auto i = d.read(
      "<Solution>\n"
      "  <Project Path=\"a.vcxproj\" />\n"
      "  <Project Path=\"b.vcxproj\" Id=\"aaaaaaaa-0000-0000-0000-000000000009\" DefaultStartup=\"true\" />\n"
      "  <Project Path=\"c.csproj\" Type=\"C#\" />\n"
      "</Solution>");
  REQUIRE(i.projects.size() == 3u);
  CHECK_EQ(i.projects[0].name, "b");  // the startup project is the first one
  CHECK_EQ(i.projects[0].guid, "{AAAAAAAA-0000-0000-0000-000000000009}");
  CHECK_EQ(i.projects[0].typeGuid, "{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}");
  CHECK_EQ(i.projects[2].typeGuid, "{FAE04EC0-301F-11D3-BF4B-00C04F79EFBC}");
  CHECK_EQ(mapped(i, "b", "Debug|Any CPU"), "Debug|x64");  // the mapping follows its project after the move
}

// ---------------------------------------------------------------------------
// Project -> configuration rules
// ---------------------------------------------------------------------------

TEST(slnx_rules_use_wildcards_and_the_later_rule_wins) {
  Dir d("rules");
  // The shapes of Microsoft's own sample: a native project with a platform only the solution has, and no build for one.
  std::string err;
  auto i = d.read(
      "<Solution>\n"
      "  <Configurations><Platform Name=\"ARM64\" /><Platform Name=\"Mobile\" /><Platform Name=\"x64\" />"
      "<Platform Name=\"x86\" /></Configurations>\n"
      "  <Project Path=\"Native.vcxproj\">\n"
      "    <Platform Solution=\"*|Mobile\" Project=\"ARM64\" />\n"
      "    <Build Solution=\"*|ARM64\" Project=\"false\" />\n"
      "  </Project>\n"
      "  <Project Path=\"Rel.vcxproj\">\n"
      "    <BuildType Solution=\"Release|*\" Project=\"Debug\" />\n"
      "    <Platform Solution=\"*|*\" Project=\"x64\" />\n"
      "    <Platform Solution=\"Release|x86\" Project=\"Win32\" />\n"
      "    <Build Solution=\"Release|*\" Project=\"false\" />\n"
      "    <Build Solution=\"Release|x64\" />\n"
      "  </Project>\n"
      "</Solution>",
      &err);
  CHECK_EQ(err, "");
  CHECK_EQ(mapped(i, "Native", "Debug|Mobile"), "Debug|ARM64");
  CHECK_EQ(mapped(i, "Native", "Release|Mobile"), "Release|ARM64");
  CHECK_EQ(mapped(i, "Native", "Debug|ARM64"), "Debug|ARM64!");  // mapped, not built
  CHECK_EQ(mapped(i, "Native", "Debug|x86"), "Debug|Win32");
  CHECK_EQ(mapped(i, "Native", "Debug|x64"), "Debug|x64");
  CHECK_EQ(mapped(i, "Rel", "Release|x86"), "Debug|Win32!");  // later Platform rule beats "*|*"; Build=false stands
  CHECK_EQ(mapped(i, "Rel", "Release|x64"), "Debug|x64");     // <Build/> without Project means true: it wins later
  CHECK_EQ(mapped(i, "Rel", "Debug|x86"), "Debug|x64");
  CHECK_EQ(mapped(i, "Rel", "Debug|ARM64"), "Debug|x64");
}

TEST(slnx_star_as_a_project_value_means_the_solution_value_and_names_match_case_insensitively) {
  Dir d("star");
  auto i = d.read(
      "<Solution><Configurations><BuildType Name=\"Debug\" /><BuildType Name=\"Profile\" /><Platform Name=\"x64\" />"
      "<Platform Name=\"x86\" /></Configurations>\n"
      "  <Project Path=\"a.vcxproj\">\n"
      "    <BuildType Solution=\"profile|X86\" Project=\"Debug\" />\n"
      "    <Platform Solution=\"Debug|*\" Project=\"*\" />\n"
      "  </Project>\n"
      "</Solution>");
  CHECK_EQ(mapped(i, "a", "Profile|x86"), "Debug|Win32");  // the rule's names match case-insensitively
  CHECK_EQ(mapped(i, "a", "Debug|x86"), "Debug|x86");      // Platform "*" is the solution's own name, not Win32
  CHECK_EQ(mapped(i, "a", "Profile|x64"), "Profile|x64");
}

TEST(slnx_any_cpu_and_anycpu_are_the_same_platform_in_rules) {
  Dir d("anycpu");
  auto i = d.read(
      "<Solution><Project Path=\"a.vcxproj\"><Platform Solution=\"*|AnyCPU\" Project=\"ARM64\" /></Project>"
      "</Solution>");
  CHECK_EQ(mapped(i, "a", "Debug|Any CPU"), "Debug|ARM64");
}

TEST(slnx_rules_naming_a_configuration_the_solution_does_not_have_are_ignored) {
  Dir d("unknown");
  std::string err;
  auto i = d.read(
      "<Solution><Configurations><Platform Name=\"x64\" /></Configurations>\n"
      "  <Project Path=\"a.vcxproj\"><Build Solution=\"Nonesuch|x64\" Project=\"false\" />"
      "<Platform Solution=\"Debug|ia64\" Project=\"x86\" /></Project></Solution>",
      &err);
  CHECK_EQ(err, "");
  CHECK_EQ(mapped(i, "a", "Debug|x64"), "Debug|x64");
  CHECK_EQ(mapped(i, "a", "Release|x64"), "Release|x64");
}

TEST(slnx_build_false_projects_are_still_evaluated_for_intellisense) {
  Dir d("nobuild");
  write(d.root / "a/a.vcxproj", "<Project><ItemGroup><ClCompile Include=\"a.cpp\" /></ItemGroup></Project>");
  write(d.root / "a/a.cpp", "int a;\n");
  write(d.root / "s.slnx",
        "<Solution><Configurations><Platform Name=\"x64\" /></Configurations>"
        "<Project Path=\"a/a.vcxproj\"><Build Solution=\"Release|x64\" Project=\"false\" /></Project></Solution>");
  auto i = compdb::readSolution(d.root / "s.slnx");
  CHECK_EQ(mapped(i, "a", "Debug|x64"), "Debug|x64");
  CHECK_EQ(mapped(i, "a", "Release|x64"), "Release|x64!");
  // Same as an .sln with ActiveCfg but no Build.0: the project still gets commands in that configuration.
  auto cmds = compdb::fromSolution(d.root / "s.slnx", hermetic("Release", "x64"));
  CHECK_EQ(cmds.size(), 1u);
}

TEST(slnx_non_native_and_no_build_project_types_follow_visual_studios_defaults) {
  Dir d("types");
  auto i = d.read(
      "<Solution>\n"
      "  <Project Path=\"m/m.csproj\" /><Project Path=\"sh/sh.vcxitems\" /><Project Path=\"s/s.shproj\" />\n"
      "  <Project Path=\"w/w.vcxproj\" Type=\"8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942\" />\n"
      "</Solution>");
  CHECK_EQ(mapped(i, "m", "Debug|Any CPU"), "Debug|AnyCPU");    // CLR projects: their own spelling of Any CPU
  CHECK_EQ(mapped(i, "sh", "Debug|Any CPU"), "Debug|AnyCPU!");  // shared item projects are never built
  CHECK_EQ(mapped(i, "s", "Release|Any CPU"), "Release|AnyCPU!");
  CHECK_EQ(mapped(i, "w", "Debug|Any CPU"), "Debug|x64");
}

TEST(slnx_custom_project_types_apply_before_the_projects_own_rules) {
  Dir d("projtypes");
  std::string err;
  auto i = d.read(
      "<Solution>\n"
      "  <Configurations><Platform Name=\"x64\" /><Platform Name=\"x86\" />\n"
      "    <ProjectType Name=\"Mine\" Extension=\"mine\"><Platform Solution=\"*|x86\" Project=\"Fancy\" "
      "/></ProjectType>\n"
      "    <ProjectType Name=\"Child\" BasedOn=\"Mine\"><BuildType Solution=\"Release|*\" Project=\"Debug\" "
      "/></ProjectType>\n"
      "    <ProjectType Name=\"Nope\" IsBuildable=\"false\" />\n"
      "    <ProjectType><Build Solution=\"Release|x64\" Project=\"false\" /></ProjectType>\n"
      "  </Configurations>\n"
      "  <Project Path=\"a.mine\" /><Project Path=\"b.vcxproj\" Type=\"Child\" /><Project Path=\"c.vcxproj\" "
      "Type=\"Nope\" />\n"
      "  <Project Path=\"d.mine\"><Platform Solution=\"*|x86\" Project=\"Own\" /></Project>\n"
      "</Solution>",
      &err);
  CHECK_EQ(err, "");
  CHECK_EQ(mapped(i, "a", "Debug|x86"), "Debug|Fancy");
  CHECK_EQ(mapped(i, "b", "Release|x86"), "Debug|Fancy");  // BasedOn: Child inherits Mine's rule, then its own
  CHECK_EQ(mapped(i, "c", "Debug|x64"), "Debug|x64!");
  CHECK_EQ(mapped(i, "a", "Release|x64"), "Release|x64!");  // the solution's default type applies to every project
  CHECK_EQ(mapped(i, "d", "Debug|x86"), "Debug|Own");
}

// ---------------------------------------------------------------------------
// Malformed input
// ---------------------------------------------------------------------------

TEST(slnx_malformed_files_report_an_error_and_never_crash) {
  Dir d("bad");
  std::string err;
  auto i = d.read("<Solution><Project Path=\"a.vcxproj\"", &err);  // truncated
  CHECK(!err.empty());
  CHECK(i.projects.empty());
  err.clear();
  i = d.read("not xml at all", &err);
  CHECK(!err.empty());
  CHECK(i.projects.empty());
  err.clear();
  i = d.read("<Project><ItemGroup /></Project>", &err);  // a project file, not a solution
  CHECK(err.find("<Solution>") != std::string::npos);
  err.clear();
  i = d.read("", &err);
  CHECK(!err.empty());
  err.clear();
  i = compdb::readSolution(d.root / "missing.slnx", &err);
  CHECK(err.find("cannot open") != std::string::npos);
  CHECK(i.projects.empty());
  // An unreadable solution yields no commands and an error, not an exception.
  std::string cerr_;
  CHECK(compdb::fromSolution(d.root / "missing.slnx", hermetic("Debug", "x64"), &cerr_).empty());
  CHECK(!cerr_.empty());
}

TEST(slnx_invalid_rules_names_and_versions_are_reported) {
  Dir d("invalid");
  std::string err;
  auto i = d.read(
      "<Solution>\n"
      "  <Project Path=\"a.vcxproj\"><Build Solution=\"Debug\" Project=\"false\" />\n"
      "    <Build Solution=\"|x64\" Project=\"false\" /><BuildType Solution=\"Debug|*\" />\n"
      "    <Build Solution=\"Debug|*\" Project=\"maybe\" /></Project>\n"
      "  <Project Path=\"a.vcxproj\" /><Project />\n"
      "</Solution>",
      &err);
  CHECK(err.find("invalid configuration 'Debug'") != std::string::npos);
  CHECK(err.find("invalid configuration '|x64'") != std::string::npos);
  CHECK(err.find("<BuildType> needs a Project value") != std::string::npos);
  CHECK(err.find("invalid boolean 'maybe'") != std::string::npos);
  CHECK(err.find("duplicate project a.vcxproj") != std::string::npos);
  CHECK(err.find("without a Path") != std::string::npos);
  CHECK_EQ(i.projects.size(), 1u);
  CHECK_EQ(mapped(i, "a", "Debug|Any CPU"), "Debug|x64");  // the invalid rules changed nothing

  err.clear();
  i = d.read("<Solution Version=\"2.0\"><Project Path=\"a.vcxproj\" /></Solution>", &err);
  CHECK(err.find("unsupported") != std::string::npos);
  CHECK(i.projects.empty());
  err.clear();
  i = d.read("<Solution Version=\"v1.0\"><Project Path=\"a.vcxproj\" /></Solution>", &err);
  CHECK(err.find("invalid Version") != std::string::npos);
  CHECK_EQ(i.projects.size(), 1u);  // Visual Studio refuses it; tenfold still lists what it can
  err.clear();
  i = d.read("<Solution Version=\"1.0\" Description=\"x\"><Project Path=\"a.vcxproj\" /></Solution>", &err);
  CHECK_EQ(err, "");
}

TEST(slnx_a_basedon_loop_does_not_hang) {
  Dir d("loop");
  auto i = d.read(
      "<Solution><Configurations><ProjectType Name=\"A\" BasedOn=\"B\" /><ProjectType Name=\"B\" BasedOn=\"A\" />"
      "</Configurations><Project Path=\"x.vcxproj\" Type=\"A\" /></Solution>");
  CHECK_EQ(i.projects.size(), 1u);
}

TEST(slnx_properties_and_unknown_elements_are_ignored) {
  Dir d("props");
  std::string err;
  auto i = d.read(
      "<!-- comment --><Solution>\n"
      "  <Properties Name=\"Hello\"><Property Name=\"A\" Value=\"1\" /></Properties>\n"
      "  <Mystery /><Folder Name=\"/F/\"><Properties Name=\"X\" /><Mystery /></Folder>\n"
      "  <Project Path=\"a.vcxproj\"><BuildDependency Project=\"b.vcxproj\" /><Properties Name=\"P\" /></Project>\n"
      "</Solution>",
      &err);
  CHECK_EQ(err, "");
  CHECK(project(i, "a") != nullptr);
}

// ---------------------------------------------------------------------------
// Everywhere a solution is understood
// ---------------------------------------------------------------------------

TEST(slnx_is_a_solution_file_by_extension) {
  CHECK(compdb::isSolutionFile("x/App.slnx"));
  CHECK(compdb::isSolutionFile("x/App.SLNX"));
  CHECK(compdb::isSolutionFile("x/App.sln"));
  CHECK(!compdb::isSolutionFile("x/App.slnf"));
  CHECK(!compdb::isSolutionFile("x/App.vcxproj"));
  CHECK(!compdb::isSolutionFile("x/App"));
}

TEST(slnx_the_workspace_lists_it_next_to_sln_files) {
  Dir d("ws");
  write(d.root / "b.slnx", "<Solution />");
  write(d.root / "a.sln", "");
  write(d.root / "c.SLNX", "<Solution />");
  write(d.root / "sub/z.slnx", "<Solution />");  // only the root counts
  write(d.root / "d.slnf", "{}");
  Workspace ws(d.root);
  auto v = ws.solutionFiles();
  std::vector<std::string> n;
  for (auto& p : v) n.push_back(p.filename().string());
  CHECK_EQ(n.size(), 3u);
  CHECK(std::find(n.begin(), n.end(), "a.sln") != n.end());
  CHECK(std::find(n.begin(), n.end(), "b.slnx") != n.end());
  CHECK(std::find(n.begin(), n.end(), "c.SLNX") != n.end());
}

TEST(slnx_msbuild_is_pointed_at_the_slnx) {
  Dir d("build");
  write(d.root / "App.slnx", "<Solution><Project Path=\"a.vcxproj\" /></Solution>");
  Workspace ws(d.root);
  ws.scan();
  ws.settings().vsConfiguration = "Release";
  ws.settings().vsPlatform = "x86";
  BuildCommand c;
  std::string err;
  REQUIRE(detectBuildCommand(ws, {}, &c, &err));
  CHECK_EQ(c.exe, "msbuild");
  REQUIRE(c.args.size() == 4u);
  CHECK_EQ(fs::path(c.args[0]).filename(), "App.slnx");
  CHECK_EQ(c.args[2], "/p:Configuration=Release");
  CHECK_EQ(c.args[3], "/p:Platform=x86");
}

TEST(slnx_single_file_build_asks_the_project_that_lists_the_file_with_the_mapped_configuration) {
  Dir d("buildfile");
  write(d.root / "App.slnx",
        "<Solution><Configurations><Platform Name=\"x86\" /></Configurations>"
        "<Project Path=\"p/p.vcxproj\"><Platform Solution=\"*|x86\" Project=\"Mapped\" /></Project></Solution>");
  write(d.root / "p/p.vcxproj", "<Project><ItemGroup><ClCompile Include=\"f.cpp\" /></ItemGroup></Project>");
  write(d.root / "p/f.cpp", "int f;\n");
  Workspace ws(d.root);
  ws.scan();
  ws.settings().vsConfiguration = "Debug";
  ws.settings().vsPlatform = "x86";
  BuildOverrides o;
  o.kind = BuildOverrides::Kind::CurrentFile;
  o.file = d.root / "p/f.cpp";
  BuildCommand c;
  std::string err;
  REQUIRE(detectBuildCommand(ws, o, &c, &err));
  CHECK_EQ(c.exe, "msbuild");
  bool sawPlatform = false, sawProject = false;
  for (auto& a : c.args) {
    sawPlatform = sawPlatform || a == "/p:Platform=Mapped";
    sawProject = sawProject || fs::path(a).filename() == "p.vcxproj";
  }
  CHECK(sawPlatform);
  CHECK(sawProject);
}

// ---------------------------------------------------------------------------
// The compile commands equal those of the equivalent .sln
// ---------------------------------------------------------------------------

TEST(slnx_pair_fixture_reads_like_its_sln) {
  auto a = compdb::readSolution(kPair / "Pair.sln");
  auto b = compdb::readSolution(kPair / "Pair.slnx");
  CHECK_EQ(names(a.configurations), names(b.configurations));
  CHECK_EQ(names(b.configurations), "Debug|x64, Debug|x86, Release|x64, Release|x86");
  for (const char* proj : {"App", "Lib", "Tool"})
    for (const auto& c : b.configurations) { CHECK_EQ(mapped(a, proj, c.name()), mapped(b, proj, c.name())); }
  CHECK_EQ(mapped(b, "Tool", "Debug|x86"), "Debug|x64");
  CHECK_EQ(mapped(b, "Tool", "Release|x64"), "Release|x64!");
}

TEST(slnx_compile_commands_equal_the_equivalent_sln_in_every_configuration) {
  for (const auto& c : compdb::readSolution(kPair / "Pair.slnx").configurations) {
    auto opt = hermetic(c.configuration, c.platform);
    std::string e1, e2;
    auto fromSln = compdb::fromSolution(kPair / "Pair.sln", opt, &e1);
    auto fromSlnx = compdb::fromSolution(kPair / "Pair.slnx", opt, &e2);
    CHECK_EQ(e1, e2);
    REQUIRE(!fromSlnx.empty());
    REQUIRE(fromSln.size() == fromSlnx.size());
    auto key = [](const CompileCommand& cc) { return cc.file.generic_string(); };
    std::sort(fromSln.begin(), fromSln.end(), [&](auto& x, auto& y) { return key(x) < key(y); });
    std::sort(fromSlnx.begin(), fromSlnx.end(), [&](auto& x, auto& y) { return key(x) < key(y); });
    for (size_t k = 0; k < fromSln.size(); ++k) {
      CHECK_EQ(key(fromSln[k]), key(fromSlnx[k]));
      CHECK(fromSln[k].arguments == fromSlnx[k].arguments);
    }
  }
  // The rules did something: Tool maps x86 to x64 and is not built in Release, yet still evaluated.
  auto debugX86 = compdb::fromSolution(kPair / "Pair.slnx", hermetic("Debug", "x86"));
  for (auto& c : debugX86) {
    const std::string f = c.file.filename().string();
    const bool m32 = std::find(c.arguments.begin(), c.arguments.end(), "-m32") != c.arguments.end();
    if (f == "tool.cpp") CHECK(!m32);
    else CHECK(m32);
    const std::string def = f == "tool.cpp"  ? "/DTOOL_PLAT_x64"
                            : f == "lib.cpp" ? "/DLIB_PLAT_Win32"
                                             : "/DAPP_PLAT_Win32";
    CHECK(std::find(c.arguments.begin(), c.arguments.end(), def) != c.arguments.end());
  }
}

namespace {

struct DbSnapshot {
  std::map<std::string, std::vector<std::string>> argvByFile;  // per configuration: "cfg file" -> argv
  std::string configs;
  std::string source;
  std::string solutionName;
};

// Runs an Editor on `root` (which holds exactly one solution file), switches through every configuration and records
// each source's compile command, with the root path made symbolic.
void snapshot(const fs::path& root, DbSnapshot& s) {
  Editor ed(root, noClangd());
  std::string err;
  REQUIRE(ed.start(&err));
  s.source = ed.semantic().status().compdbSource;
  s.configs = names(ed.vsConfigurations());
  const std::string rootStr = root.generic_string();
  for (const auto& c : ed.vsConfigurations()) {
    REQUIRE(switchTo(ed, c.name()));
    for (const char* f : {"App/app.cpp", "Lib/lib.cpp", "Tool/tool.cpp"}) {
      auto info = ed.compileCommandInfo(root / f);
      std::vector<std::string> argv;
      for (auto a : info.argv) {
        for (size_t at; (at = a.find(rootStr)) != std::string::npos;) a.replace(at, rootStr.size(), "<ROOT>");
        argv.push_back(a);
      }
      s.argvByFile[c.name() + " " + f] = argv;
      if (info.listed) s.argvByFile[c.name() + " " + f].push_back("#listed");
    }
  }
  s.solutionName = pathUtf8(ed.workspace().solutionFiles().front().extension());
}

}  // namespace

TEST(slnx_a_workspace_with_only_a_slnx_gets_the_same_compile_commands_as_the_sln) {
  Dir d("engine");
  fs::copy(kPair, d.root, fs::copy_options::recursive);
  fs::remove(d.root / "Pair.sln");
  DbSnapshot x;
  snapshot(d.root, x);
  CHECK_EQ(x.source, "vcxproj");
  CHECK_EQ(x.solutionName, ".slnx");
  fs::remove_all(d.root / ".tenfold");
  fs::remove(d.root / "Pair.slnx");
  fs::copy_file(kPair / "Pair.sln", d.root / "Pair.sln");
  DbSnapshot s;
  snapshot(d.root, s);
  CHECK_EQ(s.source, "vcxproj");
  CHECK_EQ(s.solutionName, ".sln");
  CHECK_EQ(x.configs, s.configs);
  CHECK_EQ(x.configs, "Debug|x64, Debug|x86, Release|x64, Release|x86");
  REQUIRE(x.argvByFile.size() == s.argvByFile.size());
  size_t listed = 0;
  for (const auto& [k, argv] : x.argvByFile) {
    auto it = s.argvByFile.find(k);
    REQUIRE(it != s.argvByFile.end());
    CHECK(argv == it->second);
    if (!argv.empty() && argv.back() == "#listed") ++listed;
  }
  CHECK_EQ(listed, 12u);  // 3 sources x 4 configurations: every project is evaluated in every configuration
}

TEST(slnx_configuration_switching_changes_the_flags_and_keeps_one_database_per_configuration) {
  Dir d("switch");
  fs::copy(kPair, d.root, fs::copy_options::recursive);
  fs::remove(d.root / "Pair.sln");
  Editor ed(d.root, noClangd());
  std::string err;
  REQUIRE(ed.start(&err));
  CHECK_EQ(ed.workspace().solutionFiles().size(), 1u);
  CHECK_EQ(ed.activeVsConfiguration().name(), "Debug|x64");
  auto has = [](const std::vector<std::string>& v, const std::string& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
  };
  auto info = ed.compileCommandInfo(d.root / "App/app.cpp");
  CHECK_EQ(info.source, "vcxproj");
  CHECK(info.listed);
  CHECK(has(info.argv, "/DAPP_CFG_Debug"));
  CHECK(has(info.argv, "/DAPP_PLAT_x64"));
  const fs::path dbg = ed.semantic().status().compdbPath.parent_path();
  REQUIRE(switchTo(ed, "Release|x86"));
  CHECK_EQ(ed.activeVsConfiguration().name(), "Release|x86");
  info = ed.compileCommandInfo(d.root / "App/app.cpp");
  CHECK(has(info.argv, "/DAPP_CFG_Release"));
  CHECK(has(info.argv, "/DAPP_PLAT_Win32"));  // solution x86 is the project's Win32
  CHECK(ed.semantic().status().compdbPath.parent_path() != dbg);
  REQUIRE(switchTo(ed, "Debug|x64"));
  CHECK_EQ(ed.semantic().status().compdbPath.parent_path().string(), dbg.string());
  // The compile info names the solution file.
  const auto ci = ed.compileCommandInfo(d.root / "App/app.cpp");
  CHECK_EQ(ci.solution.filename().string(), "Pair.slnx");
  // A configuration the solution does not have is refused.
  CHECK(!ed.selectVsConfiguration("Debug|Itanium", &err, [] {}));
}

TEST(slnx_editing_the_solution_file_refreshes_the_configuration_list) {
  Dir d("refresh");
  fs::copy(kPair, d.root, fs::copy_options::recursive);
  fs::remove(d.root / "Pair.sln");
  Editor ed(d.root, noClangd());
  std::string err;
  REQUIRE(ed.start(&err));
  CHECK_EQ(names(ed.vsConfigurations()), "Debug|x64, Debug|x86, Release|x64, Release|x86");
  std::string text;
  {
    std::ifstream in(d.root / "Pair.slnx", std::ios::binary);
    text.assign(std::istreambuf_iterator<char>(in), {});
  }
  const std::string x86 = "<Platform Name=\"x86\" />";
  text.replace(text.find(x86), x86.size(), "<Platform Name=\"ARM64\" />");
  write(d.root / "Pair.slnx", text);
  CHECK_EQ(names(ed.vsConfigurations()),
           "Debug|x64, Debug|ARM64, Release|x64, Release|ARM64");  // read from disk each time
}
