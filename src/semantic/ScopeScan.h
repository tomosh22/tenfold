#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace tf {

// A lexical scope map of one C/C++ file: which namespaces, classes, functions and blocks enclose a position, which
// parameters / locals / fields are declared in them with the type as written, and which using-directives,
// using-declarations and namespace aliases they hold. Built from the lexer only (no preprocessing, no types resolved,
// no templates instantiated); every guess it cannot make it leaves out, and the consumer (FastScope) treats a missing
// fact as "unknown", never as "not there". Used by the fast tier to narrow its by-name lookup (docs/ARCHITECTURE.md,
// "Scope-aware lookup in the fast tier").

struct ScanPos {
  int line = 0;
  int col = 0;
  friend bool operator==(const ScanPos&, const ScanPos&) = default;
  friend bool operator<(const ScanPos& a, const ScanPos& b) {
    return a.line != b.line ? a.line < b.line : a.col < b.col;
  }
  friend bool operator<=(const ScanPos& a, const ScanPos& b) { return !(b < a); }
};

// A parameter, a local variable or a field.
struct ScanVar {
  std::string name;
  std::string type;  // as written without cv, `*`, `&` and template arguments ("geo::Vec2"); "" when unknown (`auto`)
  std::string targ;  // the first identifier chain inside the type's template arguments ("unique_ptr<Foo>": "Foo")
  bool pointer = false;
  ScanPos at;  // the name token
};

struct ScanUsing {
  std::string ns;    // `using namespace ns;` (name empty) or the qualifier of `using ns::name;`
  std::string name;  // using-declaration: the introduced name
  ScanPos at;
};

struct ScanAlias {
  std::string alias;
  std::string target;
};

struct ScanRegion {
  enum Kind { Namespace, Class, Function, Block };
  Kind kind = Block;
  // Namespace: "a::b" as written ("(anonymous)" for `namespace {`); Class: the class name as written ("A::B" for an
  // out-of-line nested definition, "(anonymous)"); Function: the declarator name as written ("Vec2::length_sq",
  // "operator+", "~Foo"); "" otherwise.
  std::string name;
  std::vector<std::string> bases;  // Class: the base-specifier names as written, without template arguments
  bool transparent = false;        // `extern "C" {`: not a scope
  bool closed = false;
  // Containment: (begin, end) exclusive. A function's begin is its parameter list's '(' so the parameters are inside.
  ScanPos begin, end;
  ScanPos nameAt;  // Class / Function: the name token (for Class the key to find the class symbol of the index)
  int parent = -1;
  std::vector<ScanVar> vars;  // Function / Block: parameters and locals; Class: fields
  std::vector<ScanUsing> usings;
  std::vector<ScanAlias> aliases;
};

struct ScopeMap {
  std::vector<ScanRegion> regions;  // [0] is the file's global scope; the rest in order of their opening brace
  // The innermost region that contains `p` (0 = global scope).
  int regionAt(ScanPos p) const;
};

// `isC`: the file is C (class / namespace / ... are identifiers). `maxBytes`: texts larger than this are not scanned
// (a map with only the global scope is returned: the consumer falls back to the by-name answer).
ScopeMap scanScopes(std::string_view text, bool isC = false, size_t maxBytes = 8u << 20);

}  // namespace tf
