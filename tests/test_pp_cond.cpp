// W12-FAST-PP: preprocessor conditionals in the fast tier: the #if expression evaluator (three-valued), directive
// parsing, the extractor's condition arms, the persisted store, compile-command macros, the translation-unit
// evaluator, and the engine's use of all of it (docs/ARCHITECTURE.md, "Preprocessor conditionals in the fast tier").
#include <algorithm>
#include <fstream>
#include <map>
#include <set>

#include "app/Editor.h"
#include "semantic/CompileCommands.h"
#include "semantic/FastIndex.h"
#include "semantic/FastIndexStore.h"
#include "semantic/PpCond.h"
#include "semantic/PpExpr.h"
#include "test_main.h"

using namespace tf;
using namespace tf::pp;
namespace fs = std::filesystem;

namespace {

using Defs = std::map<std::string, MacroState>;

MacroLookup lookupIn(const Defs& d, MacroState otherwise = MacroState::undefined()) {
  return [&d, otherwise](std::string_view n) {
    auto it = d.find(std::string(n));
    return it == d.end() ? otherwise : it->second;
  };
}

Tri eval(const std::string& e, const Defs& d = {}, MacroState otherwise = MacroState::undefined()) {
  return evalCondition(e, lookupIn(d, otherwise));
}

const char* name(Tri t) { return t == Tri::True ? "True" : (t == Tri::False ? "False" : "Unknown"); }

#define CHECK_TRI(expr, want)                                                                            \
  do {                                                                                                   \
    const Tri _got = (expr);                                                                             \
    if (_got != (want))                                                                                  \
      tftest::fail(__FILE__, __LINE__, std::string(#expr " is ") + name(_got) + ", want " + name(want)); \
  } while (0)

CompileCommand command(std::vector<std::string> args, const std::string& file = "/w/main.cpp") {
  CompileCommand c;
  c.directory = "/w";
  c.file = file;
  c.arguments = std::move(args);
  return c;
}

std::shared_ptr<FileSymbols> extract(const std::string& path, const std::string& text) {
  auto f = std::make_shared<FileSymbols>();
  f->file = path;
  extractFile(text, *f, isCSourceFile(path));
  return f;
}

const Symbol* symbol(const FileSymbols& f, const std::string& name, size_t* index = nullptr) {
  for (size_t i = 0; i < f.symbols.size(); ++i)
    if (f.symbols[i].name == name) {
      if (index) *index = i;
      return &f.symbols[i];
    }
  return nullptr;
}

// Is symbol `n` of `f` active in the translation unit made of `files` compiled with `cmd`?
// `project`: the names the project itself mentions (some command passes them as -D / -U); the default covers the
// configuration macros the tests below use, none of the system ones.
Tri activeIn(const CompileCommand* cmd, std::vector<std::shared_ptr<const FileSymbols>> files, const FileSymbols& f,
             const std::string& n,
             std::set<std::string> project = {"LEVEL_9", "LEVEL_3", "LEVEL_1", "MODE_A", "MODE_B", "VERSION",
                                              "FEATURE_Q", "FEATURE_X", "MODE", "LEVEL", "A", "B", "C", "COND",
                                              "LATER"}) {
  TuEnv::Input in;
  if (cmd) in.command = macrosOfCommand(*cmd, Host{Host::Os::Linux, Host::Arch::X64});
  in.files = std::move(files);
  in.setByFlags = [project](const std::string& nm) { return project.count(nm) != 0; };
  const TuEnv env(std::move(in));
  Evaluator ev(env);
  size_t idx = 0;
  if (!symbol(f, n, &idx)) return Tri::Unknown;
  return ev.armActive(f, f.armOf(idx));
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------------
// the expression evaluator
// ---------------------------------------------------------------------------------------------------------------------

TEST(pp_eval_literals_and_operators) {
  CHECK_TRI(eval("1"), Tri::True);
  CHECK_TRI(eval("0"), Tri::False);
  CHECK_TRI(eval("0x10 == 16"), Tri::True);
  CHECK_TRI(eval("010 == 8"), Tri::True);
  CHECK_TRI(eval("0b101 == 5"), Tri::True);
  CHECK_TRI(eval("1'000 == 1000"), Tri::True);
  CHECK_TRI(eval("10UL == 10"), Tri::True);
  CHECK_TRI(eval("'a' == 97"), Tri::True);
  CHECK_TRI(eval("'\\n' == 10"), Tri::True);
  CHECK_TRI(eval("'\\x41' == 65"), Tri::True);
  CHECK_TRI(eval("1 + 2 * 3 == 7"), Tri::True);
  CHECK_TRI(eval("(1 + 2) * 3 == 9"), Tri::True);
  CHECK_TRI(eval("7 / 2 == 3 && 7 % 2 == 1"), Tri::True);
  CHECK_TRI(eval("-7 / 2 == -3"), Tri::True);
  CHECK_TRI(eval("1 << 4 == 16 && 256 >> 4 == 16"), Tri::True);
  CHECK_TRI(eval("(5 & 3) == 1 && (5 | 3) == 7 && (5 ^ 3) == 6"), Tri::True);
  CHECK_TRI(eval("~0 == -1"), Tri::True);
  CHECK_TRI(eval("!0"), Tri::True);
  CHECK_TRI(eval("!5"), Tri::False);
  CHECK_TRI(eval("- -3 == 3 && +4 == 4"), Tri::True);
  CHECK_TRI(eval("3 < 4 && 4 <= 4 && 5 > 4 && 5 >= 5 && 1 != 2"), Tri::True);
  CHECK_TRI(eval("1 ? 0 : 1"), Tri::False);
  CHECK_TRI(eval("0 ? 0 : 2"), Tri::True);
  CHECK_TRI(eval("1 ? 2 ? 3 : 0 : 0"), Tri::True);
  CHECK_TRI(eval("1 || 0 && 0"), Tri::True);  // && binds tighter than ||
  CHECK_TRI(eval("(1 || 0) && 0"), Tri::False);
  CHECK_TRI(eval("true"), Tri::True);
  CHECK_TRI(eval("false"), Tri::False);
  CHECK_TRI(eval("1 and not 0"), Tri::True);
  CHECK_TRI(eval("0 or 0"), Tri::False);
  CHECK_TRI(eval("(1 bitand 3) == 1 && (1 bitor 2) == 3 && (3 xor 1) == 2 && (compl 0) == -1"), Tri::True);
  CHECK_TRI(eval("1 not_eq 2"), Tri::True);
}

TEST(pp_eval_signed_and_unsigned_rules) {
  CHECK_TRI(eval("-1 < 0"), Tri::True);
  CHECK_TRI(eval("-1 < 0u"), Tri::False);        // -1 converts to unsigned
  CHECK_TRI(eval("0u - 1 > 0"), Tri::True);      // wraps
  CHECK_TRI(eval("-1 >> 1 == -1"), Tri::True);   // arithmetic shift of a signed value
  CHECK_TRI(eval("-1u >> 63 == 1"), Tri::True);  // logical shift of an unsigned one
  CHECK_TRI(eval("18446744073709551615u == -1"), Tri::True);
  CHECK_TRI(eval("0xFFFFFFFFFFFFFFFF > 0"), Tri::True);  // too big for intmax_t: unsigned
  CHECK_TRI(eval("(0 ? 1u : -1) > 0"), Tri::True);       // the conditional has the common (unsigned) type
}

TEST(pp_eval_defined_and_macros) {
  Defs d{{"A", MacroState::integer(1)}, {"B", MacroState::defined()}, {"LEVEL", MacroState::integer(3)}};
  CHECK_TRI(eval("defined(A)", d), Tri::True);
  CHECK_TRI(eval("defined A", d), Tri::True);
  CHECK_TRI(eval("defined ( A )", d), Tri::True);
  CHECK_TRI(eval("defined(B)", d), Tri::True);
  CHECK_TRI(eval("defined(C)", d), Tri::False);
  CHECK_TRI(eval("!defined(C) && defined(A)", d), Tri::True);
  CHECK_TRI(eval("A", d), Tri::True);
  CHECK_TRI(eval("C", d), Tri::False);  // an identifier that is not a macro is 0
  CHECK_TRI(eval("LEVEL >= 3 && LEVEL < 4", d), Tri::True);
  CHECK_TRI(eval("LEVEL == 2", d), Tri::False);
  CHECK_TRI(eval("(LEVEL - 1) * 2 == 4", d), Tri::True);
  CHECK_TRI(eval("defined(A) && A == 1 || C", d), Tri::True);
}

TEST(pp_eval_three_valued_logic) {
  Defs d{{"A", MacroState::integer(1)}, {"B", MacroState::defined()}, {"U", MacroState::unknown()}};
  // unknown macros
  CHECK_TRI(eval("U", d), Tri::Unknown);
  CHECK_TRI(eval("defined(U)", d), Tri::Unknown);
  CHECK_TRI(eval("!defined(U)", d), Tri::Unknown);
  CHECK_TRI(eval("U == 1", d), Tri::Unknown);
  CHECK_TRI(eval("U + 1", d), Tri::Unknown);
  // a macro that is defined but has no known value
  CHECK_TRI(eval("B", d), Tri::Unknown);
  CHECK_TRI(eval("B == 1", d), Tri::Unknown);
  // one operand decides && / || / ?:
  CHECK_TRI(eval("0 && U", d), Tri::False);
  CHECK_TRI(eval("U && 0", d), Tri::False);
  CHECK_TRI(eval("1 || U", d), Tri::True);
  CHECK_TRI(eval("U || 1", d), Tri::True);
  CHECK_TRI(eval("1 && U", d), Tri::Unknown);
  CHECK_TRI(eval("0 || U", d), Tri::Unknown);
  CHECK_TRI(eval("defined(A) && defined(U)", d), Tri::Unknown);
  CHECK_TRI(eval("!defined(Z) && defined(U)", d), Tri::Unknown);
  CHECK_TRI(eval("defined(Z) && defined(U)", d), Tri::False);
  CHECK_TRI(eval("U ? 1 : 1", d), Tri::True);  // same either way
  CHECK_TRI(eval("U ? 1 : 0", d), Tri::Unknown);
  CHECK_TRI(eval("1 ? 5 : U", d), Tri::True);
  CHECK_TRI(eval("0 ? U : 0", d), Tri::False);
  // everything else that cannot be evaluated is Unknown, never False
  CHECK_TRI(eval("__has_include(\"x.h\")", d), Tri::Unknown);
  CHECK_TRI(eval("__has_include(<a/b.h>) && 0", d), Tri::False);
  CHECK_TRI(eval("__has_builtin(__builtin_expect)", d), Tri::Unknown);
  CHECK_TRI(eval("FUNC(1, 2)", d), Tri::Unknown);
  CHECK_TRI(eval("FUNC(1, (2)) || 1", d), Tri::True);
  CHECK_TRI(eval("1 / 0", d), Tri::Unknown);
  CHECK_TRI(eval("1 % 0", d), Tri::Unknown);
  CHECK_TRI(eval("1 << 70", d), Tri::Unknown);
  CHECK_TRI(eval("1 << -1", d), Tri::Unknown);
  CHECK_TRI(eval("1.5", d), Tri::Unknown);
  CHECK_TRI(eval("1e3 > 2", d), Tri::Unknown);
  CHECK_TRI(eval("0x", d), Tri::Unknown);
  CHECK_TRI(eval("12abc", d), Tri::Unknown);
  CHECK_TRI(eval("'ab'", d), Tri::Unknown);
  CHECK_TRI(eval("\"str\"", d), Tri::Unknown);
}

TEST(pp_eval_malformed_expressions_are_unknown) {
  for (const char* bad :
       {"",        "   ",      "(",         ")",          "1 +",   "+",    "1 2",   "(1", "1)",  "1 ? 2",  "? 1 : 2",
        "defined", "defined(", "defined()", "defined(1)", "1 = 1", "1, 2", "1 $ 2", "@",  "A B", "1 ?: 2", "!"}) {
    const Tri t = eval(bad);
    if (t != Tri::Unknown) tftest::fail(__FILE__, __LINE__, std::string("malformed `") + bad + "` is " + name(t));
  }
  // very long and deeply nested input is bounded
  std::string deep(5000, '(');
  deep += "1";
  deep += std::string(5000, ')');
  CHECK_TRI(eval(deep), Tri::Unknown);
  std::string many = "1";
  for (int i = 0; i < 20000; ++i) many += "+1";
  CHECK_TRI(eval(many), Tri::Unknown);
  std::string nots(101, '!');
  CHECK_TRI(eval(nots + "1"), Tri::False);
}

TEST(pp_eval_value_of_integer_expressions) {
  const auto none = [](std::string_view) { return MacroState::unknown(); };
  CHECK_EQ(evalValue("(1 + 2)", none).value, 3);
  CHECK(evalValue("(1 + 2)", none).kind == MacroState::Kind::Int);
  CHECK(evalValue("FOO", none).kind == MacroState::Kind::Unknown);
  CHECK_EQ(evalValue("-5", none).value, -5);
  CHECK(evalValue("1u", none).isUnsigned);
}

TEST(pp_merge_states) {
  const auto one = MacroState::integer(1), two = MacroState::integer(2);
  CHECK(mergeStates(one, one) == one);
  CHECK(mergeStates(one, two) == MacroState::defined());
  CHECK(mergeStates(one, MacroState::defined()) == MacroState::defined());
  CHECK(mergeStates(MacroState::undefined(), MacroState::undefined()) == MacroState::undefined());
  CHECK(mergeStates(MacroState::undefined(), one) == MacroState::unknown());
  CHECK(mergeStates(MacroState::unknown(), one) == MacroState::unknown());
}

// ---------------------------------------------------------------------------------------------------------------------
// directives
// ---------------------------------------------------------------------------------------------------------------------

TEST(pp_directive_parsing) {
  using K = Directive::Kind;
  CHECK(parseDirective("#if X > 1").kind == K::If);
  CHECK_EQ(parseDirective("#if X > 1").arg, "X > 1");
  CHECK_EQ(parseDirective("  #  ifdef   FOO  // note").arg, "FOO");
  CHECK(parseDirective("#\tifndef FOO").kind == K::Ifndef);
  CHECK_EQ(parseDirective("#if A /* c */ && /* multi\nline */ B").arg, "A   &&   B");
  CHECK_EQ(parseDirective("#if A && \\\n  B").arg, "A &&   B");
  CHECK(parseDirective("#elif Y").kind == K::Elif);
  CHECK(parseDirective("#elifdef Y").kind == K::Elifdef);
  CHECK(parseDirective("#else // x").kind == K::Else);
  CHECK(parseDirective("#endif").kind == K::Endif);
  CHECK(parseDirective("#include <a>").kind == K::Include);
  CHECK(parseDirective("#pragma once").kind == K::Other);
  CHECK(parseDirective("#ifdefX").kind == K::Other);
  CHECK(parseDirective("#").kind == K::None);
  CHECK(parseDirective("not a directive").kind == K::None);
  CHECK_EQ(stripComments("a // b"), "a ");
  CHECK_EQ(stripComments("\"//x\" y"), "\"//x\" y");
  CHECK_EQ(stripComments("'/' /*x*/ 1"), "'/'   1");
  auto d = parseDefine("NAME(a, b) ((a) + (b))");
  REQUIRE(d.has_value());
  CHECK_EQ(d->name, "NAME");
  CHECK(d->functionLike);
  CHECK_EQ(d->body, "((a) + (b))");
  d = parseDefine("LEVEL (3)");  // a space before '(' makes it an object-like macro
  REQUIRE(d.has_value());
  CHECK(!d->functionLike);
  CHECK_EQ(d->body, "(3)");
  CHECK(!parseDefine("3abc").has_value());
  CHECK(isExpressionBody("3"));
  CHECK(isExpressionBody("(1 + 2)"));
  CHECK(isExpressionBody("0x1F"));
  CHECK(isExpressionBody("abc"));
  CHECK(isExpressionBody("FEATURE_A && !FEATURE_B"));
  CHECK(!isExpressionBody("a, b"));
  CHECK(!isExpressionBody("{ 1 }"));
  CHECK(!isExpressionBody("\"s\""));
  CHECK(!isExpressionBody(""));
}

// ---------------------------------------------------------------------------------------------------------------------
// the extractor's arms
// ---------------------------------------------------------------------------------------------------------------------

TEST(extract_records_condition_arms) {
  auto f = extract("/w/a.h",
                   "int top;\n"          // 0
                   "#if A > 1\n"         // 1
                   "int in_if;\n"        // 2
                   "#  ifdef B\n"        // 3
                   "int nested;\n"       // 4
                   "#endif\n"            // 5
                   "#elif defined(C)\n"  // 6
                   "int in_elif;\n"      // 7
                   "#else\n"             // 8
                   "int in_else;\n"      // 9
                   "#endif\n"            // 10
                   "int after;\n");      // 11
  REQUIRE(f->arms.size() == 4);
  CHECK(f->arms[0].kind == CondArm::If);
  CHECK_EQ(f->arms[0].expr, "A > 1");
  CHECK(f->arms[1].kind == CondArm::Ifdef);
  CHECK_EQ(f->arms[1].expr, "B");
  CHECK_EQ(f->arms[1].parent, 1u);
  CHECK(f->arms[2].kind == CondArm::Elif);
  CHECK_EQ(f->arms[2].prev, 1u);
  CHECK_EQ(f->arms[2].parent, 0u);
  CHECK(f->arms[3].kind == CondArm::Else);
  CHECK_EQ(f->arms[3].prev, 3u);
  auto arm = [&](const char* n) -> uint32_t {
    size_t i = 0;
    return symbol(*f, n, &i) ? f->armOf(i) : 9999u;
  };
  CHECK_EQ(arm("top"), 0u);
  CHECK_EQ(arm("in_if"), 1u);
  CHECK_EQ(arm("nested"), 2u);
  CHECK_EQ(arm("in_elif"), 3u);
  CHECK_EQ(arm("in_else"), 4u);
  CHECK_EQ(arm("after"), 0u);
}

TEST(extract_include_guard_is_not_a_condition) {
  auto f = extract("/w/g.h", "#ifndef G_H\n#define G_H\nint guarded;\n#ifdef X\nint inner;\n#endif\n#endif\n");
  size_t i = 0;
  REQUIRE(symbol(*f, "guarded", &i) != nullptr);
  CHECK_EQ(f->armOf(i), 0u);
  REQUIRE(symbol(*f, "inner", &i) != nullptr);
  CHECK(f->armOf(i) != 0);
  CHECK_EQ(f->arms.size(), 1u);
  CHECK_EQ(f->arms[0].parent, 0u);
  // the other spellings of a guard
  auto g = extract("/w/g2.h", "#if !defined(G2_H)\n#define G2_H 1\nint a;\n#endif\n");
  CHECK(g->arms.empty());
  auto h = extract("/w/g3.h", "#ifndef G3_H\n#define OTHER\nint b;\n#endif\n");
  CHECK_EQ(h->arms.size(), 1u);  // not a guard: the next directive defines another name
}

TEST(extract_macros_are_recorded_with_integer_bodies) {
  auto f = extract("/w/m.h",
                   "#define LEVEL 3\n#define EMPTY\n#define FN(x) ((x) + 1)\n#define NAME \"text\"\n"
                   "#define EXPR (LEVEL + 1)\n#undef LEVEL\n#ifdef Q\n#define IN_Q 1\n#endif\n");
  REQUIRE(f->macros.size() == 7);
  CHECK_EQ(f->macros[0].name, "LEVEL");
  REQUIRE(f->macros[0].body >= 0);
  CHECK_EQ(f->bodies[static_cast<size_t>(f->macros[0].body)], "3");
  CHECK(f->macros[1].body < 0);
  CHECK(f->macros[2].functionLike && f->macros[2].body < 0);
  CHECK(f->macros[3].body < 0);
  CHECK(f->macros[5].undef);
  CHECK_EQ(f->macros[6].name, "IN_Q");
  CHECK_EQ(f->macros[6].arm, 1u);
}

TEST(extract_conditionals_are_bounded_and_survive_nonsense) {
  // unbalanced: #endif without #if, #else without #if, never closed
  auto f = extract("/w/u.h", "#endif\n#else\n#elif X\nint a;\n#if Y\nint b;\n");
  size_t i = 0;
  REQUIRE(symbol(*f, "a", &i) != nullptr);
  CHECK_EQ(f->armOf(i), 0u);
  REQUIRE(symbol(*f, "b", &i) != nullptr);
  CHECK(f->armOf(i) != 0);
  // 100000 nested #if: no unbounded recursion later (armActive follows parents)
  std::string deep;
  for (int k = 0; k < 100000; ++k) deep += "#if 1\n";
  deep += "int deep;\n";
  auto d = extract("/w/deep.h", deep);
  CHECK(d->arms.size() <= 600);
  size_t di = 0;
  REQUIRE(symbol(*d, "deep", &di) != nullptr);
  TuEnv::Input in;
  in.files = {d};
  const TuEnv env(std::move(in));
  Evaluator ev(env);
  CHECK(ev.armActive(*d, d->armOf(di)) == Tri::True);
  // a long #elif chain is linear
  std::string chain = "#if 0\nint c0;\n";
  for (int k = 1; k < 20000; ++k)
    chain += "#elif " + std::to_string(k == 19999 ? 1 : 0) + "\nint c" + std::to_string(k) + ";\n";
  chain += "#endif\n";
  auto c = extract("/w/chain.h", chain);
  TuEnv::Input in2;
  in2.files = {c};
  const TuEnv env2(std::move(in2));
  Evaluator ev2(env2);
  size_t first = 0, last = 0;
  REQUIRE(symbol(*c, "c0", &first) != nullptr);
  REQUIRE(symbol(*c, "c19999", &last) != nullptr);
  CHECK(ev2.armActive(*c, c->armOf(first)) == Tri::False);
  CHECK(ev2.armActive(*c, c->armOf(last)) == Tri::True);
}

TEST(fastindex_reachability_distinguishes_angle_and_quoted_includes) {
  FastIndex idx;
  idx.updateFile("/w/src/a.cpp", "#include <dup.h>\n");
  idx.updateFile("/w/src/b.cpp", "#include \"dup.h\"\n");
  idx.updateFile("/w/src/dup.h", "int near_dup;\n");
  idx.updateFile("/w/inc1/dup.h", "int inc_dup;\n");
  const auto a = idx.fileSymbols("/w/src/a.cpp");
  REQUIRE(a != nullptr);
  REQUIRE(a->angled.size() == 1);
  CHECK(a->angled[0] == 1);
  CHECK(idx.fileSymbols("/w/src/b.cpp")->angled[0] == 0);
  // <dup.h> skips the includer's directory; "dup.h" starts there
  auto ra = idx.reachableFrom("/w/src/a.cpp", {}, {"/w/inc1"});
  REQUIRE(ra != nullptr);
  CHECK(ra->count(FastIndex::pathKey("/w/inc1/dup.h")) == 1);
  CHECK(ra->count(FastIndex::pathKey("/w/src/dup.h")) == 0);
  auto rb = idx.reachableFrom("/w/src/b.cpp", {}, {"/w/inc1"});
  REQUIRE(rb != nullptr);
  CHECK(rb->count(FastIndex::pathKey("/w/src/dup.h")) == 1);
  CHECK(rb->count(FastIndex::pathKey("/w/inc1/dup.h")) == 0);
  // -iquote directories are for the quoted form only
  idx.updateFile("/w/q/dup.h", "int q_dup;\n");
  auto rq = idx.reachableFrom("/w/src/a.cpp", {"/w/q"}, {"/w/inc1"});
  REQUIRE(rq != nullptr);
  CHECK(rq->count(FastIndex::pathKey("/w/q/dup.h")) == 0);
  // the store keeps the bracket kind
  const std::string blob = FastIndexStore::serialize(idx.exportFiles());
  auto back = FastIndexStore::deserialize(blob.data(), blob.size());
  REQUIRE(back.has_value());
  for (const auto& f : *back) {
    if (f->file == fs::path("/w/src/a.cpp")) CHECK(f->angled == std::vector<uint8_t>{1});
    if (f->file == fs::path("/w/src/b.cpp")) CHECK(f->angled == std::vector<uint8_t>{0});
  }
}

TEST(extract_without_conditionals_adds_nothing) {
  auto f = extract("/w/p.h", "int a;\nstruct S { int b; };\n#define X 1\n");
  CHECK(f->arms.empty());
  CHECK(f->symArm.empty());
  CHECK_EQ(f->macros.size(), 1u);
  // extractSymbols and extractFile agree on the symbols
  const auto plain = extractSymbols("int a;\nstruct S { int b; };\n#define X 1\n");
  CHECK_EQ(plain.size(), f->symbols.size());
}

// ---------------------------------------------------------------------------------------------------------------------
// the store
// ---------------------------------------------------------------------------------------------------------------------

namespace {
bool sameConditions(const FileSymbols& a, const FileSymbols& b) {
  if (a.arms.size() != b.arms.size() || a.symArm != b.symArm || a.bodies != b.bodies ||
      a.macros.size() != b.macros.size())
    return false;
  for (size_t i = 0; i < a.arms.size(); ++i) {
    const CondArm &x = a.arms[i], &y = b.arms[i];
    if (x.kind != y.kind || x.parent != y.parent || x.prev != y.prev || x.line != y.line || x.expr != y.expr)
      return false;
  }
  for (size_t i = 0; i < a.macros.size(); ++i) {
    const MacroDef &x = a.macros[i], &y = b.macros[i];
    if (x.name != y.name || x.line != y.line || x.arm != y.arm || x.body != y.body || x.undef != y.undef ||
        x.functionLike != y.functionLike)
      return false;
  }
  return true;
}
}  // namespace

TEST(fastindexstore_roundtrips_conditions) {
  FastIndex idx;
  idx.updateFile(
      "/w/cond.h",
      "#ifndef C_H\n#define C_H\n#define LEVEL 3\n#if LEVEL >= 3 && defined(X)\nint hi();\n#elif Y\nint mid();\n"
      "#else\nint lo();\n#endif\n#undef LEVEL\n#endif\n");
  idx.updateFile("/w/plain.h", "int plain();\n");
  const auto files = idx.exportFiles();
  const std::string blob = FastIndexStore::serialize(files);
  std::string err;
  auto back = FastIndexStore::deserialize(blob.data(), blob.size(), &err);
  REQUIRE(back.has_value());
  REQUIRE(back->size() == 2);
  for (const auto& b : *back) {
    const auto a = idx.fileSymbols(b->file);
    REQUIRE(a != nullptr);
    CHECK(sameConditions(*a, *b));
    CHECK_EQ(a->symbols.size(), b->symbols.size());
  }
  const auto cond = idx.fileSymbols("/w/cond.h");
  CHECK(!cond->arms.empty());
  CHECK(!cond->macros.empty());
  CHECK(!cond->symArm.empty());
  // the conditions fill the unused file record of an unconditional file with nothing
  FastIndex one;
  one.updateFile("/w/plain.h", "int plain();\n");
  CHECK(FastIndexStore::serialize(one.exportFiles()).size() < blob.size());
}

TEST(fastindexstore_rejects_inconsistent_conditions) {
  FastIndex idx;
  idx.updateFile("/w/a.h", "#if A\nint x;\n#elif B\nint y;\n#endif\n");
  std::string blob = FastIndexStore::serialize(idx.exportFiles());
  std::string err;
  REQUIRE(FastIndexStore::deserialize(blob.data(), blob.size(), &err).has_value());
  // Flip bytes everywhere (the payload hash is recomputed by hand below): every mutation either fails to load or
  // loads into acyclic, in-range tables that the evaluator survives.
  for (size_t k = 48; k < blob.size(); ++k) {
    std::string m = blob;
    m[k] = static_cast<char>(m[k] ^ 0x5A);
    // fix the checksum so the structural checks (not the hash) are what is tested
    uint64_t h = 0x9E3779B97F4A7C15ull ^ (m.size() - 48);
    auto mix = [&](uint64_t v) {
      h ^= v;
      h *= 0xFF51AFD7ED558CCDull;
      h ^= h >> 32;
    };
    const char* p = m.data() + 48;
    const size_t n = m.size() - 48;
    size_t i = 0;
    for (; i + 8 <= n; i += 8) {
      uint64_t v;
      std::memcpy(&v, p + i, 8);
      mix(v);
    }
    uint64_t tail = 0;
    if (i < n) std::memcpy(&tail, p + i, n - i);
    mix(tail);
    std::memcpy(&m[16], &h, 8);  // payloadHash sits at offset 16
    auto r = FastIndexStore::deserialize(m.data(), m.size(), &err);
    if (!r) continue;
    for (const auto& f : *r) {
      TuEnv::Input in;
      in.files = {f};
      const TuEnv env(std::move(in));
      Evaluator ev(env);
      for (uint32_t a = 0; a <= f->arms.size() + 1; ++a) (void)ev.armActive(*f, a);  // must not crash or loop
    }
  }
}

// ---------------------------------------------------------------------------------------------------------------------
// compile commands
// ---------------------------------------------------------------------------------------------------------------------

TEST(pp_command_macros_from_d_and_u_flags) {
  const Host linux{Host::Os::Linux, Host::Arch::X64};
  auto m = macrosOfCommand(command({"g++", "-DA", "-DB=3", "-D", "C=4", "-DD=", "-DE(x)=x", "-UF", "-DG=0x10",
                                    "-DH=\"s\"", "-DI=1+1", "-c", "/w/main.cpp"}),
                           linux);
  REQUIRE(m.known);
  CHECK(m.macros["A"] == MacroState::integer(1));
  CHECK(m.macros["B"] == MacroState::integer(3));
  CHECK(m.macros["C"] == MacroState::integer(4));
  CHECK(m.macros["D"] == MacroState::defined());
  CHECK(m.macros["E"] == MacroState::defined());
  CHECK(m.macros["F"] == MacroState::undefined());
  CHECK(m.macros["G"] == MacroState::integer(16));
  CHECK(m.macros["H"] == MacroState::defined());
  CHECK(m.macros["I"] == MacroState::integer(2));
  // order: a later -U undoes a -D and the other way round
  m = macrosOfCommand(command({"gcc", "-DX", "-UX", "-UY", "-DY=2", "-DZ=1", "-UZ"}), linux);
  CHECK(m.macros["X"] == MacroState::undefined());
  CHECK(m.macros["Y"] == MacroState::integer(2));
  CHECK(m.macros["Z"] == MacroState::undefined());
  // cl / clang-cl spellings; a path that starts with /D is not a define
  m = macrosOfCommand(command({"clang-cl", "/DA", "/D", "B=2", "-DC", "/UD", "/Developer/x.cpp", "/I/Dir"}), linux);
  CHECK(m.macros["A"] == MacroState::integer(1));
  CHECK(m.macros["B"] == MacroState::integer(2));
  CHECK(m.macros["C"] == MacroState::integer(1));
  CHECK(m.macros["D"] == MacroState::undefined());
  CHECK(m.macros.count("eveloper/x.cpp") == 0);
  // a response file hides flags
  m = macrosOfCommand(command({"g++", "@args.rsp", "-DA"}), linux);
  CHECK(!m.complete);
  // forced includes
  m = macrosOfCommand(command({"clang-cl", "/FI/w/pre.h", "/FI", "/w/pre2.h"}), linux);
  REQUIRE(m.forcedIncludes.size() == 2);
  CHECK_EQ(m.forcedIncludes[0], "/w/pre.h");
  m = macrosOfCommand(command({"g++", "-include", "/w/pre.h"}), linux);
  REQUIRE(m.forcedIncludes.size() == 1);
  // collectFlagNames sees the same names
  std::unordered_set<std::string> names;
  collectFlagNames(command({"clang-cl", "/DA", "/D", "B=2", "/UC", "/Developer/x.cpp"}), names);
  CHECK(names.count("A") && names.count("B") && names.count("C") && names.size() == 3);
}

TEST(pp_command_predefined_macros_only_where_certain) {
  const Host linux{Host::Os::Linux, Host::Arch::X64};
  auto st = [](CommandMacros& m, const char* n) {
    auto it = m.macros.find(n);
    return it == m.macros.end() ? MacroState::unknown() : it->second;
  };
  auto cl = macrosOfCommand(command({"cl.exe", "/std:c++17", "/DFOO"}), linux);
  CHECK(st(cl, "_MSC_VER").kind == MacroState::Kind::Defined);
  CHECK(st(cl, "_WIN32") == MacroState::integer(1));  // cl.exe is a Windows compiler whatever the host
  CHECK(st(cl, "__clang__") == MacroState::undefined());
  CHECK(st(cl, "__linux__") == MacroState::undefined());
  CHECK(st(cl, "_WIN64").kind == MacroState::Kind::Unknown);  // x86 or x64 is not said
  auto cl64 = macrosOfCommand(command({"clang-cl", "-m64", "--target=x86_64-pc-windows-msvc"}), linux);
  CHECK(st(cl64, "_WIN32") == MacroState::integer(1));
  CHECK(st(cl64, "_WIN64") == MacroState::integer(1));
  CHECK(st(cl64, "_M_X64") == MacroState::integer(1));
  CHECK(st(cl64, "__clang__") == MacroState::integer(1));
  auto cl32 = macrosOfCommand(command({"clang-cl", "--target=i686-pc-windows-msvc"}), linux);
  CHECK(st(cl32, "_WIN64") == MacroState::undefined());
  CHECK(st(cl32, "_M_IX86") == MacroState::integer(1));
  // clang-cl on a Linux host without a target is not a Windows target
  auto host = macrosOfCommand(command({"clang-cl", "/std:c++20"}), linux);
  CHECK(st(host, "_WIN32") == MacroState::undefined());
  CHECK(st(host, "_MSC_VER").kind == MacroState::Kind::Defined);
  // a flag wins over a predefined macro
  auto forced = macrosOfCommand(command({"clang-cl", "/D_WIN32", "/D_WIN64", "-m64"}), linux);
  CHECK(st(forced, "_WIN32") == MacroState::integer(1));
  CHECK(st(forced, "_WIN64") == MacroState::integer(1));
  // gcc / clang on Linux
  auto gcc = macrosOfCommand(command({"g++", "-std=c++17"}), linux);
  CHECK(st(gcc, "__linux__") == MacroState::integer(1));
  CHECK(st(gcc, "__unix__") == MacroState::integer(1));
  CHECK(st(gcc, "__x86_64__") == MacroState::integer(1));
  CHECK(st(gcc, "__GNUC__").kind == MacroState::Kind::Defined);
  CHECK(st(gcc, "__clang__") == MacroState::undefined());
  CHECK(st(gcc, "_WIN32") == MacroState::undefined());
  CHECK(st(gcc, "_MSC_VER") == MacroState::undefined());
  CHECK(st(gcc, "__cplusplus") == MacroState::integer(201703));
  auto clang = macrosOfCommand(command({"/usr/bin/clang++-17", "-std=gnu++2b"}), linux);
  CHECK(st(clang, "__clang__") == MacroState::integer(1));
  CHECK(st(clang, "__cplusplus") == MacroState::integer(202302));
  auto nostd = macrosOfCommand(command({"clang++"}), linux);
  CHECK(st(nostd, "__cplusplus").kind == MacroState::Kind::Defined);
  // an explicit target beats the host
  auto cross = macrosOfCommand(command({"clang++", "--target=x86_64-pc-windows-gnu"}), linux);
  CHECK(st(cross, "_WIN32") == MacroState::integer(1));
  CHECK(st(cross, "__linux__") == MacroState::undefined());
  // a triple-prefixed cross compiler for something unknown tells nothing about the OS
  auto arm = macrosOfCommand(command({"arm-none-eabi-gcc"}), linux);
  CHECK(st(arm, "__linux__").kind == MacroState::Kind::Unknown);
  CHECK(st(arm, "_WIN32").kind == MacroState::Kind::Unknown);
  // C
  auto c = macrosOfCommand(command({"gcc", "-c", "x.c"}, "/w/x.c"), linux);
  CHECK(st(c, "__cplusplus") == MacroState::undefined());
  auto asC = macrosOfCommand(command({"g++", "-x", "c"}), linux);
  CHECK(st(asC, "__cplusplus") == MacroState::undefined());
  // an unknown driver predefines nothing
  auto odd = macrosOfCommand(command({"/opt/weird/cc1"}), linux);
  CHECK(odd.macros.empty());
}

// ---------------------------------------------------------------------------------------------------------------------
// the translation-unit evaluator
// ---------------------------------------------------------------------------------------------------------------------

TEST(pp_tu_flags_decide_branches) {
  auto h = extract("/w/shared.h",
                   "#if defined(LEVEL_9)\nint nine();\n#elif defined(LEVEL_3)\nint three();\n#elif defined(LEVEL_1)\n"
                   "int one();\n#else\nint none();\n#endif\n");
  const auto cmd = command({"clang-cl", "/DLEVEL_3"});
  CHECK_TRI(activeIn(&cmd, {h}, *h, "nine"), Tri::False);
  CHECK_TRI(activeIn(&cmd, {h}, *h, "three"), Tri::True);
  CHECK_TRI(activeIn(&cmd, {h}, *h, "one"), Tri::False);
  CHECK_TRI(activeIn(&cmd, {h}, *h, "none"), Tri::False);
  const auto none = command({"clang-cl"});
  CHECK_TRI(activeIn(&none, {h}, *h, "none"), Tri::True);  // nothing sets a LEVEL_n: the #else
  // no compile command: nothing is known about the flags
  CHECK_TRI(activeIn(nullptr, {h}, *h, "nine"), Tri::Unknown);
  CHECK_TRI(activeIn(nullptr, {h}, *h, "none"), Tri::Unknown);
}

TEST(pp_tu_value_comparisons_and_chains) {
  auto h =
      extract("/w/v.h",
              "#if VERSION >= 5\nint v5();\n#elif VERSION >= 3 && defined(MODE_A)\nint v3a();\n#elif defined(MODE_B)\n"
              "int v3b();\n#else\nint vold();\n#endif\n");
  auto run = [&](std::vector<std::string> flags, const std::string& sym) {
    std::vector<std::string> a{"gcc"};
    for (auto& f : flags) a.push_back(f);
    const auto cmd = command(a);
    return activeIn(&cmd, {h}, *h, sym);
  };
  CHECK_TRI(run({"-DVERSION=6"}, "v5"), Tri::True);
  CHECK_TRI(run({"-DVERSION=6"}, "v3a"), Tri::False);
  CHECK_TRI(run({"-DVERSION=4", "-DMODE_A"}, "v5"), Tri::False);
  CHECK_TRI(run({"-DVERSION=4", "-DMODE_A"}, "v3a"), Tri::True);
  CHECK_TRI(run({"-DVERSION=4", "-DMODE_A"}, "v3b"), Tri::False);
  CHECK_TRI(run({"-DVERSION=4"}, "v3b"), Tri::False);
  CHECK_TRI(run({"-DVERSION=4"}, "vold"), Tri::True);
  CHECK_TRI(run({"-DVERSION=4", "-DMODE_B"}, "v3b"), Tri::True);
  CHECK_TRI(run({"-DVERSION=4", "-DMODE_B"}, "vold"), Tri::False);
}

TEST(pp_tu_absence_is_false_only_for_names_the_project_mentions) {
  auto h = extract("/w/a.h",
                   "#ifdef FEATURE_X\nint with_x();\n#endif\n#ifdef _SOME_RESERVED\nint with_reserved();\n#endif\n"
                   "#ifdef HAVE_UNSEEN\nint with_unseen();\n#endif\n#ifdef linux\nint with_linux();\n#endif\n"
                   "#ifdef INT_MAX\nint with_intmax();\n#endif\n#ifdef IN_HEADER\nint with_header();\n#endif\n");
  const auto cmd = command({"g++"});
  // a name the project mentions (a -D / -U of some command) and nothing defines in this unit: Undefined
  CHECK_TRI(activeIn(&cmd, {h}, *h, "with_x"), Tri::False);
  // every other absent name is Unknown, whatever its spelling: a system header or a generated file may define it
  CHECK_TRI(activeIn(&cmd, {h}, *h, "with_reserved"), Tri::Unknown);
  CHECK_TRI(activeIn(&cmd, {h}, *h, "with_unseen"), Tri::Unknown);
  CHECK_TRI(activeIn(&cmd, {h}, *h, "with_linux"), Tri::Unknown);
  CHECK_TRI(activeIn(&cmd, {h}, *h, "with_intmax"), Tri::Unknown);
  // the project's other configuration set it (-DENABLE_PROFILER in Debug only): the Release unit has it Undefined
  auto prof = extract("/w/prof.h", "#ifdef ENABLE_PROFILER\nint profiler_on();\n#else\nint profiler_off();\n#endif\n");
  const std::set<std::string> otherConfig{"ENABLE_PROFILER"};
  const auto release = command({"g++", "-O2"});
  const auto debug = command({"g++", "-DENABLE_PROFILER"});
  CHECK_TRI(activeIn(&release, {prof}, *prof, "profiler_on", otherConfig), Tri::False);
  CHECK_TRI(activeIn(&release, {prof}, *prof, "profiler_off", otherConfig), Tri::True);
  CHECK_TRI(activeIn(&debug, {prof}, *prof, "profiler_on", otherConfig), Tri::True);
  CHECK_TRI(activeIn(&release, {prof}, *prof, "profiler_on", {}), Tri::Unknown);  // nobody mentions it
  // a name that only some indexed project header #defines is a project macro (definedInWorkspace)
  {
    TuEnv::Input in;
    in.command = macrosOfCommand(cmd, Host{Host::Os::Linux, Host::Arch::X64});
    in.files = {h};
    in.definedInWorkspace = [](const std::string& n) { return n == "IN_HEADER"; };
    const TuEnv env(std::move(in));
    Evaluator ev(env);
    size_t i = 0;
    REQUIRE(symbol(*h, "with_header", &i) != nullptr);
    CHECK_TRI(ev.armActive(*h, h->armOf(i)), Tri::False);
    REQUIRE(symbol(*h, "with_unseen", &i) != nullptr);
    CHECK_TRI(ev.armActive(*h, h->armOf(i)), Tri::Unknown);
  }
  // the motivating case: <sys/socket.h> defines MSG_NOSIGNAL, the project never mentions it
  auto sock = extract("/w/sock.cpp",
                      "#include <sys/socket.h>\n#ifdef MSG_NOSIGNAL\nstatic int sendFlags() { return MSG_NOSIGNAL; }\n"
                      "#else\nstatic int sendFlags() { return 0; }\n#endif\n");
  size_t first = 0, second = 0;
  for (size_t k = 0; k < sock->symbols.size(); ++k)
    if (sock->symbols[k].name == "sendFlags") (first ? second : first) = k;
  TuEnv::Input in;
  in.command = macrosOfCommand(cmd, Host{Host::Os::Linux, Host::Arch::X64});
  in.files = {sock};
  const TuEnv env(std::move(in));
  Evaluator ev(env);
  CHECK_TRI(ev.armActive(*sock, sock->armOf(first)), Tri::Unknown);
  CHECK_TRI(ev.armActive(*sock, sock->armOf(second)), Tri::Unknown);
  // a response file may carry any -D
  const auto rsp = command({"g++", "@flags.rsp"});
  CHECK_TRI(activeIn(&rsp, {h}, *h, "with_x"), Tri::Unknown);
  // the target's predefined macros are known
  auto p = extract("/w/p.h",
                   "#ifdef __linux__\nint on_linux();\n#endif\n#ifdef _WIN32\nint on_win();\n#endif\n"
                   "#if __cplusplus >= 201703L\nint cxx17();\n#endif\n#ifdef __cplusplus\nint cxx();\n#endif\n");
  const auto g17 = command({"g++", "-std=c++17"});
  CHECK_TRI(activeIn(&g17, {p}, *p, "on_linux"), Tri::True);
  CHECK_TRI(activeIn(&g17, {p}, *p, "on_win"), Tri::False);
  CHECK_TRI(activeIn(&g17, {p}, *p, "cxx17"), Tri::True);
  const auto g11 = command({"g++", "-std=c++11"});
  CHECK_TRI(activeIn(&g11, {p}, *p, "cxx17"), Tri::False);
  CHECK_TRI(activeIn(&g11, {p}, *p, "cxx"), Tri::True);
  const auto cc = command({"gcc"}, "/w/x.c");
  CHECK_TRI(activeIn(&cc, {p}, *p, "cxx"), Tri::False);
}

TEST(pp_tu_defines_in_the_files_of_the_unit) {
  // same file: a #define above decides, an #undef undoes it, a conditional #define may or may not have run
  auto f = extract("/w/s.cpp",
                   "#define LEVEL 2\n#if LEVEL == 2\nint two();\n#endif\n#if LEVEL > 2\nint big();\n#endif\n"
                   "#undef LEVEL\n#ifdef LEVEL\nint still();\n#endif\n"
                   "#ifdef FEATURE_Q\n#define COND 1\n#endif\n#ifdef COND\nint cond();\n#endif\n"
                   "#ifdef LATER\nint before_define();\n#endif\n#define LATER\n");
  const auto cmd = command({"g++"});
  CHECK_TRI(activeIn(&cmd, {f}, *f, "two"), Tri::True);
  CHECK_TRI(activeIn(&cmd, {f}, *f, "big"), Tri::False);
  CHECK_TRI(activeIn(&cmd, {f}, *f, "still"), Tri::False);
  // FEATURE_Q is not defined anywhere, so the #define never ran and COND is not defined
  CHECK_TRI(activeIn(&cmd, {f}, *f, "cond"), Tri::False);
  CHECK_TRI(activeIn(&cmd, {f}, *f, "before_define"), Tri::False);  // the #define comes later in the same file
  // with FEATURE_Q on the command line the conditional define did run
  const auto q = command({"g++", "-DFEATURE_Q"});
  CHECK_TRI(activeIn(&q, {f}, *f, "cond"), Tri::True);
  // ... and when it is unknown whether it ran, so is COND
  CHECK_TRI(activeIn(nullptr, {f}, *f, "cond"), Tri::Unknown);

  // another file's #define: the order is not known, so a macro that is also undefined at the start is Unknown
  auto cfg = extract("/w/cfg.h", "#define MODE 2\n");
  auto use = extract("/w/use.h", "#if MODE == 2\nint mode2();\n#endif\n#ifdef MODE\nint has_mode();\n#endif\n");
  CHECK_TRI(activeIn(&cmd, {use, cfg}, *use, "mode2"), Tri::Unknown);
  // when the command line has the same value, every path agrees
  const auto m2 = command({"g++", "-DMODE=2"});
  CHECK_TRI(activeIn(&m2, {use, cfg}, *use, "mode2"), Tri::True);
  const auto m3 = command({"g++", "-DMODE=3"});
  CHECK_TRI(activeIn(&m3, {use, cfg}, *use, "mode2"), Tri::Unknown);  // 3 or 2: both are possible
  CHECK_TRI(activeIn(&m3, {use, cfg}, *use, "has_mode"), Tri::True);
  // the unit's files are the only ones that count: cfg.h is not part of a unit that does not include it
  CHECK_TRI(activeIn(&cmd, {use}, *use, "mode2"), Tri::False);
}

TEST(pp_tu_include_guard_else_never_runs) {
  auto g = extract("/w/g.h", "#ifndef G_H\n#define G_H\nint in_guard();\n#else\nint in_else();\n#endif\n");
  const auto cmd = command({"g++"});
  CHECK_TRI(activeIn(&cmd, {g}, *g, "in_guard"), Tri::True);
  CHECK_TRI(activeIn(&cmd, {g}, *g, "in_else"), Tri::False);
}

TEST(pp_tu_cyclic_definitions_terminate) {
  auto f =
      extract("/w/c.h", "#if B\n#define A 1\n#endif\n#if A\n#define B 1\n#endif\n#if A && B\nint both();\n#endif\n");
  auto g = extract("/w/d.h", "#if A\n#define B 1\n#endif\n");
  const auto cmd = command({"g++"});
  (void)activeIn(&cmd, {f, g}, *f, "both");  // must return (the answer is not interesting)
  CHECK(true);
}

TEST(pp_tu_macro_bodies_are_expanded_where_they_are_used) {
  auto f =
      extract("/w/b.h",
              "#define A B\n#define B 2\n#if A == 2\nint a_is_2();\n#endif\n#if A == 3\nint a_is_3();\n#endif\n"
              "#define C (B * 5)\n#if C == 10\nint c_is_10();\n#endif\n#undef B\n#if C == 10\nint c_late();\n#endif\n"
              "#define SELF SELF\n#if SELF\nint selfref();\n#endif\n#define FN(x) x\n#if FN\nint fnbare();\n#endif\n");
  const auto cmd = command({"g++"});
  CHECK_TRI(activeIn(&cmd, {f}, *f, "a_is_2"), Tri::True);
  CHECK_TRI(activeIn(&cmd, {f}, *f, "a_is_3"), Tri::False);
  CHECK_TRI(activeIn(&cmd, {f}, *f, "c_is_10"), Tri::True);
  CHECK_TRI(activeIn(&cmd, {f}, *f, "c_late"), Tri::False);  // B is gone when C is expanded: (0 * 5)
  CHECK_TRI(activeIn(&cmd, {f}, *f, "selfref"), Tri::Unknown);
  CHECK_TRI(activeIn(&cmd, {f}, *f, "fnbare"), Tri::Unknown);
}

TEST(pp_command_flag_bodies_debug_runtime_and_forced_includes) {
  const Host linux{Host::Os::Linux, Host::Arch::X64};
  auto m = macrosOfCommand(command({"g++", "-DCHAIN=VAL", "-DVAL=2", "-DLOOP=LOOP2", "-DLOOP2=LOOP"}), linux);
  CHECK(m.macros["CHAIN"] == MacroState::integer(2));
  CHECK(m.macros["LOOP"] == MacroState::defined());
  auto dbg = macrosOfCommand(command({"clang-cl", "/MDd"}), linux);
  CHECK(dbg.macros["_DEBUG"] == MacroState::integer(1));
  auto rel = macrosOfCommand(command({"clang-cl", "/MD"}), linux);
  CHECK(rel.macros["_DEBUG"] == MacroState::undefined());
  auto def = macrosOfCommand(command({"clang-cl", "/MD", "/D_DEBUG"}), linux);
  CHECK(def.macros["_DEBUG"] == MacroState::integer(1));
  // a forced include runs before the main file: its #define decides, even though the project never defines it otherwise
  auto forced = extract("/w/force.h", "#define FORCED_MACRO 1\n");
  auto main = extract("/w/main.cpp", "#ifdef FORCED_MACRO\nint yes();\n#else\nint no();\n#endif\n");
  TuEnv::Input in;
  in.command = macrosOfCommand(command({"g++", "-include", "/w/force.h"}), linux);
  in.prelude = {forced};
  in.files = {main};
  in.definedInWorkspace = [](const std::string& n) { return n == "FORCED_MACRO"; };
  const TuEnv env(std::move(in));
  Evaluator ev(env);
  size_t i = 0;
  REQUIRE(symbol(*main, "yes", &i) != nullptr);
  CHECK_TRI(ev.armActive(*main, main->armOf(i)), Tri::True);
  REQUIRE(symbol(*main, "no", &i) != nullptr);
  CHECK_TRI(ev.armActive(*main, main->armOf(i)), Tri::False);
  // without the forced include the project macro is simply not defined
  TuEnv::Input in2;
  in2.command = macrosOfCommand(command({"g++"}), linux);
  in2.files = {main};
  in2.definedInWorkspace = [](const std::string& n) { return n == "FORCED_MACRO"; };
  const TuEnv env2(std::move(in2));
  Evaluator ev2(env2);
  REQUIRE(symbol(*main, "yes", &i) != nullptr);
  CHECK_TRI(ev2.armActive(*main, main->armOf(i)), Tri::False);
}

TEST(pp_tu_has_include_uses_the_callback) {
  auto f = extract("/w/h.h",
                   "#if __has_include(\"x.h\")\nint yes();\n#endif\n#if __has_include(<y>) || "
                   "__has_include(\"z.h\")\nint either();\n#endif\n"
                   "#if defined(__has_include) && __has_include(\"x.h\")\nint guarded();\n#endif\n");
  auto run = [&](bool withHook, const char* sym) {
    TuEnv::Input in;
    in.command = macrosOfCommand(command({"g++"}), Host{Host::Os::Linux, Host::Arch::X64});
    in.files = {f};
    if (withHook)
      in.hasInclude = [](const std::string& sp, bool angled, const fs::path&) {
        if (sp == "x.h") return Tri::True;
        if (angled) return Tri::Unknown;
        return Tri::False;
      };
    const TuEnv env(std::move(in));
    Evaluator ev(env);
    size_t i = 0;
    if (!symbol(*f, sym, &i)) return Tri::Unknown;
    return ev.armActive(*f, f->armOf(i));
  };
  CHECK_TRI(run(true, "yes"), Tri::True);
  CHECK_TRI(run(false, "yes"), Tri::Unknown);
  CHECK_TRI(run(true, "either"), Tri::Unknown);  // <y> is unknown, "z.h" is absent
  CHECK_TRI(run(true, "guarded"), Tri::True);    // g++ defines __has_include
}

// ---------------------------------------------------------------------------------------------------------------------
// the engine
// ---------------------------------------------------------------------------------------------------------------------

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
  // compile_commands.json: file -> extra arguments
  void commands(const std::map<std::string, std::vector<std::string>>& m, const std::string& driver = "clang++") const {
    std::string j = "[\n";
    bool first = true;
    for (const auto& [file, args] : m) {
      if (!first) j += ",\n";
      first = false;
      j += "{\"directory\": \"" + root.generic_string() + "\", \"file\": \"" + (root / file).generic_string() +
           "\", \"arguments\": [\"" + driver + "\"";
      for (const auto& a : args) j += ", \"" + a + "\"";
      j += ", \"-c\", \"" + (root / file).generic_string() + "\"]}";
    }
    j += "\n]\n";
    write("compile_commands.json", j);
  }
};

// Fast-tier definition answers at the first `needle` of `file` ("|" marks the caret), as "file:line,...", sorted.
std::string fastDef(Editor& ed, const Project& p, const char* file, int line, const std::string& spec) {
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
  ed.gotoDefinition(cb);
  ed.waitFor([&] { return done; }, 5000);
  return out;
}

Editor::Options noClangd() {
  Editor::Options o;
  o.semantic.useClangd = false;
  return o;
}

}  // namespace

TEST(engine_fast_answers_follow_the_defines_of_the_compile_command) {
  Project p("tenfold_pp_engine");
  p.write("shared.h",
          "#pragma once\n"                       // 0
          "#if defined(LEVEL_9)\n"               // 1
          "inline int level() { return 9; }\n"   // 2
          "#elif defined(LEVEL_3)\n"             // 3
          "inline int level() { return 3; }\n"   // 4
          "#else\n"                              // 5
          "inline int level() { return 0; }\n"   // 6
          "#endif\n"                             // 7
          "#if VERSION >= 5\n"                   // 8
          "inline int ver() { return 5; }\n"     // 9
          "#else\n"                              // 10
          "inline int ver() { return 1; }\n"     // 11
          "#endif\n"                             // 12
          "#if __has_include(<nowhere.h>)\n"     // 13
          "inline int unk() { return 1; }\n"     // 14
          "#else\n"                              // 15
          "inline int unk() { return 2; }\n"     // 16
          "#endif\n"                             // 17
          "#if 0\n"                              // 18
          "inline int dead() { return 1; }\n"    // 19
          "#endif\n"                             // 20
          "#ifdef NEVER_DEFINED_ANYWHERE\n"      // 21
          "inline int only() { return 1; }\n"    // 22
          "#endif\n"                             // 23
          "#if __has_include(\"a.cpp\")\n"       // 24
          "inline int hq_yes() { return 1; }\n"  // 25
          "#else\n"                              // 26
          "inline int hq_yes() { return 0; }\n"  // 27
          "#endif\n"                             // 28
          "#if __has_include(\"nowhere.h\")\n"   // 29
          "inline int hq_no() { return 1; }\n"   // 30
          "#else\n"                              // 31
          "inline int hq_no() { return 0; }\n"   // 32
          "#endif\n");                           // 33
  p.write("a.cpp",
          "#include \"shared.h\"\n"
          "int a = level() + ver() + unk() + dead() + only() + hq_yes() + hq_no();\n");
  p.write("b.cpp",
          "#include \"shared.h\"\n"
          "int b = level() + ver();\n");
  p.commands({{"a.cpp", {"-DLEVEL_3", "-DVERSION=7"}}, {"b.cpp", {"-DLEVEL_9", "-DVERSION=2"}}});
  Editor ed(p.root, noClangd());
  std::string err;
  REQUIRE(ed.start(&err));
  CHECK(ed.semantic().waitForFastIndex(10000));
  // a.cpp compiles shared.h with LEVEL_3 and VERSION 7, b.cpp with LEVEL_9 and VERSION 2
  CHECK_EQ(fastDef(ed, p, "a.cpp", 1, "= |level()"), "shared.h:4");
  CHECK_EQ(fastDef(ed, p, "b.cpp", 1, "= |level()"), "shared.h:2");
  CHECK_EQ(fastDef(ed, p, "a.cpp", 1, "|ver()"), "shared.h:9");
  CHECK_EQ(fastDef(ed, p, "b.cpp", 1, "|ver()"), "shared.h:11");
  // __has_include cannot be evaluated: both branches stay
  CHECK_EQ(fastDef(ed, p, "a.cpp", 1, "|unk()"), "shared.h:14,shared.h:16");
  // #if 0 is dead, and so is never answered when something else is there; but never narrowed to nothing
  CHECK_EQ(fastDef(ed, p, "a.cpp", 1, "|dead()"), "shared.h:19");
  CHECK_EQ(fastDef(ed, p, "a.cpp", 1, "|only()"), "shared.h:22");
  // __has_include looks at the file system: a header next to the includer is there, a quoted project header found
  // nowhere is not
  CHECK_EQ(fastDef(ed, p, "a.cpp", 1, "|hq_yes()"), "shared.h:25");
  CHECK_EQ(fastDef(ed, p, "a.cpp", 1, "|hq_no()"), "shared.h:32");
}

TEST(engine_fast_answers_use_the_translation_unit_of_an_included_header) {
  Project p("tenfold_pp_header");
  p.write("mode.h",
          "#pragma once\n"                                 // 0
          "#ifdef FAST\n"                                  // 1
          "inline int mode() { return 1; }\n"              // 2
          "#else\n"                                        // 3
          "inline int mode() { return 0; }\n"              // 4
          "#endif\n"                                       // 5
          "inline int twice() { return 2 * mode(); }\n");  // 6
  p.write("main.cpp", "#include \"mode.h\"\nint m = mode();\n");
  p.commands({{"main.cpp", {"-DFAST"}}});
  Editor ed(p.root, noClangd());
  std::string err;
  REQUIRE(ed.start(&err));
  CHECK(ed.semantic().waitForFastIndex(10000));
  // the caret in the header: mode.h has no compile command of its own; main.cpp includes it, with -DFAST
  CHECK_EQ(fastDef(ed, p, "mode.h", 6, "|mode()"), "mode.h:2");
  CHECK_EQ(fastDef(ed, p, "main.cpp", 1, "|mode()"), "mode.h:2");
}

TEST(engine_fast_answers_judge_a_definition_in_its_own_translation_unit) {
  Project p("tenfold_pp_own_tu");
  p.write("api.h", "#pragma once\nint platform_name();\n");
  p.write("api_win.cpp", "#include \"api.h\"\n#ifdef _WIN32\nint platform_name() { return 1; }\n#endif\n");
  p.write("api_posix.cpp", "#include \"api.h\"\n#ifndef _WIN32\nint platform_name() { return 2; }\n#endif\n");
  p.write("main.cpp", "#include \"api.h\"\nint v = platform_name();\n");
  p.commands({{"main.cpp", {"-DX"}}, {"api_win.cpp", {"-DX"}}, {"api_posix.cpp", {"-DX"}}}, "g++");
  Editor ed(p.root, noClangd());
  std::string err;
  REQUIRE(ed.start(&err));
  CHECK(ed.semantic().waitForFastIndex(10000));
#if defined(_WIN32)
  CHECK_EQ(fastDef(ed, p, "main.cpp", 1, "|platform_name"), "api_win.cpp:2");
#elif defined(__linux__)
  // g++ on a Linux host: _WIN32 is certainly not defined, the Windows definition is not offered
  CHECK_EQ(fastDef(ed, p, "main.cpp", 1, "|platform_name"), "api_posix.cpp:2");
#endif
}

TEST(engine_fast_answers_without_a_compile_command_keep_every_branch) {
  Project p("tenfold_pp_nocmd");
  p.write("a.cpp", "#ifdef FEATURE\nint f() { return 1; }\n#else\nint f() { return 2; }\n#endif\nint use = f();\n");
  Editor ed(p.root, noClangd());
  std::string err;
  REQUIRE(ed.start(&err));
  CHECK(ed.semantic().waitForFastIndex(10000));
  // (the heuristic compile database has no -D: it is "a command", but the flags are a guess, not the build's)
  const std::string r = fastDef(ed, p, "a.cpp", 5, "= |f()");
  CHECK(r == "a.cpp:1,a.cpp:3" || r == "a.cpp:3");
}

TEST(engine_fast_conditions_follow_the_visual_studio_configuration) {
  Project p("tenfold_pp_vsconfig");
  p.write("app.sln",
          "\nMicrosoft Visual Studio Solution File, Format Version 12.00\n"
          "Project(\"{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}\") = \"app\", \"app.vcxproj\", "
          "\"{CCCCCCCC-0000-0000-0000-000000000001}\"\nEndProject\nGlobal\n"
          "\tGlobalSection(SolutionConfigurationPlatforms) = preSolution\n"
          "\t\tDebug|x64 = Debug|x64\n\t\tRelease|x64 = Release|x64\n\tEndGlobalSection\n"
          "\tGlobalSection(ProjectConfigurationPlatforms) = postSolution\n"
          "\t\t{CCCCCCCC-0000-0000-0000-000000000001}.Debug|x64.ActiveCfg = Debug|x64\n"
          "\t\t{CCCCCCCC-0000-0000-0000-000000000001}.Debug|x64.Build.0 = Debug|x64\n"
          "\t\t{CCCCCCCC-0000-0000-0000-000000000001}.Release|x64.ActiveCfg = Release|x64\n"
          "\t\t{CCCCCCCC-0000-0000-0000-000000000001}.Release|x64.Build.0 = Release|x64\n"
          "\tEndGlobalSection\nEndGlobal\n");
  p.write("app.vcxproj",
          "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
          "<Project DefaultTargets=\"Build\" xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">\n"
          "  <ItemGroup Label=\"ProjectConfigurations\">\n"
          "    <ProjectConfiguration Include=\"Debug|x64\"><Configuration>Debug</Configuration>"
          "<Platform>x64</Platform></ProjectConfiguration>\n"
          "    <ProjectConfiguration Include=\"Release|x64\"><Configuration>Release</Configuration>"
          "<Platform>x64</Platform></ProjectConfiguration>\n"
          "  </ItemGroup>\n"
          "  <Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.Default.props\" />\n"
          "  <Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.props\" />\n"
          "  <ItemDefinitionGroup Condition=\"'$(Configuration)'=='Release'\"><ClCompile>\n"
          "    <PreprocessorDefinitions>TURBO;%(PreprocessorDefinitions)</PreprocessorDefinitions>\n"
          "  </ClCompile></ItemDefinitionGroup>\n"
          "  <ItemGroup><ClCompile Include=\"main.cpp\" /></ItemGroup>\n"
          "  <Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.targets\" />\n"
          "</Project>\n");
  p.write("speed.h",
          "#pragma once\n"
          "#ifdef TURBO\n"
          "inline int speed() { return 2; }\n"
          "#else\n"
          "inline int speed() { return 1; }\n"
          "#endif\n");
  p.write("main.cpp", "#include \"speed.h\"\nint main() { return speed(); }\n");
  Editor ed(p.root, noClangd());
  std::string err;
  REQUIRE(ed.start(&err));
  CHECK(ed.semantic().waitForFastIndex(10000));
  auto switchTo = [&](const char* name) {
    bool applied = false;
    std::string e;
    if (!ed.selectVsConfiguration(name, &e, [&] { applied = true; })) return false;
    return ed.waitFor([&] { return applied; }, 60000);
  };
  REQUIRE(switchTo("Debug|x64"));
  // Debug has no TURBO anywhere in its database: nobody in this snapshot mentions the name, so it is Unknown
  CHECK_EQ(fastDef(ed, p, "main.cpp", 1, "return |speed()"), "speed.h:2,speed.h:4");
  REQUIRE(switchTo("Release|x64"));
  CHECK_EQ(fastDef(ed, p, "main.cpp", 1, "return |speed()"), "speed.h:2");
  REQUIRE(switchTo("Debug|x64"));
  CHECK_EQ(fastDef(ed, p, "main.cpp", 1, "return |speed()"), "speed.h:2,speed.h:4");
}

TEST(engine_fast_answers_follow_macro_definitions_of_the_caret_file) {
  Project p("tenfold_pp_macros");
  p.write("m.cpp",
          "#define LEVEL 1\n"   // 0
          "int a1 = LEVEL;\n"   // 1
          "#undef LEVEL\n"      // 2
          "#define LEVEL 2\n"   // 3
          "int a2 = LEVEL;\n"   // 4
          "#undef LEVEL\n"      // 5
          "int LEVEL = 3;\n"    // 6
          "int a3 = LEVEL;\n"   // 7
          "int late = 1;\n"     // 8
          "int b1 = late;\n"    // 9
          "#define late 99\n"   // 10
          "int b2 = late;\n"    // 11
          "#ifdef NEVER_SET\n"  // 12
          "#define COND 1\n"    // 13
          "#endif\n"            // 14
          "#define COND 2\n"    // 15
          "int c = COND;\n");   // 16
  p.commands({{"m.cpp", {"-DX"}}}, "g++");
  Editor ed(p.root, noClangd());
  std::string err;
  REQUIRE(ed.start(&err));
  CHECK(ed.semantic().waitForFastIndex(10000));
  CHECK_EQ(fastDef(ed, p, "m.cpp", 1, "= |LEVEL"), "m.cpp:0");
  CHECK_EQ(fastDef(ed, p, "m.cpp", 4, "= |LEVEL"), "m.cpp:3");
  CHECK_EQ(fastDef(ed, p, "m.cpp", 7, "= |LEVEL"), "m.cpp:6");  // the macro is gone: the variable
  CHECK_EQ(fastDef(ed, p, "m.cpp", 9, "= |late"), "m.cpp:8");
  CHECK_EQ(fastDef(ed, p, "m.cpp", 11, "= |late"), "m.cpp:10");  // after the #define the word is the macro
  // NEVER_SET is mentioned by nobody: whether the #define under it ran is not known, both stay
  CHECK_EQ(fastDef(ed, p, "m.cpp", 16, "= |COND"), "m.cpp:13,m.cpp:15");
}

TEST(engine_fast_answers_see_forced_includes_and_flag_values) {
  Project p("tenfold_pp_forced");
  p.write("force.h", "#pragma once\n#define FORCED_MACRO 1\n");
  p.write("use.cpp",
          "#ifdef FORCED_MACRO\n"            // 0
          "int branch() { return 1; }\n"     // 1
          "#else\n"                          // 2
          "int branch() { return 0; }\n"     // 3
          "#endif\n"                         // 4
          "#if CHAIN == 2\n"                 // 5
          "int chain() { return 2; }\n"      // 6
          "#else\n"                          // 7
          "int chain() { return 0; }\n"      // 8
          "#endif\n"                         // 9
          "int v = branch() + chain();\n");  // 10
  p.write(
      "plain.cpp",
      "#ifdef FORCED_MACRO\nint other() { return 1; }\n#else\nint other() { return 0; }\n#endif\nint w = other();\n");
  std::string j = "[\n";
  j += "{\"directory\": \"" + p.root.generic_string() + "\", \"file\": \"" + (p.root / "use.cpp").generic_string() +
       "\", \"arguments\": [\"g++\", \"-include\", \"" + (p.root / "force.h").generic_string() +
       "\", \"-DCHAIN=VAL\", \"-DVAL=2\", \"-c\", \"" + (p.root / "use.cpp").generic_string() + "\"]},\n";
  j += "{\"directory\": \"" + p.root.generic_string() + "\", \"file\": \"" + (p.root / "plain.cpp").generic_string() +
       "\", \"arguments\": [\"g++\", \"-c\", \"" + (p.root / "plain.cpp").generic_string() + "\"]}\n]\n";
  p.write("compile_commands.json", j);
  Editor ed(p.root, noClangd());
  std::string err;
  REQUIRE(ed.start(&err));
  CHECK(ed.semantic().waitForFastIndex(10000));
  CHECK_EQ(fastDef(ed, p, "use.cpp", 10, "= |branch()"), "use.cpp:1");
  CHECK_EQ(fastDef(ed, p, "use.cpp", 10, "+ |chain()"), "use.cpp:6");
  CHECK_EQ(fastDef(ed, p, "plain.cpp", 5, "= |other()"), "plain.cpp:3");  // no forced include in this unit
}

TEST(engine_fast_answers_keep_both_branches_of_a_system_header_macro) {
  Project p("tenfold_pp_strict");
  p.write("net.cpp",
          "#include <sys/socket.h>\n"                          // 0
          "#ifdef MSG_NOSIGNAL\n"                              // 1
          "static int sendFlags() { return MSG_NOSIGNAL; }\n"  // 2
          "#else\n"                                            // 3
          "static int sendFlags() { return 0; }\n"             // 4
          "#endif\n"                                           // 5
          "int use = sendFlags();\n"                           // 6
          "#ifdef ENABLE_PROFILER\n"                           // 7
          "int prof() { return 1; }\n"                         // 8
          "#else\n"                                            // 9
          "int prof() { return 0; }\n"                         // 10
          "#endif\n"                                           // 11
          "int up = prof();\n");                               // 12
  p.write("other.cpp", "int x;\n");
  // the build passes -DENABLE_PROFILER for another unit only: the project mentions the name, this unit lacks it
  p.commands({{"net.cpp", {"-O2"}}, {"other.cpp", {"-DENABLE_PROFILER"}}}, "g++");
  Editor ed(p.root, noClangd());
  std::string err;
  REQUIRE(ed.start(&err));
  CHECK(ed.semantic().waitForFastIndex(10000));
  CHECK_EQ(fastDef(ed, p, "net.cpp", 6, "= |sendFlags()"), "net.cpp:2,net.cpp:4");
  CHECK_EQ(fastDef(ed, p, "net.cpp", 12, "= |prof()"), "net.cpp:10");
}
