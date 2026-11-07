#include <algorithm>
#include <fstream>

#include "app/Editor.h"
#include "semantic/FastIndex.h"
#include "semantic/FastScope.h"
#include "semantic/ScopeScan.h"
#include "test_main.h"

using namespace tf;

namespace {
const ScanRegion* find(const ScopeMap& m, ScanRegion::Kind k, const std::string& name) {
  for (auto& r : m.regions)
    if (r.kind == k && r.name == name) return &r;
  return nullptr;
}
const ScanVar* var(const ScanRegion& r, const std::string& name) {
  for (auto& v : r.vars)
    if (v.name == name) return &v;
  return nullptr;
}
}  // namespace

TEST(scopescan_regions_namespaces_classes_functions) {
  // clang-format off
  const char* src = R"cpp(
namespace geo::detail {
struct Base { int b; };
class Vec : public Base, private geo::Other<int> {
 public:
  int x = 0;
  std::unique_ptr<Base> owned;
  double len(int a, const Vec& o = {}) const { return a; }
};
}
namespace { int hidden; }
void geo::detail::Vec::run(int n) {
  int local = 1, *p = nullptr;
  auto it = new Vec();
  for (int i = 0; i < n; ++i) { Vec inner; }
  auto f = [&](int q) { return q + local; };
}
extern "C" { int c_fn(void) { return 0; } }
)cpp";
  // clang-format on
  ScopeMap m = scanScopes(src);
  const ScanRegion* ns = find(m, ScanRegion::Namespace, "geo::detail");
  REQUIRE(ns != nullptr);
  CHECK(find(m, ScanRegion::Namespace, "(anonymous)") != nullptr);
  const ScanRegion* vec = find(m, ScanRegion::Class, "Vec");
  REQUIRE(vec != nullptr);
  CHECK_EQ(vec->bases.size(), static_cast<size_t>(2));
  CHECK_EQ(vec->bases[0], std::string("Base"));
  CHECK_EQ(vec->bases[1], std::string("geo::Other"));
  const ScanVar* owned = var(*vec, "owned");
  REQUIRE(owned != nullptr);
  CHECK_EQ(owned->type, std::string("std::unique_ptr"));
  CHECK_EQ(owned->targ, std::string("Base"));
  CHECK(var(*vec, "x") != nullptr);
  const ScanRegion* len = find(m, ScanRegion::Function, "len");
  REQUIRE(len != nullptr);
  CHECK(var(*len, "a") != nullptr);
  const ScanVar* o = var(*len, "o");
  REQUIRE(o != nullptr);
  CHECK_EQ(o->type, std::string("Vec"));
  const ScanRegion* run = find(m, ScanRegion::Function, "geo::detail::Vec::run");
  REQUIRE(run != nullptr);
  CHECK(var(*run, "n") != nullptr);
  const ScanVar* local = var(*run, "local");
  REQUIRE(local != nullptr);
  const ScanVar* pp = var(*run, "p");
  REQUIRE(pp != nullptr);
  CHECK(pp->pointer);
  const ScanVar* it = var(*run, "it");
  REQUIRE(it != nullptr);
  CHECK_EQ(it->type, std::string("Vec"));  // auto = new Vec()
  CHECK(it->pointer);
  CHECK(var(*run, "i") != nullptr);  // for-init
  CHECK(var(*run, "f") != nullptr);  // a lambda variable survives the lambda body
  // regionAt: a caret inside run's body is in run; one in the anonymous namespace is not
  CHECK_EQ(m.regionAt({13, 8}), static_cast<int>(run - &m.regions[0]));
  CHECK(m.regionAt({10, 18}) > 0);
  CHECK(find(m, ScanRegion::Function, "c_fn") != nullptr);
}

TEST(scopescan_using_aliases_and_initializer_braces) {
  // clang-format off
  const char* src = R"cpp(
namespace fs = std::filesystem;
using namespace geo::util;
using geo::Other;
struct S {
  int a{1};
  int b = {2};
  S() : a{3}, b{4} {}
  void m() { int z; }
};
)cpp";
  // clang-format on
  ScopeMap m = scanScopes(src);
  REQUIRE(!m.regions[0].aliases.empty());
  CHECK_EQ(m.regions[0].aliases[0].alias, std::string("fs"));
  CHECK_EQ(m.regions[0].aliases[0].target, std::string("std::filesystem"));
  REQUIRE(m.regions[0].usings.size() == 2);
  CHECK_EQ(m.regions[0].usings[0].ns, std::string("geo::util"));
  CHECK_EQ(m.regions[0].usings[0].name, std::string(""));
  CHECK_EQ(m.regions[0].usings[1].ns, std::string("geo"));
  CHECK_EQ(m.regions[0].usings[1].name, std::string("Other"));
  const ScanRegion* s = find(m, ScanRegion::Class, "S");
  REQUIRE(s != nullptr);
  CHECK(var(*s, "a") != nullptr);
  CHECK(var(*s, "b") != nullptr);
  // the constructor's initializer braces are not function bodies; both methods are regions
  CHECK(find(m, ScanRegion::Function, "S") != nullptr);
  CHECK(find(m, ScanRegion::Function, "m") != nullptr);
  CHECK(find(m, ScanRegion::Function, "a") == nullptr);
}

TEST(scopescan_is_bounded_and_never_throws) {
  CHECK_EQ(scanScopes(std::string(100, '{'), false, 50).regions.size(), static_cast<size_t>(1));
  ScopeMap deep = scanScopes(std::string(5000, '{') + std::string(5000, '}'));
  CHECK(deep.regions.size() >= 1);
  scanScopes("namespace {{{ class struct ( ) [ ] ; ; } } template<<<>>> operator");
  scanScopes("");
}

TEST(extract_static_and_default_arguments) {
  const char* src = R"cpp(
    static int counter;
    static void helper(int a, int b = 2);
    int plain(int a, ...);
    template <class... A>
    void pack(int a, A... rest);
    struct S {
      static int member;
      static void sm();
    };
  )cpp";
  auto syms = extractSymbols(src);
  auto get = [&](const std::string& n) -> const Symbol* {
    for (auto& s : syms)
      if (s.name == n) return &s;
    return nullptr;
  };
  REQUIRE(get("counter") != nullptr);
  CHECK(get("counter")->fileLocal);
  REQUIRE(get("helper") != nullptr);
  CHECK(get("helper")->fileLocal);
  CHECK_EQ(static_cast<int>(get("helper")->arity), 2);
  CHECK_EQ(static_cast<int>(get("helper")->minArity), 1);
  CHECK(!get("plain")->fileLocal);
  CHECK(get("plain")->variadic);
  CHECK_EQ(static_cast<int>(get("plain")->minArity), 1);
  CHECK(get("pack")->variadic);
  CHECK_EQ(static_cast<int>(get("pack")->minArity), 1);
  CHECK(!get("member")->fileLocal);  // a static member is not file-local
  CHECK(!get("sm")->fileLocal);
}

TEST(engine_fast_answers_use_the_scope_of_the_caret) {
  namespace fs = std::filesystem;
  fs::path root = fs::weakly_canonical(fs::temp_directory_path()) / "tenfold_fast_scope";
  std::error_code ec;
  fs::remove_all(root, ec);
  fs::create_directories(root);
  auto write = [&](const char* name, const std::string& text) { std::ofstream(root / name, std::ios::binary) << text; };
  write("shapes.h",
        "#pragma once\n"                                                           // 0
        "namespace a { struct P { int v; int get(); }; int mk(int x); }\n"         // 1
        "namespace b { struct P { int w; int get(); }; int mk(int x, int y); }\n"  // 2
        "struct Base { int shared(); int own(); };\n"                              // 3
        "struct Derived : Base { int own(); int run(); };\n");                     // 4
  write("shapes.cpp",
        "#include \"shapes.h\"\n"                            // 0
        "static int helper() { return 1; }\n"                // 1
        "int a::P::get() { return v; }\n"                    // 2
        "int b::P::get() { return w; }\n"                    // 3
        "int Base::shared() { return 1; }\n"                 // 4
        "int Base::own() { return 1; }\n"                    // 5
        "int Derived::own() { return 2; }\n"                 // 6
        "int Derived::run() { return shared() + own(); }\n"  // 7
        "int a::mk(int x) { return x; }\n"                   // 8
        "int b::mk(int x, int y) { return x + y; }\n");      // 9
  write("other.cpp",
        "#include \"shapes.h\"\n"                                               // 0
        "static int helper() { return 2; }\n"                                   // 1
        "int use_helper() { return helper(); }\n"                               // 2
        "namespace b {\n"                                                       // 3
        "int use_mk() { return mk(1, 2); }\n"                                   // 4
        "int use_p() { P p; return p.get(); }\n"                                // 5
        "}\n"                                                                   // 6
        "int use_a() {\n"                                                       // 7
        "  a::P p;\n"                                                           // 8
        "  b::P q;\n"                                                           // 9
        "  int get = 0;\n"                                                      // 10
        "  Derived d; Base* bp = &d;\n"                                         // 11
        "  return p.get() + q.get() + get + d.own() + bp->own() + a::mk(1);\n"  // 12
        "}\n");
  Editor::Options o;
  o.semantic.useClangd = false;
  Editor ed(root, o);
  std::string err;
  REQUIRE(ed.start(&err));
  CHECK(ed.semantic().waitForFastIndex(10000));
  auto ask = [&](const char* file, int line, const std::string& spec) {
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
    ed.gotoDefinition(cb);
    ed.waitFor([&] { return done; }, 5000);
    return out;
  };
  // a static function of another file is not offered; the caret's own file wins
  CHECK_EQ(ask("other.cpp", 2, "return |helper"), "other.cpp:1");
  CHECK_EQ(ask("shapes.cpp", 7, "|shared()"), "shapes.cpp:4");
  // inside a member function: own class first (hides the base), then the base
  CHECK_EQ(ask("shapes.cpp", 7, "+ |own()"), "shapes.cpp:6");
  // overloads in the caret's namespace chain, by argument count
  CHECK_EQ(ask("other.cpp", 4, "|mk(1, 2)"), "shapes.cpp:9");
  // member access through a declared type
  CHECK_EQ(ask("other.cpp", 5, "p.|get()"), "shapes.cpp:3");
  CHECK_EQ(ask("other.cpp", 12, "p.|get() + q"), "shapes.cpp:2");
  CHECK_EQ(ask("other.cpp", 12, "q.|get()"), "shapes.cpp:3");
  CHECK_EQ(ask("other.cpp", 12, "d.|own()"), "shapes.cpp:6");
  CHECK_EQ(ask("other.cpp", 12, "bp->|own()"), "shapes.cpp:5");
  // a qualifier picks the namespace; the argument count the overload
  CHECK_EQ(ask("other.cpp", 12, "a::|mk(1)"), "shapes.cpp:8");
  // a local variable hides the functions of the same name
  CHECK_EQ(ask("other.cpp", 12, "+ |get +"), "other.cpp:10");
}

#include "semantic/FastIndexStore.h"

TEST(fastindexstore_roundtrips_arity_range_variadic_and_file_local) {
  FastIndex a;
  a.updateFile("/w/a.cpp",
               "static int counter;\n"
               "static void helper(int a, int b = 2);\n"
               "int plain(int a, ...);\n"
               "struct S { static int member; };\n");
  const std::string blob = FastIndexStore::serialize(a.exportFiles());
  std::string err;
  auto back = FastIndexStore::deserialize(blob.data(), blob.size(), &err);
  REQUIRE(back.has_value());
  REQUIRE(back->size() == 1);
  const FileSymbols& f = *(*back)[0];
  auto get = [&](const std::string& n) -> const Symbol* {
    for (auto& s : f.symbols)
      if (s.name == n) return &s;
    return nullptr;
  };
  REQUIRE(get("counter") && get("helper") && get("plain") && get("member"));
  CHECK(get("counter")->fileLocal);
  CHECK(get("helper")->fileLocal);
  CHECK_EQ(static_cast<int>(get("helper")->arity), 2);
  CHECK_EQ(static_cast<int>(get("helper")->minArity), 1);
  CHECK(get("plain")->variadic);
  CHECK(!get("plain")->fileLocal);
  CHECK(!get("member")->definition);  // `static int member;` in a class declares
}

TEST(fastindex_reachability_follows_includes_and_include_directories) {
  FastIndex idx;
  idx.updateFile("/w/src/main.cpp", "#include \"engine.h\"\n#include \"util/log.h\"\n");
  idx.updateFile("/w/sub1/include/engine.h", "#include \"detail.h\"\nstruct Engine {};\n");
  idx.updateFile("/w/sub2/include/engine.h", "struct Engine {};\n");
  idx.updateFile("/w/sub1/include/detail.h", "int d1;\n");
  idx.updateFile("/w/sub2/include/detail.h", "int d2;\n");
  idx.updateFile("/w/inc/util/log.h", "void log();\n");
  // path-suffix rule alone: both engine.h are candidates
  auto both = idx.reachableFrom("/w/src/main.cpp");
  REQUIRE(both != nullptr);
  CHECK(both->count(FastIndex::pathKey("/w/sub1/include/engine.h")) == 1);
  CHECK(both->count(FastIndex::pathKey("/w/sub2/include/engine.h")) == 1);
  CHECK(both->count(FastIndex::pathKey("/w/inc/util/log.h")) == 1);
  // the compile command's include directory picks one, and the nested include follows the includer's directory
  auto one = idx.reachableFrom("/w/src/main.cpp", {}, {"/w/sub1/include", "/w/inc"});
  REQUIRE(one != nullptr);
  CHECK(one->count(FastIndex::pathKey("/w/sub1/include/engine.h")) == 1);
  CHECK(one->count(FastIndex::pathKey("/w/sub2/include/engine.h")) == 0);
  CHECK(one->count(FastIndex::pathKey("/w/sub1/include/detail.h")) == 1);
  CHECK(one->count(FastIndex::pathKey("/w/sub2/include/detail.h")) == 0);
  // an edit that changes the include list is seen
  idx.updateFile("/w/src/main.cpp", "#include \"util/log.h\"\n");
  auto after = idx.reachableFrom("/w/src/main.cpp");
  REQUIRE(after != nullptr);
  CHECK(after->count(FastIndex::pathKey("/w/sub1/include/engine.h")) == 0);
}
