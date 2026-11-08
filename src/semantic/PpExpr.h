#pragma once
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace tf::pp {

// Preprocessor-conditional logic of the fast tier (docs/ARCHITECTURE.md, "Preprocessor conditionals in the fast tier").
// Pure text and arithmetic: no files, no index. Everything is three-valued: a condition is True, False, or Unknown
// when something it depends on cannot be told (a function-like macro, `__has_include`, a macro whose definition may
// come from a header nobody read). Callers drop code only when its condition is certainly False.

enum class Tri : uint8_t { False, True, Unknown };

constexpr Tri triOf(bool b) { return b ? Tri::True : Tri::False; }
constexpr Tri triNot(Tri a) { return a == Tri::Unknown ? Tri::Unknown : (a == Tri::True ? Tri::False : Tri::True); }
constexpr Tri triAnd(Tri a, Tri b) {
  if (a == Tri::False || b == Tri::False) return Tri::False;
  return (a == Tri::True && b == Tri::True) ? Tri::True : Tri::Unknown;
}
constexpr Tri triOr(Tri a, Tri b) {
  if (a == Tri::True || b == Tri::True) return Tri::True;
  return (a == Tri::False && b == Tri::False) ? Tri::False : Tri::Unknown;
}

// What is known about one macro name at one point of the translation unit.
struct MacroState {
  enum class Kind : uint8_t {
    Unknown,    // may or may not be defined
    Undefined,  // certainly not defined (`defined(X)` is false, `X` in an #if is 0)
    Defined,    // certainly defined, value not known (empty, function-like, not an integer)
    Int         // certainly defined as the integer constant `value`
  };
  Kind kind = Kind::Unknown;
  int64_t value = 0;
  bool isUnsigned = false;
  static MacroState unknown() { return {}; }
  static MacroState undefined() { return {Kind::Undefined, 0, false}; }
  static MacroState defined() { return {Kind::Defined, 0, false}; }
  static MacroState integer(int64_t v, bool u = false) { return {Kind::Int, v, u}; }
  friend bool operator==(const MacroState&, const MacroState&) = default;
};

// The state that covers both `a` and `b` (the macro is one or the other): equal states stay, two defined states
// become Defined, anything else Unknown.
MacroState mergeStates(const MacroState& a, const MacroState& b);

// Macro state by name; called for every identifier of an expression that is not `defined`.
using MacroLookup = std::function<MacroState(std::string_view name)>;

// `__has_include("spelling")` / `__has_include(<spelling>)`: whether the header can be found. Optional; without it
// (or when it answers Unknown) the operator is Unknown.
using HasIncludeFn = std::function<Tri(std::string_view spelling, bool angled)>;

// Value of an `#if` / `#elif` expression: `defined X`, `defined(X)`, integer and character constants, identifiers
// (macros with an integer value; an undefined identifier is 0), `! ~ + -`, `* / %`, `+ -`, `<< >>`, `< <= > >=`,
// `== !=`, `&`, `^`, `|`, `&&`, `||`, `?:` and parentheses, with the C rules for signed / unsigned operands. `and` /
// `or` / `not` / `bitand` / `bitor` / `xor` / `compl` / `not_eq` are the operators they spell. `true` / `false` are 1
// / 0. Function-like macro invocations, `__has_include(...)` and the other `__has_*` operators, division by zero, an
// expression that does not parse, or one longer than a few thousand tokens evaluate to Unknown. `&&`, `||` and `?:`
// are decided by one operand when that is enough (`0 && unknown` is False).
Tri evalCondition(std::string_view expr, const MacroLookup& lookup, const HasIncludeFn* hasInclude = nullptr);

// The integer value of an expression when every part of it is known (the result's kind is Int), else Unknown.
MacroState evalValue(std::string_view expr, const MacroLookup& lookup, const HasIncludeFn* hasInclude = nullptr);

// One preprocessor directive line (a Preprocessor token's text, '#' included) with comments removed and
// backslash-newline joined.
struct Directive {
  enum class Kind : uint8_t {
    None,  // not a directive / no name
    If,
    Ifdef,
    Ifndef,
    Elif,
    Elifdef,
    Elifndef,
    Else,
    Endif,
    Define,
    Undef,
    Include,
    Other
  };
  Kind kind = Kind::None;
  std::string arg;  // everything after the directive name, trimmed
};
Directive parseDirective(std::string_view tokenText);

// `#define NAME(params) body` / `#define NAME body`: the name, whether '(' follows it directly, and the body (trimmed).
struct DefineParts {
  std::string name;
  bool functionLike = false;
  std::string body;
};
std::optional<DefineParts> parseDefine(std::string_view arg);

// The name of `#ifdef X` / `#undef X` / `#elifdef X` (the first identifier of `arg`), or "".
std::string firstIdentifier(std::string_view arg);

// `a // c` and `/* c */` removed, backslash-newline joined, string and character literals respected.
std::string stripComments(std::string_view text);

}  // namespace tf::pp
