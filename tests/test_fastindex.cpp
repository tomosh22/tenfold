#include <algorithm>
#include <fstream>

#include "app/Editor.h"
#include "semantic/CppLexer.h"
#include "semantic/FastIndex.h"
#include "test_main.h"

using namespace tf;

namespace {
const Symbol* find(const std::vector<Symbol>& syms, const std::string& name, SymbolKind kind = SymbolKind::Unknown) {
  for (auto& s : syms)
    if (s.name == name && (kind == SymbolKind::Unknown || s.kind == kind)) return &s;
  return nullptr;
}
}  // namespace

TEST(lexer_basic_tokens) {
  CppLexer lx("int a = 0x1F; // c\n/* b */ auto s = R\"x(raw)\")x\"; p->q::r <<= 2;\n#define X(y) \\\n  y\nfoo");
  auto t = lx.all();
  std::vector<std::string> texts;
  for (auto& x : t) texts.emplace_back(x.text);
  std::vector<std::string> want = {"int", "a",  "=", "0x1F", ";", "auto", "s", "=", "R\"x(raw)\")x\"",      ";",
                                   "p",   "->", "q", "::",   "r", "<<=",  "2", ";", "#define X(y) \\\n  y", "foo",
                                   ""};
  CHECK_EQ(texts.size(), want.size());
  for (size_t i = 0; i < std::min(texts.size(), want.size()); ++i) CHECK_EQ(texts[i], want[i]);
  CHECK(t[0].kind == Tok::Keyword);
  CHECK(t[1].kind == Tok::Identifier);
  CHECK(t[3].kind == Tok::Number);
  CHECK(t[8].kind == Tok::String);
  CHECK(t[18].kind == Tok::Preprocessor);
  CHECK_EQ(t[19].line, 4);  // 'foo' is on line 5 (0-based 4) thanks to the continuation
}

TEST(extract_namespaces_classes_functions) {
  const char* src = R"cpp(
#include <x>
#define MAX(a, b) ((a) > (b) ? (a) : (b))
    namespace outer::inner {
    class Widget : public Base, private Other {
     public:
      Widget();
      ~Widget();
      explicit Widget(int x) : x_(x) {}
      int size() const;
      virtual void draw() = 0;
      static Widget* create(const char* name) { return nullptr; }
      template <typename T>
      T as() const {
        return T();
      }
      int x_ = 0;
      std::vector<int> items, extra;

     private:
      struct Impl;
    };
    int Widget::size() const { return x_; }
    Widget::Widget() = default;
    inline int helper(int a, int b) noexcept { return MAX(a, b); }
    int counter = 0;
    extern int externCounter;
    typedef unsigned long ulong_t;
    typedef void (*Callback)(int);
    using Alias = std::vector<Widget>;
    enum Color { Red, Green = 5, Blue };
    enum class Mode : int { Fast, Slow };
    template <class T>
    concept Numeric = std::is_arithmetic_v<T>;
    }  // namespace outer::inner
    namespace {
    static int hidden = 1;
    }
    auto lambda = [](int x) { return x; };
    DECLARE_SOMETHING(Foo)
    struct Pod {
      int a;
      float b;
    };
  )cpp";
  auto syms = extractSymbols(src);
  auto* ns = find(syms, "inner", SymbolKind::Namespace);
  REQUIRE(ns != nullptr);
  CHECK_EQ(ns->scope, "outer");
  auto* w = find(syms, "Widget", SymbolKind::Class);
  REQUIRE(w != nullptr);
  CHECK_EQ(w->scope, "outer::inner");
  CHECK_EQ(w->line, 4);
  // Constructors / destructor
  int ctors = 0;
  for (auto& s : syms)
    if (s.kind == SymbolKind::Constructor && s.scope == "outer::inner::Widget") ++ctors;
  CHECK_EQ(ctors, 4);  // Widget(), ~Widget(), Widget(int), out-of-line Widget::Widget()
  auto* size = find(syms, "size", SymbolKind::Method);
  REQUIRE(size != nullptr);
  CHECK_EQ(size->scope, "outer::inner::Widget");
  // Out-of-line definition should be recorded as a definition in the same scope.
  int sizeDefs = 0;
  for (auto& s : syms)
    if (s.name == "size" && s.definition && s.scope == "outer::inner::Widget") ++sizeDefs;
  CHECK_EQ(sizeDefs, 1);
  auto* draw = find(syms, "draw");
  REQUIRE(draw != nullptr);
  CHECK(!draw->definition);
  auto* create = find(syms, "create");
  REQUIRE(create != nullptr);
  CHECK(create->definition);
  auto* as = find(syms, "as");
  REQUIRE(as != nullptr);
  CHECK(as->isTemplate);
  CHECK(find(syms, "x_", SymbolKind::Field) != nullptr);
  CHECK(find(syms, "items", SymbolKind::Field) != nullptr);
  CHECK(find(syms, "extra", SymbolKind::Field) != nullptr);
  auto* helper = find(syms, "helper", SymbolKind::Function);
  REQUIRE(helper != nullptr);
  CHECK_EQ(helper->scope, "outer::inner");
  CHECK(helper->definition);
  auto* counter = find(syms, "counter", SymbolKind::Variable);
  REQUIRE(counter != nullptr);
  CHECK(counter->definition);
  auto* ext = find(syms, "externCounter", SymbolKind::Variable);
  REQUIRE(ext != nullptr);
  CHECK(!ext->definition);
  CHECK(find(syms, "ulong_t", SymbolKind::Typedef) != nullptr);
  CHECK(find(syms, "Callback", SymbolKind::Typedef) != nullptr);
  CHECK(find(syms, "Alias", SymbolKind::Typedef) != nullptr);
  CHECK(find(syms, "Color", SymbolKind::Enum) != nullptr);
  auto* green = find(syms, "Green", SymbolKind::Enumerator);
  REQUIRE(green != nullptr);
  CHECK_EQ(green->scope, "outer::inner");
  auto* slow = find(syms, "Slow", SymbolKind::Enumerator);
  REQUIRE(slow != nullptr);
  CHECK_EQ(slow->scope, "outer::inner::Mode");
  CHECK(find(syms, "Numeric", SymbolKind::Concept) != nullptr);
  CHECK(find(syms, "MAX", SymbolKind::Macro) != nullptr);
  auto* hidden = find(syms, "hidden", SymbolKind::Variable);
  REQUIRE(hidden != nullptr);
  CHECK_EQ(hidden->scope, "(anonymous)");
  CHECK(find(syms, "lambda", SymbolKind::Variable) != nullptr);
  // A bare macro invocation must not swallow the following struct.
  CHECK(find(syms, "DECLARE_SOMETHING") == nullptr);
  auto* pod = find(syms, "Pod", SymbolKind::Struct);
  REQUIRE(pod != nullptr);
  CHECK_EQ(pod->scope, "");
  CHECK(find(syms, "a", SymbolKind::Field) != nullptr);
  // Nothing inside function bodies leaks out.
  CHECK(find(syms, "nullptr") == nullptr);
  CHECK(find(syms, "name") == nullptr);
}

TEST(extract_sample_project_header) {
  std::string path = std::string(TENFOLD_SOURCE_DIR) + "/examples/sample_project/include/geometry.h";
  std::vector<std::filesystem::path> files{path};
  FastIndex idx;
  auto st = idx.indexFiles(files, 2);
  CHECK_EQ(st.files, 1u);
  auto fs = idx.fileSymbols(path);
  REQUIRE(fs != nullptr);
  auto& syms = fs->symbols;
  auto* vec2 = find(syms, "Vec2", SymbolKind::Struct);
  REQUIRE(vec2 != nullptr);
  CHECK(vec2->isTemplate);
  CHECK_EQ(vec2->scope, "geo");
  int lengthSq = 0;
  for (auto& s : syms)
    if (s.name == "length_sq") ++lengthSq;
  CHECK_EQ(lengthSq, 2);  // declaration + out-of-line template definition
  CHECK(find(syms, "operator+") != nullptr);
  CHECK(find(syms, "Vec2f", SymbolKind::Typedef) != nullptr);
  CHECK(find(syms, "Vec2i", SymbolKind::Typedef) != nullptr);
  int traits = 0;
  for (auto& s : syms)
    if (s.name == "Traits") ++traits;
  CHECK_EQ(traits, 2);  // primary + specialisation
  CHECK(find(syms, "value_type", SymbolKind::Typedef) != nullptr);
  CHECK(find(syms, "area", SymbolKind::Method) != nullptr);
  CHECK(find(syms, "GEO_GETTER", SymbolKind::Macro) != nullptr);
  // The heuristic parser CANNOT see macro-generated members. This is exactly
  // the class of miss the clangd tier exists to cover.
  CHECK(find(syms, "get_w") == nullptr);
  CHECK(find(syms, "make_default_shape", SymbolKind::Function) != nullptr);
  CHECK(find(syms, "first_area", SymbolKind::Function) != nullptr);
  CHECK(find(syms, "kMaxShapes", SymbolKind::Variable) != nullptr);
  CHECK(find(syms, "Kind", SymbolKind::Enum) != nullptr);
  auto* kVisible = find(syms, "kVisible", SymbolKind::Enumerator);
  REQUIRE(kVisible != nullptr);
  CHECK_EQ(kVisible->scope, "geo");
}

TEST(fuzzy_scoring_and_search) {
  CHECK(FastIndex::fuzzyScore("Vec2", "Vec2") > FastIndex::fuzzyScore("vec2", "Vec2"));
  CHECK(FastIndex::fuzzyScore("vec2", "Vec2") > FastIndex::fuzzyScore("vec", "Vec2f"));
  CHECK(FastIndex::fuzzyScore("mds", "make_default_shape") > 0);
  CHECK(FastIndex::fuzzyScore("mds", "make_default_shape") > FastIndex::fuzzyScore("mds", "madness"));
  CHECK_EQ(FastIndex::fuzzyScore("xyz", "make_default_shape"), 0);
  CHECK(FastIndex::fuzzyScore("gLS", "getLengthSquared") > FastIndex::fuzzyScore("gls", "gaslights"));

  FastIndex idx;
  idx.updateFile("/w/a.h",
                 "namespace geo { struct Vec2 {}; struct Vec3 {}; void make_default_shape(); }\nint vector_count;");
  idx.updateFile("/w/b.h", "struct Vec2Renderer {};");
  auto r = idx.search("Vec2");
  REQUIRE(!r.empty());
  CHECK_EQ(r[0].sym().name, "Vec2");
  CHECK_EQ(r[1].sym().name, "Vec2Renderer");
  auto q = idx.search("geo::Vec");
  REQUIRE(q.size() >= 2u);
  CHECK_EQ(q[0].sym().scope, "geo");
  auto ex = idx.lookup("Vec3");
  CHECK_EQ(ex.size(), 1u);
  CHECK_EQ(idx.symbolCount(), 6u);
  idx.updateFile("/w/b.h", "");
  CHECK_EQ(idx.symbolCount(), 5u);
  CHECK(idx.search("Renderer").empty());
  idx.removeFile("/w/a.h");
  CHECK_EQ(idx.symbolCount(), 0u);
}

// ---------------------------------------------------------------------------
// W9-FIX-C1: indexing gaps and answer shaping of the fast tier
// ---------------------------------------------------------------------------
namespace {
const Symbol* findAt(const std::vector<Symbol>& syms, const std::string& name, int line, int col) {
  for (auto& s : syms)
    if (s.name == name && s.line == line && s.col == col) return &s;
  return nullptr;
}
}  // namespace

TEST(extract_macros_object_function_comments_continuations) {
  const std::string src =
      "#define PLAIN 1\n"
      "#define FUNC(a, b) ((a) + (b))\n"
      "# define   SPACED(x) x\n"
      "#define /*@anchor*/ANCHORED(x) (x)\n"
      "#define \\\n"
      "    CONTINUED 2\n"
      "#define /* multi\n"
      "   line */ AFTER_COMMENT 3\n"
      "#  /* c */ define AFTER_HASH 4\n"
      "#define _under 5\n"
      "#define\n"
      "#define NOARGS() 0\n"
      "#define 9bad\n"
      "#defined notamacro\n"
      "#if 0\n#define IN_DEAD_BRANCH 1\n#endif\n";
  auto syms = extractSymbols(src);
  auto macro = [&](const std::string& n) { return find(syms, n, SymbolKind::Macro); };
  REQUIRE(macro("PLAIN") != nullptr);
  CHECK_EQ(macro("PLAIN")->line, 0);
  CHECK_EQ(macro("PLAIN")->col, 8);
  CHECK_EQ(macro("PLAIN")->arity, -1);  // object-like
  CHECK_EQ(macro("FUNC")->arity, 2);    // function-like: its parameter count
  CHECK_EQ(macro("SPACED")->arity, 1);
  CHECK(macro("PLAIN")->definition);
  CHECK_EQ(macro("PLAIN")->scope, "");
  REQUIRE(macro("FUNC") != nullptr);
  CHECK_EQ(macro("FUNC")->col, 8);
  REQUIRE(macro("SPACED") != nullptr);
  CHECK_EQ(macro("SPACED")->col, 11);
  REQUIRE(macro("ANCHORED") != nullptr);  // a comment between `define` and the name
  CHECK_EQ(macro("ANCHORED")->line, 3);
  CHECK_EQ(macro("ANCHORED")->col, 19);
  REQUIRE(macro("CONTINUED") != nullptr);  // a backslash-newline before the name
  CHECK_EQ(macro("CONTINUED")->line, 5);
  CHECK_EQ(macro("CONTINUED")->col, 4);
  REQUIRE(macro("AFTER_COMMENT") != nullptr);  // a block comment spanning a line break
  CHECK_EQ(macro("AFTER_COMMENT")->line, 7);
  CHECK_EQ(macro("AFTER_COMMENT")->col, 11);
  CHECK(macro("AFTER_HASH") == nullptr);  // `# /* c */ define` is rare enough: only blanks between # and define
  REQUIRE(macro("_under") != nullptr);
  REQUIRE(macro("NOARGS") != nullptr);
  CHECK_EQ(macro("NOARGS")->arity, 0);
  CHECK(macro("IN_DEAD_BRANCH") != nullptr);  // the index is over-inclusive on purpose: no #if evaluation
  CHECK(macro("9bad") == nullptr);
  CHECK(macro("notamacro") == nullptr);
  CHECK(macro("define") == nullptr);
}

TEST(extract_hidden_friends_and_friend_declarations) {
  const std::string src =
      "namespace geo {\n"
      "struct V {\n"
      "  friend int dot(const V& a, const V& b) { return 0; }\n"
      "  friend bool operator==(const V&, const V&) = default;\n"
      "  friend void declared_only(V);\n"
      "  friend class Other;\n"
      "  friend struct Other2;\n"
      "  template <class T> friend T tpl(T t, V) { return t; }\n"
      "  int member;\n"
      "};\n"
      "}\n"
      "struct W { friend W make(int) { return {}; } void m(); };\n";
  auto syms = extractSymbols(src);
  auto* dot = find(syms, "dot");
  REQUIRE(dot != nullptr);
  CHECK(dot->kind == SymbolKind::Function);
  CHECK(dot->definition);
  CHECK_EQ(dot->scope, "geo");  // a member of the namespace, not of the class
  CHECK_EQ(dot->arity, 2);
  CHECK_EQ(dot->line, 2);
  CHECK_EQ(dot->col, 13);
  auto* tpl = find(syms, "tpl");
  REQUIRE(tpl != nullptr);
  CHECK_EQ(tpl->scope, "geo");
  CHECK(tpl->isTemplate);
  auto* make = find(syms, "make");
  REQUIRE(make != nullptr);
  CHECK_EQ(make->scope, "");
  // Declarations and `friend class` are not symbols; the members after them still are.
  CHECK(find(syms, "declared_only") == nullptr);
  CHECK(find(syms, "Other") == nullptr);
  CHECK(find(syms, "Other2") == nullptr);
  CHECK(find(syms, "operator==") == nullptr);  // defaulted: no body
  auto* member = find(syms, "member", SymbolKind::Field);
  REQUIRE(member != nullptr);
  CHECK_EQ(member->scope, "geo::V");
  auto* m = find(syms, "m", SymbolKind::Method);
  REQUIRE(m != nullptr);
  CHECK_EQ(m->scope, "W");
  // A friend outside a class is just skipped.
  auto top = extractSymbols("friend int stray(int);\nint after(int a);\n");
  CHECK(find(top, "stray") == nullptr);
  CHECK(find(top, "after") != nullptr);
}

TEST(extract_operator_names_conversions_and_literals) {
  const std::string src =
      "struct Meters; namespace std { struct string; }\n"
      "struct Feet {\n"
      "  operator Meters() const;\n"
      "  explicit operator bool() const { return true; }\n"
      "  operator const char*() const;\n"
      "  operator std::string() const;\n"
      "  Feet operator+(Feet o) const;\n"
      "  bool operator<(Feet o) const;\n"
      "  bool operator<=>(Feet o) const;\n"
      "  Feet& operator+=(Feet o);\n"
      "  int operator()(int a, int b) const;\n"
      "  int operator[](int i) const;\n"
      "  void* operator new(unsigned long n);\n"
      "  void operator delete[](void* p);\n"
      "  Feet* operator->();\n"
      "  bool operator!() const;\n"
      "};\n"
      "Feet Feet::operator+(Feet o) const { return o; }\n"
      "Feet::operator Meters() const { return {}; }\n"
      "Feet operator\"\" _ft(long double v) { return {}; }\n"
      "bool operator==(const Feet&, const Feet&);\n"
      "int after;\n";
  auto syms = extractSymbols(src);
  auto at = [&](const std::string& name, int line) {
    const Symbol* r = nullptr;
    for (auto& s : syms)
      if (s.name == name && s.line == line) r = &s;
    return r;
  };
  // The symbol sits on the `operator` keyword; the name is canonical whatever the spacing.
  REQUIRE(at("operator Meters", 2) != nullptr);
  CHECK_EQ(at("operator Meters", 2)->col, 2);
  CHECK(at("operator Meters", 2)->scope == "Feet");
  CHECK(at("operator Meters", 2)->kind == SymbolKind::Method);
  CHECK(!at("operator Meters", 2)->definition);
  REQUIRE(at("operator bool", 3) != nullptr);
  CHECK(at("operator bool", 3)->definition);
  CHECK(at("operator const char*", 4) != nullptr);
  REQUIRE(at("operator std::string", 5) != nullptr);  // the "::" inside the type is not a qualifier
  CHECK_EQ(at("operator std::string", 5)->scope, "Feet");
  CHECK(at("operator+", 6) != nullptr);
  CHECK(at("operator<", 7) != nullptr);
  CHECK(at("operator<=>", 8) != nullptr);
  CHECK(at("operator+=", 9) != nullptr);
  REQUIRE(at("operator()", 10) != nullptr);
  CHECK_EQ(at("operator()", 10)->arity, 2);
  CHECK(at("operator[]", 11) != nullptr);
  CHECK(at("operator new", 12) != nullptr);
  CHECK(at("operator delete[]", 13) != nullptr);
  CHECK(at("operator->", 14) != nullptr);
  CHECK(at("operator!", 15) != nullptr);
  // Out of line: same name, scope Feet, a definition.
  REQUIRE(at("operator+", 17) != nullptr);
  CHECK_EQ(at("operator+", 17)->scope, "Feet");
  CHECK(at("operator+", 17)->definition);
  CHECK_EQ(at("operator+", 17)->col, 11);
  REQUIRE(at("operator Meters", 18) != nullptr);
  CHECK_EQ(at("operator Meters", 18)->scope, "Feet");
  CHECK(at("operator Meters", 18)->definition);
  CHECK(at("operator\"\"_ft", 19) != nullptr);
  CHECK(at("operator==", 20) != nullptr);
  CHECK(find(syms, "after", SymbolKind::Variable) != nullptr);
  // The same canonical names from the text after a written `operator` (the caret path of the engine).
  CHECK_EQ(operatorFunctionName("+(a, b)"), "operator+");
  CHECK_EQ(operatorFunctionName(" +  (a, b)"), "operator+");
  CHECK_EQ(operatorFunctionName("()(1)"), "operator()");
  CHECK_EQ(operatorFunctionName("[](1)"), "operator[]");
  CHECK_EQ(operatorFunctionName(" Meters() const"), "operator Meters");
  CHECK_EQ(operatorFunctionName("  const   char *()"), "operator const char*");
  CHECK_EQ(operatorFunctionName(" new[](n)"), "operator new[]");
  CHECK_EQ(operatorFunctionName("<<(os, v)"), "operator<<");
  CHECK_EQ(operatorFunctionName(" std::string()"), "operator std::string");
  CHECK_EQ(operatorFunctionName(""), "");
  CHECK_EQ(operatorFunctionName("(x)"), "");
}

TEST(extract_typedef_bodies_enumerators_and_tags) {
  const std::string src =
      "typedef enum { A_ONE, A_TWO = 2, A_THREE } AnonEnum;\n"
      "typedef enum Tagged { T_X, T_Y } TaggedT;\n"
      "typedef enum Fwd Fwd;\n"
      "typedef struct node node;\n"
      "typedef struct Rec { int f; } Rec;\n"
      "typedef union { int a; float b; } AnonU;\n"
      "typedef struct { int p; } AnonS, *AnonSP;\n"
      "namespace n { typedef enum { N_ONE } NE; }\n"
      "enum class Scoped { S1, S2 };\n"
      "int after;\n";
  auto syms = extractSymbols(src);
  for (const char* e : {"A_ONE", "A_TWO", "A_THREE"}) {
    auto* s = find(syms, e, SymbolKind::Enumerator);
    REQUIRE(s != nullptr);
    CHECK_EQ(s->scope, "");
    CHECK_EQ(s->line, 0);
  }
  CHECK_EQ(find(syms, "A_ONE")->col, 15);
  CHECK(find(syms, "AnonEnum", SymbolKind::Typedef) != nullptr);
  CHECK(find(syms, "Tagged", SymbolKind::Enum) != nullptr);
  CHECK(find(syms, "T_X", SymbolKind::Enumerator) != nullptr);
  CHECK(find(syms, "T_Y", SymbolKind::Enumerator) != nullptr);
  CHECK(find(syms, "TaggedT", SymbolKind::Typedef) != nullptr);
  CHECK(find(syms, "Fwd", SymbolKind::Typedef) != nullptr);
  CHECK(find(syms, "Fwd", SymbolKind::Enum) == nullptr);  // `typedef enum Fwd Fwd;` has no body
  CHECK(find(syms, "node", SymbolKind::Typedef) != nullptr);
  CHECK(find(syms, "node", SymbolKind::Struct) == nullptr);
  auto* rec = find(syms, "Rec", SymbolKind::Struct);  // a tag with a body is indexed next to the typedef name
  REQUIRE(rec != nullptr);
  CHECK(rec->definition);
  CHECK(find(syms, "Rec", SymbolKind::Typedef) != nullptr);
  CHECK(find(syms, "AnonU", SymbolKind::Typedef) != nullptr);
  CHECK(find(syms, "AnonSP", SymbolKind::Typedef) != nullptr);  // one name per declarator
  CHECK(find(syms, "AnonS", SymbolKind::Typedef) != nullptr);
  auto* ne = find(syms, "N_ONE", SymbolKind::Enumerator);
  REQUIRE(ne != nullptr);
  CHECK_EQ(ne->scope, "n");
  auto* s1 = find(syms, "S1", SymbolKind::Enumerator);
  REQUIRE(s1 != nullptr);
  CHECK_EQ(s1->scope, "Scoped");
  CHECK(find(syms, "after", SymbolKind::Variable) != nullptr);
}

TEST(extract_function_arity) {
  const std::string src =
      "int a0();\nint a0v(void);\nint a1(int x);\nint a2(int x, char y);\n"
      "int a3(std::map<int, int> m, int (*cb)(int, int), int z = f(1, 2));\n"
      "int av(const char* fmt, ...);\nvoid a2d(int, int) {}\n"
      "struct S { S(); S(int); S(int, int); ~S(); void m(int, int, int); };\n";
  auto syms = extractSymbols(src);
  auto arity = [&](const std::string& n, int line) {
    for (auto& s : syms)
      if (s.name == n && s.line == line && s.kind != SymbolKind::Struct) return static_cast<int>(s.arity);
    return -99;
  };
  CHECK_EQ(arity("a0", 0), 0);
  CHECK_EQ(arity("a0v", 1), 0);
  CHECK_EQ(arity("a1", 2), 1);
  CHECK_EQ(arity("a2", 3), 2);
  CHECK_EQ(arity("a3", 4), 3);
  CHECK_EQ(arity("av", 5), 2);
  CHECK_EQ(arity("a2d", 6), 2);
  CHECK_EQ(arity("S", 7), 0);   // the first constructor on that line
  CHECK_EQ(arity("~S", 7), 0);  // a destructor is named "~S" in its class too, not "S"
  CHECK(find(syms, "~S", SymbolKind::Constructor) != nullptr);
  CHECK_EQ(arity("m", 7), 3);
  // Variables and types carry no arity.
  auto v = extractSymbols("int plain;\nstruct T {};\n");
  for (auto& s : v) CHECK_EQ(static_cast<int>(s.arity), -1);
}

namespace {
// Indexes `files` (name -> text) and looks `name` up the way the engine does, then shapes the answer.
std::string shaped(const std::vector<std::pair<std::string, std::string>>& files, const std::string& name,
                   FastIndex::Shape shape) {
  FastIndex idx;
  for (auto& [f, text] : files) idx.updateFile(std::filesystem::path("/virtual") / f, text);
  std::vector<std::string> out;
  for (auto& m : FastIndex::shapeAnswer(idx.lookup(name), shape)) {
    const Symbol& s = m.sym();
    out.push_back(m.file->file.filename().string() + ":" + std::to_string(s.line) + ":" + symbolKindName(s.kind) +
                  (s.definition ? ":def" : ":decl"));
  }
  std::sort(out.begin(), out.end());
  std::string joined;
  for (auto& o : out) joined += (joined.empty() ? "" : ", ") + o;
  return joined;
}
FastIndex::Shape want(FastIndex::Want w) {
  FastIndex::Shape s;
  s.want = w;
  return s;
}
}  // namespace

TEST(shape_prototype_and_definition_are_paired_by_name_scope_arity) {
  const std::vector<std::pair<std::string, std::string>> files = {
      {"a.h",
       "int f(int x);\nint f(int x, int y);\nint only_decl(int);\nint other(int);\nextern int g;\n"
       "namespace n { int h(); }\nstruct C { void m(int); void m(); int field; };\n"},
      {"a.cpp",
       "int f(int x) { return x; }\nint only_def(int a) { return a; }\nint g = 1;\n"
       "namespace n { int h() { return 0; } }\nvoid C::m(int) {}\nvoid C::m() {}\n"}};
  using W = FastIndex::Want;
  // `f(int)` is paired; `f(int, int)` has no definition and stays a prototype (a different arity).
  CHECK_EQ(shaped(files, "f", want(W::Definition)), "a.cpp:0:function:def, a.h:1:function:decl");
  CHECK_EQ(shaped(files, "f", want(W::Declaration)), "a.h:0:function:decl, a.h:1:function:decl");
  CHECK_EQ(shaped(files, "f", want(W::Any)), "a.cpp:0:function:def, a.h:0:function:decl, a.h:1:function:decl");
  // Only a prototype / only a definition: that one answers both questions.
  CHECK_EQ(shaped(files, "only_decl", want(W::Definition)), "a.h:2:function:decl");
  CHECK_EQ(shaped(files, "only_def", want(W::Declaration)), "a.cpp:1:function:def");
  // Variables, namespaced functions, out-of-line methods.
  CHECK_EQ(shaped(files, "g", want(W::Definition)), "a.cpp:2:variable:def");
  CHECK_EQ(shaped(files, "g", want(W::Declaration)), "a.h:4:variable:decl");
  CHECK_EQ(shaped(files, "h", want(W::Definition)), "a.cpp:3:function:def");
  CHECK_EQ(shaped(files, "h", want(W::Declaration)), "a.h:5:function:decl");
  CHECK_EQ(shaped(files, "m", want(W::Definition)), "a.cpp:4:method:def, a.cpp:5:method:def");
  CHECK_EQ(shaped(files, "m", want(W::Declaration)), "a.h:6:method:decl, a.h:6:method:decl");
  // Same name in another scope is a different entity: its prototype is not dropped.
  const std::vector<std::pair<std::string, std::string>> two = {
      {"x.cpp", "namespace a { int f(); }\nnamespace b { int f(); int f() { return 1; } }\n"}};
  CHECK_EQ(shaped(two, "f", want(W::Definition)), "x.cpp:0:function:decl, x.cpp:1:function:def");
}

TEST(shape_class_name_is_the_class_not_its_constructors) {
  const std::vector<std::pair<std::string, std::string>> files = {
      {"w.h", "class Widget {\n public:\n  Widget();\n  Widget(int);\n  ~Widget();\n};\n"},
      {"w.cpp", "Widget::Widget() {}\nWidget::Widget(int) {}\nWidget::~Widget() {}\n"}};
  FastIndex::Shape any;
  CHECK_EQ(shaped(files, "Widget", any), "w.h:0:class:def");
  // The destructor is its own name.
  CHECK_EQ(shaped(files, "~Widget", any), "w.cpp:2:constructor:def, w.h:4:constructor:decl");
  // On the name of a constructor: the constructors of that class, in the wanted flavour.
  FastIndex::Shape ctor = want(FastIndex::Want::Definition);
  ctor.constructorOnly = true;
  ctor.constructorScope = "Widget";
  CHECK_EQ(shaped(files, "Widget", ctor), "w.cpp:0:constructor:def, w.cpp:1:constructor:def");
  // Only constructors known (the class itself is not indexed): they are all there is.
  const std::vector<std::pair<std::string, std::string>> noClass = {{"c.cpp", "Gone::Gone() {}\n"}};
  CHECK_EQ(shaped(noClass, "Gone", any), "c.cpp:0:constructor:def");
}

TEST(shape_typedef_versus_tag_of_the_same_name) {
  const std::vector<std::pair<std::string, std::string>> files = {
      {"n.h", "typedef struct node node;\nstruct node {\n  int v;\n};\n"},
      {"o.h", "typedef struct Rec { int f; } Rec;\n"},
      {"p.h", "namespace p { struct T {}; }\nstruct T {};\ntypedef int T2;\n"}};
  FastIndex::Shape plain, tag;
  tag.afterTagKeyword = true;
  CHECK_EQ(shaped(files, "node", plain), "n.h:0:typedef:def");
  CHECK_EQ(shaped(files, "node", tag), "n.h:1:struct:def");
  CHECK_EQ(shaped(files, "Rec", plain), "o.h:0:typedef:def");
  CHECK_EQ(shaped(files, "Rec", tag), "o.h:0:struct:def");
  // Same name in different scopes: nothing is dropped.
  CHECK_EQ(shaped(files, "T", plain), "p.h:0:struct:def, p.h:1:struct:def");
  // A typedef alone, a tag alone: always the one there is.
  CHECK_EQ(shaped(files, "T2", tag), "p.h:2:typedef:def");
}

TEST(extract_parenthesised_function_declarator) {
  auto syms = extractSymbols("int (clamp01)(int v);\nint (clamp02)(int v) { return v; }\nint (*fp)(int);\nint (x);\n");
  auto* c1 = find(syms, "clamp01", SymbolKind::Function);
  REQUIRE(c1 != nullptr);
  CHECK(!c1->definition);
  CHECK_EQ(c1->arity, 1);
  CHECK_EQ(c1->col, 5);
  auto* c2 = find(syms, "clamp02", SymbolKind::Function);
  REQUIRE(c2 != nullptr);
  CHECK(c2->definition);
  CHECK(find(syms, "fp") == nullptr);  // pointer declarators are still not indexed
}

// The approximate-answer path of the engine (no clangd): what the word under the caret means.
TEST(engine_fast_answers_pair_prototypes_and_read_the_word_around_the_caret) {
  namespace fs = std::filesystem;
  fs::path root = fs::weakly_canonical(fs::temp_directory_path()) / "tenfold_c1_fast_answers";
  std::error_code ec;
  fs::remove_all(root, ec);
  fs::create_directories(root);
  auto write = [&](const char* name, const std::string& text) { std::ofstream(root / name, std::ios::binary) << text; };
  write("a.h",
        "#pragma once\n"                                   // 0
        "#define /*c*/SQUARE(x) ((x) * (x))\n"             // 1
        "typedef struct node node;\n"                      // 2
        "struct node { int v; };\n"                        // 3
        "typedef enum { K_A, K_B } kind_t;\n"              // 4
        "int add(int a, int b);\n"                         // 5
        "class W {\n"                                      // 6
        " public:\n"                                       // 7
        "  W();\n"                                         // 8
        "  W(int);\n"                                      // 9
        "  ~W();\n"                                        // 10
        "  operator bool() const;\n"                       // 11
        "  friend int hid(const W&, int) { return 1; }\n"  // 12
        "};\n");
  write("a.cpp",
        "#include \"a.h\"\n"                                             // 0
        "int add(int a, int b) { return a + b; }\n"                      // 1
        "W::W() {}\n"                                                    // 2
        "W::W(int) {}\n"                                                 // 3
        "W::~W() {}\n"                                                   // 4
        "W::operator bool() const { return true; }\n"                    // 5
        "int late = LATE_MACRO;\n"                                       // 6
        "#define LATE_MACRO 1\n"                                         // 7
        "int use() {\n"                                                  // 8
        "  W w;\n"                                                       // 9
        "  int r = add(1, 2) + SQUARE(2) + K_A + hid(w, 1) + SQUARE;\n"  // 10
        "  struct /*t*/node* n = nullptr; node* m = nullptr;\n"          // 11
        "  w.~W(); bool b = w.operator bool();\n"                        // 12
        "  return r;\n"                                                  // 13
        "}\n");
  Editor::Options o;
  o.semantic.useClangd = false;
  Editor ed(root, o);
  std::string err;
  REQUIRE(ed.start(&err));
  CHECK(ed.semantic().waitForFastIndex(10000));
  auto ask = [&](const char* file, int line, const std::string& spec, bool declaration) {
    std::ifstream in(root / file);
    std::string s;
    for (int i = 0; i <= line && std::getline(in, s);) ++i;
    const size_t bar = spec.find('|');
    std::string needle = spec;
    needle.erase(bar, 1);
    const size_t at = s.find(needle);
    if (at == std::string::npos) return std::string("no such text: ") + spec;
    if (!ed.gotoLocation({root / file, {line, static_cast<int>(at + bar)}})) return std::string("goto failed");
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
  };
  // prototype and definition: definition for "definition", prototype for "declaration"
  CHECK_EQ(ask("a.cpp", 10, "|add(1", false), "a.cpp:1");
  CHECK_EQ(ask("a.cpp", 10, "|add(1", true), "a.h:5");
  // macros: function-like only where a '(' follows; a macro defined further down is not offered above
  CHECK_EQ(ask("a.cpp", 10, "|SQUARE(2)", false), "a.h:1");
  CHECK_EQ(ask("a.cpp", 10, "SQUARE|;", false), "");
  CHECK_EQ(ask("a.cpp", 6, "|LATE_MACRO", false), "");
  // anonymous-enum enumerator and hidden friend
  CHECK_EQ(ask("a.cpp", 10, "|K_A", false), "a.h:4");
  CHECK_EQ(ask("a.cpp", 10, "|hid(w", false), "a.h:12");
  // typedef outside `struct`, tag after it (also past a comment)
  CHECK_EQ(ask("a.cpp", 11, "struct /*t*/|node", false), "a.h:3");
  CHECK_EQ(ask("a.cpp", 11, "; |node*", false), "a.h:2");
  // class name: the class; ~Name: the destructor; the caret on a constructor: constructors of that arity
  CHECK_EQ(ask("a.cpp", 9, "|W w", false), "a.h:6");
  CHECK_EQ(ask("a.cpp", 12, "~|W()", false), "a.cpp:4");
  CHECK_EQ(ask("a.cpp", 12, "~|W()", true), "a.h:10");
  CHECK_EQ(ask("a.cpp", 3, "W::|W(int)", false), "a.cpp:3");
  CHECK_EQ(ask("a.h", 8, "|W();", true), "a.h:8");
  CHECK_EQ(ask("a.h", 8, "|W();", false), "a.cpp:2");
  // a written `operator <type>`
  CHECK_EQ(ask("a.cpp", 12, "|operator bool()", false), "a.cpp:5");
  CHECK_EQ(ask("a.cpp", 12, "|operator bool()", true), "a.h:11");
}
