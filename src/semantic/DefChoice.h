#pragma once
// Text helpers behind the "choose the target of go to definition" answers (W10-CHOOSE): which spelling makes a type
// name a constructor call, what kind of alias a declaration line is, what an alias names, and how to compare a
// constructor's parameter types. Pure functions over one line of source; the engine (SemanticEngine::gotoDefinition)
// asks clangd for the facts and uses these only to find the positions to ask about and to label the answers.
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tf::defchoice {

// `line` with every /* comment */ and the // comment replaced by blanks of the same length, so columns stay valid and
// the other helpers need not know about comments.
std::string blankComments(std::string_view line);

// Column of the '(' or '{' that makes the word ending at byte column `wordEnd` of `line` a construction:
//   Widget(2)   Widget{2}   new Widget(2)   Widget w(2);   Widget w{2};   Widget<int> w(2);   : Base(0)
// -1 when the word is followed by anything else (`Widget w;`, `Widget* p`, `Widget::make()`, `Widget w = x`).
// Declarations such as `Widget make(int)` also give a column; the caller tells them apart by what clangd answers.
int constructPoint(std::string_view line, int wordEnd);

enum class AliasForm { None, Typedef, Using /* using X = Y; */, UsingDecl /* using ns::f; */ };
// What the declaration written on `line` is, judged by its first keywords (an optional `template <...>` is skipped).
// `using namespace` and `using enum` are None.
AliasForm aliasForm(std::string_view line);

// The type an alias names: `using Bar = ns::Foo<int>;` -> {name "Foo", qualifier "ns", col of "Foo"};
// `typedef Foo *Bar;` -> {"Foo"}. `nameCol` is the column of the alias name. Nothing for builtin types, function
// pointer / array typedefs, struct bodies and declarations that continue on another line.
struct AliasRhs {
  int col = -1;
  std::string name, qualifier;
};
std::optional<AliasRhs> aliasRhs(std::string_view line, int nameCol);

// `using Base::Base;` -> "Base"; `using ns::Base::Base;` -> "ns::Base"; empty when `line` is not such a declaration.
std::string usingQualifier(std::string_view line);

// Parameter types of the parameter list opening at `openParen`, names and default arguments removed, spacing
// normalised ("const char*", "int"). Empty vector for `()`; `ok` false when the list does not close on this line.
std::vector<std::string> paramTypes(std::string_view line, int openParen, bool* ok = nullptr);
// The same list from clang's printed function type, e.g. "'Derived' 'void (int, const char *) noexcept(false)'" (the
// arcana of a CXXConstructExpr in clangd's textDocument/ast) -> {"int", "const char*"}. `ok` false without one.
std::vector<std::string> signatureParamTypes(std::string_view arcana, bool* ok = nullptr);

// "Widget(int, const char*)" for the constructor / function whose name starts at `nameCol` of `line`; just the name
// when no parameter list follows on this line.
std::string signatureText(std::string_view line, int nameCol);

}  // namespace tf::defchoice
