#pragma once
// FAST-DECL: names declared through macros (docs/ARCHITECTURE.md, "Names declared through macros").
//
// The extractor records the function-like macros a file defines (MacroTemplate) and the calls at declaration position
// (MacroCall). expandCall() expands one call with the template its resolver names, bounded in depth, size and number of
// nested expansions, and runs the declaration extractor over the result: `DECLARE_CLASS(Gadget)` declares `Gadget`.
// Symbols are positioned where clangd lands: the argument token for a name spelled in the call, the macro name at the
// call for a name that only exists in the expansion (pasted, or written in the macro's body).
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "semantic/CppLexer.h"
#include "semantic/FastIndex.h"

namespace tf {

// The declarations extractor over a token list that ends with (or is given) an End token: `scope` is the qualified
// enclosing scope, `innermost` its kind. Tokens must keep their text alive for the call. Defined in FastIndex.cpp.
std::vector<Symbol> extractFromTokens(std::vector<Token> tokens, const std::string& scope, SymbolKind innermost);

namespace mx {

constexpr size_t kMaxTemplatesPerFile = 1u << 14;
constexpr size_t kMaxBodyBytes = 2048;
constexpr size_t kMaxParams = 24;
constexpr size_t kMaxObjectBodyBytes = 256;
// `argc` of a macro name that is not followed by '(' (an object-like macro use).
constexpr size_t kNoArgs = ~size_t{0};

// Fills out.templates from the `#define` directives among `toks`: every function-like macro, and the object-like ones
// whose body holds an identifier (bounded).
void scanTemplates(const std::vector<Token>& toks, FileSymbols& out);

struct Limits {
  int maxDepth = 8;         // nested macro expansions (a macro call inside a body or an argument)
  int maxExpansions = 64;   // macro expansions in one call
  size_t maxTokens = 4096;  // tokens of any intermediate or final expansion
  // Optional work budget shared by the calls of one file (tokens produced by substitutions, rescans and argument
  // lexing): a call that would exceed what is left declares nothing, so a file of thousands of heavy calls costs a
  // bounded amount.
  size_t* budget = nullptr;
};

// The template to expand `name` called with `argc` arguments and the file that defines it, or null (not a known macro /
// ambiguous / wrong count).
struct Resolved {
  const MacroTemplate* tpl = nullptr;
  const std::filesystem::path* file = nullptr;
};
using Resolver = std::function<Resolved(const std::string& name, size_t argc)>;

// A declared symbol and the file it is spelled in: empty = the calling file, else the file of the macro definition (a
// name written in a macro's body is declared where the body is, as clangd answers).
struct Expanded {
  Symbol sym;
  std::filesystem::path file;
};

// The symbols the call declares, in source order, without duplicates; empty when the macro is unknown, the expansion
// exceeds a limit or does not balance its braces and parentheses (a macro that opens a scope another one closes).
std::vector<Expanded> expandCall(const MacroCall& call, const Resolver& resolve, const Limits& limits = {});

// The expansion's tokens as text (for tests and diagnostics): spelling of each token joined by single spaces. Empty
// when the call does not expand.
std::string expandCallText(const MacroCall& call, const Resolver& resolve, const Limits& limits = {});

}  // namespace mx
}  // namespace tf
