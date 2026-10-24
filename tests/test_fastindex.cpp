#include <algorithm>

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
  std::vector<std::string> want = {"int", "a", "=", "0x1F", ";", "auto", "s", "=", "R\"x(raw)\")x\"", ";", "p", "->", "q", "::", "r", "<<=", "2", ";", "#define X(y) \\\n  y", "foo", ""};
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
  template <typename T> T as() const { return T(); }
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
template <class T> concept Numeric = std::is_arithmetic_v<T>;
}  // namespace
namespace { static int hidden = 1; }
auto lambda = [](int x) { return x; };
DECLARE_SOMETHING(Foo)
struct Pod { int a; float b; };
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
  for (auto& s : syms) if (s.kind == SymbolKind::Constructor && s.scope == "outer::inner::Widget") ++ctors;
  CHECK_EQ(ctors, 4);  // Widget(), ~Widget(), Widget(int), out-of-line Widget::Widget()
  auto* size = find(syms, "size", SymbolKind::Method);
  REQUIRE(size != nullptr);
  CHECK_EQ(size->scope, "outer::inner::Widget");
  // Out-of-line definition should be recorded as a definition in the same scope.
  int sizeDefs = 0;
  for (auto& s : syms) if (s.name == "size" && s.definition && s.scope == "outer::inner::Widget") ++sizeDefs;
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
  for (auto& s : syms) if (s.name == "length_sq") ++lengthSq;
  CHECK_EQ(lengthSq, 2);  // declaration + out-of-line template definition
  CHECK(find(syms, "operator+") != nullptr);
  CHECK(find(syms, "Vec2f", SymbolKind::Typedef) != nullptr);
  CHECK(find(syms, "Vec2i", SymbolKind::Typedef) != nullptr);
  int traits = 0;
  for (auto& s : syms) if (s.name == "Traits") ++traits;
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
  idx.updateFile("/w/a.h", "namespace geo { struct Vec2 {}; struct Vec3 {}; void make_default_shape(); }\nint vector_count;");
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
