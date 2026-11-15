// FAST-DECL: names declared through macros, X-macro lists, forward declarations and inline namespaces in the fast tier
// (docs/ARCHITECTURE.md, "Names declared through macros"): the extractor's records, the macro expander, the expansion
// layer of FastIndex, the persisted store, and the engine's answers.
#include <algorithm>
#include <fstream>
#include <map>
#include <set>

#include "app/Editor.h"
#include "semantic/FastIndex.h"
#include "semantic/FastIndexStore.h"
#include "semantic/MacroExpand.h"
#include "test_main.h"

using namespace tf;
namespace fs = std::filesystem;

namespace {

std::shared_ptr<FileSymbols> extract(const std::string& path, const std::string& text) {
  auto f = std::make_shared<FileSymbols>();
  f->file = path;
  extractFile(text, *f, isCSourceFile(path));
  return f;
}

// A resolver over the templates of one file: the last definition of the name with a fitting argument count.
mx::Resolver resolverOf(const FileSymbols& f) {
  return [&f](const std::string& name, size_t argc) -> mx::Resolved {
    const MacroTemplate* best = nullptr;
    for (const MacroTemplate& t : f.templates) {
      if (t.name != name) continue;
      const bool fits =
          t.objectLike ? argc == mx::kNoArgs
                       : (argc != mx::kNoArgs && (argc == t.params.size() || (t.variadic && argc >= t.params.size()) ||
                                                  (t.params.size() == 1 && argc == 0)));
      if (fits) best = &t;
    }
    return {best, best ? &f.file : nullptr};
  };
}

// Names (with scope and "line:col") the first call of `text` declares through its own file's macros.
std::vector<std::string> expandFirst(const std::string& text, size_t callIndex = 0) {
  auto f = extract("/w/x.cpp", text);
  std::vector<std::string> out;
  if (callIndex >= f->calls.size()) return {"no call"};
  for (const mx::Expanded& e : mx::expandCall(f->calls[callIndex], resolverOf(*f)))
    out.push_back((e.sym.scope.empty() ? "" : e.sym.scope + "::") + e.sym.name + "@" + std::to_string(e.sym.line) +
                  ":" + std::to_string(e.sym.col) + (e.file.empty() ? "" : "[" + e.file.filename().string() + "]"));
  return out;
}

std::string expandText(const std::string& text, size_t callIndex = 0) {
  auto f = extract("/w/x.cpp", text);
  if (callIndex >= f->calls.size()) return "no call";
  return mx::expandCallText(f->calls[callIndex], resolverOf(*f));
}

std::set<std::string> names(const std::vector<FastIndex::Match>& ms) {
  std::set<std::string> out;
  for (const auto& m : ms) out.insert(m.sym().qualified());
  return out;
}

}  // namespace

// ---- the extractor's records ---------------------------------------------------------------------------------------

TEST(mx_templates_are_recorded_with_their_parameters_and_body_positions) {
  auto f = extract("/w/a.h",
                   "#define DECL(Name, ...) struct Name { int v; };\n"
                   "#define /*c*/ TWICE(x) \\\n   x x\n"
                   "#define OBJ other_name\n"
                   "#define NUM 42\n"
                   "#define GNU(args...) f(args)\n");
  REQUIRE(f->templates.size() == 3u);  // DECL, TWICE, OBJ (NUM has no identifier; GNU named variadic is not kept)
  CHECK_EQ(f->templates[0].name, std::string("DECL"));
  CHECK(f->templates[0].variadic);
  CHECK_EQ(f->templates[0].params.size(), 1u);
  CHECK_EQ(f->templates[0].body, std::string("struct Name { int v ; } ;"));
  CHECK_EQ(f->templates[0].pos.size(), 8u);
  CHECK_EQ(f->templates[0].pos[0], static_cast<uint32_t>(24));  // `struct` is at column 24 of line 0
  CHECK_EQ(f->templates[1].name, std::string("TWICE"));
  CHECK_EQ(f->templates[1].body, std::string("x x"));
  CHECK_EQ(f->templates[1].pos[0], (1u << 16) | 3u);  // after the continuation: line + 1, column 3
  CHECK(f->templates[2].objectLike);
  CHECK_EQ(f->templates[2].body, std::string("other_name"));
}

TEST(mx_calls_at_declaration_position_are_recorded) {
  auto f = extract("/w/a.cpp",
                   "namespace ns {\n"
                   "DECLARE_CLASS(Gadget)\n"
                   "struct S {\n"
                   "  DECLARE_FIELD(int, count)\n"
                   "  S(int x);\n"  // a constructor is not a macro call
                   "  int plain = 0;\n"
                   "};\n"
                   "int CAT(var_, one) = 1;\n"  // a typed declarator named by a macro
                   "int proto(int a);\n"        // a prototype is not
                   "}\n");
  REQUIRE(f->calls.size() == 3u);
  CHECK_EQ(f->calls[0].name, std::string("DECLARE_CLASS"));
  CHECK_EQ(f->calls[0].scope, std::string("ns"));
  CHECK_EQ(f->calls[0].args.size(), 1u);
  CHECK_EQ(f->calls[0].args[0].text, std::string("Gadget"));
  CHECK_EQ(f->calls[0].args[0].line, 1);
  CHECK_EQ(f->calls[0].args[0].col, 14);
  CHECK_EQ(f->calls[1].name, std::string("DECLARE_FIELD"));
  CHECK_EQ(f->calls[1].scope, std::string("ns::S"));
  CHECK(f->calls[1].scopeKind == SymbolKind::Struct);
  CHECK_EQ(f->calls[1].args.size(), 2u);
  CHECK_EQ(f->calls[2].name, std::string("CAT"));
  CHECK(f->calls[2].withContext);
  CHECK_EQ(f->calls[2].pre.text, std::string("int"));
  CHECK_EQ(f->calls[2].post.text, std::string("= 1"));
  // the member after the in-class call is still indexed (the call did not swallow it)
  bool plain = false;
  for (const Symbol& s : f->symbols) plain = plain || s.name == "plain";
  CHECK(plain);
}

TEST(mx_macro_lists_are_included_after_a_define) {
  auto f = extract("/w/a.cpp",
                   "#include \"plain.h\"\n"
                   "enum class Color {\n"
                   "#define X(n) n,\n"
                   "#include \"colors.def\"\n"
                   "#undef X\n"
                   "};\n"
                   "#define Y(n) int n;\n"
                   "#include \"vars.def\"\n"
                   "enum { \n"
                   "#define Z(n) n,\n"
                   "#include \"anon.def\"\n"
                   "};\n");
  REQUIRE(f->xincludes.size() == 2u);  // an anonymous enum's list is not kept (nothing to wrap it in)
  CHECK_EQ(f->xincludes[0].spelling, std::string("colors.def"));
  CHECK_EQ(f->xincludes[0].enumName, std::string("Color"));
  CHECK(f->xincludes[0].enumScoped);
  CHECK_EQ(f->xincludes[1].spelling, std::string("vars.def"));
  CHECK(f->xincludes[1].enumName.empty());
  // the macro defined inside the enumerator list is a symbol now
  bool x = false;
  for (const Symbol& s : f->symbols) x = x || (s.name == "X" && s.kind == SymbolKind::Macro);
  CHECK(x);
}

TEST(mx_a_macro_defined_in_a_function_body_is_indexed) {
  auto f = extract("/w/a.cpp", "void f() {\n#define IN_BODY 7\n  int x = IN_BODY;\n}\nint after = IN_BODY;\n");
  bool found = false;
  for (const Symbol& s : f->symbols)
    found = found || (s.name == "IN_BODY" && s.kind == SymbolKind::Macro && s.line == 1);
  CHECK(found);
}

// ---- the expander --------------------------------------------------------------------------------------------------

TEST(mx_expansion_declares_what_the_macro_body_spells) {
  const std::string defs =
      "#define DECLARE_CLASS(Name) class Name { public: int value; };\n"
      "#define MAKE_FUNC(Name, Expr) inline int Name() { return Expr; }\n"
      "#define DECLARE_ENUM(Name, A, B) enum class Name { A, B };\n";
  // a name spelled in the call is declared where the argument is; a name only in the body, in the macro's own file
  auto cls = expandFirst(defs + "DECLARE_CLASS(Gadget)\n");
  CHECK_EQ(cls.size(), 2u);
  CHECK_EQ(cls[0], std::string("Gadget@3:14"));
  CHECK_EQ(cls[1], std::string("Gadget::value@0:53[x.cpp]"));
  auto fn = expandFirst(defs + "MAKE_FUNC(compute, 1)\n");
  CHECK_EQ(fn.size(), 1u);
  CHECK_EQ(fn[0], std::string("compute@3:10"));
  auto en = expandFirst(defs + "DECLARE_ENUM(Hue, Warm, Cold)\n");
  CHECK_EQ(en.size(), 3u);
  CHECK_EQ(en[0], std::string("Hue@3:13"));
  CHECK_EQ(en[1], std::string("Hue::Warm@3:18"));
  CHECK_EQ(en[2], std::string("Hue::Cold@3:24"));
}

TEST(mx_expansion_pastes_stringises_and_handles_variadics) {
  const std::string defs =
      "#define CAT_(a, b) a##b\n"
      "#define CAT(a, b) CAT_(a, b)\n"
      "#define WORD limit\n"
      "#define STR(x) #x\n"
      "#define GETTER(type, name) type get_##name() const { return name##_; }\n"
      "#define CALL(fn, ...) fn(__VA_ARGS__)\n"
      "#define OPT(x, ...) int x(__VA_OPT__(int a))\n";
  CHECK_EQ(expandText(defs + "int CAT_(a, b) = 1;\n"), std::string("int ab = 1 ;"));
  CHECK_EQ(expandText(defs + "int CAT_(WORD, _x) = 1;\n"), std::string("int WORD_x = 1 ;"));  // no extra level
  CHECK_EQ(expandText(defs + "int CAT(WORD, _x) = 1;\n"), std::string("int limit_x = 1 ;"));  // expanded first
  CHECK_EQ(expandText(defs + "int CAT(tmp_, __LINE__) = 1;\n"), std::string("int tmp_8 = 1 ;"));
  CHECK_EQ(expandText(defs + "STR(a b   c)\n"), std::string("\"a b c\""));
  CHECK_EQ(expandText(defs + "GETTER(int, count)\n"), std::string("int get_count ( ) const { count_ ; }").substr(0, 0) +
                                                          "int get_count ( ) const { return count_ ; }");
  CHECK_EQ(expandText(defs + "CALL(f, 1, 2)\n"), std::string("f ( 1 , 2 )"));
  CHECK_EQ(expandText(defs + "OPT(g)\n"), std::string("int g ( )"));
  CHECK_EQ(expandText(defs + "OPT(g, 1)\n"), std::string("int g ( int a )"));
}

TEST(mx_expansion_follows_nested_macros_and_passed_macro_names) {
  const std::string defs =
      "#define FIELDS(X) X(int, width) X(int, height)\n"
      "#define DECL_FIELD(t, n) t n;\n"
      "#define OUTER(Name) INNER(Name, 1)\n"
      "#define INNER(N, v) int N = v;\n";
  CHECK_EQ(expandText(defs + "struct R { FIELDS(DECL_FIELD) };\n", 0), std::string("int width ; int height ;"));
  CHECK_EQ(expandText(defs + "OUTER(count)\n"), std::string("int count = 1 ;"));
}

TEST(mx_expansion_is_bounded_and_safe) {
  // a macro that expands to itself stays itself: the call yields what is left (no declaration)
  CHECK(expandFirst("#define A(x) A(x)\nA(y)\n").empty());
  CHECK(expandFirst("#define A(x) B(x)\n#define B(x) A(x)\nA(y)\n").empty());
  // exponential growth is cut by the token limit: nothing is declared, nothing explodes
  std::string defs = "#define E0(x) x x\n";
  for (int i = 1; i < 20; ++i)
    defs +=
        "#define E" + std::to_string(i) + "(x) E" + std::to_string(i - 1) + "(x) E" + std::to_string(i - 1) + "(x)\n";
  CHECK(expandFirst(defs + "E19(zz)\n").empty());
  // deep nesting stops at the depth limit
  std::string chain;
  for (int i = 0; i < 40; ++i) chain += "#define D" + std::to_string(i) + "(x) D" + std::to_string(i + 1) + "(x)\n";
  chain += "#define D40(x) int x;\n";
  CHECK(expandFirst(chain + "D0(deep)\n").empty());
  // a macro that opens a scope another one closes is not expanded
  CHECK(expandFirst("#define OPEN(n) namespace n {\nOPEN(zz)\n").empty());
  CHECK(expandFirst("#define BAD(n) int n(\nBAD(zz)\n").empty());
  // the work budget shared by the calls of a file: once spent, a call declares nothing
  {
    auto f = extract("/w/x.cpp", "#define M(n) struct n { int a; int b; };\nM(P)\nM(Q)\n");
    size_t budget = 40;
    mx::Limits lim;
    lim.budget = &budget;
    CHECK(!mx::expandCall(f->calls[0], resolverOf(*f), lim).empty());  // costs some of it
    size_t before = budget;
    CHECK(before < 40u);
    budget = 3;
    CHECK(mx::expandCall(f->calls[1], resolverOf(*f), lim).empty());
    CHECK_EQ(budget, 0u);
  }
  // an unknown macro, a wrong argument count
  CHECK(expandFirst("#define M(a, b) int a;\nM(x)\n").empty());
  CHECK(expandFirst("UNKNOWN(x)\n").empty());
}

// ---- the expansion layer of the index -----------------------------------------------------------------------------

TEST(mx_index_expands_calls_with_macros_of_included_files_and_keeps_them_out_of_the_file_list) {
  FastIndex idx;
  idx.updateFile("/w/inc/declare.h", "#pragma once\n#define DECLARE_CLASS(Name) class Name { public: int value; };\n");
  idx.updateFile("/w/src/a.cpp", "#include \"declare.h\"\nDECLARE_CLASS(Gadget)\nint use = 0;\n");
  CHECK_EQ(idx.fileCount(), 2u);
  CHECK(idx.expandedSymbolCount() == 2u);
  auto g = idx.lookup("Gadget");
  REQUIRE(g.size() == 1u);
  CHECK(g[0].file->file == fs::path("/w/src/a.cpp"));
  CHECK_EQ(g[0].sym().line, 1);
  CHECK(g[0].sym().kind == SymbolKind::Class);
  // `value` is spelled in the macro's body: it is declared in the macro's file
  auto v = idx.lookup("value");
  REQUIRE(v.size() == 1u);
  CHECK(v[0].file->file == fs::path("/w/inc/declare.h"));
  CHECK_EQ(v[0].sym().scope, std::string("Gadget"));
  CHECK_EQ(idx.search("Gadget").size(), 1u);
  CHECK_EQ(idx.exportFiles().size(), 2u);  // the expansion entries are not persisted
  for (const auto& f : idx.exportFiles()) CHECK(!f->origin);
  // the calling file changes: the symbols follow
  idx.updateFile("/w/src/a.cpp", "#include \"declare.h\"\n\nDECLARE_CLASS(Gizmo)\n");
  CHECK(idx.lookup("Gadget").empty());
  CHECK_EQ(idx.lookup("Gizmo").size(), 1u);
  // the macro changes: the symbols follow
  idx.updateFile("/w/inc/declare.h", "#pragma once\n#define DECLARE_CLASS(Name) struct Name##Impl { int other; };\n");
  CHECK(idx.lookup("Gizmo").empty());
  CHECK_EQ(idx.lookup("GizmoImpl").size(), 1u);
  CHECK_EQ(idx.lookup("other").size(), 1u);
  CHECK(idx.lookup("value").empty());
  // the macro's file goes: nothing is declared
  idx.removeFile("/w/inc/declare.h");
  CHECK(idx.lookup("GizmoImpl").empty());
  CHECK_EQ(idx.expandedSymbolCount(), 0u);
  idx.removeFile("/w/src/a.cpp");
  CHECK_EQ(idx.fileCount(), 0u);
}

TEST(mx_index_does_not_guess_between_different_definitions) {
  FastIndex idx;
  idx.updateFile("/w/a/m.h", "#define DECL(N) int a_##N;\n");
  idx.updateFile("/w/b/m.h", "#define DECL(N) int b_##N;\n");
  idx.updateFile("/w/a/use.cpp", "#include \"m.h\"\nDECL(one)\n");
  idx.updateFile("/w/b/use.cpp", "#include \"m.h\"\nDECL(two)\n");
  idx.updateFile("/w/c/lone.cpp", "DECL(three)\n");  // sees neither header
  // each caller sees the header it includes
  CHECK_EQ(idx.lookup("a_one").size(), 1u);
  CHECK_EQ(idx.lookup("b_two").size(), 1u);
  CHECK(idx.lookup("a_two").empty());
  CHECK(idx.lookup("b_one").empty());
  CHECK(idx.lookup("a_three").empty());
  CHECK(idx.lookup("b_three").empty());
}

TEST(mx_index_in_file_macros_follow_define_and_undef_order) {
  FastIndex idx;
  idx.updateFile("/w/a.cpp",
                 "#define D(N) int first_##N;\n"
                 "D(x)\n"
                 "#undef D\n"
                 "D(y)\n"
                 "#define D(N) int second_##N;\n"
                 "D(z)\n");
  CHECK_EQ(idx.lookup("first_x").size(), 1u);
  CHECK(idx.lookup("first_y").empty());
  CHECK(idx.lookup("second_y").empty());
  CHECK_EQ(idx.lookup("second_z").size(), 1u);
  CHECK(idx.lookup("first_z").empty());
}

TEST(mx_index_reads_x_macro_lists_with_the_macros_defined_before_the_include) {
  FastIndex idx;
  idx.updateFile("/w/inc/colors.def", "X(Red, 1)\nX(Green, 2)\n");
  idx.updateFile("/w/src/colors.cpp",
                 "enum class Color {\n"
                 "#define X(n, v) n = v,\n"
                 "#include \"colors.def\"\n"
                 "#undef X\n"
                 "};\n"
                 "enum Idx {\n"
                 "#define X(n, v) idx_##n,\n"
                 "#include \"colors.def\"\n"
                 "#undef X\n"
                 "};\n"
                 "#define X(n, v) int get_##n();\n"
                 "#include \"colors.def\"\n"
                 "#undef X\n");
  auto red = idx.lookup("Red");
  REQUIRE(red.size() == 1u);
  CHECK(red[0].sym().kind == SymbolKind::Enumerator);
  CHECK_EQ(red[0].sym().scope, std::string("Color"));
  CHECK(red[0].file->file == fs::path("/w/inc/colors.def"));  // spelled in the list
  CHECK_EQ(red[0].sym().line, 0);
  CHECK_EQ(red[0].sym().col, 2);
  auto idxRed = idx.lookup("idx_Red");  // pasted: declared at the list's call
  REQUIRE(idxRed.size() == 1u);
  CHECK(idxRed[0].sym().kind == SymbolKind::Enumerator);
  CHECK(idxRed[0].sym().scope.empty());  // an unscoped enum: its enumerators sit in the enclosing scope
  CHECK_EQ(idxRed[0].sym().col, 0);
  CHECK_EQ(idx.lookup("get_Green").size(), 1u);
  CHECK_EQ(idx.lookup("Color").size(), 1u);  // the wrapper's own enum is not a second declaration
  // the list changes: the names follow; the list is removed: they go
  idx.updateFile("/w/inc/colors.def", "X(Blue, 4)\n");
  CHECK(idx.lookup("Red").empty());
  CHECK_EQ(idx.lookup("Blue").size(), 1u);
  idx.removeFile("/w/inc/colors.def");
  CHECK(idx.lookup("Blue").empty());
}

// ---- forward declarations and inline namespaces --------------------------------------------------------------------

TEST(forward_declarations_are_kept_for_declaration_queries_only) {
  FastIndex idx;
  idx.updateFile(
      "/w/a.h",
      "namespace n {\nclass Widget;\nstruct Node;\n}\ntemplate <class T> class Box;\nstruct Node2 { struct In; };\n");
  idx.updateFile("/w/b.h", "namespace n { class Widget { int x; }; }\n");
  auto fwd = idx.forwardDeclarations("Widget");
  REQUIRE(fwd.size() == 1u);
  CHECK(fwd[0].sym().forward);
  CHECK(!fwd[0].sym().definition);
  CHECK_EQ(fwd[0].sym().scope, std::string("n"));
  CHECK_EQ(fwd[0].sym().line, 1);
  CHECK(idx.forwardDeclarations("Box").size() == 1u && idx.forwardDeclarations("Box")[0].sym().isTemplate);
  CHECK(idx.forwardDeclarations("In").size() == 1u);
  // neither lookup() nor search() see them
  auto l = idx.lookup("Widget");
  REQUIRE(l.size() == 1u);
  CHECK(l[0].sym().definition);
  CHECK(idx.lookup("Node").empty());
  CHECK_EQ(idx.search("Widget").size(), 1u);
  CHECK(idx.search("n::Node").empty());
  CHECK(idx.search("Node").size() == 1u);  // Node2
  // they leave with their file
  idx.removeFile("/w/a.h");
  CHECK(idx.forwardDeclarations("Widget").empty());
}

TEST(a_class_pairs_with_its_forward_declaration_as_a_prototype_does) {
  FastIndex::Shape decl, def;
  decl.want = FastIndex::Want::Declaration;
  def.want = FastIndex::Want::Definition;
  FastIndex idx;
  idx.updateFile("/w/a.h", "class W;\n");
  idx.updateFile("/w/b.h", "class W { };\n");
  std::vector<FastIndex::Match> both = idx.lookup("W");
  for (auto& m : idx.forwardDeclarations("W")) both.push_back(m);
  REQUIRE(both.size() == 2u);
  auto d = FastIndex::shapeAnswer(both, decl);
  REQUIRE(d.size() == 1u);
  CHECK(d[0].sym().forward);
  auto f = FastIndex::shapeAnswer(both, def);
  REQUIRE(f.size() == 1u);
  CHECK(f[0].sym().definition);
}

TEST(inline_namespaces_are_marked) {
  auto f = extract("/w/a.h",
                   "namespace api {\ninline namespace v1 {\nint call(int);\n}\nnamespace v2 {\nint call(int);\n}\n}\n");
  int inlineCount = 0, plain = 0;
  for (const Symbol& s : f->symbols)
    if (s.kind == SymbolKind::Namespace) (s.arity == 0 ? inlineCount : plain) += 1;
  CHECK_EQ(inlineCount, 1);
  CHECK_EQ(plain, 2);  // api, v2
}

// ---- the persisted store -------------------------------------------------------------------------------------------

TEST(mx_store_round_trip_and_every_flipped_byte_is_handled) {
  FastIndexStore::Entries es;
  es.push_back(extract("/w/m.h", "#define DECL(N, ...) struct N { int v; };\n#define OBJ other\n"));
  es.push_back(extract("/w/a.cpp",
                       "class Fwd;\n"
                       "#include \"m.h\"\n"
                       "DECL(Gadget, 1)\n"
                       "int CAT(a_, b) = 3;\n"
                       "enum class C {\n#define X(n) n,\n#include \"c.def\"\n};\n"));
  const std::string bytes = FastIndexStore::serialize(es);
  auto back = FastIndexStore::deserialize(bytes.data(), bytes.size());
  REQUIRE(back.has_value());
  REQUIRE(back->size() == 2u);
  const FileSymbols& m = *(*back)[0];
  REQUIRE(m.templates.size() == 2u);
  CHECK_EQ(m.templates[0].name, std::string("DECL"));
  CHECK(m.templates[0].variadic);
  CHECK(m.templates[0].pos == es[0]->templates[0].pos);
  CHECK(m.templates[1].objectLike);
  const FileSymbols& a = *(*back)[1];
  REQUIRE(a.calls.size() == 2u);
  CHECK_EQ(a.calls[0].name, std::string("DECL"));
  CHECK_EQ(a.calls[0].args.size(), 2u);
  CHECK_EQ(a.calls[0].args[1].text, std::string("1"));
  CHECK(a.calls[1].withContext);
  CHECK_EQ(a.calls[1].pre.text, std::string("int"));
  REQUIRE(a.xincludes.size() == 1u);
  CHECK_EQ(a.xincludes[0].enumName, std::string("C"));
  CHECK(a.xincludes[0].enumScoped);
  bool fwd = false;
  for (const Symbol& s : a.symbols) fwd = fwd || (s.name == "Fwd" && s.forward);
  CHECK(fwd);
  // every byte flipped: load either fails cleanly or yields entries that are internally consistent
  for (size_t i = 48; i < bytes.size(); ++i) {  // the 48-byte header is checked field by field; the payload by its hash
    std::string mutated = bytes;
    mutated[i] = static_cast<char>(mutated[i] ^ 0x5a);
    auto r = FastIndexStore::deserialize(mutated.data(), mutated.size());
    CHECK(!r.has_value());
  }
  // truncated at every length
  for (size_t n = 0; n < bytes.size(); n += 7) CHECK(!FastIndexStore::deserialize(bytes.data(), n).has_value());
}

// ---- the engine ----------------------------------------------------------------------------------------------------

namespace {

struct Project {
  fs::path root;
  explicit Project(const std::string& name) {
    root = fs::weakly_canonical(fs::temp_directory_path()) / name;
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root);
  }
  ~Project() {
    std::error_code ec;
    fs::remove_all(root, ec);
  }
  void write(const std::string& rel, const std::string& text) const {
    fs::create_directories((root / rel).parent_path());
    std::ofstream(root / rel, std::ios::binary) << text;
  }
};

// Fast-tier answers at the first `spec` of `file` line `line` ("|" marks the caret), as "file:line,...", sorted.
std::string fastAnswer(Editor& ed, const Project& p, const char* file, int line, const std::string& spec,
                       bool declaration = false) {
  std::ifstream in(p.root / file);
  std::string s;
  for (int i = 0; i <= line && std::getline(in, s);) ++i;
  const size_t bar = spec.find('|');
  std::string needle = spec;
  needle.erase(bar, 1);
  const size_t at = s.find(needle);
  if (at == std::string::npos) return "no such text: " + spec;
  if (!ed.gotoLocation({p.root / file, {line, static_cast<int>(at + bar)}})) return "goto failed";
  bool done = false;
  std::string out;
  auto cb = [&](Confidence, const std::vector<SymbolLocation>& locs, const std::string&) {
    std::vector<std::string> v;
    for (auto& l : locs) v.push_back(l.loc.file.filename().string() + ":" + std::to_string(l.loc.pos.line));
    std::sort(v.begin(), v.end());
    for (auto& x : v) out += (out.empty() ? "" : ",") + x;
    done = true;
  };
  if (declaration) ed.gotoDeclaration(cb);
  else ed.gotoDefinition(cb);
  ed.waitFor([&] { return done; }, 5000);
  return out;
}

Editor::Options noClangd() {
  Editor::Options o;
  o.semantic.useClangd = false;
  return o;
}

}  // namespace

TEST(engine_fast_answers_for_names_declared_through_macros) {
  Project p("tenfold_mx_engine");
  p.write("inc/declare.h",
          "#pragma once\n"                                                                     // 0
          "#define DECLARE_CLASS(Name) class Name { public: int value; };\n"                   // 1
          "#define MAKE_FUNC(Name, Expr) inline int Name() { return Expr; }\n"                 // 2
          "#define DEFINE_GETTER(type, name) type get_##name() const { return name##_; }\n");  // 3
  p.write("src/b.cpp",
          "#include \"declare.h\"\n"                   // 0
          "DECLARE_CLASS(Gadget)\n"                    // 1
          "MAKE_FUNC(compute, 1)\n"                    // 2
          "struct Holder {\n"                          // 3
          "  int count_;\n"                            // 4
          "  DEFINE_GETTER(int, count)\n"              // 5
          "  int plain = 0;\n"                         // 6
          "};\n"                                       // 7
          "void use() {\n"                             // 8
          "  Gadget g; g.value = compute();\n"         // 9
          "  Holder h; h.get_count(); h.plain = 1;\n"  // 10
          "}\n");                                      // 11
  Editor ed(p.root, noClangd());
  std::string err;
  REQUIRE(ed.start(&err));
  CHECK(ed.semantic().waitForFastIndex(10000));
  CHECK_EQ(fastAnswer(ed, p, "src/b.cpp", 9, "|Gadget g"), "b.cpp:1");
  CHECK_EQ(fastAnswer(ed, p, "src/b.cpp", 9, "= |compute"), "b.cpp:2");
  CHECK_EQ(fastAnswer(ed, p, "src/b.cpp", 9, "g.|value"), "declare.h:1");  // spelled in the macro's body
  CHECK_EQ(fastAnswer(ed, p, "src/b.cpp", 10, ".|get_count"), "b.cpp:5");  // pasted: the call
  CHECK_EQ(fastAnswer(ed, p, "src/b.cpp", 10, ".|plain"), "b.cpp:6");      // not swallowed by the call before it
}

TEST(engine_fast_answers_respect_conditions_around_a_macro_call) {
  Project p("tenfold_mx_cond");
  p.write("m.h", "#pragma once\n#define DECL(N) int N() { return 1; }\n#define DECL2(N) int N() { return 2; }\n");
  p.write("a.cpp",
          "#include \"m.h\"\n"     // 0
          "#ifdef FAST_ON\n"       // 1
          "DECL(pick)\n"           // 2
          "#else\n"                // 3
          "DECL2(pick)\n"          // 4
          "#endif\n"               // 5
          "int use = pick();\n");  // 6
  const std::string a = (p.root / "a.cpp").generic_string();
  p.write("compile_commands.json", "[{\"directory\": \"" + p.root.generic_string() + "\", \"file\": \"" + a +
                                       "\", \"arguments\": [\"clang++\", \"-DFAST_ON\", \"-c\", \"" + a + "\"]}]\n");
  Editor ed(p.root, noClangd());
  std::string err;
  REQUIRE(ed.start(&err));
  CHECK(ed.semantic().waitForFastIndex(10000));
  // FAST_ON is defined: the call in the #ifdef arm declares pick; the #else arm's call is not offered
  CHECK_EQ(fastAnswer(ed, p, "a.cpp", 6, "= |pick"), "a.cpp:2");
}

TEST(engine_fast_declaration_of_a_class_is_its_forward_declaration) {
  Project p("tenfold_mx_fwd");
  p.write("fwd.h", "#pragma once\nclass Widget;\n");
  p.write("widget.h", "#pragma once\nclass Widget {\n public:\n  int size() const;\n};\n");
  p.write("use.cpp", "#include \"fwd.h\"\n#include \"widget.h\"\nint f(Widget& w) { return w.size(); }\n");
  Editor ed(p.root, noClangd());
  std::string err;
  REQUIRE(ed.start(&err));
  CHECK(ed.semantic().waitForFastIndex(10000));
  CHECK_EQ(fastAnswer(ed, p, "use.cpp", 2, "f(|Widget"), "widget.h:1");     // definition: the definition
  CHECK_EQ(fastAnswer(ed, p, "use.cpp", 2, "f(|Widget", true), "fwd.h:1");  // declaration: the forward declaration
}

TEST(engine_fast_answers_see_through_an_inline_namespace) {
  Project p("tenfold_mx_inline");
  p.write("v.h",
          "#pragma once\n"
          "namespace api {\n"        // 1
          "inline namespace v1 {\n"  // 2
          "int call(int);\n"         // 3
          "}\n"
          "namespace v2 {\n"  // 5
          "int call(int);\n"  // 6
          "}\n"
          "}\n");
  p.write("u.cpp", "#include \"v.h\"\nint a = api::call(1);\nint b = api::v2::call(1);\n");
  Editor ed(p.root, noClangd());
  std::string err;
  REQUIRE(ed.start(&err));
  CHECK(ed.semantic().waitForFastIndex(10000));
  CHECK_EQ(fastAnswer(ed, p, "u.cpp", 1, "api::|call"), "v.h:3");
  CHECK_EQ(fastAnswer(ed, p, "u.cpp", 2, "v2::|call"), "v.h:6");
}
