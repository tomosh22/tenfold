// VS-NAV: navigation like Visual Studio. Go To Document on an #include line (VS_BEHAVIOUR N1), the roles and the order
// of Find All References (N5) and the miscellaneous-files rule for diagnostics (I5). Text and file-system helpers
// (semantic/IncludeNav), the engine's answers without clangd (the include answer is exact and needs none), and with a
// real clangd where the test says so.
#include <fstream>
#include <iostream>
#include <memory>

#include "app/Editor.h"
#include "core/PathUtil.h"
#include "semantic/IncludeNav.h"
#include "test_main.h"
#include "workspace/Workspace.h"

#define REQUIRE_EQ(a, b) REQUIRE((a) == (b))

using namespace tf;
using namespace tf::incnav;
namespace fs = std::filesystem;

// ---- text helpers --------------------------------------------------------------------------------------------------

TEST(vsnav_include_directive_is_found_when_the_caret_is_on_it) {
  auto at = [](const std::string& line, int col) { return includeAt(line, col); };
  auto r = at("#include \"util.h\"", 0);
  REQUIRE(r.has_value());
  CHECK_EQ(r->spelling, std::string("util.h"));
  CHECK(!r->angled);
  CHECK_EQ(r->begin, 0);
  CHECK_EQ(r->end, 17);
  CHECK(at("#include \"util.h\"", 9).has_value());   // on the opening quote
  CHECK(at("#include \"util.h\"", 12).has_value());  // inside the spelling
  CHECK(at("#include \"util.h\"", 17).has_value());  // just after the closing quote
  CHECK(!at("#include \"util.h\"  // see", 20).has_value());
  r = at("  #  include   <sys/types.h>", 5);
  REQUIRE(r.has_value());
  CHECK_EQ(r->spelling, std::string("sys/types.h"));
  CHECK(r->angled);
  CHECK(at("#import <Foo.h>", 3).has_value());
  CHECK(at("#include_next <stdlib.h>", 3).has_value());
  r = at("#include /*c*/ \"a.h\" /* x */", 15);
  REQUIRE(r.has_value());
  CHECK_EQ(r->spelling, std::string("a.h"));
  // not directives with a literal spelling
  CHECK(!at("#include HEADER", 3).has_value());
  CHECK(!at("#include \"open.h", 3).has_value());
  CHECK(!at("#include <>", 3).has_value());
  CHECK(!at("// #include \"a.h\"", 5).has_value());
  CHECK(!at("/* #include \"a.h\" */", 5).has_value());
  CHECK(!at("#define X \"a.h\"", 3).has_value());
  CHECK(!at("int include = 1;", 3).has_value());
  CHECK(!at("#include \"a.h\"", -1).has_value());
}

TEST(vsnav_roles_of_a_reference) {
  auto role = [](const std::string& line, const std::string& name, int nth = 0) {
    size_t p = std::string::npos;
    for (int i = 0; i <= nth; ++i) p = line.find(name, p == std::string::npos ? 0 : p + 1);
    return std::string(roleName(classifyReference(line, static_cast<int>(p), static_cast<int>(name.size()))));
  };
  CHECK_EQ(role("  x = 1;", "x"), std::string("Write"));
  CHECK_EQ(role("  int x = 1;", "x"), std::string("Write"));
  CHECK_EQ(role("  x += 2;", "x"), std::string("Write"));
  CHECK_EQ(role("  x <<= 2;", "x"), std::string("Write"));
  CHECK_EQ(role("  x++;", "x"), std::string("Write"));
  CHECK_EQ(role("  --x;", "x"), std::string("Write"));
  CHECK_EQ(role("  a[i] = 3;", "a"), std::string("Write"));
  CHECK_EQ(role("  a[i][j] -= 3;", "a"), std::string("Write"));
  CHECK_EQ(role("  if (x == 1) {}", "x"), std::string("Read"));
  CHECK_EQ(role("  if (x <= 1) {}", "x"), std::string("Read"));
  CHECK_EQ(role("  if (x != 1) {}", "x"), std::string("Read"));
  CHECK_EQ(role("  return x;", "x"), std::string("Read"));
  CHECK_EQ(role("  y = x + 1;", "x"), std::string("Read"));
  CHECK_EQ(role("  y = x + 1;", "y"), std::string("Write"));
  CHECK_EQ(role("  f(x);", "x"), std::string("Read"));
  CHECK_EQ(role("  x = 1; // x = 2", "x"), std::string("Write"));
  CHECK_EQ(role("#define LIMIT 10", "LIMIT"), std::string("Other"));
  CHECK_EQ(role("#include \"limit.h\"", "limit"), std::string("Other"));
  CHECK_EQ(role("#if LIMIT > 3", "LIMIT"), std::string("Read"));
  CHECK_EQ(role("using ns::limit;", "limit"), std::string("Other"));
  CHECK_EQ(role("typedef int limit;", "limit"), std::string("Other"));
  CHECK_EQ(role("namespace limit {", "limit"), std::string("Other"));
}

// ---- search path ---------------------------------------------------------------------------------------------------

namespace {
struct Tree {
  fs::path dir;
  explicit Tree(const char* name) {
    dir = fs::weakly_canonical(fs::temp_directory_path()) / name;
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
  }
  ~Tree() {
    std::error_code ec;
    fs::remove_all(dir, ec);
  }
  void put(const std::string& rel, const std::string& text) {
    fs::create_directories((dir / rel).parent_path());
    std::ofstream(dir / rel, std::ios::binary) << text;
  }
};
}  // namespace

TEST(vsnav_include_search_path_follows_the_compile_command) {
  Tree t("tenfold_vsnav_path");
  t.put("src/a.cpp", "");
  t.put("src/local.h", "");
  t.put("src/dup.h", "");
  t.put("inc/dup.h", "");
  t.put("inc/sub/deep.h", "");
  t.put("quote/q.h", "");
  t.put("sys/sysh.h", "");
  CompileCommand cc;
  cc.directory = t.dir;
  cc.file = "src/a.cpp";
  cc.arguments = {"clang++", "-Iinc", "-iquote", "quote", "-isystem", (t.dir / "sys").string(), "-c", "src/a.cpp"};
  const SearchPath sp = searchPathOf(cc);
  CHECK_EQ(sp.quote.size(), size_t(1));
  CHECK_EQ(sp.angle.size(), size_t(2));
  const fs::path includer = t.dir / "src/a.cpp";
  auto res = [&](const std::string& spelling, bool angled) {
    IncludeRef r;
    r.spelling = spelling;
    r.angled = angled;
    auto f = resolveInclude(r, includer, sp);
    return f ? fs::relative(*f, t.dir).generic_string() : std::string("-");
  };
  CHECK_EQ(res("local.h", false), std::string("src/local.h"));
  CHECK_EQ(res("dup.h", false), std::string("src/dup.h"));  // the includer's directory first
  CHECK_EQ(res("dup.h", true), std::string("inc/dup.h"));   // <...> never looks next to the includer
  CHECK_EQ(res("sub/deep.h", false), std::string("inc/sub/deep.h"));
  CHECK_EQ(res("sub\\deep.h", false), std::string("inc/sub/deep.h"));
  CHECK_EQ(res("q.h", false), std::string("quote/q.h"));
  CHECK_EQ(res("q.h", true), std::string("-"));  // -iquote is for "..." only
  CHECK_EQ(res("sysh.h", true), std::string("sys/sysh.h"));
  CHECK_EQ(res("missing.h", false), std::string("-"));

  CompileCommand cl;  // clang-cl spelling
  cl.directory = t.dir;
  cl.file = "src/a.cpp";
  cl.arguments = {"clang-cl", "/I", "inc", "/Iquote", "/c", "src/a.cpp"};
  const SearchPath sc = searchPathOf(cl);
  CHECK_EQ(sc.angle.size(), size_t(2));
}

// ---- the engine ----------------------------------------------------------------------------------------------------

namespace {
struct NavProject {
  Tree tree{"tenfold_vsnav_engine"};
  std::unique_ptr<Editor> ed;
  std::string mainText;

  explicit NavProject(bool clangd = false) {
    tree.put("src/util.h", "/*top*/#pragma once\ninline int util_value() { return 1; }\n");
    tree.put("src/dup.h", "#pragma once\n");
    tree.put("include/dup.h", "#pragma once\n");
    tree.put("include/sub/deep.h", "#pragma once\nint deep_value();\n");
    tree.put("lib/lib.h", "#pragma once\n");
    tree.put("src/lonely.h", "#pragma once\nint lonely_value();\n");
    tree.put("deep/down/orphan.h", "#pragma once\nint orphan_down();\n");
    tree.put("deep/other/orphan.h", "#pragma once\nint orphan_other();\n");
    mainText =
        "#include \"util.h\"\n"
        "#include \"dup.h\"\n"
        "#include \"sub/deep.h\"\n"
        "#include <lib.h>\n"
        "#include <no_such_header.h>\n"
        "#include \"deep/down/orphan.h\"\n"
        "int counter = 0;\n"
        "int main() {\n"
        "  counter = util_value();\n"
        "  counter += 2;\n"
        "  return counter;\n"
        "}\n";
    tree.put("src/main.cpp", mainText);
    const std::string d = tree.dir.generic_string();
    std::ofstream(tree.dir / "compile_commands.json")
        << "[{\"directory\":\"" << d << "\",\"file\":\"" << d << "/src/main.cpp\",\"arguments\":[\"clang++\","
        << "\"-std=c++20\",\"-Iinclude\",\"-Ilib\",\"-c\",\"" << d << "/src/main.cpp\"]}]\n";
    Editor::Options o;
    o.semantic.useClangd = clangd;
    o.semantic.persistFastIndex = false;
    ed = std::make_unique<Editor>(tree.dir, o);
    std::string err;
    ed->start(&err);
    ed->semantic().waitForFastIndex(30000);
    std::string e2;
    ed->open(tree.dir / "src/main.cpp", &e2);
    if (clangd) {
      ed->semantic().waitForFile(tree.dir / "src/main.cpp", 60000);
      ed->semantic().waitForClangdIndex(60000);
    }
  }
  ~NavProject() { ed.reset(); }

  struct Got {
    std::vector<std::pair<Confidence, std::vector<SymbolLocation>>> calls;
  };
  Got ask(int line, int col, bool decl = false) {
    ed->activateDocument(*ed->document(tree.dir / "src/main.cpp"), Position{line, col});
    auto got = std::make_shared<Got>();
    bool done = false;
    auto cb = [&, got](Confidence c, const std::vector<SymbolLocation>& l, const std::string& err) {
      got->calls.push_back({c, l});
      if (c == Confidence::Authoritative || !err.empty()) done = true;
    };
    if (decl) ed->gotoDeclaration(cb);
    else ed->gotoDefinition(cb);
    ed->waitFor([&] { return done; }, 20000);
    return *got;
  }
  std::string rel(const fs::path& p) const { return fs::relative(p, tree.dir).generic_string(); }
};
}  // namespace

TEST(vsnav_f12_on_an_include_line_opens_the_file_without_clangd) {
  NavProject pr;
  // Caret on the opening quote, in the middle of the spelling, and at the very start of the line.
  auto g = pr.ask(0, 9);
  REQUIRE_EQ(g.calls.size(), size_t(1));
  CHECK(g.calls[0].first == Confidence::Authoritative);
  REQUIRE_EQ(g.calls[0].second.size(), size_t(1));
  CHECK_EQ(pr.rel(g.calls[0].second[0].loc.file), std::string("src/util.h"));
  CHECK_EQ(g.calls[0].second[0].loc.pos.line, 0);
  CHECK_EQ(g.calls[0].second[0].loc.pos.column, 0);
  CHECK_EQ(g.calls[0].second[0].extent.end.column, 19);  // the first line of the file
  // gotoDefinition went to the file (Editor::gotoLocation), so the history has the jump.
  CHECK_EQ(pr.ed->cursor().file.filename().string(), std::string("util.h"));
  pr.ed->activateDocument(*pr.ed->document(pr.tree.dir / "src/main.cpp"), Position{0, 0});
  g = pr.ask(1, 12);  // "dup.h": the includer's directory beats -Iinclude
  REQUIRE_EQ(g.calls.size(), size_t(1));
  CHECK_EQ(pr.rel(g.calls[0].second[0].loc.file), std::string("src/dup.h"));
  g = pr.ask(2, 0);  // "sub/deep.h" through -Iinclude
  REQUIRE_EQ(g.calls.size(), size_t(1));
  CHECK_EQ(pr.rel(g.calls[0].second[0].loc.file), std::string("include/sub/deep.h"));
  g = pr.ask(3, 11, true);  // <lib.h> through -Ilib; Go To Declaration does the same
  REQUIRE_EQ(g.calls.size(), size_t(1));
  CHECK_EQ(pr.rel(g.calls[0].second[0].loc.file), std::string("lib/lib.h"));
  // The caret is not on the directive: an ordinary name is not an include.
  g = pr.ask(8, 14);  // util_value
  REQUIRE(!g.calls.empty());
  bool toUtil = false;
  for (const auto& l : g.calls.back().second) toUtil = toUtil || pr.rel(l.loc.file) == "src/util.h";
  CHECK(toUtil);
}

TEST(vsnav_include_that_is_not_found_answers_nothing) {
  NavProject pr;
  auto g = pr.ask(4, 10);  // <no_such_header.h>
  REQUIRE(!g.calls.empty());
  CHECK(g.calls.back().second.empty());
  CHECK_EQ(pr.ed->cursor().file.filename().string(), std::string("main.cpp"));
}

TEST(vsnav_include_off_the_search_path_falls_back_to_the_nearest_workspace_file) {
  NavProject pr;
  // "deep/down/orphan.h" is on no search path; two workspace files end with that spelling's tail, the fast index
  // offers the one whose path matches: an approximate answer (there is no clangd to make it final).
  auto g = pr.ask(5, 12);
  REQUIRE(!g.calls.empty());
  REQUIRE(!g.calls.back().second.empty());
  CHECK(g.calls.back().first == Confidence::Approximate);
  CHECK_EQ(pr.rel(g.calls.back().second[0].loc.file), std::string("deep/down/orphan.h"));
}

TEST(vsnav_include_with_clangd_is_exact_and_references_carry_roles) {
  if (findExecutable("clangd").empty()) return;
  NavProject pr(true);
  auto g = pr.ask(0, 9);
  REQUIRE(!g.calls.empty());
  CHECK(g.calls.back().first == Confidence::Authoritative);
  REQUIRE_EQ(g.calls.back().second.size(), size_t(1));
  CHECK_EQ(pr.rel(g.calls.back().second[0].loc.file), std::string("src/util.h"));

  // Find All References on `counter`: the declaration first, then the places by line; roles from the text.
  pr.ed->activateDocument(*pr.ed->document(pr.tree.dir / "src/main.cpp"), Position{6, 4});
  std::vector<SymbolLocation> refs;
  bool done = false;
  pr.ed->findReferences([&](Confidence, const std::vector<SymbolLocation>& l, const std::string&) {
    refs = l;
    done = true;
  });
  pr.ed->waitFor([&] { return done; }, 30000);
  REQUIRE_EQ(refs.size(), size_t(4));
  CHECK_EQ(refs[0].role, std::string("Definition"));
  CHECK_EQ(refs[0].loc.pos.line, 6);
  CHECK_EQ(refs[1].role, std::string("Write"));  // counter = util_value();
  CHECK_EQ(refs[2].role, std::string("Write"));  // counter += 2;
  CHECK_EQ(refs[3].role, std::string("Read"));   // return counter;
  CHECK(refs[1].loc.pos.line < refs[2].loc.pos.line && refs[2].loc.pos.line < refs[3].loc.pos.line);
}

TEST(vsnav_miscellaneous_files_are_files_in_no_translation_unit) {
  NavProject pr;
  SemanticEngine& se = pr.ed->semantic();
  CHECK(!se.isMiscFile(pr.tree.dir / "src/main.cpp"));        // a translation unit
  CHECK(!se.isMiscFile(pr.tree.dir / "src/util.h"));          // included by it
  CHECK(!se.isMiscFile(pr.tree.dir / "include/sub/deep.h"));  // through -Iinclude
  CHECK(!se.isMiscFile(pr.tree.dir / "lib/lib.h"));           // through -Ilib
  CHECK(se.isMiscFile(pr.tree.dir / "src/lonely.h"));         // nothing includes it
  CHECK(se.isMiscFile(pr.tree.dir / "scratch.cpp"));          // not in the database
  CHECK(!se.miscFileDiagnostics());                           // the default: no squiggles there
}

TEST(vsnav_no_database_means_no_miscellaneous_files) {
  Tree t("tenfold_vsnav_nodb");
  t.put("a.cpp", "int a;\n");
  Editor::Options o;
  o.semantic.useClangd = false;
  o.semantic.persistFastIndex = false;
  Editor ed(t.dir, o);
  std::string err;
  ed.start(&err);
  ed.semantic().waitForFastIndex(30000);
  // The heuristic database lists every source file; a header nobody includes is outside it.
  CHECK(!ed.semantic().isMiscFile(t.dir / "a.cpp"));
}

TEST(vsnav_diagnostics_of_a_miscellaneous_file_are_held_back_until_the_switch_is_on) {
  if (findExecutable("clangd").empty()) return;
  NavProject pr(true);
  const fs::path main = pr.tree.dir / "src/main.cpp", scratch = pr.tree.dir / "scratch.cpp";
  pr.tree.put("scratch.cpp", "int broken = ;\n");
  std::string err;
  REQUIRE(pr.ed->open(scratch, &err));
  pr.ed->semantic().waitForFile(scratch, 60000);
  Document* doc = pr.ed->document(main);
  REQUIRE(doc != nullptr);
  pr.ed->activateDocument(*doc, Position{0, 0});
  pr.ed->setSelections({Selection{{0, 0}, {0, 0}}});
  pr.ed->insertText("int also_broken = ;\n");
  // A translation unit of the database keeps its squiggles ...
  CHECK(pr.ed->waitFor([&] { return !pr.ed->diagnostics(main).empty(); }, 30000));
  // ... a file in none shows none (VS "Miscellaneous Files"), however long clangd has had to report.
  pr.ed->waitFor([] { return false; }, 1500);
  CHECK(pr.ed->diagnostics(scratch).empty());
  CHECK(!pr.ed->miscFileDiagnostics());
  // The switch shows what clangd said, and turning it off hides it again.
  pr.ed->setMiscFileDiagnostics(true);
  CHECK(pr.ed->waitFor([&] { return !pr.ed->diagnostics(scratch).empty(); }, 30000));
  pr.ed->setMiscFileDiagnostics(false);
  CHECK(pr.ed->diagnostics(scratch).empty());
  CHECK(!pr.ed->diagnostics(main).empty());
}
