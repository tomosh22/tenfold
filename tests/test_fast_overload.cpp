// FAST-OVL: overloads of equal arity and operator tokens in the fast tier (docs/ARCHITECTURE.md, "Overloads and
// operators in the fast tier"): the shape of declared types (ScopeScan), parameter lists read from declarations
// (FastOverload), argument type inference and overload selection, operator tokens, user-defined literals.
#include <algorithm>
#include <fstream>

#include "app/Editor.h"
#include "semantic/FastIndex.h"
#include "semantic/FastOverload.h"
#include "semantic/ScopeScan.h"
#include "test_main.h"

using namespace tf;

namespace {

const ScanVar* varIn(const ScopeMap& m, const std::string& name) {
  for (const ScanRegion& r : m.regions)
    for (const ScanVar& v : r.vars)
      if (v.name == name) return &v;
  return nullptr;
}

// The signature of the function whose name starts the given text (line 0, column of `needle` on its line).
OvSignature sigOf(const std::string& text, const std::string& name, int nth = 0) {
  size_t at = std::string::npos;
  for (int i = 0; i <= nth; ++i) at = text.find(name, at == std::string::npos ? 0 : at + 1);
  if (at == std::string::npos) return {};
  int line = 0;
  size_t ls = 0;
  for (size_t i = 0; i < at; ++i)
    if (text[i] == '\n') {
      ++line;
      ls = i + 1;
    }
  return parseSignature(text, line, static_cast<int>(at - ls), name);
}

std::string show(const OvType& t) {
  std::string s;
  switch (t.kind) {
    case OvType::Unknown: s = "?"; break;
    case OvType::Prim: s = t.name; break;
    case OvType::Named: s = t.name; break;
    case OvType::NullPtr: s = "nullptr_t"; break;
    case OvType::Generic: s = "$" + t.name; break;
  }
  if (t.constBase) s = "const " + s;
  s += std::string(t.ptr, '*');
  if (t.ref) s += t.ref == 1 ? "&" : "&&";
  return s;
}

std::string params(const OvSignature& s) {
  std::string r;
  for (const OvParam& p : s.params) r += (r.empty() ? "" : ", ") + show(p.ty);
  return r;
}

}  // namespace

TEST(scopescan_records_the_shape_of_declared_types) {
  const char* src = R"cpp(
    void f(const int a, unsigned long b, const char* c, char* const d, Foo& e, const Foo& g, Foo&& h, int** i) {
      auto n = 42;
      auto x = 1.5f;
      auto s = "text";
      auto ch = 'c';
      auto ok = true;
      auto p = new Foo(1);
      long long big = 0;
      std::string str;
      const std::vector<Foo>& vr = v;
    }
  )cpp";
  ScopeMap m = scanScopes(src);
  auto v = [&](const char* n) -> ScanVar {
    const ScanVar* r = varIn(m, n);
    CHECK(r != nullptr);
    return r ? *r : ScanVar{};
  };
  CHECK_EQ(v("a").prim, std::string("int"));
  CHECK(v("a").constBase);
  CHECK(v("a").constTop);
  CHECK_EQ(v("b").prim, std::string("unsigned long"));
  CHECK_EQ(v("c").prim, std::string("char"));
  CHECK_EQ(static_cast<int>(v("c").ptrDepth), 1);
  CHECK(v("c").constBase);
  CHECK(!v("c").constTop);  // the pointer itself is not const
  CHECK(v("d").constTop);   // `char *const`
  CHECK(!v("d").constBase);
  CHECK_EQ(v("e").type, std::string("Foo"));
  CHECK_EQ(static_cast<int>(v("e").ref), 1);
  CHECK(!v("e").constBase);
  CHECK(v("g").constBase);
  CHECK_EQ(static_cast<int>(v("h").ref), 2);
  CHECK_EQ(static_cast<int>(v("i").ptrDepth), 2);
  CHECK_EQ(v("n").prim, std::string("int"));  // auto from a literal
  CHECK_EQ(v("x").prim, std::string("float"));
  CHECK_EQ(v("s").prim, std::string("char"));
  CHECK_EQ(static_cast<int>(v("s").ptrDepth), 1);
  CHECK(v("s").constBase);
  CHECK_EQ(v("ch").prim, std::string("char"));
  CHECK_EQ(v("ok").prim, std::string("bool"));
  CHECK_EQ(v("p").type, std::string("Foo"));
  CHECK_EQ(static_cast<int>(v("p").ptrDepth), 1);
  CHECK_EQ(v("big").prim, std::string("long long"));
  CHECK_EQ(v("str").type, std::string("std::string"));
  CHECK(v("str").prim.empty());
  CHECK_EQ(v("vr").targ, std::string("Foo"));
  CHECK_EQ(static_cast<int>(v("vr").ref), 1);
}

TEST(literal_types_follow_the_standard) {
  auto prim = [](const char* t, bool str = false, bool chr = false) {
    ScanVar v;
    if (!literalVarType(t, str, chr, v)) return std::string("-");
    return v.prim + (v.ptrDepth ? std::string("*") : std::string());
  };
  CHECK_EQ(prim("42"), std::string("int"));
  CHECK_EQ(prim("0"), std::string("int"));
  CHECK_EQ(prim("42u"), std::string("unsigned int"));
  CHECK_EQ(prim("42L"), std::string("long"));
  CHECK_EQ(prim("42UL"), std::string("unsigned long"));
  CHECK_EQ(prim("42ull"), std::string("unsigned long long"));
  CHECK_EQ(prim("3000000000"), std::string("long"));  // does not fit an int
  CHECK_EQ(prim("0xFFFFFFFF"), std::string("unsigned int"));
  CHECK_EQ(prim("0b101"), std::string("int"));
  CHECK_EQ(prim("1'000'000"), std::string("int"));
  CHECK_EQ(prim("1.5"), std::string("double"));
  CHECK_EQ(prim("1.5f"), std::string("float"));
  CHECK_EQ(prim("1e3"), std::string("double"));
  CHECK_EQ(prim("2.5L"), std::string("long double"));
  CHECK_EQ(prim("2.5_km"), std::string("-"));  // a user-defined suffix
  CHECK_EQ(prim("12_m"), std::string("-"));
  CHECK_EQ(prim("0x1p3"), std::string("-"));
  CHECK_EQ(prim("'c'", false, true), std::string("char"));
  CHECK_EQ(prim("L'c'", false, true), std::string("wchar_t"));
  CHECK_EQ(prim("\"s\"", true), std::string("char*"));
  CHECK_EQ(prim("u8\"s\"", true), std::string("char8_t*"));
  CHECK_EQ(prim("\"s\"_sv", true), std::string("-"));
  CHECK_EQ(prim("true"), std::string("bool"));
  CHECK_EQ(prim("99999999999999999999999"), std::string("-"));
}

TEST(ovsig_reads_parameter_types_and_qualifiers) {
  const std::string text =
      "int a(int x, double y);\n"                                        //
      "void b(const char* s, const std::string& t, Foo&& u, Foo& v);\n"  //
      "struct S {\n"                                                     //
      "  int c() const;\n"                                               //
      "  int c();\n"                                                     //
      "  int d() &;\n"                                                   //
      "  int d() &&;\n"                                                  //
      "  int e() const& noexcept;\n"                                     //
      "  static int f(unsigned x = 3, long long y = 4);\n"               //
      "  virtual void g(int) override;\n"                                //
      "};\n"                                                             //
      "void h(void);\n"                                                  //
      "void i(int arr[], char (*fp)(int), ...);\n"                       //
      "int j(unsigned, signed char, long double, std::size_t n);\n"      //
      "void k(Foo<Bar>* p, ns::Baz<int, 3> const& q);\n";                //
  OvSignature a = sigOf(text, "a");
  REQUIRE(a.ok);
  CHECK_EQ(params(a), std::string("int, double"));
  OvSignature b = sigOf(text, "b(");
  REQUIRE(b.ok);
  CHECK_EQ(params(b), std::string("const char*, const std::string&, Foo&&, Foo&"));
  OvSignature c0 = sigOf(text, "c(");
  OvSignature c1 = sigOf(text, "c(", 1);
  CHECK(c0.ok && c0.isConst);
  CHECK(c1.ok && !c1.isConst);
  CHECK_EQ(static_cast<int>(sigOf(text, "d(").refQual), 1);
  CHECK_EQ(static_cast<int>(sigOf(text, "d(", 1).refQual), 2);
  OvSignature e = sigOf(text, "e(");
  CHECK(e.isConst);
  CHECK_EQ(static_cast<int>(e.refQual), 1);
  OvSignature f = sigOf(text, "f(");
  CHECK(f.isStatic);
  CHECK_EQ(params(f), std::string("unsigned int, long long"));
  CHECK(f.params[0].hasDefault && f.params[1].hasDefault);
  CHECK_EQ(params(sigOf(text, "g(")), std::string("int"));
  OvSignature h = sigOf(text, "h(");
  REQUIRE(h.ok);
  CHECK(h.params.empty());  // (void)
  OvSignature i = sigOf(text, "i(");
  REQUIRE(i.ok);
  CHECK(i.cVariadic);
  CHECK_EQ(show(i.params[0].ty), std::string("int*"));  // an array parameter is a pointer
  CHECK(i.params[1].ty.kind == OvType::Unknown);        // a function pointer is not read
  OvSignature j = sigOf(text, "j(");
  REQUIRE(j.ok);
  CHECK_EQ(params(j), std::string("unsigned int, signed char, long double, std::size_t"));
  OvSignature k = sigOf(text, "k(");
  REQUIRE(k.ok);
  CHECK_EQ(show(k.params[0].ty), std::string("Foo*"));
  CHECK_EQ(k.params[0].ty.targ, std::string("Bar"));
  CHECK_EQ(show(k.params[1].ty), std::string("const ns::Baz&"));
}

TEST(ovsig_templates_operators_and_wrong_positions) {
  const std::string text =
      "template <class T, int N>\n"                       //
      "int pick(T v, const T& w, T* p, T&& r, int n);\n"  //
      "struct V {\n"                                      //
      "  V& operator+=(const V& o);\n"                    //
      "  double& operator[](int i);\n"                    //
      "  double operator()(double a, double b) const;\n"  //
      "  explicit operator bool() const;\n"               //
      "  auto operator<=>(const V&) const = default;\n"   //
      "};\n"                                              //
      "V operator+(V a, const V& b);\n"                   //
      "Length operator\"\"_km(long double v);\n";         //
  OvSignature p = sigOf(text, "pick");
  REQUIRE(p.ok);
  CHECK(p.isTemplate);
  CHECK_EQ(p.tparams.size(), static_cast<size_t>(2));
  CHECK_EQ(params(p), std::string("$T, const $T&, $T*, $T&&, int"));
  CHECK_EQ(params(sigOf(text, "operator+=")), std::string("const V&"));
  CHECK_EQ(params(sigOf(text, "operator[]")), std::string("int"));
  CHECK_EQ(params(sigOf(text, "operator()")), std::string("double, double"));
  CHECK(sigOf(text, "operator()").isConst);
  OvSignature conv = sigOf(text, "operator bool");
  CHECK(conv.ok && conv.params.empty() && conv.isConst);
  OvSignature spaceship = sigOf(text, "operator<=>");
  CHECK(spaceship.ok && spaceship.isDefaulted);
  CHECK_EQ(params(sigOf(text, "operator+(")), std::string("V, const V&"));
  CHECK_EQ(params(sigOf(text, "operator\"\"_km")), std::string("long double"));
  // a position that does not hold the name, or past the end, is "not parsed"
  CHECK(!parseSignature(text, 0, 3, "pick").ok);
  CHECK(!parseSignature(text, 99, 0, "pick").ok);
  CHECK(!parseSignature(text, 1, 0, "pick").ok);
  CHECK(!parseSignature("", 0, 0, "f").ok);
  CHECK(!parseSignature("int f", 0, 4, "f").ok);        // no parameter list
  CHECK(!parseSignature("int f(int a", 0, 4, "f").ok);  // unterminated
}

TEST(ovsig_never_throws_on_garbage) {
  const char* junk[] = {"f(",         "f((((",       "operator",    "operator(",         "operator()",
                        "f(<<>>",     "f(int x, ",   "f(...)",      "f(a<b<c<d<e, int)", "f(int[[[",
                        "f(int (*)(", "template<f(", "f(auto&&...)"};
  for (const char* j : junk) {
    parseSignature(j, 0, 0, "f");
    parseSignature(j, 0, 0, "operator");
  }
  parseSignature(std::string(100000, '('), 0, 0, "f");
  CHECK(true);
}

namespace {

struct OvProject {
  std::filesystem::path root;
  std::unique_ptr<Editor> ed;
  explicit OvProject(const char* dir, const std::vector<std::pair<std::string, std::string>>& files) {
    namespace fs = std::filesystem;
    root = fs::weakly_canonical(fs::temp_directory_path()) / dir;
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root);
    for (auto& [n, t] : files) std::ofstream(root / n, std::ios::binary) << t;
    Editor::Options o;
    o.semantic.useClangd = false;
    ed = std::make_unique<Editor>(root, o);
    std::string err;
    REQUIRE(ed->start(&err));
    CHECK(ed->semantic().waitForFastIndex(10000));
  }
  // The definition answer at the `|` of `spec` on `line` of `file`, as sorted "name:line" entries.
  std::string ask(const char* file, int line, const std::string& spec) {
    std::ifstream in(root / file);
    std::string s;
    for (int i = 0; i <= line && std::getline(in, s);) ++i;
    const size_t bar = spec.find('|');
    std::string needle = spec;
    needle.erase(bar, 1);
    const size_t at = s.find(needle);
    if (at == std::string::npos) return std::string("no such text: ") + spec;
    if (!ed->gotoLocation({root / file, {line, static_cast<int>(at + bar)}})) return std::string("goto failed");
    bool done = false;
    std::string out;
    auto cb = [&](Confidence, const std::vector<SymbolLocation>& locs, const std::string&) {
      std::vector<std::string> v;
      for (auto& l : locs) v.push_back(l.loc.file.filename().string() + ":" + std::to_string(l.loc.pos.line));
      std::sort(v.begin(), v.end());
      for (auto& x : v) out += (out.empty() ? "" : ",") + x;
      done = true;
    };
    ed->gotoDefinition(cb);
    ed->waitFor([&] { return done; }, 5000);
    return out;
  }
};

}  // namespace

TEST(engine_overloads_by_argument_types) {
  // clang-format off
  OvProject p("tenfold_fast_ovl",
              {{"ov.h",
                "#pragma once\n"                                                       // 0
                "#include <string>\n"                                                  // 1
                "int conv(int);\n"                                                     // 2
                "int conv(double);\n"                                                  // 3
                "int conv(const char*);\n"                                             // 4
                "int ptrq(int);\n"                                                     // 5
                "int ptrq(void*);\n"                                                   // 6
                "struct Cel { double d; };\n"                                          // 7
                "struct Kel { double k; Kel(double v); Kel(Cel c); };\n"               // 8
                "int temp(Kel);\n"                                                     // 9
                "int temp(const char*);\n"                                             // 10
                "enum class Color { Red, Green };\n"                                   // 11
                "int paint(Color);\n"                                                  // 12
                "int paint(int);\n"                                                    // 13
                "struct Buf {\n"                                                       // 14
                "  int at(int i);\n"                                                   // 15
                "  int at(int i) const;\n"                                             // 16
                "  int who() &;\n"                                                     // 17
                "  int who() &&;\n"                                                    // 18
                "  int who() const&;\n"                                                // 19
                "  void put(const Buf&);\n"                                            // 20
                "  void put(Buf&&);\n"                                                 // 21
                "};\n"                                                                 // 22
                "template <class T> int pick(T);\n"                                    // 23
                "template <class T> int pick(T*);\n"                                   // 24
                "int pick(int);\n"                                                     // 25
                "void log(const char*, int level = 1);\n"                              // 26
                "void log(const std::string&);\n"                                      // 27
                "int ref(int&);\n"                                                     // 28
                "int ref(const int&);\n"                                               // 29
                "int uns(unsigned);\n"                                                 // 30
                "int uns(long);\n"                                                     // 31
                "struct Holder { int count; const char* name; Buf buf; };\n"},         // 32
               {"main.cpp",
                "#include \"ov.h\"\n"                                                  // 0
                "int g_value = 3;\n"                                                   // 1
                "int run(int n, const char* s, double d, Holder& h, const Buf& cb) {\n"  // 2
                "  Buf buf; const Buf cbuf; int local = 1; auto lit = 2.5;\n"          // 3
                "  conv(1); conv(1.5); conv(\"s\"); conv('c'); conv(1.5f);\n"          // 4
                "  conv(n); conv(d); conv(s); conv(lit); conv(local);\n"               // 5
                "  conv(h.count); conv(h.name); conv(static_cast<double>(n));\n"       // 6
                "  ptrq(0); ptrq(nullptr); ptrq(&local);\n"                            // 7
                "  temp(Kel(3.0)); temp(Cel{20.0}); temp(\"x\");\n"                    // 8
                "  paint(Color::Red); paint(3);\n"                                     // 9
                "  buf.at(0); cbuf.at(0); cb.at(0); h.buf.at(0);\n"                    // 10
                "  buf.who(); Buf().who(); cbuf.who(); std::move(buf).who();\n"       // 11
                "  buf.put(buf); buf.put(Buf()); buf.put(std::move(buf));\n"           // 12
                "  pick(1); pick(1.5); pick(&local); pick<int>(1);\n"                  // 13
                "  log(\"hello\"); log(std::string(\"x\")); log(\"x\", 2);\n"          // 14
                "  ref(local); ref(3); ref(n);\n"                                      // 15
                "  uns(3u); uns(3L); uns(n);\n"                                        // 16
                "  conv(unknown_fn()); conv(local + 1); conv(h.nothing);\n"            // 17
                "  return 0;\n"                                                        // 18
                "}\n"}});
  // clang-format on
  // by literal type: int / double / const char* / char (promotes to int) / float (promotes to double)
  CHECK_EQ(p.ask("main.cpp", 4, "|conv(1)"), "ov.h:2");
  CHECK_EQ(p.ask("main.cpp", 4, "|conv(1.5)"), "ov.h:3");
  CHECK_EQ(p.ask("main.cpp", 4, "|conv(\"s\")"), "ov.h:4");
  CHECK_EQ(p.ask("main.cpp", 4, "|conv('c')"), "ov.h:2");
  CHECK_EQ(p.ask("main.cpp", 4, "|conv(1.5f)"), "ov.h:3");
  // by declared type of a parameter, a local, `auto` from a literal, a field of a parameter
  CHECK_EQ(p.ask("main.cpp", 5, "|conv(n)"), "ov.h:2");
  CHECK_EQ(p.ask("main.cpp", 5, "|conv(d)"), "ov.h:3");
  CHECK_EQ(p.ask("main.cpp", 5, "|conv(s)"), "ov.h:4");
  CHECK_EQ(p.ask("main.cpp", 5, "|conv(lit)"), "ov.h:3");
  CHECK_EQ(p.ask("main.cpp", 5, "|conv(local)"), "ov.h:2");
  CHECK_EQ(p.ask("main.cpp", 6, "|conv(h.count)"), "ov.h:2");
  CHECK_EQ(p.ask("main.cpp", 6, "|conv(h.name)"), "ov.h:4");
  CHECK_EQ(p.ask("main.cpp", 6, "|conv(static_cast<double>(n))"), "ov.h:3");
  // 0 is an int, nullptr a pointer, &local an int* (void* takes it, int does not)
  CHECK_EQ(p.ask("main.cpp", 7, "|ptrq(0)"), "ov.h:5");
  CHECK_EQ(p.ask("main.cpp", 7, "|ptrq(nullptr)"), "ov.h:6");
  CHECK_EQ(p.ask("main.cpp", 7, "|ptrq(&local)"), "ov.h:6");
  // class arguments: a construction is exact; Cel reaches Kel through its constructor; a string literal the pointer
  CHECK_EQ(p.ask("main.cpp", 8, "|temp(Kel(3.0))"), "ov.h:9");
  CHECK_EQ(p.ask("main.cpp", 8, "|temp(Cel{20.0})"), "ov.h:9");
  CHECK_EQ(p.ask("main.cpp", 8, "|temp(\"x\")"), "ov.h:10");
  // an enumerator names its enum
  CHECK_EQ(p.ask("main.cpp", 9, "|paint(Color::Red)"), "ov.h:12");
  CHECK_EQ(p.ask("main.cpp", 9, "|paint(3)"), "ov.h:13");
  // const / non-const objects, through a parameter and through a field
  CHECK_EQ(p.ask("main.cpp", 10, "buf.|at(0)"), "ov.h:15");
  CHECK_EQ(p.ask("main.cpp", 10, "cbuf.|at(0)"), "ov.h:16");
  CHECK_EQ(p.ask("main.cpp", 10, "cb.|at(0)"), "ov.h:16");
  CHECK_EQ(p.ask("main.cpp", 10, "h.buf.|at(0)"), "ov.h:15");
  // ref-qualified members: lvalue, temporary, const lvalue, std::move
  CHECK_EQ(p.ask("main.cpp", 11, "buf.|who()"), "ov.h:17");
  CHECK_EQ(p.ask("main.cpp", 11, "Buf().|who()"), "ov.h:18");
  CHECK_EQ(p.ask("main.cpp", 11, "cbuf.|who()"), "ov.h:19");
  CHECK_EQ(p.ask("main.cpp", 11, "std::move(buf).|who()"), "ov.h:18");
  // const& against &&
  CHECK_EQ(p.ask("main.cpp", 12, "buf.|put(buf)"), "ov.h:20");
  CHECK_EQ(p.ask("main.cpp", 12, "buf.|put(Buf())"), "ov.h:21");
  CHECK_EQ(p.ask("main.cpp", 12, "buf.|put(std::move(buf))"), "ov.h:21");
  // templates: a plain function beats a template on a tie, T* is more specialised than T, explicit arguments
  CHECK_EQ(p.ask("main.cpp", 13, "|pick(1)"), "ov.h:25");
  CHECK_EQ(p.ask("main.cpp", 13, "|pick(1.5)"), "ov.h:23");
  CHECK_EQ(p.ask("main.cpp", 13, "|pick(&local)"), "ov.h:24");
  CHECK_EQ(p.ask("main.cpp", 13, "|pick<int>(1)"), "ov.h:23");
  // a string literal is a pointer, not a std::string (user-defined conversion)
  CHECK_EQ(p.ask("main.cpp", 14, "|log(\"hello\")"), "ov.h:26");
  CHECK_EQ(p.ask("main.cpp", 14, "|log(std::string(\"x\"))"), "ov.h:27");
  CHECK_EQ(p.ask("main.cpp", 14, "|log(\"x\", 2)"), "ov.h:26");
  // a non-const lvalue reference takes only a non-const lvalue; the const one binds the rest
  CHECK_EQ(p.ask("main.cpp", 15, "|ref(local)"), "ov.h:28");
  CHECK_EQ(p.ask("main.cpp", 15, "|ref(3)"), "ov.h:29");
  // unsigned / long: exact match decides
  CHECK_EQ(p.ask("main.cpp", 16, "|uns(3u)"), "ov.h:30");
  CHECK_EQ(p.ask("main.cpp", 16, "|uns(3L)"), "ov.h:31");
  // evidence that is missing keeps the whole set: an unknown call, an expression, an unknown field
  CHECK_EQ(p.ask("main.cpp", 17, "|conv(unknown_fn())"), "ov.h:2,ov.h:3,ov.h:4");
  CHECK_EQ(p.ask("main.cpp", 17, "|conv(local + 1)"), "ov.h:2,ov.h:3,ov.h:4");
  CHECK_EQ(p.ask("main.cpp", 17, "|conv(h.nothing)"), "ov.h:2,ov.h:3,ov.h:4");
}

TEST(engine_operator_tokens_resolve_to_user_declared_operators) {
  // clang-format off
  OvProject p("tenfold_fast_ops",
              {{"v.h",
                "#pragma once\n"                                                       // 0
                "namespace m {\n"                                                      // 1
                "struct Vec {\n"                                                       // 2
                "  double x = 0, y = 0;\n"                                             // 3
                "  Vec& operator+=(const Vec& o);\n"                                   // 4
                "  Vec operator-() const;\n"                                           // 5
                "  double& operator[](int i);\n"                                       // 6
                "  const double& operator[](int i) const;\n"                           // 7
                "  double operator()(double s) const;\n"                               // 8
                "  double operator()(double a, double b) const;\n"                     // 9
                "  Vec& operator++();\n"                                               // 10
                "  Vec operator++(int);\n"                                             // 11
                "  bool operator==(const Vec&) const;\n"                               // 12
                "};\n"                                                                 // 13
                "Vec operator+(const Vec& a, const Vec& b);\n"                         // 14
                "Vec operator*(const Vec& v, double s);\n"                             // 15
                "Vec operator*(double s, const Vec& v);\n"                             // 16
                "struct Out;\n"                                                        // 17
                "Out& operator<<(Out& o, const Vec& v);\n"                             // 18
                "struct Ptr { Vec* operator->() const; Vec& operator*() const; };\n"   // 19
                "enum class Flags : unsigned { A = 1, B = 2 };\n"                      // 20
                "Flags operator|(Flags a, Flags b);\n"                                 // 21
                "struct Len { long double m; };\n"                                     // 22
                "Len operator\"\"_km(long double v);\n"                                // 23
                "Len operator\"\"_m(unsigned long long v);\n"                          // 24
                "struct Id { int v; Id(int x); bool operator==(const Id&) const; };\n" // 25
                "}\n"},                                                                // 26
               {"main.cpp",
                "#include \"v.h\"\n"                                                   // 0
                "using namespace m;\n"                                                 // 1
                "int run(Vec a, const Vec& cb, Ptr pt, Out& os, Flags f, Id x, Id y) {\n"  // 2
                "  Vec b, c = a + b;\n"                                                // 3
                "  a += b; Vec n = -a;\n"                                              // 4
                "  Vec m1 = a * 2.0; Vec m2 = 2.0 * a;\n"                              // 5
                "  double e0 = a /**/[0]; double e1 = cb /**/[1];\n"                             // 6
                "  double c1 = a /**/(1.0); double c2 = a /**/(1.0, 2.0);\n"                     // 7
                "  ++a; a /**/++;\n"                                                        // 8
                "  bool q = a == b;\n"                                                 // 9
                "  os << a;\n"                                                         // 10
                "  Vec* pv = pt.operator->(); Vec w = *pt; double px = pt /**/->x;\n"       // 11
                "  Flags g = f | Flags::B;\n"                                          // 12
                "  Len l1 = 2.5_km; Len l2 = 3_m;\n"                                   // 13
                "  bool r = x == y; bool r2 = 1 == y;\n"                               // 14
                "  int i = 1 + 2; int* ip = &i; int k = *ip * 3; bool lt = i < k;\n"   // 15
                "  // a + b\n"                                                         // 16
                "  const char* t = \"a + b\";\n"                                       // 17
                "  /* a + b\n"                                                         // 18
                "     a + b */\n"                                                      // 19
                "  Vec chain = a + b + c;\n"                                           // 20
                "  return 0;\n"                                                        // 21
                "}\n"}});
  // clang-format on
  CHECK_EQ(p.ask("main.cpp", 3, "a |+ b"), "v.h:14");
  CHECK_EQ(p.ask("main.cpp", 4, "a |+= b"), "v.h:4");
  CHECK_EQ(p.ask("main.cpp", 4, "= |-a"), "v.h:5");
  // binary and unary minus are different functions; the operand order picks the `*`
  CHECK_EQ(p.ask("main.cpp", 5, "a |* 2.0"), "v.h:15");
  CHECK_EQ(p.ask("main.cpp", 5, "2.0 |* a"), "v.h:16");
  // subscript by constness, call by arity
  CHECK_EQ(p.ask("main.cpp", 6, "a /**/|[0]"), "v.h:6");
  CHECK_EQ(p.ask("main.cpp", 6, "cb /**/|[1]"), "v.h:7");
  CHECK_EQ(p.ask("main.cpp", 7, "a /**/|(1.0)"), "v.h:8");
  CHECK_EQ(p.ask("main.cpp", 7, "a /**/|(1.0, 2.0)"), "v.h:9");
  // prefix and postfix increment
  CHECK_EQ(p.ask("main.cpp", 8, "|++a"), "v.h:10");
  CHECK_EQ(p.ask("main.cpp", 8, "a /**/|++;"), "v.h:11");
  CHECK_EQ(p.ask("main.cpp", 9, "a |== b"), "v.h:12");
  // a free `<<` whose left operand is a library type's result is still found by its class operand
  CHECK_EQ(p.ask("main.cpp", 10, "os |<< a"), "v.h:18");
  CHECK_EQ(p.ask("main.cpp", 11, "pt /**/|->x"), "v.h:19");
  CHECK_EQ(p.ask("main.cpp", 12, "f || Flags::B"), "v.h:21");
  // user-defined literals by the literal's kind
  CHECK_EQ(p.ask("main.cpp", 13, "|2.5_km"), "v.h:23");
  CHECK_EQ(p.ask("main.cpp", 13, "|3_m"), "v.h:24");
  // rewritten comparison: the reversed operands of `1 == y` reach Id::operator==
  CHECK_EQ(p.ask("main.cpp", 14, "x |== y"), "v.h:25");
  CHECK_EQ(p.ask("main.cpp", 14, "1 |== y"), "v.h:25");
  // builtin operators, comments, strings and chained results are not answered
  CHECK_EQ(p.ask("main.cpp", 15, "1 |+ 2"), "");
  CHECK_EQ(p.ask("main.cpp", 15, "|* 3"), "");
  CHECK_EQ(p.ask("main.cpp", 15, "i |< k"), "");
  CHECK_EQ(p.ask("main.cpp", 16, "a |+ b"), "");
  CHECK_EQ(p.ask("main.cpp", 17, "a |+ b"), "");
  CHECK_EQ(p.ask("main.cpp", 19, "a |+ b"), "");
  // `a + b + c`: the first `+` has operands the lexer knows; the second one's left is a result (still a free function)
  CHECK_EQ(p.ask("main.cpp", 20, "a |+ b + c"), "v.h:14");
}

TEST(engine_adl_adds_the_namespaces_of_the_arguments) {
  // clang-format off
  OvProject p("tenfold_fast_adl",
              {{"sh.h",
                "#pragma once\n"                                 // 0
                "namespace shape {\n"                            // 1
                "struct Circle { int r = 1; };\n"                // 2
                "struct Square { int s = 1; };\n"                // 3
                "void draw(const Circle&);\n"                    // 4
                "void draw(const Square&);\n"                    // 5
                "}\n"                                            // 6
                "namespace other { void draw(const shape::Circle&); }\n"  // 7
                "void draw(int);\n"},                            // 8
               {"main.cpp",
                "#include \"sh.h\"\n"                            // 0
                "int main() {\n"                                 // 1
                "  shape::Circle c; shape::Square sq;\n"         // 2
                "  draw(c); draw(sq); draw(3); other::draw(c);\n"  // 3
                "}\n"}});
  // clang-format on
  CHECK_EQ(p.ask("main.cpp", 3, "|draw(c)"), "sh.h:4");
  CHECK_EQ(p.ask("main.cpp", 3, "|draw(sq)"), "sh.h:5");
  CHECK_EQ(p.ask("main.cpp", 3, "|draw(3)"), "sh.h:8");
  CHECK_EQ(p.ask("main.cpp", 3, "other::|draw(c)"), "sh.h:7");
}
