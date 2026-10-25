#include <algorithm>

#include "semantic/CompileCommands.h"
#include "semantic/XmlLite.h"
#include "test_main.h"

using namespace tf;
namespace fs = std::filesystem;

namespace {
bool hasArg(const CompileCommand& c, const std::string& a) {
  return std::find(c.arguments.begin(), c.arguments.end(), a) != c.arguments.end();
}
const CompileCommand* forFile(const std::vector<CompileCommand>& cs, const std::string& suffix) {
  for (auto& c : cs)
    if (c.file.generic_string().ends_with(suffix)) return &c;
  return nullptr;
}
}  // namespace

TEST(xml_parse_basics) {
  std::string err;
  auto doc = xml::parse(
      "<?xml version=\"1.0\"?>\n<!-- c -->\n<A x=\"1\" y='two &amp; three'>\n  <B>text "
      "&lt;here&gt;</B><C/><![CDATA[raw <x>]]></A>",
      &err);
  REQUIRE(doc != nullptr);
  CHECK_EQ(doc->name, "A");
  CHECK_EQ(doc->attr("x"), "1");
  CHECK_EQ(doc->attr("y"), "two & three");
  REQUIRE(doc->child("B") != nullptr);
  CHECK_EQ(doc->child("B")->text, "text <here>");
  CHECK(doc->child("C") != nullptr);
  CHECK(doc->text.find("raw <x>") != std::string::npos);
  auto bad = xml::parse("<A><B></A>", &err);
  CHECK(bad == nullptr);
  CHECK(!err.empty());
}

TEST(msbuild_properties_and_conditions) {
  std::map<std::string, std::string> p{{"Configuration", "Debug"}, {"Platform", "x64"}, {"Dir", "C:\\x\\"}};
  CHECK_EQ(compdb::expandProperties("$(Dir)inc;$(Missing)z", p), "C:\\x\\inc;z");
  CHECK(compdb::evaluateCondition("'$(Configuration)|$(Platform)'=='Debug|x64'", p, "."));
  CHECK(!compdb::evaluateCondition("'$(Configuration)|$(Platform)'=='Release|x64'", p, "."));
  CHECK(compdb::evaluateCondition("'$(Configuration)'!='Release'", p, "."));
  CHECK(compdb::evaluateCondition("'$(Configuration)'=='debug'", p, "."));  // case-insensitive like MSBuild
  CHECK(compdb::evaluateCondition("'$(Configuration)'=='Debug' and '$(Platform)'=='x64'", p, "."));
  CHECK(compdb::evaluateCondition("'$(Configuration)'=='Release' or '$(Platform)'=='x64'", p, "."));
  CHECK(compdb::evaluateCondition("!Exists('definitely_missing_file.props')", p, "."));
  CHECK(compdb::evaluateCondition("Exists('CMakeLists.txt')", p, TENFOLD_SOURCE_DIR));
  CHECK(compdb::evaluateCondition("", p, "."));
}

TEST(vcxproj_to_compile_commands) {
  fs::path sln = fs::path(TENFOLD_SOURCE_DIR) / "tests/fixtures/vs/Sample.sln";
  auto projs = compdb::projectsInSolution(sln);
  REQUIRE(projs.size() == 1u);
  CHECK(projs[0].filename() == "Sample.vcxproj");

  compdb::VcxprojOptions opt;
  opt.configuration = "Debug";
  opt.platform = "x64";
  std::string err;
  auto cmds = compdb::fromSolution(sln, opt, &err);
  CHECK_EQ(err, "");
  CHECK_EQ(cmds.size(), 2u);  // legacy.cpp excluded in Debug
  const CompileCommand* m = forFile(cmds, "Sample/src/main.cpp");
  REQUIRE(m != nullptr);
  CHECK_EQ(m->arguments[0], "clang-cl");
  CHECK(hasArg(*m, "/std:c++20"));
  fs::path pdir = fs::weakly_canonical(sln.parent_path() / "Sample");
  CHECK(hasArg(*m, "/I" + (pdir / "engine/include").generic_string()));
  CHECK(hasArg(*m, "/I" + (pdir / "src").generic_string()));
  // From the imported common.props, via $(SolutionDir)
  fs::path sdir = fs::weakly_canonical(sln.parent_path());
  CHECK(hasArg(*m, "/I" + (sdir / "third_party/include").generic_string()));
  CHECK(hasArg(*m, "/DCOMMON_DEFINE=1"));
  CHECK(hasArg(*m, "/DSAMPLE_EXPORTS"));
  CHECK(hasArg(*m, "/D_DEBUG"));
  CHECK(!hasArg(*m, "/DNDEBUG"));
  CHECK(hasArg(*m, "/MDd"));
  CHECK(hasArg(*m, "/GR-"));
  CHECK(hasArg(*m, "/Zc:__cplusplus"));
  CHECK(hasArg(*m, "-m64"));
  CHECK(m->directory == pdir);
  const CompileCommand* e = forFile(cmds, "Sample/src/engine.cpp");
  REQUIRE(e != nullptr);
  CHECK(hasArg(*e, "/DENGINE_FILE"));
  CHECK(hasArg(*e, "/DSAMPLE_EXPORTS"));  // %(PreprocessorDefinitions) merged the group-level defines
  CHECK(!hasArg(*m, "/DENGINE_FILE"));

  opt.configuration = "Release";
  auto rel = compdb::fromSolution(sln, opt, &err);
  CHECK_EQ(rel.size(), 3u);
  const CompileCommand* rm = forFile(rel, "main.cpp");
  REQUIRE(rm != nullptr);
  CHECK(hasArg(*rm, "/DNDEBUG"));
  CHECK(hasArg(*rm, "/MD"));
  CHECK(!hasArg(*rm, "/Zc:__cplusplus"));

  // Round trip through compile_commands.json
  fs::path tmp = fs::temp_directory_path() / "tenfold_test_cc.json";
  CHECK(CompileDatabase::save(cmds, tmp));
  auto loaded = CompileDatabase::load(tmp);
  CHECK_EQ(loaded.size(), cmds.size());
  CHECK(loaded[0].arguments == cmds[0].arguments);
  fs::remove(tmp);
}

TEST(heuristic_compile_commands) {
  fs::path root = fs::path(TENFOLD_SOURCE_DIR) / "examples/sample_project";
  auto cmds = compdb::heuristic(root, {root / "src/main.cpp", root / "src/geometry.cpp"}, {root / "include/geometry.h"},
                                "c++20");
  CHECK_EQ(cmds.size(), 2u);
  CHECK(hasArg(cmds[0], "-std=c++20"));
  CHECK(hasArg(cmds[0], "-I" + (root / "include").generic_string()));
  CHECK_EQ(cmds[0].arguments[0], "clang++");
}

TEST(infer_include_dirs) {
  std::vector<fs::path> headers = {"/w/engine/include/engine/core.h", "/w/engine/include/engine/math/vec.h",
                                   "/w/third/foo/foo.h", "/w/src/local.h", "/w/other/engine/core.h"};
  std::vector<std::string> incs = {"engine/core.h", "engine/math/vec.h", "foo/foo.h",
                                   "local.h",       "<vector>",          "missing.h"};
  auto dirs = compdb::inferIncludeDirs(headers, incs);
  REQUIRE(dirs.size() >= 3u);
  CHECK(dirs[0] == fs::path("/w/engine/include"));  // covers two includes, beats /w/other
  CHECK(std::find(dirs.begin(), dirs.end(), fs::path("/w/third")) != dirs.end());
  CHECK(std::find(dirs.begin(), dirs.end(), fs::path("/w/src")) != dirs.end());
  CHECK(std::find(dirs.begin(), dirs.end(), fs::path("/w/other")) == dirs.end());
  fs::path root = "/w";
  auto cmds = compdb::heuristic(root, {"/w/src/a.cpp"}, headers, "c++20", incs);
  REQUIRE(cmds.size() == 1u);
  CHECK(hasArg(cmds[0], "-I/w/engine/include"));
  CHECK(hasArg(cmds[0], "-I/w/src"));
  CHECK(!hasArg(cmds[0], "-I/w/other"));
}
