#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "semantic/FastScope.h"

namespace tf {

// FAST-OVL: overloads of equal arity and operator tokens in the fast tier (docs/ARCHITECTURE.md, "Overloads and
// operators in the fast tier"). Everything here is "narrow when the evidence is clear, else leave the candidates
// alone": a type the lexer cannot know is Unknown and never rules a candidate out, a rule that would leave nothing is
// not applied. The types of a candidate's parameters are not in the index (it keeps counts): they are read from the
// declaration in the file's text on demand (FileSigs), and the types of the arguments are inferred from what is
// written (literals, declared types of locals / parameters / fields, constructions, casts, `std::move`).

// The declared type of one parameter (or the inferred type of one argument).
struct OvType {
  enum Kind : uint8_t {
    Unknown,  // not known: matches anything, rules nothing out
    Prim,     // a builtin type: `name` is canonical ("int", "unsigned long", "long double", "bool", "void"...)
    Named,    // a class, struct, union, enum or alias, as written ("geo::Vec2"); `targ` = first template argument
    NullPtr,  // std::nullptr_t (an argument only)
    Generic   // a template parameter of the function (deduced; matches by value, reference and pointer patterns)
  };
  enum class Cat : uint8_t { Unknown, LValue, PRValue, XValue };  // value category (arguments)
  Kind kind = Unknown;
  std::string name;
  std::string targ;
  uint8_t ptr = 0;         // pointer depth
  bool constBase = false;  // `const` at the level before the first `*`
  bool constPtr = false;   // the outermost pointer itself is const (`T *const`)
  uint8_t ref = 0;         // parameter: 1 `&`, 2 `&&`
  Cat cat = Cat::Unknown;  // argument
  bool zeroLit = false;    // argument: the literal 0 (a null pointer constant)
  std::string raw;         // parameter: normalised spelling, equal for the same declared type
  bool targExact = false;  // Named: `targ` is the whole first template argument
};

struct OvParam {
  OvType ty;
  bool hasDefault = false;
};

// The parsed parameter list of a function declaration.
struct OvSignature {
  bool ok = false;  // false: not parsed (unusual syntax); the candidate is then never ruled out
  std::vector<OvParam> params;
  bool cVariadic = false;  // a trailing `...`
  bool pack = false;       // a parameter pack
  bool isConst = false;    // member function qualifiers
  uint8_t refQual = 0;     // 1 `&`, 2 `&&`
  bool isStatic = false;
  bool isDefaulted = false;  // `= default`
  bool constrained = false;  // SFINAE / `requires` / `decltype` in the declaration: the template may not be viable
  bool isTemplate = false;   // preceded by `template <...>`
  std::vector<std::string> tparams;       // the function's own template parameters
  std::vector<std::string> classTParams;  // out-of-line member of a class template: the class header's parameters
};

// Parses the function declared with the name token at (line, col) of `text`; `name` is the index's name for it (used
// to check the position: a position that does not start with the name yields ok = false).
OvSignature parseSignature(std::string_view text, int line, int col, std::string_view name, bool isC = false);

// The text of a file with its signatures parsed on demand and cached.
class FileSigs {
 public:
  FileSigs(std::string text, bool isC);
  std::shared_ptr<const OvSignature> at(int line, int col, std::string_view name) const;
  // Does the class whose name token is at (line, col) declare a conversion function (`operator T()`) in its body?
  // Null / unknown answers are "yes" (the conservative one) in the caller.
  bool classHasConversionOperator(int line, int col) const;
  // The template parameters of the `template <...>` header before the declaration whose name token is at (line, col).
  std::vector<std::string> templateParamsBefore(int line, int col) const;
  size_t size() const { return text_.size(); }

 private:
  std::string text_;
  bool isC_ = false;
  std::vector<size_t> lineStarts_;
  mutable std::mutex mu_;
  mutable std::map<std::pair<int, int>, std::shared_ptr<const OvSignature>> sigs_;
  mutable std::map<std::pair<int, int>, bool> conv_;
};

// FileSigs of files, cached until the file's text version changes (same stamps as ScopeCache).
class SigCache {
 public:
  std::shared_ptr<const FileSigs> get(const std::string& key, uint64_t a, uint64_t b,
                                      const std::function<std::shared_ptr<const FileSigs>()>& build);
  void clear();

 private:
  struct Entry {
    uint64_t a = 0, b = 0;
    std::shared_ptr<const FileSigs> sigs;
    uint64_t tick = 0;
  };
  static constexpr size_t kMaxEntries = 24;
  static constexpr size_t kMaxBytes = 6u << 20;
  std::mutex mu_;
  std::unordered_map<std::string, Entry> map_;
  uint64_t tick_ = 0;
  size_t bytes_ = 0;
};

// Narrows `matches` (function / method candidates of the call at the caret, already narrowed by scope and argument
// count) by the types of the call's arguments (and of the receiver's cv / ref qualification for a member call).
// Candidates that are certainly not viable, or that another candidate certainly beats, are dropped; whatever the
// evidence does not decide stays. Never returns fewer than one candidate when given one.
std::vector<FastIndex::Match> narrowByOverload(const ScopeEnv& env, const CaretInfo& ci,
                                               std::vector<FastIndex::Match> matches);

// The namespaces argument-dependent lookup adds for the call at the caret: those of the classes (and their bases) and
// enumerations of the arguments whose types are known. Empty when no call follows the word or no argument type is
// known.
std::set<std::string> adlScopesOfCall(const ScopeEnv& env, const CaretInfo& ci);

// An operator token at a caret: `a + b`, `-a`, `x[i]`, `f(x)` on an object, `p->m`, `a++`, `os << v`, a literal with a
// user-defined suffix. Returns the operator functions the operands' types select (as index matches, not yet shaped
// into definitions / declarations), empty when the caret is not on an operator token this can resolve or when no
// user-declared operator is a candidate. `ci.text` is the caret's line and `ci.wordStart` the byte column of the
// caret; `ci.at`, `ci.map`, `ci.lineAt` and `ci.file` as for narrowByScope.
std::vector<FastIndex::Match> operatorTokenMatches(const ScopeEnv& env, const CaretInfo& ci);

}  // namespace tf
