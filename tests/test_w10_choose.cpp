// W10-CHOOSE: go to definition lets the user choose the target through a using-declaration, an alias and a constructor
// call. Pure text helpers (semantic/DefChoice), the engine's answers with a real clangd (labels, order, default),
// the Editor's handling of the two tiers, and the corpus format extension (`def A = B C default C`).
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>

#include "app/Editor.h"
#include "core/PathUtil.h"
#include "corpus/Corpus.h"
#include "semantic/DefChoice.h"
#include "test_main.h"
#include "workspace/Workspace.h"

#define REQUIRE_EQ(a, b) REQUIRE((a) == (b))

using namespace tf;
using namespace tf::defchoice;
namespace fs = std::filesystem;

// ---- text helpers --------------------------------------------------------------------------------------------------

TEST(w10_construct_point_finds_the_paren_or_brace_of_a_construction) {
  auto at = [](const std::string& line, const std::string& word, int nth = 0) {
    size_t p = 0;
    for (int i = 0; i <= nth; ++i) p = line.find(word, i ? p + 1 : 0);
    return constructPoint(line, static_cast<int>(p + word.size()));
  };
  CHECK_EQ(at("  Widget h = Widget(2);", "Widget", 1), 19);
  CHECK_EQ(at("  Widget* g = new Widget(5);", "Widget", 1), 24);
  CHECK_EQ(at("  Widget b(3);", "Widget"), 10);
  CHECK_EQ(at("  Widget j{4};", "Widget"), 10);
  CHECK_EQ(at("  Widget  j  {4};", "Widget"), 13);
  CHECK_EQ(at("  Pair<int, double> p2(1, 2.0);", "Pair"), 22);
  CHECK_EQ(at("  return Pair<int, Box<int>>{1, {}};", "Pair"), 28);
  CHECK_EQ(at("X::X() : Base(0) {}", "Base"), 13);
  // not constructions
  CHECK_EQ(at("  Widget w;", "Widget"), -1);
  CHECK_EQ(at("  Widget w = x;", "Widget"), -1);
  CHECK_EQ(at("  Widget* p(nullptr);", "Widget"), -1);
  CHECK_EQ(at("  Widget::make(2);", "Widget"), -1);
  CHECK_EQ(at("  Widget const* p;", "Widget"), -1);
  CHECK_EQ(at("  Pair<int, double> p;", "Pair"), -1);
  CHECK_EQ(at("  Widget operator()(int);", "Widget"), -1);
}

TEST(w10_alias_forms_and_what_an_alias_names) {
  CHECK(aliasForm("typedef Foo Bar;") == AliasForm::Typedef);
  CHECK(aliasForm("  using Bar = Foo;") == AliasForm::Using);
  CHECK(aliasForm("template <class T> using Vec = std::vector<T>;") == AliasForm::Using);
  CHECK(aliasForm("using ns::f;") == AliasForm::UsingDecl);
  CHECK(aliasForm("using typename Base::type;") == AliasForm::UsingDecl);
  CHECK(aliasForm("using Base::Base;") == AliasForm::UsingDecl);
  CHECK(aliasForm("using namespace std;") == AliasForm::None);
  CHECK(aliasForm("using enum Mode;") == AliasForm::None);
  CHECK(aliasForm("int using_x = 1;") == AliasForm::None);

  auto col = [](const std::string& line, const char* name) { return static_cast<int>(line.find(name)); };
  auto rhs = aliasRhs("using Bar = ns::Foo<int>;", 6);
  REQUIRE(rhs.has_value());
  CHECK_EQ(rhs->name, std::string("Foo"));
  CHECK_EQ(rhs->qualifier, std::string("ns"));
  CHECK_EQ(rhs->col, 16);
  rhs = aliasRhs("typedef const Foo *FooPtr;", col("typedef const Foo *FooPtr;", "FooPtr"));
  REQUIRE(rhs.has_value());
  CHECK_EQ(rhs->name, std::string("Foo"));
  rhs = aliasRhs("typedef struct node node;", 20);  // the second `node` is the alias
  REQUIRE(rhs.has_value());
  CHECK_EQ(rhs->name, std::string("node"));
  rhs = aliasRhs("typedef std::map<int, Foo> Table;", col("typedef std::map<int, Foo> Table;", "Table"));
  REQUIRE(rhs.has_value());
  CHECK_EQ(rhs->name, std::string("map"));
  CHECK_EQ(rhs->qualifier, std::string("std"));
  CHECK(!aliasRhs("typedef int myint;", 12).has_value());              // builtin
  CHECK(!aliasRhs("typedef unsigned long ulong;", 22).has_value());    // builtin
  CHECK(!aliasRhs("typedef void (*Callback)(int);", 16).has_value());  // function pointer
  CHECK(!aliasRhs("using X =", 6).has_value());                        // continues on the next line
  CHECK_EQ(usingQualifier("using Base::Base;"), std::string("Base"));
  CHECK_EQ(usingQualifier("  using ns::Base::Base;"), std::string("ns::Base"));
  CHECK_EQ(usingQualifier("using ns::f;"), std::string(""));
}

TEST(w10_parameter_types_and_signatures_compare_without_names) {
  auto key = [](const std::vector<std::string>& v) {
    std::string s;
    for (auto& x : v) s += (s.empty() ? "" : "|") + x;
    return s;
  };
  bool ok = false;
  CHECK_EQ(key(paramTypes("explicit Base(int v) : v_(v) {}", 13, &ok)), std::string("int"));
  CHECK(ok);
  CHECK_EQ(key(paramTypes("Derived(const char*);", 7, &ok)), std::string("const char*"));
  CHECK_EQ(key(paramTypes("Widget(const Widget&);", 6, &ok)), std::string("const Widget&"));
  CHECK_EQ(key(paramTypes("Widget(Widget&& o) noexcept;", 6, &ok)), std::string("Widget&&"));
  CHECK_EQ(key(paramTypes("W(std::string s, int n = 3, std::map<int, int> m = {});", 1, &ok)),
           std::string("std::string|int|std::map<int,int>"));
  CHECK_EQ(key(paramTypes("W(unsigned x, unsigned long long);", 1, &ok)), std::string("unsigned|unsigned long long"));
  CHECK_EQ(key(paramTypes("W(void);", 1, &ok)), std::string(""));
  CHECK_EQ(key(paramTypes("W();", 1, &ok)), std::string(""));
  CHECK(ok);
  paramTypes("W(int a,", 1, &ok);
  CHECK(!ok);  // the list continues on the next line
  // clang's printed constructor type (the arcana of a CXXConstructExpr in clangd's textDocument/ast)
  CHECK_EQ(key(signatureParamTypes("CXXConstructExpr 0x1 <a.cpp:37:11, col:15> 'Derived' 'void (int) noexcept(false)'",
                                   &ok)),
           std::string("int"));
  CHECK(ok);
  CHECK_EQ(key(signatureParamTypes("CXXConstructExpr 0x1 <x> 'W' 'void (const char *, std::string)'", &ok)),
           std::string("const char*|std::string"));
  CHECK_EQ(key(signatureParamTypes("CXXConstructExpr 0x1 <x> 'W' 'void ()'", &ok)), std::string(""));
  CHECK(ok);
  signatureParamTypes("IntegerLiteral 0x1 <x> 'int' 3", &ok);
  CHECK(!ok);
  CHECK_EQ(signatureText("  explicit Widget(int size);", 11), std::string("Widget(int)"));
  CHECK_EQ(signatureText("  Widget(int a, int b = 2);", 2), std::string("Widget(int, int)"));
  CHECK_EQ(signatureText("  Widget w;", 2), std::string("Widget"));
  CHECK_EQ(blankComments("a /*@x*/b // c").size(), size_t(14));
  CHECK_EQ(blankComments("typedef Foo /*@B*/B1;"), std::string("typedef Foo       B1;"));
  CHECK_EQ(blankComments("s = \"// not\" + x; // c"), std::string("s = \"// not\" + x;     "));
}

// ---- corpus format: `def A = B C default C` ------------------------------------------------------------------------

TEST(w10_corpus_format_checks_the_whole_choice_list_and_its_default) {
  using namespace corpus;
  AnchorMap anchors;
  std::vector<std::string> errors;
  scanAnchorsInText("a.cpp", "struct /*@cls*/W { /*@ctor*/W(int); };\nvoid f() { /*@use*/W(1); }\n", anchors, errors);
  REQUIRE(errors.empty());
  std::vector<Item> items;
  REQUIRE(parseExpectations("def use = cls ctor default ctor\ndef use >= cls\ndef use -> cls | ctor\n", "t", anchors,
                            items, errors));
  REQUIRE_EQ(items.size(), size_t(3));
  CHECK(items[0].rel == Rel::Eq);
  CHECK_EQ(items[0].targets.size(), size_t(2));
  CHECK_EQ(items[0].preferred, std::string("ctor"));
  CHECK(items[1].rel == Rel::Ge);
  CHECK(items[2].rel == Rel::Arrow);

  auto loc = [&](const char* name, bool preferred) {
    const Anchor& a = anchors.at(name);
    Loc l;
    l.file = a.file;
    l.line = a.line;
    l.col = a.col;
    l.preferred = preferred;
    return l;
  };
  CHECK(checkLocations(items[0], {loc("cls", false), loc("ctor", true)}, anchors).ok);
  CHECK(checkLocations(items[0], {loc("ctor", true), loc("cls", false)}, anchors).ok);    // the order is not judged
  CHECK(!checkLocations(items[0], {loc("cls", true), loc("ctor", false)}, anchors).ok);   // wrong default
  CHECK(!checkLocations(items[0], {loc("cls", false), loc("ctor", false)}, anchors).ok);  // no default
  CHECK(!checkLocations(items[0], {loc("ctor", true)}, anchors).ok);                      // the class is missing
  CHECK(checkLocations(items[1], {loc("cls", false), loc("ctor", false)}, anchors).ok);
  errors.clear();
  CHECK(!parseExpectations("def use -> cls default cls\n", "t", anchors, items, errors));  // only sets take a default
  CHECK(!parseExpectations("def use = cls default\n", "t", anchors, items, errors));
}

// ---- engine + Editor with a real clangd -----------------------------------------------------------------------------

namespace {

bool haveClangd() { return !findExecutable("clangd").empty(); }

const char* kHeader = R"(#pragma once
struct Foo { int v; };
typedef Foo B1;
typedef B1 B2;
using B3 = B2;
template <class T> struct Holder { T v; };
template <class T> using Hold = Holder<T>;
typedef int myint;
namespace ns { int f(int); struct T { int t; }; }
using ns::f;
using ns::T;
class Widget {
 public:
  Widget();
  explicit Widget(int size);
  Widget(int w, int h);
  int size() const { return size_; }
 private:
  int size_;
};
struct Gadget { int n = 1; };
struct Base { Base(int); Base(double); Base(int, int); };
struct Der : Base { using Base::Base; };
struct Plain { int a; int b; };
int plain_fn(int);
)";

const char* kMain = R"(#include "t.h"
Widget::Widget() : size_(0) {}
Widget::Widget(int size) : size_(size) {}
Widget::Widget(int w, int h) : Widget(w * h) {}
int plain_fn(int x) { return x; }
int use() {
  B3 b3;
  B2 b2;
  Hold<int> hold;
  myint mi = 1;
  ns::T nt;
  T ut;
  int r = f(1) + plain_fn(2);
  Widget w1(3);
  Widget w2{4};
  Widget w3 = Widget(5);
  Widget* w4 = new Widget(6);
  Widget w5(1, 2);
  Gadget g1 = Gadget();
  Gadget g2(g1);
  Der d1(1);
  Der d2(2.0);
  Plain p{1, 2};
  return b3.v + b2.v + hold.v + mi + nt.t + ut.t + r + w1.size() + w2.size() + w3.size() + w4->size() + w5.size() + g2.n + d1.v + p.a;
}
)";

struct Project {
  fs::path dir;
  std::unique_ptr<Editor> ed;
  std::string text;
  std::string rel(const fs::path& p) const { return fs::relative(p, dir).generic_string(); }

  explicit Project(int budgetMs = 250) {
    static int n = 0;
    dir = fs::weakly_canonical(fs::temp_directory_path()) / ("tenfold_w10_" + std::to_string(n++));
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir / "src");
    std::ofstream(dir / "src/t.h") << kHeader;
    std::ofstream(dir / "src/main.cpp") << kMain;
    std::ofstream(dir / "compile_commands.json")
        << "[{\"directory\":\"" << dir.generic_string() << "\",\"file\":\"" << (dir / "src/main.cpp").generic_string()
        << "\",\"arguments\":[\"clang++\",\"-std=c++20\",\"-c\",\"" << (dir / "src/main.cpp").generic_string()
        << "\"]}]\n";
    Editor::Options o;
    o.semantic.useClangd = true;
    o.semantic.persistFastIndex = false;
    o.semantic.clangdBudgetMs = budgetMs;
    ed = std::make_unique<Editor>(dir, o);
    std::string err;
    if (!ed->start(&err)) std::cerr << "    editor start: " << err << "\n";
    std::string e2;
    ed->open(dir / "src/main.cpp", &e2);
    ed->semantic().waitForFile(dir / "src/main.cpp", 60000);
    ed->semantic().waitForClangdIndex(60000);
    std::ifstream in(dir / "src/main.cpp");
    text.assign(std::istreambuf_iterator<char>(in), {});
  }
  ~Project() {
    ed.reset();
    std::error_code ec;
    fs::remove_all(dir, ec);
  }

  // Caret on the `nth` occurrence of `token` (first character) after the line starting with `lineStart`.
  Position at(const std::string& lineStart, const std::string& token, int nth = 0) const {
    std::istringstream ss(text);
    std::string l;
    for (int n = 0; std::getline(ss, l); ++n) {
      if (l.rfind(lineStart, 0) != 0) continue;
      size_t p = std::string::npos;
      for (int i = 0; i <= nth; ++i) p = l.find(token, p == std::string::npos ? 0 : p + 1);
      return {n, static_cast<int>(p)};
    }
    return {-1, -1};
  }

  struct Got {
    std::vector<std::pair<Confidence, std::vector<SymbolLocation>>> calls;
    std::string err;
  };
  // Runs go to definition at `p` and waits for the authoritative answer (or a failure).
  Got def(Position p) {
    ed->activateDocument(*ed->document(dir / "src/main.cpp"), p);
    auto got = std::make_shared<Got>();
    bool done = false;
    ed->gotoDefinition([&, got](Confidence c, const std::vector<SymbolLocation>& l, const std::string& err) {
      got->calls.push_back({c, l});
      got->err = err;
      if (c == Confidence::Authoritative || !err.empty()) done = true;
    });
    ed->waitFor([&] { return done; }, 20000);
    return *got;
  }
  std::string where(const SymbolLocation& l) const {
    return rel(l.loc.file) + ":" + std::to_string(l.loc.pos.line + 1);
  }
  // "label@file:line" of every entry, '*' marking the preferred one.
  std::string show(const std::vector<SymbolLocation>& v) const {
    std::string s;
    for (const SymbolLocation& l : v)
      s += (s.empty() ? "" : " | ") + std::string(l.preferred ? "*" : "") + l.label + "@" + where(l);
    return s;
  }
};

bool has(const std::string& h, const std::string& n) { return h.find(n) != std::string::npos; }

}  // namespace

TEST(w10_alias_chain_is_listed_with_the_real_entity_last_and_preferred) {
  if (!haveClangd()) return;
  Project pr;
  auto g = pr.def(pr.at("  B3 b3", "B3"));
  REQUIRE(!g.calls.empty());
  const auto& fin = g.calls.back();
  CHECK(fin.first == Confidence::Authoritative);
  REQUIRE_EQ(fin.second.size(), size_t(4));
  std::cerr << "    B3: " << pr.show(fin.second) << "\n";
  CHECK_EQ(fin.second[0].label, std::string("alias B3 → B2"));
  CHECK_EQ(fin.second[1].label, std::string("typedef B2 → B1"));
  CHECK_EQ(fin.second[2].label, std::string("typedef B1 → Foo"));
  CHECK_EQ(fin.second[3].label, std::string("struct Foo"));
  CHECK(!fin.second[0].preferred && !fin.second[1].preferred && !fin.second[2].preferred && fin.second[3].preferred);
  CHECK_EQ(pr.where(fin.second[3]), std::string("src/t.h:2"));
  // Nothing jumped: several targets.
  CHECK_EQ(pr.ed->cursor().file.filename().string(), std::string("main.cpp"));

  g = pr.def(pr.at("  B2 b2", "B2"));
  REQUIRE_EQ(g.calls.back().second.size(), size_t(3));
  CHECK_EQ(g.calls.back().second[2].label, std::string("struct Foo"));

  // Alias template: the alias and the template it names.
  g = pr.def(pr.at("  Hold<int>", "Hold"));
  REQUIRE_EQ(g.calls.back().second.size(), size_t(2));
  std::cerr << "    Hold: " << pr.show(g.calls.back().second) << "\n";
  CHECK(has(g.calls.back().second[0].label, "Hold"));
  CHECK_EQ(g.calls.back().second[1].label, std::string("struct Holder"));
  CHECK(g.calls.back().second[1].preferred);
}

TEST(w10_alias_of_a_builtin_type_is_a_single_target_that_jumps_directly) {
  if (!haveClangd()) return;
  Project pr;
  auto g = pr.def(pr.at("  myint mi", "myint"));
  REQUIRE(!g.calls.empty());
  REQUIRE_EQ(g.calls.back().second.size(), size_t(1));
  std::cerr << "    myint: " << pr.show(g.calls.back().second) << "\n";
  CHECK(has(g.calls.back().second[0].label, "typedef myint"));
  CHECK_EQ(pr.ed->cursor().file.filename().string(), std::string("t.h"));  // jumped
  CHECK_EQ(pr.ed->cursor().pos.line, 7);
  // Back returns to the caret.
  CHECK(pr.ed->back());
  CHECK_EQ(pr.ed->cursor().file.filename().string(), std::string("main.cpp"));
}

TEST(w10_using_declaration_is_listed_before_the_entity_which_is_preferred) {
  if (!haveClangd()) return;
  Project pr;
  auto g = pr.def(pr.at("  int r = f(1)", "f"));
  REQUIRE(!g.calls.empty());
  const auto& fin = g.calls.back().second;
  std::cerr << "    f: " << pr.show(fin) << "\n";
  REQUIRE_EQ(fin.size(), size_t(2));
  CHECK_EQ(fin[0].label, std::string("using-declaration"));
  CHECK(has(fin[1].label, "function ns::f"));
  CHECK(!fin[0].preferred && fin[1].preferred);
  g = pr.def(pr.at("  T ut", "T"));
  REQUIRE_EQ(g.calls.back().second.size(), size_t(2));
  CHECK_EQ(g.calls.back().second[0].label, std::string("using-declaration"));
  CHECK_EQ(g.calls.back().second[1].label, std::string("struct ns::T"));
  // Without a using-declaration the answer is the plain single target.
  g = pr.def(pr.at("  ns::T nt", "T"));
  REQUIRE_EQ(g.calls.back().second.size(), size_t(1));
  CHECK(g.calls.back().second[0].label.empty());
}

TEST(w10_constructor_call_offers_the_class_and_the_constructor_that_is_called) {
  if (!haveClangd()) return;
  Project pr;
  struct Case {
    const char* line;
    const char* token;
    int nth;
    const char* ctor;  // label of the constructor entry
    int ctorLine;      // 1-based line of the entry (the out-of-line definition)
  };
  const Case cases[] = {
      {"  Widget w1(3)", "Widget", 0, "constructor Widget(int)", 3},
      {"  Widget w2{4}", "Widget", 0, "constructor Widget(int)", 3},
      {"  Widget w3 = Widget(5)", "Widget", 1, "constructor Widget(int)", 3},
      {"  Widget* w4 = new Widget(6)", "Widget", 1, "constructor Widget(int)", 3},
      {"  Widget w5(1, 2)", "Widget", 0, "constructor Widget(int, int)", 4},
      {"Widget::Widget(int w, int h) : Widget(w * h)", "Widget", 2, "constructor Widget(int)", 3},  // delegating
  };
  for (const Case& c : cases) {
    auto g = pr.def(pr.at(c.line, c.token, c.nth));
    REQUIRE(!g.calls.empty());
    const auto& fin = g.calls.back().second;
    std::cerr << "    " << c.line << ": " << pr.show(fin) << "\n";
    REQUIRE_EQ(fin.size(), size_t(2));
    CHECK_EQ(fin[0].label, std::string("class Widget"));
    CHECK_EQ(fin[1].label, std::string(c.ctor));
    CHECK(!fin[0].preferred && fin[1].preferred);
    CHECK_EQ(pr.where(fin[0]), std::string("src/t.h:12"));
    CHECK_EQ(fin[1].loc.pos.line + 1, c.ctorLine);
    CHECK_EQ(pr.rel(fin[1].loc.file), std::string(c.ctorLine == 3 || c.ctorLine == 4 ? "src/main.cpp" : "src/t.h"));
  }
}

TEST(w10_implicit_aggregate_and_inherited_constructors) {
  if (!haveClangd()) return;
  Project pr;
  // The class has no written constructor: the class, said to be an implicit constructor; one target, so it jumps.
  auto g = pr.def(pr.at("  Gadget g2(g1)", "Gadget"));
  REQUIRE(!g.calls.empty());
  std::cerr << "    Gadget g2(g1): " << pr.show(g.calls.back().second) << "\n";
  REQUIRE_EQ(g.calls.back().second.size(), size_t(1));
  CHECK_EQ(g.calls.back().second[0].label, std::string("struct Gadget (implicit constructor)"));
  CHECK_EQ(pr.ed->cursor().file.filename().string(), std::string("t.h"));
  CHECK(pr.ed->back());
  // Aggregate initialisation calls no constructor: the class alone.
  g = pr.def(pr.at("  Plain p{1, 2}", "Plain"));
  REQUIRE_EQ(g.calls.back().second.size(), size_t(1));
  CHECK(has(pr.where(g.calls.back().second[0]), "src/t.h:24"));
  CHECK(pr.ed->back());
  // Inherited constructors: the one that was resolved, not the using-declaration.
  g = pr.def(pr.at("  Der d1(1)", "Der"));
  REQUIRE(!g.calls.empty());
  {
    const auto& fin = g.calls.back().second;
    std::cerr << "    Der d1(1): " << pr.show(fin) << "\n";
    REQUIRE_EQ(fin.size(), size_t(2));
    CHECK_EQ(fin[0].label, std::string("struct Der"));
    CHECK_EQ(fin[1].label, std::string("constructor Base(int) (inherited)"));
    CHECK(fin[1].preferred);
    CHECK_EQ(pr.where(fin[1]), std::string("src/t.h:22"));
  }
  g = pr.def(pr.at("  Der d2(2.0)", "Der"));
  REQUIRE_EQ(g.calls.back().second.size(), size_t(2));
  CHECK_EQ(g.calls.back().second[1].label, std::string("constructor Base(double) (inherited)"));
}

TEST(w10_single_target_still_jumps_directly_and_a_declaration_is_left_alone) {
  if (!haveClangd()) return;
  Project pr;
  auto g = pr.def(pr.at("  int r = f(1) + plain_fn(2)", "plain_fn"));
  REQUIRE(!g.calls.empty());
  REQUIRE_EQ(g.calls.back().second.size(), size_t(1));
  CHECK(g.calls.back().second[0].label.empty());
  CHECK_EQ(pr.ed->cursor().pos.line, 4);  // jumped to the definition in main.cpp
  // The caret on the constructor's own name, and on the class name of its out-of-line definition: no chooser.
  g = pr.def(pr.at("Widget::Widget(int size)", "Widget", 1));
  REQUIRE(!g.calls.empty());
  for (const auto& call : g.calls) CHECK(call.second.size() <= 2);
  g = pr.def(pr.at("  Widget* w4", "w4"));  // a variable: untouched
  REQUIRE_EQ(g.calls.back().second.size(), size_t(1));
  CHECK(g.calls.back().second[0].label.empty());
}

// The fast tier answers first when clangd is slow (budget 0 here): an answer that clangd may still turn into a choice
// is not acted on; any other single answer jumps at once, as before.
TEST(w10_approximate_answer_that_may_become_a_choice_does_not_jump) {
  if (!haveClangd()) return;
  Project pr(0);
  const Location start = pr.ed->cursor();
  auto g = pr.def(pr.at("  Widget w1(3)", "Widget"));
  REQUIRE(g.calls.size() >= 2);
  CHECK(g.calls[0].first == Confidence::Approximate);
  REQUIRE_EQ(g.calls[0].second.size(), size_t(1));
  CHECK(g.calls[0].second[0].refinable);
  CHECK_EQ(g.calls.back().second.size(), size_t(2));
  CHECK(g.calls.back().first == Confidence::Authoritative);
  CHECK_EQ(pr.ed->cursor().file.filename().string(), std::string("main.cpp"));  // never jumped: the chooser decides
  (void)start;
  // A plain function: the approximate answer is not refinable, so the editor jumps on it at once.
  g = pr.def(pr.at("  int r = f(1) + plain_fn(2)", "plain_fn"));
  REQUIRE(!g.calls.empty());
  CHECK(!g.calls[0].second.empty() && !g.calls[0].second[0].refinable);
  CHECK_EQ(pr.ed->cursor().pos.line, 4);
}

TEST(w10_without_clangd_the_fast_answer_jumps_even_for_a_possible_constructor_call) {
  if (!haveClangd()) return;
  Project pr(0);
  pr.ed->semantic().setFastOnlyQueries(false);
  // A class used like a constructor call in a document clangd does not know: the fast answer is all there is.
  const fs::path txt = pr.dir / "src/extra.cpp";
  std::ofstream(txt) << "#include \"t.h\"\nint extra() { Foo x{1}; Widget w(2); return w.size() + x.v; }\n";
  std::string err;
  Document* d = pr.ed->open(txt, &err);
  REQUIRE(d != nullptr);
  pr.ed->semantic().setFastOnlyQueries(true);  // clangd held back: the engine reports "clangd not running"
  pr.ed->activateDocument(*d, {1, 24});        // on `Widget` of `Widget w(2)`
  bool done = false;
  pr.ed->gotoDefinition([&](Confidence, const std::vector<SymbolLocation>&, const std::string&) { done = true; });
  pr.ed->waitFor([&] { return done; }, 10000);
  pr.ed->semantic().setFastOnlyQueries(false);
  // The answer carries an error (no clangd): the editor jumps to the fast tier's single answer, the class.
  CHECK_EQ(pr.ed->cursor().file.filename().string(), std::string("t.h"));
}
