// PROJ-ADD (docs/VS_BEHAVIOUR.md section 20): the project-file editor behind "New File" in a solution workspace's
// explorer (src/workspace/ProjectEdit.*). The files are edited as text: every byte that is not the new element stays
// (encoding, BOM, line endings, indentation, comments). The explorer flow is tested in tests/ui/test_vs_tree.cpp.
#include <atomic>
#include <fstream>
#include <sstream>

#include "core/PathUtil.h"
#include "core/Platform.h"
#include "test_main.h"
#include "workspace/ProjectEdit.h"
#include "workspace/Workspace.h"

using namespace tf;
using namespace tf::projedit;
namespace fs = std::filesystem;

namespace {

struct Dir {
  fs::path root;
  Dir() {
    static std::atomic<int> n{0};
    root =
        fs::temp_directory_path() / ("tf_projedit_" + std::to_string(currentProcessId()) + "_" + std::to_string(n++));
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root);
    root = fs::weakly_canonical(root, ec);
  }
  ~Dir() {
    std::error_code ec;
    fs::remove_all(root, ec);
  }
};

void write(const fs::path& p, const std::string& text) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary | std::ios::trunc) << text;
}
std::string slurp(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}
std::string crlf(std::string s) {
  std::string o;
  for (char c : s) {
    if (c == '\n') o += '\r';
    o += c;
  }
  return o;
}
void setReadOnly(const fs::path& p) {
  std::error_code ec;
  fs::permissions(p, fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write,
                  fs::perm_options::remove, ec);
}
void setWritable(const fs::path& p) {
  std::error_code ec;
  fs::permissions(p, fs::perms::owner_write, fs::perm_options::add, ec);
}

// What Visual Studio 2022 writes for a small console project (LF here; crlf() makes it the on-disk form).
const char* kProject =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
    "<Project DefaultTargets=\"Build\" xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">\n"
    "  <ItemGroup Label=\"ProjectConfigurations\">\n"
    "    <ProjectConfiguration Include=\"Debug|x64\">\n"
    "      <Configuration>Debug</Configuration>\n"
    "      <Platform>x64</Platform>\n"
    "    </ProjectConfiguration>\n"
    "  </ItemGroup>\n"
    "  <PropertyGroup Label=\"Globals\">\n"
    "    <VCProjectVersion>17.0</VCProjectVersion>\n"
    "  </PropertyGroup>\n"
    "  <Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.Default.props\" />\n"
    "  <!-- the items -->\n"
    "  <ItemGroup>\n"
    "    <ClCompile Include=\"main.cpp\" />\n"
    "    <ClCompile Include=\"util.cpp\">\n"
    "      <ExcludedFromBuild Condition=\"'$(Configuration)|$(Platform)'=='Debug|x64'\">true</ExcludedFromBuild>\n"
    "    </ClCompile>\n"
    "  </ItemGroup>\n"
    "  <ItemGroup>\n"
    "    <ClInclude Include=\"util.h\" />\n"
    "  </ItemGroup>\n"
    "  <Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.targets\" />\n"
    "</Project>\n";

const char* kFilters =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
    "<Project ToolsVersion=\"4.0\" xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">\n"
    "  <ItemGroup>\n"
    "    <Filter Include=\"Source Files\">\n"
    "      <UniqueIdentifier>{4FC737F1-C7A5-4376-A066-2A32D752A2FF}</UniqueIdentifier>\n"
    "    </Filter>\n"
    "    <Filter Include=\"Source Files\\Sub\">\n"
    "      <UniqueIdentifier>{4FC737F1-C7A5-4376-A066-2A32D752A200}</UniqueIdentifier>\n"
    "    </Filter>\n"
    "    <Filter Include=\"Header Files\" />\n"
    "  </ItemGroup>\n"
    "  <ItemGroup>\n"
    "    <ClCompile Include=\"main.cpp\">\n"
    "      <Filter>Source Files</Filter>\n"
    "    </ClCompile>\n"
    "  </ItemGroup>\n"
    "</Project>\n";

// plans, commits, and returns the new project text ("" on failure)
bool add(const fs::path& project, const std::string& relFile, const std::string& filter = {},
         std::string* err = nullptr) {
  AddPlan plan;
  if (!planAdd({project, project.parent_path() / pathFromUtf8(relFile), filter}, plan, err)) return false;
  return commit(plan, err);
}

}  // namespace

TEST(projedit_item_types_follow_the_extension) {
  for (const char* f : {"a.cpp", "A.C", "x.cc", "x.cxx", "x.c++", "b.CPP"})
    CHECK_EQ(itemTypeFor(f), std::string("ClCompile"));
  for (const char* f : {"a.h", "a.hpp", "a.hh", "a.hxx", "a.inl", "A.H"})
    CHECK_EQ(itemTypeFor(f), std::string("ClInclude"));
  CHECK_EQ(itemTypeFor("app.rc"), std::string("ResourceCompile"));
  for (const char* f : {"notes.txt", "README", "x.md", "data.json", "x.natvis"})
    CHECK_EQ(itemTypeFor(f), std::string("None"));
}

TEST(projedit_include_is_relative_with_backslashes) {
  const fs::path proj = fs::path("/w/App/App.vcxproj");
  CHECK_EQ(includeFor(proj, "/w/App/main.cpp"), std::string("main.cpp"));
  CHECK_EQ(includeFor(proj, "/w/App/src/util/x.cpp"), std::string("src\\util\\x.cpp"));
  CHECK_EQ(includeFor(proj, "/w/Shared/x.h"), std::string("..\\Shared\\x.h"));
}

TEST(projedit_a_source_goes_after_the_last_item_of_its_type_and_nothing_else_changes) {
  Dir d;
  const std::string before = "\xEF\xBB\xBF" + crlf(kProject);  // Visual Studio: UTF-8 with a BOM, CRLF
  write(d.root / "App.vcxproj", before);
  std::string err;
  REQUIRE(add(d.root / "App.vcxproj", "src\\new.cpp", {}, &err));
  const std::string after = slurp(d.root / "App.vcxproj");
  const std::string item = "\r\n    <ClCompile Include=\"src\\new.cpp\" />";
  const size_t at = before.find("    </ClCompile>\r\n  </ItemGroup>") + std::string("    </ClCompile>").size();
  REQUIRE(at != std::string::npos);
  CHECK_EQ(after, before.substr(0, at) + item + before.substr(at));
  CHECK(after.compare(0, 3, "\xEF\xBB\xBF") == 0);
  CHECK(after.find("\n", 0) != std::string::npos);
  for (size_t i = 0; i < after.size(); ++i)
    if (after[i] == '\n') CHECK(i > 0 && after[i - 1] == '\r');  // no bare LF came in
}

TEST(projedit_a_header_joins_the_clinclude_group_and_other_files_get_a_new_group_after_the_last_one) {
  Dir d;
  write(d.root / "App.vcxproj", crlf(kProject));
  REQUIRE(add(d.root / "App.vcxproj", "util2.h"));
  REQUIRE(add(d.root / "App.vcxproj", "notes.txt"));
  const std::string expect = crlf(
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
      "<Project DefaultTargets=\"Build\" xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">\n"
      "  <ItemGroup Label=\"ProjectConfigurations\">\n"
      "    <ProjectConfiguration Include=\"Debug|x64\">\n"
      "      <Configuration>Debug</Configuration>\n"
      "      <Platform>x64</Platform>\n"
      "    </ProjectConfiguration>\n"
      "  </ItemGroup>\n"
      "  <PropertyGroup Label=\"Globals\">\n"
      "    <VCProjectVersion>17.0</VCProjectVersion>\n"
      "  </PropertyGroup>\n"
      "  <Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.Default.props\" />\n"
      "  <!-- the items -->\n"
      "  <ItemGroup>\n"
      "    <ClCompile Include=\"main.cpp\" />\n"
      "    <ClCompile Include=\"util.cpp\">\n"
      "      <ExcludedFromBuild Condition=\"'$(Configuration)|$(Platform)'=='Debug|x64'\">true</ExcludedFromBuild>\n"
      "    </ClCompile>\n"
      "  </ItemGroup>\n"
      "  <ItemGroup>\n"
      "    <ClInclude Include=\"util.h\" />\n"
      "    <ClInclude Include=\"util2.h\" />\n"
      "  </ItemGroup>\n"
      "  <ItemGroup>\n"
      "    <None Include=\"notes.txt\" />\n"
      "  </ItemGroup>\n"
      "  <Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.targets\" />\n"
      "</Project>\n");
  CHECK_EQ(slurp(d.root / "App.vcxproj"), expect);
}

TEST(projedit_a_mixed_group_gets_the_item_after_the_last_one_of_its_type) {
  Dir d;
  write(d.root / "Lib.vcxproj",
        "<Project>\n  <ItemGroup>\n    <ClCompile Include=\"a.cpp\" />\n    <ClInclude Include=\"a.h\" />\n"
        "    <ClCompile Include=\"b.cpp\" />\n    <None Include=\"n.txt\" />\n  </ItemGroup>\n</Project>\n");
  REQUIRE(add(d.root / "Lib.vcxproj", "c.cpp"));
  CHECK_EQ(
      slurp(d.root / "Lib.vcxproj"),
      std::string("<Project>\n  <ItemGroup>\n    <ClCompile Include=\"a.cpp\" />\n    <ClInclude Include=\"a.h\" />\n"
                  "    <ClCompile Include=\"b.cpp\" />\n    <ClCompile Include=\"c.cpp\" />\n"
                  "    <None Include=\"n.txt\" />\n  </ItemGroup>\n</Project>\n"));
}

TEST(projedit_a_conditional_or_labelled_group_is_not_used_for_a_new_item) {
  Dir d;
  write(d.root / "P.vcxproj",
        "<Project>\n  <ItemGroup Condition=\"'$(Configuration)'=='Release'\">\n    <ClCompile Include=\"rel.cpp\" />\n"
        "  </ItemGroup>\n  <ItemGroup>\n    <ClCompile Include=\"a.cpp\" />\n  </ItemGroup>\n</Project>\n");
  REQUIRE(add(d.root / "P.vcxproj", "b.cpp"));
  CHECK_EQ(
      slurp(d.root / "P.vcxproj"),
      std::string(
          "<Project>\n  <ItemGroup Condition=\"'$(Configuration)'=='Release'\">\n"
          "    <ClCompile Include=\"rel.cpp\" />\n  </ItemGroup>\n  <ItemGroup>\n"
          "    <ClCompile Include=\"a.cpp\" />\n    <ClCompile Include=\"b.cpp\" />\n  </ItemGroup>\n</Project>\n"));
  // only a conditional group has ClInclude items: a new group, after the last plain one that holds items
  write(d.root / "Q.vcxproj",
        "<Project>\n  <ItemGroup>\n    <ClCompile Include=\"a.cpp\" />\n  </ItemGroup>\n"
        "  <ItemGroup Condition=\"x\">\n    <ClInclude Include=\"r.h\" />\n  </ItemGroup>\n</Project>\n");
  REQUIRE(add(d.root / "Q.vcxproj", "a.h"));
  CHECK_EQ(
      slurp(d.root / "Q.vcxproj"),
      std::string("<Project>\n  <ItemGroup>\n    <ClCompile Include=\"a.cpp\" />\n  </ItemGroup>\n"
                  "  <ItemGroup>\n    <ClInclude Include=\"a.h\" />\n  </ItemGroup>\n"
                  "  <ItemGroup Condition=\"x\">\n    <ClInclude Include=\"r.h\" />\n  </ItemGroup>\n</Project>\n"));
}

TEST(projedit_an_empty_item_group_is_filled_whether_written_self_closed_or_open) {
  Dir d;
  write(d.root / "A.vcxproj", crlf("<Project>\n  <ItemGroup />\n  <Import Project=\"x\" />\n</Project>\n"));
  REQUIRE(add(d.root / "A.vcxproj", "a.cpp"));
  CHECK_EQ(slurp(d.root / "A.vcxproj"), crlf("<Project>\n  <ItemGroup>\n    <ClCompile Include=\"a.cpp\" />\n  "
                                             "</ItemGroup>\n  <Import Project=\"x\" />\n</Project>\n"));
  write(d.root / "B.vcxproj", "<Project>\n  <ItemGroup>\n  </ItemGroup>\n</Project>\n");
  REQUIRE(add(d.root / "B.vcxproj", "a.h"));
  CHECK_EQ(slurp(d.root / "B.vcxproj"),
           std::string("<Project>\n  <ItemGroup>\n    <ClInclude Include=\"a.h\" />\n  </ItemGroup>\n</Project>\n"));
}

TEST(projedit_with_no_item_group_a_new_one_goes_before_the_cpp_targets_import) {
  Dir d;
  write(
      d.root / "A.vcxproj",
      "<Project>\n  <PropertyGroup />\n  <Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.targets\" />\n</Project>\n");
  REQUIRE(add(d.root / "A.vcxproj", "a.cpp"));
  CHECK_EQ(
      slurp(d.root / "A.vcxproj"),
      std::string("<Project>\n  <PropertyGroup />\n  <ItemGroup>\n    <ClCompile Include=\"a.cpp\" />\n  </ItemGroup>\n"
                  "  <Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.targets\" />\n</Project>\n"));
  write(d.root / "B.vcxproj", "<Project>\n  <PropertyGroup />\n</Project>");  // no final newline either
  REQUIRE(add(d.root / "B.vcxproj", "a.cpp"));
  CHECK_EQ(slurp(d.root / "B.vcxproj"), std::string("<Project>\n  <PropertyGroup />\n  <ItemGroup>\n    <ClCompile "
                                                    "Include=\"a.cpp\" />\n  </ItemGroup>\n</Project>"));
}

TEST(projedit_line_endings_and_indentation_follow_the_file) {
  Dir d;
  write(d.root / "LF.vcxproj",
        "<Project>\n\t<ItemGroup>\n\t\t<ClCompile Include=\"a.cpp\" />\n\t</ItemGroup>\n</Project>\n");
  REQUIRE(add(d.root / "LF.vcxproj", "b.cpp"));
  REQUIRE(add(d.root / "LF.vcxproj", "b.h"));
  CHECK_EQ(
      slurp(d.root / "LF.vcxproj"),
      std::string("<Project>\n\t<ItemGroup>\n\t\t<ClCompile Include=\"a.cpp\" />\n\t\t<ClCompile Include=\"b.cpp\" />\n"
                  "\t</ItemGroup>\n\t<ItemGroup>\n\t\t<ClInclude Include=\"b.h\" />\n\t</ItemGroup>\n</Project>\n"));
  // a file with mixed endings keeps the majority for what is new
  write(d.root / "Mixed.vcxproj",
        "<Project>\r\n  <ItemGroup>\r\n    <ClCompile Include=\"a.cpp\" />\n  </ItemGroup>\r\n</Project>\r\n");
  REQUIRE(add(d.root / "Mixed.vcxproj", "b.cpp"));
  CHECK_EQ(
      slurp(d.root / "Mixed.vcxproj"),
      std::string(
          "<Project>\r\n  <ItemGroup>\r\n    <ClCompile Include=\"a.cpp\" />\r\n    <ClCompile Include=\"b.cpp\" />\n"
          "  </ItemGroup>\r\n</Project>\r\n"));
}

TEST(projedit_a_listed_file_is_not_added_twice) {
  Dir d;
  write(d.root / "App.vcxproj", crlf(kProject));
  AddPlan plan;
  REQUIRE(planAdd({d.root / "App.vcxproj", d.root / "MAIN.cpp", {}}, plan, nullptr));
  CHECK(!plan.editProject);  // another case, same item
  REQUIRE(planAdd({d.root / "App.vcxproj", d.root / "util.h", {}}, plan, nullptr));
  CHECK(!plan.editProject);
  CHECK(plan.files().empty());
  REQUIRE(commit(plan, nullptr));
  CHECK_EQ(slurp(d.root / "App.vcxproj"), crlf(kProject));
}

TEST(projedit_names_and_paths_are_escaped_for_xml_and_msbuild) {
  Dir d;
  write(d.root / "App.vcxproj", crlf(kProject));
  REQUIRE(add(d.root / "App.vcxproj", "a&b.cpp"));
  CHECK(slurp(d.root / "App.vcxproj").find("<ClCompile Include=\"a&amp;b.cpp\" />") != std::string::npos);
  for (const char* bad : {"a;b.cpp", "a%b.cpp", "a$b.cpp", "a@b.cpp", "a*b.cpp", "a?b.cpp"}) {
    CHECK(!nameProblem(bad).empty());
    std::string err;
    CHECK(!add(d.root / "App.vcxproj", bad, {}, &err));
    CHECK(!err.empty());
  }
  CHECK(!nameProblem(std::string("a\x01") + "b.cpp").empty());
  CHECK(nameProblem("a b-c_d.cpp").empty());
}

TEST(projedit_a_filter_gets_an_entry_in_the_filters_file_with_its_formatting) {
  Dir d;
  const std::string proj = "\xEF\xBB\xBF" + crlf(kProject), filt = "\xEF\xBB\xBF" + crlf(kFilters);
  write(d.root / "App.vcxproj", proj);
  write(d.root / "App.vcxproj.filters", filt);
  REQUIRE(add(d.root / "App.vcxproj", "src\\new.cpp", "Source Files\\Sub"));
  const size_t at = filt.find("    </ClCompile>\r\n  </ItemGroup>") + std::string("    </ClCompile>").size();
  CHECK_EQ(slurp(d.root / "App.vcxproj.filters"), filt.substr(0, at) +
                                                      crlf("\n    <ClCompile Include=\"src\\new.cpp\">\n      "
                                                           "<Filter>Source Files\\Sub</Filter>\n    </ClCompile>") +
                                                      filt.substr(at));
  CHECK(slurp(d.root / "App.vcxproj").find("<ClCompile Include=\"src\\new.cpp\" />") != std::string::npos);
  // a header: the filters file has no ClInclude group yet: a new one after the last
  REQUIRE(add(d.root / "App.vcxproj", "new.h", "Header Files"));
  const std::string f2 = slurp(d.root / "App.vcxproj.filters");
  CHECK(f2.find(crlf(
            "  </ItemGroup>\n  <ItemGroup>\n    <ClInclude Include=\"new.h\">\n      <Filter>Header Files</Filter>\n"
            "    </ClInclude>\n  </ItemGroup>\n</Project>\n")) != std::string::npos);
  CHECK(f2.compare(0, 3, "\xEF\xBB\xBF") == 0);
}

TEST(projedit_without_a_filter_or_a_filters_file_only_the_project_changes) {
  Dir d;
  write(d.root / "App.vcxproj", crlf(kProject));
  write(d.root / "App.vcxproj.filters", crlf(kFilters));
  REQUIRE(add(d.root / "App.vcxproj", "root.cpp"));  // at the project root: no <Filter>
  CHECK_EQ(slurp(d.root / "App.vcxproj.filters"), crlf(kFilters));
  write(d.root / "Other.vcxproj", crlf(kProject));
  AddPlan plan;
  REQUIRE(planAdd({d.root / "Other.vcxproj", d.root / "x.cpp", "Source Files"}, plan, nullptr));  // no filters file
  CHECK(plan.editProject);
  CHECK(!plan.editFilters);
  CHECK(!fs::exists(d.root / "Other.vcxproj.filters"));
}

TEST(projedit_refuses_what_it_cannot_edit_and_changes_nothing) {
  Dir d;
  std::string err;
  write(d.root / "Bad.vcxproj", "<Project><ItemGroup>");
  CHECK(!add(d.root / "Bad.vcxproj", "a.cpp", {}, &err));
  CHECK(!err.empty());
  CHECK_EQ(slurp(d.root / "Bad.vcxproj"), std::string("<Project><ItemGroup>"));
  write(d.root / "U16.vcxproj", std::string("\xFF\xFE<\0P\0", 6));
  CHECK(!add(d.root / "U16.vcxproj", "a.cpp", {}, &err));
  CHECK(!add(d.root / "Missing.vcxproj", "a.cpp", {}, &err));
  // a comment that contains tags does not confuse the scan
  write(d.root / "C.vcxproj",
        "<Project>\n  <!-- <ItemGroup><ClCompile Include=\"x.cpp\" /></ItemGroup> -->\n</Project>\n");
  REQUIRE(add(d.root / "C.vcxproj", "x.cpp"));
  CHECK(slurp(d.root / "C.vcxproj").find("<!-- <ItemGroup><ClCompile Include=\"x.cpp\" /></ItemGroup> -->") !=
        std::string::npos);
  CHECK(slurp(d.root / "C.vcxproj")
            .find("\n  <ItemGroup>\n    <ClCompile Include=\"x.cpp\" />\n  </ItemGroup>\n</Project>") !=
        std::string::npos);
}

TEST(projedit_only_vcxproj_is_edited_and_sdk_style_projects_need_no_edit) {
  Dir d;
  std::string why;
  write(d.root / "A.vcxproj", crlf(kProject));
  CHECK(classify(d.root / "A.vcxproj", &why) == Support::Edit);
  write(d.root / "Sdk.csproj", "<Project Sdk=\"Microsoft.NET.Sdk\">\n  <PropertyGroup />\n</Project>\n");
  CHECK(classify(d.root / "Sdk.csproj", &why) == Support::NoEditNeeded);
  CHECK(why.find("glob") != std::string::npos);
  write(d.root / "Old.csproj",
        "<Project ToolsVersion=\"15.0\">\n  <ItemGroup><Compile Include=\"a.cs\" /></ItemGroup>\n</Project>\n");
  CHECK(classify(d.root / "Old.csproj", &why) == Support::Unsupported);
  CHECK(why.find("vcxproj") != std::string::npos);
  AddPlan plan;
  CHECK(!planAdd({d.root / "Old.csproj", d.root / "b.cs", {}}, plan, &why));
  CHECK_EQ(slurp(d.root / "Old.csproj"),
           std::string(
               "<Project ToolsVersion=\"15.0\">\n  <ItemGroup><Compile Include=\"a.cs\" /></ItemGroup>\n</Project>\n"));
}

TEST(projedit_a_read_only_project_or_filters_file_is_refused_with_no_partial_edit) {
  Dir d;
  const std::string proj = crlf(kProject), filt = crlf(kFilters);
  write(d.root / "App.vcxproj", proj);
  write(d.root / "App.vcxproj.filters", filt);
  AddPlan plan;
  REQUIRE(planAdd({d.root / "App.vcxproj", d.root / "n.cpp", "Source Files"}, plan, nullptr));
  CHECK(unwritable(plan).empty());
  // the filters file is read-only (as a Perforce file that is not checked out): the project is not written either
  setReadOnly(d.root / "App.vcxproj.filters");
  const auto ro = unwritable(plan);
  REQUIRE(ro.size() == 1);
  CHECK_EQ(pathUtf8(ro[0].filename()), std::string("App.vcxproj.filters"));
  std::string err;
  CHECK(!commit(plan, &err));
  CHECK(err.find("read-only") != std::string::npos);
  CHECK(err.find("App.vcxproj.filters") != std::string::npos);
  CHECK_EQ(slurp(d.root / "App.vcxproj"), proj);
  CHECK_EQ(slurp(d.root / "App.vcxproj.filters"), filt);
  setWritable(d.root / "App.vcxproj.filters");
  setReadOnly(d.root / "App.vcxproj");
  CHECK(!commit(plan, &err));
  CHECK(err.find("App.vcxproj") != std::string::npos);
  CHECK_EQ(slurp(d.root / "App.vcxproj.filters"), filt);
  setWritable(d.root / "App.vcxproj");
  CHECK(commit(plan, &err));  // after the check-out it goes through
  CHECK(slurp(d.root / "App.vcxproj").find("n.cpp") != std::string::npos);
  CHECK(slurp(d.root / "App.vcxproj.filters").find("n.cpp") != std::string::npos);
}

TEST(projedit_a_new_project_item_is_a_workspace_file_at_once) {
  Dir d;
  write(d.root / "App.sln",
        "Microsoft Visual Studio Solution File, Format Version 12.00\n"
        "Project(\"{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}\") = \"App\", \"App.vcxproj\", "
        "\"{CCCCCCCC-0000-0000-0000-000000000001}\"\nEndProject\n");
  write(d.root / "App.vcxproj", crlf(kProject));
  write(d.root / "main.cpp", "int main() {}\n");
  Workspace ws(d.root);
  ws.scan();
  REQUIRE(ws.isSolutionWorkspace());
  const fs::path f = d.root / "fresh.md";  // an extension the folder rules do not list
  write(f, "x\n");
  ws.noteFileAdded(f);
  CHECK(!ws.isMember(f));  // a loose file of a solution workspace is not a member
  CHECK(ws.noteProjectItemAdded(f));
  CHECK(ws.isMember(f));
  CHECK(!ws.noteProjectItemAdded(f));  // no duplicate
  size_t n = 0;
  for (const fs::path& p : ws.allFiles()) n += pathsEqual(p, f);
  CHECK_EQ(n, size_t(1));
  Workspace folder(d.root / "sub");  // a folder workspace: like noteFileAdded
  write(d.root / "sub" / "a.cpp", "x\n");
  folder.scan();
  write(d.root / "sub" / "b.cpp", "x\n");
  CHECK(folder.noteProjectItemAdded(d.root / "sub" / "b.cpp"));
  CHECK(folder.isMember(d.root / "sub" / "b.cpp"));
}
