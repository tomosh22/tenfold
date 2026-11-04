// Translation-unit context for included files (src/semantic/TuContext.*): the pure planning logic, and, with a real
// clangd, the engine giving a .tpp / unity part the context of its includer.
#include <atomic>
#include <fstream>
#include <iostream>
#include <map>
#include <set>

#include "semantic/TuContext.h"
#include "test_main.h"

using namespace tf;
namespace fs = std::filesystem;

TEST(tu_scan_directives) {
  const std::string text =
      "#pragma once\n"
      "#include <vector>\n"
      "/* #include \"no.h\"\n"
      "#include \"no2.h\" */\n"
      "// #include \"no3.h\"\n"
      "#ifdef A\n"
      "  #  include \"a/b.h\"\n"
      "#if X\n"
      "#include \"c.h\" // trailing\n"
      "#endif\n"
      "#endif\n"
      "#define LONG \\\n"
      "  #include \"no4.h\"\n"
      "#include \"d.h\"";
  auto d = tu::scanIncludeDirectives(text);
  REQUIRE(d.size() == 4);
  CHECK_EQ(d[0].spelling, "vector");
  CHECK(d[0].angled);
  CHECK_EQ(d[0].line, 1);
  CHECK_EQ(d[0].condDepth, 0);
  CHECK_EQ(d[1].spelling, "a/b.h");
  CHECK_EQ(d[1].line, 6);
  CHECK_EQ(d[1].condDepth, 1);
  CHECK_EQ(d[2].spelling, "c.h");
  CHECK_EQ(d[2].condDepth, 2);
  CHECK_EQ(d[3].spelling, "d.h");
  CHECK_EQ(d[3].line, 13);
  CHECK_EQ(d[3].condDepth, 0);
  CHECK_EQ(text.substr(d[3].offset, 4), "#inc");
}

TEST(tu_prefix_keeps_lines_and_closes_conditionals) {
  const std::string text = "#ifndef G\n#define G\nstruct S {};\n#include \"x.tpp\"\nint after;\n#endif\n";
  auto d = tu::scanIncludeDirectives(text);
  REQUIRE(d.size() == 1);
  CHECK_EQ(d[0].condDepth, 1);
  const std::string p = tu::prefixBefore(text, d[0]);
  CHECK_EQ(p, "#ifndef G\n#define G\nstruct S {};\n#endif\n");
}

TEST(tu_include_spelling_resolution) {
  const fs::path root = fs::weakly_canonical(fs::temp_directory_path()) / "tenfold_tu_resolve";
  std::error_code ec;
  fs::remove_all(root, ec);
  fs::create_directories(root / "inc" / "tb");
  fs::create_directories(root / "src");
  std::ofstream(root / "inc" / "tb" / "stack.tpp") << "x";
  std::ofstream(root / "inc" / "tb" / "stack.h") << "x";
  std::ofstream(root / "src" / "stack.tpp") << "x";  // a different file with the same name
  const fs::path tpp = root / "inc" / "tb" / "stack.tpp";
  CHECK(tu::includeSpellingResolves("stack.tpp", root / "inc" / "tb" / "stack.h", tpp));  // next to the includer
  CHECK(tu::includeSpellingResolves("tb/stack.tpp", root / "src" / "main.cpp", tpp));     // via the include path
  CHECK(!tu::includeSpellingResolves("stack.tpp", root / "src" / "main.cpp", tpp));       // src/stack.tpp wins
  CHECK(tu::includeSpellingResolves("../inc/tb/stack.tpp", root / "src" / "main.cpp", tpp));
  CHECK(!tu::includeSpellingResolves("other.tpp", root / "inc" / "tb" / "stack.h", tpp));
  fs::remove_all(root, ec);
}

namespace {
struct FakeTree {
  std::map<std::string, std::string> files;    // path -> text
  std::map<std::string, CompileCommand> cmds;  // path -> command
  tu::Env env() {
    tu::Env e;
    e.includersOf = [this](const fs::path& t) {
      std::vector<fs::path> out;
      for (auto& [p, text] : files)
        for (auto& d : tu::scanIncludeDirectives(text))
          if (fs::path(d.spelling).filename() == t.filename()) {
            out.push_back(p);
            break;
          }
      return out;
    };
    e.readText = [this](const fs::path& f, std::string& t) {
      auto it = files.find(f.generic_string());
      if (it == files.end()) return false;
      t = it->second;
      return true;
    };
    e.commandFor = [this](const fs::path& f) -> const CompileCommand* {
      auto it = cmds.find(f.generic_string());
      return it == cmds.end() ? nullptr : &it->second;
    };
    return e;
  }
};
CompileCommand cmd(const std::string& file) {
  CompileCommand c;
  c.directory = "/w";
  c.file = file;
  c.arguments = {"clang++", "-std=c++20", "-I/w/inc", "-c", file, "-o", "x.o"};
  return c;
}
}  // namespace

TEST(tu_plan_chain_and_arguments) {
  FakeTree t;
  t.files["/w/main.cpp"] = "#include <vector>\n#include \"stack.h\"\nint main() {}\n";
  t.files["/w/stack.h"] = "#pragma once\ntemplate <class T> struct S { void f(); };\n#include \"stack.tpp\"\n";
  t.files["/w/stack.tpp"] = "template <class T> void S<T>::f() {}\n";
  t.cmds["/w/main.cpp"] = cmd("/w/main.cpp");
  auto plan = tu::planContext("/w/stack.tpp", t.env());
  REQUIRE(plan.has_value());
  REQUIRE(plan->links.size() == 2);
  CHECK_EQ(plan->links[0].file.generic_string(), "/w/main.cpp");
  CHECK_EQ(plan->links[0].directive.line, 1);
  CHECK_EQ(plan->links[0].prefix, "#include <vector>\n");
  CHECK_EQ(plan->links[1].file.generic_string(), "/w/stack.h");
  CHECK_EQ(plan->links[1].prefix, "#pragma once\ntemplate <class T> struct S { void f(); };\n");
  tu::CommandOptions co;
  auto args = tu::buildArguments(*plan, "/w/stack.tpp", {"/c/a.h", "/c/b.h"}, co);
  const std::vector<std::string> want = {"clang++", "-std=c++20", "-I/w/inc", "-c",         "-include",
                                         "/c/a.h",  "-iquote",    "/w",       "-include",   "/c/b.h",
                                         "-iquote", "/w",         "-x",       "c++-header", "/w/stack.tpp"};
  CHECK(args == want);
}

TEST(tu_plan_none_when_nothing_includes_the_file_or_it_brings_its_own_context) {
  FakeTree t;
  t.files["/w/main.cpp"] = "#include \"a.inl\"\n";
  t.files["/w/a.inl"] = "int x;\n";
  t.files["/w/lone.inl"] = "int y;\n";
  t.cmds["/w/main.cpp"] = cmd("/w/main.cpp");
  CHECK(tu::planContext("/w/a.inl", t.env()).has_value());
  CHECK(!tu::planContext("/w/lone.inl", t.env()).has_value());
  // A self-contained inline file includes the header that includes it: it is parsed on its own.
  t.files["/w/main.cpp"] = "#include \"b.h\"\n";
  t.files["/w/b.h"] = "struct B { void f(); };\n#include \"b.inl\"\n";
  t.files["/w/b.inl"] = "#include \"b.h\"\ninline void B::f() {}\n";
  CHECK(!tu::planContext("/w/b.inl", t.env()).has_value());
  // Cycles do not loop.
  t.files["/w/c.h"] = "#include \"d.h\"\n";
  t.files["/w/d.h"] = "#include \"c.h\"\n";
  t.files["/w/c.inl"] = "int z;\n";
  CHECK(!tu::planContext("/w/c.h", t.env()).has_value());
}

TEST(tu_plan_unity_parts_see_their_predecessors) {
  FakeTree t;
  t.files["/w/unity.cpp"] = "#include \"api.h\"\n#include \"alpha.cpp\"\n#include \"beta.cpp\"\n";
  t.files["/w/alpha.cpp"] = "int alpha() { return 1; }\n";
  t.files["/w/beta.cpp"] = "int beta() { return alpha(); }\n";
  t.cmds["/w/unity.cpp"] = cmd("/w/unity.cpp");
  auto plan = tu::planContext("/w/beta.cpp", t.env());
  REQUIRE(plan.has_value());
  REQUIRE(plan->links.size() == 1);
  CHECK_EQ(plan->links[0].prefix, "#include \"api.h\"\n#include \"alpha.cpp\"\n");
  tu::CommandOptions co;
  co.headerLike = false;
  auto args = tu::buildArguments(*plan, "/w/beta.cpp", {"/c/u.h"}, co);
  CHECK_EQ(args[args.size() - 3], std::string("-x"));
  CHECK_EQ(args[args.size() - 2], std::string("c++"));
  CHECK_EQ(args.back(), std::string("/w/beta.cpp"));
}

TEST(tu_arguments_clang_cl) {
  FakeTree t;
  t.files["/w/main.cpp"] = "#include \"x.inl\"\n";
  t.files["/w/x.inl"] = "int x;\n";
  CompileCommand c;
  c.directory = "/w";
  c.file = "/w/main.cpp";
  c.arguments = {"clang-cl", "/c", "/std:c++20", "main.cpp"};
  t.cmds["/w/main.cpp"] = c;
  auto plan = tu::planContext("/w/x.inl", t.env());
  REQUIRE(plan.has_value());
  tu::CommandOptions co;
  co.clMode = true;
  auto args = tu::buildArguments(*plan, "/w/x.inl", {"/c/p.h"}, co);
  const std::vector<std::string> want = {"clang-cl", "/c", "/std:c++20", "/FI/c/p.h", "/I/w", "/TP", "/w/x.inl"};
  CHECK(args == want);
}

// ---- the engine with a real clangd ----------------------------------------------------------------------------------

#include "app/Editor.h"
#include "core/PathUtil.h"

namespace {

void writeFile(const fs::path& p, const std::string& text) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary) << text;
}

bool haveClangd() {
  const char* path = std::getenv("PATH");
  if (!path) return false;
  std::string s = path;
  size_t b = 0;
  while (b <= s.size()) {
    size_t e = s.find(':', b);
    if (e == std::string::npos) e = s.size();
    if (fs::exists(fs::path(s.substr(b, e - b)) / "clangd")) return true;
    b = e + 1;
  }
  return false;
}

// A project whose Stack members are defined in stack.tpp, included at the end of stack.h; only main.cpp has a command.
struct TpuProject {
  fs::path root;
  explicit TpuProject(const std::string& name) {
    root = fs::weakly_canonical(fs::temp_directory_path()) / ("tenfold_tu_" + name);
    std::error_code ec;
    fs::remove_all(root, ec);
    writeFile(root / "include/stack.h",
              "#pragma once\n#include <vector>\ntemplate <class T> class Stack {\n public:\n  T& top();\n  void "
              "push(const T& v);\n"
              " private:\n  std::vector<T> data_;\n};\n#include \"stack.tpp\"\n");
    writeFile(root / "include/stack.tpp",
              "#pragma once\ntemplate <class T> T& Stack<T>::top() { return data_.back(); }\n"
              "template <class T> void Stack<T>::push(const T& v) { data_.push_back(v); }\n");
    writeFile(root / "src/main.cpp", "#include \"stack.h\"\nint main() { Stack<int> s; s.push(1); return s.top(); }\n");
    writeFile(root / "compile_commands.json", "[{\"directory\":\"" + root.generic_string() + "\",\"file\":\"" +
                                                  (root / "src/main.cpp").generic_string() +
                                                  "\",\"arguments\":[\"clang++\",\"-std=c++20\",\"-I" +
                                                  (root / "include").generic_string() + "\",\"-c\",\"" +
                                                  (root / "src/main.cpp").generic_string() + "\"]}]\n");
  }
  ~TpuProject() {
    std::error_code ec;
    fs::remove_all(root, ec);
  }
};

std::unique_ptr<Editor> startEditor(const fs::path& root) {
  Editor::Options o;
  o.semantic.useClangd = true;
  o.semantic.clangdBudgetMs = 150;
  auto ed = std::make_unique<Editor>(root, o);
  std::string err;
  if (!ed->start(&err)) std::cerr << "    start: " << err << "\n";
  return ed;
}

std::vector<SymbolLocation> navigate(Editor& ed, const fs::path& file, int line, int col, bool refs) {
  std::vector<SymbolLocation> out;
  bool done = false;
  ed.gotoLocation({file, {line, col}});
  auto cb = [&](Confidence c, const std::vector<SymbolLocation>& l, const std::string&) {
    if (c == Confidence::Authoritative || !ed.semantic().clangd()) {
      out = l;
      done = true;
    }
  };
  if (refs) ed.findReferences(cb);
  else ed.gotoDefinition(cb);
  ed.waitFor([&] { return done; }, 60000);
  return out;
}

}  // namespace

TEST(tu_engine_gives_a_tpp_the_context_of_its_includer) {
  if (!haveClangd()) {
    std::cerr << "    (clangd not found: skipping)\n";
    return;
  }
  TpuProject p("tpp");
  auto ed = startEditor(p.root);
  const fs::path tpp = p.root / "include/stack.tpp", hdr = p.root / "include/stack.h";
  Document* d = ed->open(p.root / "src/main.cpp");
  REQUIRE(d != nullptr);
  CHECK(ed->semantic().waitForFile(p.root / "src/main.cpp", 90000));
  CHECK(ed->semantic().waitForClangdIndex(90000));
  REQUIRE(ed->open(tpp) != nullptr);
  CHECK(ed->semantic().waitForFile(tpp, 90000));
  auto ctx = ed->semantic().tuContext(tpp);
  REQUIRE(ctx.has_value());
  CHECK_EQ(ctx->root.filename().string(), "main.cpp");
  REQUIRE(ctx->chain.size() == 2u);
  CHECK_EQ(ctx->chain[1].filename().string(), "stack.h");

  // `data_` inside stack.tpp resolves to the member declared in stack.h (not to nothing), and the answer is located in
  // stack.h, not in the prefix copy clangd saw.
  auto defs = navigate(*ed, tpp, 1,
                       static_cast<int>(std::string("template <class T> T& Stack<T>::top() { return ").size()), false);
  REQUIRE(!defs.empty());
  CHECK_EQ(defs.front().loc.file.filename().string(), "stack.h");
  CHECK_EQ(defs.front().loc.pos.line, 7);
  for (auto& l : defs) CHECK(l.loc.file.string().find(".tenfold") == std::string::npos);

  // References of Stack::top from the header still include the definition inside stack.tpp, now that stack.tpp is open.
  auto refs = navigate(*ed, hdr, 4, 6, true);
  std::set<std::string> where;
  for (auto& r : refs) where.insert(r.loc.file.filename().string() + ":" + std::to_string(r.loc.pos.line));
  CHECK(where.count("stack.tpp:1"));
  CHECK(where.count("main.cpp:1"));
  CHECK(where.count("stack.h:4"));
  CHECK_EQ(where.size(), refs.size());  // no duplicates from the prefix copy
}

TEST(tu_engine_files_that_are_translation_units_or_headers_get_no_context) {
  if (!haveClangd()) {
    std::cerr << "    (clangd not found: skipping)\n";
    return;
  }
  TpuProject p("none");
  auto ed = startEditor(p.root);
  REQUIRE(ed->open(p.root / "src/main.cpp") != nullptr);
  REQUIRE(ed->open(p.root / "include/stack.h") != nullptr);
  CHECK(!ed->semantic().tuContext(p.root / "src/main.cpp").has_value());
  CHECK(!ed->semantic().tuContext(p.root / "include/stack.h").has_value());
}

TEST(tu_engine_rename_coverage_reports_spellings_the_edit_set_misses) {
  TpuProject p("cov");
  Editor::Options o;
  o.semantic.useClangd = false;
  Editor ed(p.root, o);
  std::string err;
  REQUIRE(ed.start(&err));
  CHECK(ed.semantic().waitForFastIndex(30000));
  const fs::path hdr = p.root / "include/stack.h", tpp = p.root / "include/stack.tpp", main = p.root / "src/main.cpp";
  FileEdits edits;  // clangd renamed the declaration and the call, but not the definition in stack.tpp
  edits[hdr].push_back({Range{{4, 5}, {4, 8}}, "peek"});
  auto colOf = [](const fs::path& f, int line, const std::string& tok) {
    std::ifstream in(f);
    std::string l;
    for (int i = 0; i <= line; ++i) std::getline(in, l);
    return static_cast<int>(l.find(tok));
  };
  const int mc = colOf(main, 1, "top"), tc = colOf(tpp, 1, "top");
  edits[main].push_back({Range{{1, mc}, {1, mc + 3}}, "peek"});
  auto cov = ed.semantic().renameCoverage("top", edits, hdr);
  REQUIRE(cov.total == 1u);
  CHECK_EQ(cov.missed[0].file.filename().string(), "stack.tpp");
  CHECK_EQ(cov.missed[0].pos.line, 1);
  // With the definition in the edit set nothing is missed.
  edits[tpp].push_back({Range{{1, tc}, {1, tc + 3}}, "peek"});
  CHECK_EQ(ed.semantic().renameCoverage("top", edits, hdr).total, 0u);
  // Comments and strings do not count.
  writeFile(p.root / "src/extra.cpp", "#include \"stack.h\"\n// top\nconst char* s = \"top\";\n");
  CHECK_EQ(ed.semantic().renameCoverage("top", edits, hdr).total, 0u);
}

TEST(tu_ends_inside_body_detects_class_body_fragments) {
  CHECK(tu::endsInsideBody("class S {\n public:\n"));
  CHECK(tu::endsInsideBody("void f() {\n  int x;\n"));
  CHECK(!tu::endsInsideBody("namespace a {\nnamespace b {\n"));
  CHECK(!tu::endsInsideBody("extern \"C\" {\n"));
  CHECK(!tu::endsInsideBody("struct S { int x; };\nenum class E { A, B };\n"));
  CHECK(!tu::endsInsideBody("// class S {\n/* { */ const char* s = \"{\";\n#define OPEN {\n"));
  CHECK(tu::endsInsideBody("namespace a {\nstruct S {\n"));
}

TEST(tu_engine_class_body_fragments_are_not_sent_to_clangd) {
  if (!haveClangd()) {
    std::cerr << "    (clangd not found: skipping)\n";
    return;
  }
  TpuProject p("frag");
  writeFile(p.root / "include/box.h", "#pragma once\nclass Box {\n public:\n#include \"box_members.inl\"\n};\n");
  writeFile(p.root / "include/box_members.inl", "int size() const { return n_; }\nint n_ = 0;\n");
  writeFile(p.root / "src/main.cpp",
            "#include \"stack.h\"\n#include \"box.h\"\nint main() { Box b; return b.size(); }\n");
  auto ed = startEditor(p.root);
  const fs::path inl = p.root / "include/box_members.inl";
  REQUIRE(ed->open(p.root / "src/main.cpp") != nullptr);
  CHECK(ed->semantic().waitForClangdIndex(90000));
  REQUIRE(ed->open(inl) != nullptr);
  CHECK(!ed->semantic().tuContext(inl).has_value());
  CHECK(ed->semantic().fileReady(inl));  // "ready" at once: nothing to wait for
  // The references of Box::size still include its declaration in the fragment (the open document did not shadow it).
  auto refs = navigate(*ed, p.root / "src/main.cpp", 2, 31, true);
  std::set<std::string> where;
  for (auto& r : refs) where.insert(r.loc.file.filename().string());
  CHECK(where.count("box_members.inl"));
  CHECK(where.count("main.cpp"));
  // A query inside the fragment is an error naming the reason, not a hang.
  auto inside = navigate(*ed, inl, 0, 4, false);
  CHECK(inside.empty() || inside.front().confidence == Confidence::Approximate);
}

TEST(tu_engine_implementations_prefer_the_out_of_line_definition) {
  if (!haveClangd()) {
    std::cerr << "    (clangd not found: skipping)\n";
    return;
  }
  TpuProject p("impl");
  writeFile(p.root / "src/h.h", "struct B { virtual int f(); };\nstruct D : B { int f() override; };\n");
  writeFile(
      p.root / "src/d.cpp",
      "#include \"h.h\"\nint B::f() { return 1; }\nint D::f() { return 2; }\nint main() { D d; return d.f(); }\n");
  writeFile(p.root / "compile_commands.json", "[{\"directory\":\"" + p.root.generic_string() + "\",\"file\":\"" +
                                                  (p.root / "src/d.cpp").generic_string() +
                                                  "\",\"arguments\":[\"clang++\",\"-std=c++20\",\"-c\",\"" +
                                                  (p.root / "src/d.cpp").generic_string() + "\"]}]\n");
  auto ed = startEditor(p.root);
  REQUIRE(ed->open(p.root / "src/d.cpp") != nullptr);
  CHECK(ed->semantic().waitForClangdIndex(90000));
  REQUIRE(ed->open(p.root / "src/h.h") != nullptr);
  CHECK(ed->semantic().waitForFile(p.root / "src/h.h", 90000));
  std::vector<SymbolLocation> impls;
  std::atomic<bool> done{false};
  ed->semantic().findImplementations(*ed->document(p.root / "src/h.h"), {0, 23},
                                     [&](Confidence c, std::vector<SymbolLocation> l, const std::string&) {
                                       if (c == Confidence::Authoritative) {
                                         impls = std::move(l);
                                         done = true;
                                       }
                                     });
  ed->waitFor([&] { return done.load(); }, 60000);
  REQUIRE(done);
  bool outOfLine = false, inClass = false;
  for (auto& l : impls)
    std::cerr << "    impl: " << l.loc.file.filename().string() << ":" << l.loc.pos.line << ":" << l.loc.pos.column
              << "\n";
  for (auto& l : impls) {
    if (l.loc.file.filename() == "d.cpp" && l.loc.pos.line == 2) outOfLine = true;
    if (l.loc.file.filename() == "h.h" && l.loc.pos.line == 1) inClass = true;
  }
  CHECK(outOfLine);
  CHECK(!inClass);
}
