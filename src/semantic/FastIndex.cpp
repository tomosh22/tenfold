#include "semantic/FastIndex.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_set>

#ifdef __unix__
#include <sys/stat.h>
#endif

#include "semantic/CppLexer.h"

namespace tf {

const char* symbolKindName(SymbolKind k) {
  switch (k) {
    case SymbolKind::Namespace: return "namespace";
    case SymbolKind::Class: return "class";
    case SymbolKind::Struct: return "struct";
    case SymbolKind::Union: return "union";
    case SymbolKind::Enum: return "enum";
    case SymbolKind::Enumerator: return "enumerator";
    case SymbolKind::Function: return "function";
    case SymbolKind::Method: return "method";
    case SymbolKind::Constructor: return "constructor";
    case SymbolKind::Variable: return "variable";
    case SymbolKind::Field: return "field";
    case SymbolKind::Typedef: return "typedef";
    case SymbolKind::Macro: return "macro";
    case SymbolKind::Concept: return "concept";
    default: return "unknown";
  }
}

// ---------------------------------------------------------------------------
// Symbol extraction
// ---------------------------------------------------------------------------
namespace {

class Extractor {
 public:
  explicit Extractor(std::string_view src, std::vector<std::string>* includes)
      : toks_(CppLexer(src).all()), includes_(includes) {}

  std::vector<Symbol> run() {
    while (!at(Tok::End)) step();
    return std::move(out_);
  }

 private:
  struct Scope {
    std::string name;
    SymbolKind kind;
    int depth;
  };

  const Token& tok(size_t k = 0) const {
    size_t idx = std::min(i_ + k, toks_.size() - 1);
    return toks_[idx];
  }
  bool at(Tok k) const { return tok().kind == k; }
  bool atP(std::string_view p) const { return tok().kind == Tok::Punct && tok().text == p; }
  bool atKw(std::string_view k) const { return tok().kind == Tok::Keyword && tok().text == k; }
  void advance(size_t n = 1) { i_ = std::min(i_ + n, toks_.size() - 1); }

  std::string scopeName() const {
    std::string s;
    for (auto& sc : scopes_) {
      if (!s.empty()) s += "::";
      s += sc.name;
    }
    return s;
  }
  bool inClass() const {
    return !scopes_.empty() && (scopes_.back().kind == SymbolKind::Class || scopes_.back().kind == SymbolKind::Struct ||
                                scopes_.back().kind == SymbolKind::Union);
  }

  void emit(const Token& nameTok, std::string name, SymbolKind kind, bool definition, std::string scope = "\x01") {
    Symbol s;
    s.name = std::move(name);
    s.scope = scope == "\x01" ? scopeName() : std::move(scope);
    s.kind = kind;
    s.line = nameTok.line;
    s.col = nameTok.col;
    s.definition = definition;
    s.isTemplate = pendingTemplate_;
    out_.push_back(std::move(s));
  }

  // Skip a balanced group starting at the current open token.
  void skipBalanced(std::string_view open, std::string_view close) {
    if (!atP(open)) return;
    int d = 0;
    while (!at(Tok::End)) {
      if (atP(open)) ++d;
      else if (atP(close)) {
        if (--d == 0) {
          advance();
          return;
        }
      }
      advance();
    }
  }

  // Skip a template argument/parameter list: template <...>. Handles nesting
  // and >> tokens; stops sensibly on ; or { at depth 0.
  void skipAngles() {
    if (!atP("<")) return;
    int d = 0;
    while (!at(Tok::End)) {
      if (atP("<")) ++d;
      else if (atP(">")) {
        if (--d <= 0) {
          advance();
          return;
        }
      } else if (atP(">>")) {
        d -= 2;
        if (d <= 0) {
          advance();
          return;
        }
      } else if (atP("(")) {
        skipBalanced("(", ")");
        continue;
      } else if (atP("{") || atP(";")) return;
      advance();
    }
  }

  // Advance to just after the terminating ';' of the current statement,
  // balancing braces/parens/brackets. Stops after a top-level '}' too when
  // `stopAtBrace` (used for things like "X x{};" vs "struct X {};").
  void skipStatement() {
    int paren = 0, brace = 0;
    while (!at(Tok::End)) {
      if (atP("(")) ++paren;
      else if (atP(")")) --paren;
      else if (atP("{")) ++brace;
      else if (atP("}")) {
        if (--brace < 0) return;
      } else if (atP(";") && paren <= 0 && brace <= 0) {
        advance();
        return;
      }
      advance();
    }
  }

  void skipAttributes() {
    while (true) {
      if (atP("[") && tok(1).kind == Tok::Punct && tok(1).text == "[") {
        // [[...]]
        advance(2);
        int d = 2;
        while (!at(Tok::End) && d > 0) {
          if (atP("[")) ++d;
          else if (atP("]")) --d;
          advance();
        }
        continue;
      }
      if ((atKw("__declspec") || atKw("alignas") || atKw("__attribute__"))) {
        advance();
        skipBalanced("(", ")");
        continue;
      }
      if (at(Tok::Identifier) &&
          (tok().text.ends_with("_API") || tok().text.ends_with("_EXPORT") || tok().text.ends_with("_DLL")) &&
          (tok(1).kind == Tok::Keyword || tok(1).kind == Tok::Identifier) && !tok(1).is("(")) {
        // Likely an export macro like MYLIB_API before a declaration: ignore it.
        advance();
        continue;
      }
      break;
    }
  }

  void openScope(std::string name, SymbolKind kind) {
    ++depth_;
    scopes_.push_back({std::move(name), kind, depth_});
  }

  void step() {
    const Token& t = tok();
    switch (t.kind) {
      case Tok::Preprocessor: handleDirective(); return;
      case Tok::Punct:
        if (t.is("{")) {
          ++depth_;
          advance();
          return;
        }
        if (t.is("}")) {
          --depth_;
          while (!scopes_.empty() && scopes_.back().depth > depth_) scopes_.pop_back();
          advance();
          return;
        }
        if (t.is(";")) {
          advance();
          pendingTemplate_ = false;
          return;
        }
        if (t.is("[")) {
          skipAttributes();
          if (atP("[")) advance();
          return;
        }
        advance();
        return;
      case Tok::Keyword:
        if (t.is("template")) {
          advance();
          skipAngles();
          pendingTemplate_ = true;
          return;
        }
        if (t.is("namespace")) {
          parseNamespace();
          return;
        }
        if (t.is("class") || t.is("struct") || t.is("union")) {
          parseClassLike();
          return;
        }
        if (t.is("enum")) {
          parseEnum();
          return;
        }
        if (t.is("typedef")) {
          parseTypedef();
          return;
        }
        if (t.is("using")) {
          parseUsing();
          return;
        }
        if (t.is("concept")) {
          advance();
          if (at(Tok::Identifier)) emit(tok(), std::string(tok().text), SymbolKind::Concept, true);
          skipStatement();
          pendingTemplate_ = false;
          return;
        }
        if (t.is("extern") && tok(1).kind == Tok::String && tok(2).is("{")) {
          advance(2);
          return;
        }  // extern "C" {
        if (t.is("friend")) {
          skipStatement();
          pendingTemplate_ = false;
          return;
        }
        if (t.is("static_assert") || t.is("asm") || t.is("__asm") || t.is("__pragma") || t.is("_Pragma")) {
          skipStatement();
          return;
        }
        if ((t.is("public") || t.is("private") || t.is("protected")) && tok(1).is(":")) {
          advance(2);
          return;
        }
        if (t.is("__declspec") || t.is("alignas") || t.is("__attribute__")) {
          skipAttributes();
          return;
        }
        parseDeclaration();
        return;
      case Tok::Identifier: parseDeclaration(); return;
      default: advance(); return;
    }
  }

  void handleDirective() {
    std::string_view text = tok().text;
    advance();
    size_t p = 1;
    while (p < text.size() && (text[p] == ' ' || text[p] == '\t')) ++p;
    if (includes_ && text.substr(p, 7) == "include") {
      p += 7;
      while (p < text.size() && (text[p] == ' ' || text[p] == '\t')) ++p;
      if (p < text.size() && (text[p] == '"' || text[p] == '<')) {
        char close = text[p] == '"' ? '"' : '>';
        size_t e = text.find(close, p + 1);
        if (e != std::string_view::npos) includes_->emplace_back(text.substr(p + 1, e - p - 1));
      }
      return;
    }
    if (text.substr(p, 6) == "define") {
      p += 6;
      while (p < text.size() && (text[p] == ' ' || text[p] == '\t')) ++p;
      size_t e = p;
      while (e < text.size() && (std::isalnum(static_cast<unsigned char>(text[e])) || text[e] == '_')) ++e;
      if (e > p) {
        Token nameTok = toks_[i_ - 1];
        nameTok.col += static_cast<int>(p);
        emit(nameTok, std::string(text.substr(p, e - p)), SymbolKind::Macro, true, "");
      }
    }
  }

  // Reads a (possibly qualified, possibly template-id) name: A::B<T>::C
  // Returns the name as written and the token of its last component.
  bool readQualifiedName(std::string& name, Token& lastTok) {
    name.clear();
    bool any = false;
    if (atP("::")) {
      name += "::";
      advance();
    }
    while (true) {
      if (at(Tok::Identifier) || (atKw("operator"))) {
        if (atKw("operator")) {
          lastTok = tok();
          name += "operator";
          advance();
          // operator symbol(s) or conversion type
          if (atP("(") && tok(1).is(")")) {
            name += "()";
            advance(2);
          } else if (atP("[") && tok(1).is("]")) {
            name += "[]";
            advance(2);
          } else if (at(Tok::Punct)) {
            while (at(Tok::Punct) && !atP("(") && !atP("<")) {
              name += std::string(tok().text);
              advance();
            }
          } else {
            while (!at(Tok::End) && !atP("(")) {
              name += std::string(tok().text);
              advance();
            }
          }
          return true;
        }
        lastTok = tok();
        name += std::string(tok().text);
        any = true;
        advance();
        if (atP("<")) {
          // template-id: include raw text (approximation: use "<...>" marker)
          size_t save = i_;
          skipAngles();
          if (i_ > save) name += "<>";
        }
        if (atP("::")) {
          name += "::";
          advance();
          continue;
        }
        return true;
      }
      if (atP("~") && tok(1).kind == Tok::Identifier) {
        advance();
        lastTok = tok();
        name += "~" + std::string(tok().text);
        advance();
        return true;
      }
      return any;
    }
  }

  void parseNamespace() {
    advance();  // namespace
    while (atKw("inline")) advance();
    skipAttributes();
    if (atP("{")) {  // anonymous
      openScope("(anonymous)", SymbolKind::Namespace);
      advance();
      return;
    }
    std::vector<std::pair<std::string, Token>> parts;
    while (at(Tok::Identifier)) {
      parts.emplace_back(std::string(tok().text), tok());
      advance();
      if (atP("::")) {
        advance();
        continue;
      }
      break;
    }
    if (atP("=")) {  // namespace alias
      if (!parts.empty()) emit(parts[0].second, parts[0].first, SymbolKind::Namespace, false);
      skipStatement();
      return;
    }
    if (atP("{")) {
      // "namespace a::b::c {" opens several scopes with a single brace.
      bool first = true;
      for (auto& [n, tk] : parts) {
        emit(tk, n, SymbolKind::Namespace, true);
        if (first) openScope(n, SymbolKind::Namespace);
        else scopes_.push_back({n, SymbolKind::Namespace, depth_});
        first = false;
      }
      if (parts.empty()) openScope("(anonymous)", SymbolKind::Namespace);
      advance();
      return;
    }
    skipStatement();
  }

  void parseClassLike() {
    SymbolKind kind = atKw("class") ? SymbolKind::Class : atKw("struct") ? SymbolKind::Struct : SymbolKind::Union;
    advance();
    skipAttributes();
    std::string name;
    Token nameTok{};
    bool hasName = readQualifiedName(name, nameTok);
    while (atKw("final") || (at(Tok::Identifier) && tok().text == "final")) advance();
    skipAttributes();
    if (atP(";")) {
      pendingTemplate_ = false;
      advance();
      return;
    }  // forward declaration
    if (atP(":")) {  // base clause
      advance();
      while (!at(Tok::End) && !atP("{") && !atP(";")) {
        if (atP("<")) {
          skipAngles();
          continue;
        }
        advance();
      }
    }
    if (atP("{")) {
      std::string scopeNameForMembers = hasName ? name : "(anonymous)";
      // Strip qualification for the member scope: "A::B<>" -> keep as written minus "<>".
      std::string clean = scopeNameForMembers;
      size_t pos;
      while ((pos = clean.find("<>")) != std::string::npos) clean.erase(pos, 2);
      if (hasName) {
        // Qualified names (out-of-line nested definitions) keep their written scope.
        std::string unq = clean, sc = scopeName();
        size_t q = clean.rfind("::");
        if (q != std::string::npos) {
          unq = clean.substr(q + 2);
          sc = sc.empty() ? clean.substr(0, q) : sc + "::" + clean.substr(0, q);
        }
        emit(nameTok, unq, kind, true, sc);
      }
      openScope(clean, kind);
      pendingTemplate_ = false;
      advance();
      return;
    }
    // Something else: "struct X x;" (elaborated type specifier in a declaration).
    pendingTemplate_ = false;
    parseDeclaration();
  }

  void parseEnum() {
    advance();  // enum
    bool scoped = false;
    if (atKw("class") || atKw("struct")) {
      scoped = true;
      advance();
    }
    skipAttributes();
    std::string name;
    Token nameTok{};
    bool hasName = readQualifiedName(name, nameTok);
    if (atP(":")) {  // underlying type
      advance();
      while (!at(Tok::End) && !atP("{") && !atP(";")) advance();
    }
    if (atP(";")) {
      advance();
      return;
    }  // opaque declaration
    if (!atP("{")) {
      parseDeclaration();
      return;
    }
    if (hasName) emit(nameTok, name, SymbolKind::Enum, true);
    std::string enumScope = scoped && hasName ? (scopeName().empty() ? name : scopeName() + "::" + name) : scopeName();
    advance();  // {
    // enumerators
    while (!at(Tok::End) && !atP("}")) {
      skipAttributes();
      if (at(Tok::Identifier)) {
        emit(tok(), std::string(tok().text), SymbolKind::Enumerator, true, enumScope);
        advance();
      }
      // skip initializer up to ',' or '}'
      int d = 0;
      while (!at(Tok::End)) {
        if (atP("(") || atP("{") || atP("[")) ++d;
        else if (atP(")") || atP("]")) --d;
        else if (atP("}")) {
          if (d == 0) break;
          --d;
        } else if (atP(",") && d == 0) {
          advance();
          break;
        }
        advance();
      }
    }
    if (atP("}")) advance();
    // trailing "} name;" for anonymous enums or variables: swallow to ';'
    while (!at(Tok::End) && !atP(";")) {
      if (at(Tok::Identifier) && (tok(1).is(";") || tok(1).is(",") || tok(1).is("="))) {
        emit(tok(), std::string(tok().text), inClass() ? SymbolKind::Field : SymbolKind::Variable, true);
      }
      advance();
    }
    if (atP(";")) advance();
  }

  void parseTypedef() {
    advance();  // typedef
    // typedef struct {...} Name;  / typedef struct Tag {...} Name; / typedef T Name; / typedef void (*Fn)(int);
    size_t startIdx = i_;
    std::vector<Token> idents;
    Token fnPtrName{};
    bool haveFnPtr = false;
    int brace = 0, paren = 0;
    while (!at(Tok::End)) {
      if (atP("{")) {
        if (brace == 0 && (tok(-0).kind != Tok::End)) {
          // Anonymous/inline struct body inside typedef: index its members with a placeholder scope.
          // Find the tag name (identifier before '{', if any) for the member scope.
        }
        ++brace;
      } else if (atP("}")) {
        --brace;
      } else if (atP("(")) {
        if (paren == 0 && brace == 0 && tok(1).is("*") && tok(2).kind == Tok::Identifier) {
          fnPtrName = tok(2);
          haveFnPtr = true;
        }
        ++paren;
      } else if (atP(")")) {
        --paren;
      } else if (atP(";") && brace == 0 && paren == 0) {
        break;
      } else if (at(Tok::Identifier) && brace == 0 && paren == 0) {
        idents.push_back(tok());
      } else if (atP("<") && brace == 0 && paren == 0) {
        skipAngles();
        continue;
      }
      advance();
    }
    (void)startIdx;
    if (haveFnPtr) {
      emit(fnPtrName, std::string(fnPtrName.text), SymbolKind::Typedef, true);
    } else if (!idents.empty()) {
      emit(idents.back(), std::string(idents.back().text), SymbolKind::Typedef, true);
    }
    if (atP(";")) advance();
    pendingTemplate_ = false;
  }

  void parseUsing() {
    advance();  // using
    if (atKw("namespace") || atKw("enum")) {
      skipStatement();
      return;
    }
    if (at(Tok::Identifier) && tok(1).is("=")) { emit(tok(), std::string(tok().text), SymbolKind::Typedef, true); }
    skipStatement();
    pendingTemplate_ = false;
  }

  bool isDeclTerminatorAfterParams() const {
    // Tokens that may legally follow a function parameter list.
    static const char* ok[] = {"{",
                               ";",
                               ":",
                               "=",
                               ",",
                               ")",
                               "->",
                               "&",
                               "&&",
                               "[",
                               "const",
                               "volatile",
                               "noexcept",
                               "override",
                               "final",
                               "throw",
                               "try",
                               "requires",
                               "__attribute__",
                               "__declspec",
                               "mutable"};
    for (auto s : ok)
      if (tok().text == s) return true;
    return tok().kind == Tok::Identifier && tok().text != "typedef";  // e.g. trailing macro like ATTR_WARN_UNUSED
  }

  void parseDeclaration() {
    // Scan a declaration at namespace/class scope. We look for either
    //   <type tokens> NAME ( params ) ... { | ;      -> function
    //   <type tokens> NAME [= init | { init } | , | ;] -> variable/field
    const size_t startIdx = i_;
    const int startLine = tok().line;
    int typeTokens = 0;
    std::string lastName;
    Token lastNameTok{};
    bool haveName = false;
    bool isExtern = false, isStatic = false;
    (void)isStatic;

    while (!at(Tok::End)) {
      const Token& t = tok();
      if (t.kind == Tok::Preprocessor) {
        pendingTemplate_ = false;
        return;
      }
      if (t.kind == Tok::Keyword) {
        if (t.is("extern")) isExtern = true;
        if (t.is("static")) isStatic = true;
        if (t.is("operator")) {
          std::string nm;
          Token lt{};
          readQualifiedName(nm, lt);
          lastName = nm;
          lastNameTok = lt;
          haveName = true;
          continue;
        }
        if (t.is("decltype") || t.is("sizeof") || t.is("alignof") || t.is("noexcept") || t.is("typeid") ||
            t.is("__declspec") || t.is("alignas") || t.is("__attribute__") || t.is("__uuidof")) {
          advance();
          skipBalanced("(", ")");
          ++typeTokens;
          continue;
        }
        if (t.is("class") || t.is("struct") || t.is("union") || t.is("enum") || t.is("typename")) {
          // elaborated type specifier: "struct Foo x;" or "typename T::type x;"
          advance();
          continue;
        }
        if (t.is("namespace") || t.is("template") || t.is("using") || t.is("typedef") || t.is("friend"))
          return;  // let step() handle
        ++typeTokens;
        advance();
        continue;
      }
      if (t.kind == Tok::Identifier) {
        if (haveName) ++typeTokens;  // previous name was actually part of the type
        std::string nm;
        Token lt{};
        readQualifiedName(nm, lt);
        if (nm.empty()) {
          advance();
          continue;
        }
        lastName = nm;
        lastNameTok = lt;
        haveName = true;
        continue;
      }
      if (t.kind == Tok::Punct) {
        if (t.is("(")) {
          if (!haveName) {  // "(*fp)(int)" style or macro garbage: skip group
            skipBalanced("(", ")");
            continue;
          }
          if (typeTokens == 0 && !inClass() && lastName.find("::") == std::string::npos && !lastName.starts_with("~") &&
              !lastName.starts_with("operator")) {
            // "NAME(" at namespace scope with no return type: almost certainly a macro invocation.
            skipBalanced("(", ")");
            if (atP(";")) advance();
            pendingTemplate_ = false;
            return;
          }
          // Function: parameters
          const int nameLine = lastNameTok.line;
          skipBalanced("(", ")");
          // Macro-invocation heuristic: next token on a later line and not a valid continuation.
          if (tok().line > std::max(nameLine, startLine) && !isDeclTerminatorAfterParams() && !atP("{")) {
            if (typeTokens == 0) {
              pendingTemplate_ = false;
              return;
            }
          }
          // Trailing part: qualifiers, trailing return, ctor-init, requires, = 0/default/delete.
          bool definition = false;
          bool sawPureOrDefault = false;
          while (!at(Tok::End)) {
            if (atP("{")) {
              definition = true;
              break;
            }
            if (atP(";")) break;
            if (atP("=")) {
              advance();
              if (at(Tok::Number) || atKw("default") || atKw("delete")) { sawPureOrDefault = true; }
              continue;
            }
            if (atP("(")) {
              skipBalanced("(", ")");
              continue;
            }
            if (atP("<")) {
              skipAngles();
              continue;
            }
            if (atP("[")) {
              skipBalanced("[", "]");
              continue;
            }
            if (atP(":")) {  // ctor-initializer: everything up to '{'
              while (!at(Tok::End) && !atP("{") && !atP(";")) {
                if (atP("(")) {
                  skipBalanced("(", ")");
                  continue;
                }
                if (atP("{")) break;
                advance();
              }
              continue;
            }
            if (atP("}")) return;  // malformed; bail out
            advance();
          }
          SymbolKind kind = SymbolKind::Function;
          std::string scope = scopeName();
          std::string unq = lastName;
          size_t q = lastName.rfind("::");
          if (q != std::string::npos && q > 0) {
            unq = lastName.substr(q + 2);
            std::string qual = lastName.substr(0, q);
            size_t pos;
            while ((pos = qual.find("<>")) != std::string::npos) qual.erase(pos, 2);
            scope = scope.empty() ? qual : scope + "::" + qual;
            kind = SymbolKind::Method;
          } else if (inClass()) {
            kind = SymbolKind::Method;
            if (unq == scopes_.back().name || unq == "~" + scopes_.back().name) kind = SymbolKind::Constructor;
          } else if (q == 0) {
            unq = lastName.substr(2);
          }
          // Out-of-line "Foo::Foo(" or "Foo::~Foo("
          if (kind == SymbolKind::Method) {
            size_t sq = scope.rfind("::");
            std::string cls = sq == std::string::npos ? scope : scope.substr(sq + 2);
            if (unq == cls || unq == "~" + cls) kind = SymbolKind::Constructor;
          }
          if (definition) {
            emit(lastNameTok, unq, kind, true, scope);
            skipBalanced("{", "}");
            // "} ;" after a function body is harmless; also handles "}" of lambda-less bodies.
            if (atP(";")) advance();
          } else {
            emit(lastNameTok, unq, kind, !sawPureOrDefault && false, scope);
            if (atP(";")) advance();
          }
          pendingTemplate_ = false;
          return;
        }
        if (t.is("=") || t.is(";") || t.is(",") || t.is("{") || t.is("[")) {
          if (haveName && typeTokens > 0 && lastName.find("::") == std::string::npos) {
            emit(lastNameTok, lastName, inClass() ? SymbolKind::Field : SymbolKind::Variable, !isExtern);
          } else if (haveName && typeTokens > 0) {
            // out-of-line static member definition: "int Foo::count = 0;"
            size_t q = lastName.rfind("::");
            std::string qual = lastName.substr(0, q);
            size_t pos;
            while ((pos = qual.find("<>")) != std::string::npos) qual.erase(pos, 2);
            std::string sc = scopeName().empty() ? qual : scopeName() + "::" + qual;
            emit(lastNameTok, lastName.substr(q + 2), SymbolKind::Field, true, sc);
          }
          if (t.is(",")) {
            // more declarators: "int a, b, c;"
            advance();
            haveName = false;
            while (!at(Tok::End) && !atP(";")) {
              while (atP("*") || atP("&") || atP("&&")) advance();
              if (at(Tok::Identifier)) {
                emit(tok(), std::string(tok().text), inClass() ? SymbolKind::Field : SymbolKind::Variable, !isExtern);
                advance();
              }
              // skip initializer / array / to next ',' or ';'
              int d = 0;
              while (!at(Tok::End)) {
                if (atP("(") || atP("{") || atP("[")) ++d;
                else if (atP(")") || atP("}") || atP("]")) --d;
                else if ((atP(",")) && d == 0) {
                  advance();
                  break;
                } else if (atP(";") && d == 0) break;
                advance();
              }
            }
            if (atP(";")) advance();
            pendingTemplate_ = false;
            return;
          }
          if (t.is(";")) {
            advance();
            pendingTemplate_ = false;
            return;
          }
          // '=' or '{' or '[' : skip initializer to ';'
          skipStatement();
          pendingTemplate_ = false;
          return;
        }
        if (t.is("*") || t.is("&") || t.is("&&") || t.is("::")) {
          ++typeTokens;
          advance();
          continue;
        }
        if (t.is("<")) {
          skipAngles();
          ++typeTokens;
          continue;
        }
        if (t.is("}")) return;  // scope close; let step() handle
        if (t.is(")")) {
          advance();
          continue;
        }
        if (t.is("...")) {
          advance();
          continue;
        }
        // Anything else: give up on this statement.
        advance();
        if (i_ == startIdx) advance();
        continue;
      }
      // numbers/strings in a declaration: part of an initializer we failed to recognise.
      advance();
    }
  }

  std::vector<Token> toks_;
  size_t i_ = 0;
  int depth_ = 0;
  std::vector<Scope> scopes_;
  std::vector<Symbol> out_;
  bool pendingTemplate_ = false;
  std::vector<std::string>* includes_ = nullptr;
};

}  // namespace

std::vector<Symbol> extractSymbols(std::string_view source, std::vector<std::string>* includes) {
  return Extractor(source, includes).run();
}

// ---------------------------------------------------------------------------
// Index
// ---------------------------------------------------------------------------

namespace {
std::string keyFor(const std::filesystem::path& p) { return p.generic_string(); }

bool readFile(const std::filesystem::path& p, std::string& out) {
  std::ifstream in(p, std::ios::binary);
  if (!in) return false;
  std::stringstream ss;
  ss << in.rdbuf();
  out = ss.str();
  return true;
}
}  // namespace

namespace {
// One stat() call: mtime in nanoseconds since the epoch (clock ticks elsewhere) and size.
bool statFile(const std::filesystem::path& p, int64_t* mtime, uint64_t* size) {
#ifdef __unix__
  struct stat st;
  if (::stat(p.c_str(), &st) != 0) return false;
  *mtime = static_cast<int64_t>(st.st_mtim.tv_sec) * 1000000000ll + st.st_mtim.tv_nsec;
  *size = static_cast<uint64_t>(st.st_size);
  return true;
#else
  std::error_code ec;
  *mtime = std::filesystem::last_write_time(p, ec).time_since_epoch().count();
  if (ec) return false;
  *size = std::filesystem::file_size(p, ec);
  return !ec;
#endif
}
}  // namespace

namespace {

inline char lowerAscii(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c; }
inline bool isLowerAscii(char c) { return c >= 'a' && c <= 'z'; }
inline bool isUpperAscii(char c) { return c >= 'A' && c <= 'Z'; }

// Prefilter masks (search): one bit per character class, case-insensitive. a-z get a bit each, all digits share one,
// '_' and ':' have one, every other byte is folded onto the last three. A candidate can only match a query if
// (queryMask & ~candidateMask) == 0; folding only ever adds bits to both sides, so it can cause false positives
// but never false negatives.
// Runs fn(threadIndex) on `threads` threads (the caller is thread 0).
template <class F>
void runParallel(int threads, F&& fn) {
  std::vector<std::thread> pool;
  for (int t = 1; t < threads; ++t) pool.emplace_back([&fn, t] { fn(t); });
  fn(0);
  for (auto& th : pool) th.join();
}

int hardwareThreads(int requested) {
  if (requested > 0) return requested;
  return static_cast<int>(std::clamp(std::thread::hardware_concurrency(), 1u, 8u));
}

constexpr std::array<uint32_t, 256> makeBitTable() {
  std::array<uint32_t, 256> t{};
  for (unsigned c = 0; c < 256; ++c) {
    unsigned l = (c >= 'A' && c <= 'Z') ? c + 32 : c;
    if (l >= 'a' && l <= 'z') t[c] = 1u << (l - 'a');
    else if (l >= '0' && l <= '9') t[c] = 1u << 26;
    else if (l == '_') t[c] = 1u << 27;
    else if (l == ':') t[c] = 1u << 28;
    else t[c] = 1u << (29 + (l % 3));
  }
  return t;
}
constexpr std::array<uint32_t, 256> kBitTable = makeBitTable();

inline uint32_t charBit(char ch) { return kBitTable[static_cast<unsigned char>(ch)]; }
inline uint32_t maskOf(std::string_view s) {
  uint32_t m = 0;
  for (char ch : s) m |= charBit(ch);
  return m;
}

}  // namespace

void FastIndex::addLocked(std::shared_ptr<const FileSymbols> fs) {
  std::string key = keyFor(fs->file);
  removeLocked(key);
  for (size_t i = 0; i < fs->symbols.size(); ++i) {
    auto& refs = byName_[fs->symbols[i].name];
    if (refs.empty()) ++namesGen_;  // a fresh node: its vector is still empty
    refs.push_back({fs, static_cast<uint32_t>(i)});
  }
  symbolCount_ += fs->symbols.size();
  fileInfo_[fs.get()].seq = ++nextSeq_;
  files_[key] = std::move(fs);
  ++generation_;
}

void FastIndex::removeLocked(const std::string& key) {
  auto it = files_.find(key);
  if (it == files_.end()) return;
  auto old = it->second;
  for (auto& s : old->symbols) {
    auto bn = byName_.find(s.name);
    if (bn == byName_.end()) continue;
    std::erase_if(bn->second, [&](const Ref& r) { return r.file == old; });
    if (bn->second.empty()) {
      byName_.erase(bn);
      ++namesGen_;
    }
  }
  symbolCount_ -= old->symbols.size();
  fileInfo_.erase(old.get());
  files_.erase(it);
  ++generation_;
}

FastIndex::Stats FastIndex::indexFiles(const std::vector<std::filesystem::path>& files, int threads,
                                       std::function<void(size_t, size_t)> progress) {
  return indexFilesCached(files, {}, threads, std::move(progress));
}

FastIndex::Stats FastIndex::indexFilesCached(const std::vector<std::filesystem::path>& files,
                                             std::vector<std::shared_ptr<const FileSymbols>> cached, int threads,
                                             std::function<void(size_t, size_t)> progress) {
  auto t0 = std::chrono::steady_clock::now();
  if (threads <= 0) threads = static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
  std::unordered_map<std::string, std::shared_ptr<const FileSymbols>> cache;
  cache.reserve(cached.size());
  for (auto& c : cached) cache.emplace(keyFor(c->file), std::move(c));
  cached.clear();

  std::vector<std::shared_ptr<const FileSymbols>> results(files.size());
  std::atomic<size_t> next{0}, done{0}, bytes{0}, reused{0}, relexed{0}, inCache{0};
  std::vector<std::thread> pool;
  for (int t = 0; t < threads; ++t) {
    pool.emplace_back([&] {
      std::string content;
      while (true) {
        size_t i = next++;
        if (i >= files.size()) break;
        int64_t mtime = 0;
        uint64_t size = 0;
        if (statFile(files[i], &mtime, &size)) {
          auto it = cache.find(keyFor(files[i]));  // read-only after construction: safe from all workers
          if (it != cache.end()) ++inCache;
          if (it != cache.end() && it->second->mtime == mtime && it->second->size == size && mtime != 0) {
            results[i] = it->second;
            ++reused;
          } else if (readFile(files[i], content)) {
            bytes += content.size();
            auto fs = std::make_shared<FileSymbols>();
            fs->file = files[i];
            fs->mtime = mtime;
            fs->size = size;
            fs->symbols = extractSymbols(content, &fs->includes);
            results[i] = std::move(fs);
            ++relexed;
          }
        }
        size_t d = ++done;
        if (progress) progress(d, files.size());
      }
    });
  }
  for (auto& th : pool) th.join();

  size_t kept = reused;
  std::vector<std::shared_ptr<const FileSymbols>> batch;
  batch.reserve(files.size());
  for (auto& r : results)
    if (r) batch.push_back(std::move(r));
  importFiles(std::move(batch));

  Stats st;
  st.files = fileCount();
  st.symbols = symbolCount();
  st.bytes = bytes;
  st.reused = kept;
  st.relexed = relexed;
  st.dropped = cache.size() > inCache ? cache.size() - inCache : 0;
  st.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  return st;
}

void FastIndex::importFiles(std::vector<std::shared_ptr<const FileSymbols>> files) {
  size_t total = 0;
  for (auto& f : files) total += f->symbols.size();
  std::unique_lock<std::shared_mutex> lk(mu_);
  if (files_.empty()) {
    files_.reserve(files.size());
    byName_.reserve(total / 2 + 16);
  }
  for (auto& f : files) addLocked(std::move(f));
}

std::vector<std::shared_ptr<const FileSymbols>> FastIndex::exportFiles() const {
  std::shared_lock<std::shared_mutex> lk(mu_);
  std::vector<std::shared_ptr<const FileSymbols>> out;
  out.reserve(files_.size());
  for (auto& [k, fs] : files_) out.push_back(fs);
  return out;
}

void FastIndex::updateFile(const std::filesystem::path& file, std::string_view text) {
  auto fs = std::make_shared<FileSymbols>();
  fs->file = file;
  fs->symbols = extractSymbols(text, &fs->includes);
  std::unique_lock<std::shared_mutex> lk(mu_);
  addLocked(std::move(fs));
}

std::vector<std::string> FastIndex::allIncludes() const {
  std::shared_lock<std::shared_mutex> lk(mu_);
  std::unordered_set<std::string> seen;
  for (auto& [k, fs] : files_)
    for (auto& inc : fs->includes) seen.insert(inc);
  return {seen.begin(), seen.end()};
}

void FastIndex::removeFile(const std::filesystem::path& file) {
  std::unique_lock<std::shared_mutex> lk(mu_);
  removeLocked(keyFor(file));
}

void FastIndex::clear() {
  std::unique_lock<std::shared_mutex> lk(mu_);
  files_.clear();
  fileInfo_.clear();
  byName_.clear();
  ++namesGen_;
  symbolCount_ = 0;
  ++generation_;
}

size_t FastIndex::fileCount() const {
  std::shared_lock<std::shared_mutex> lk(mu_);
  return files_.size();
}
size_t FastIndex::symbolCount() const {
  std::shared_lock<std::shared_mutex> lk(mu_);
  return symbolCount_;
}

std::shared_ptr<const FileSymbols> FastIndex::fileSymbols(const std::filesystem::path& file) const {
  std::shared_lock<std::shared_mutex> lk(mu_);
  auto it = files_.find(keyFor(file));
  return it == files_.end() ? nullptr : it->second;
}

std::vector<FastIndex::Match> FastIndex::lookup(std::string_view name) const {
  std::shared_lock<std::shared_mutex> lk(mu_);
  std::vector<Match> out;
  auto it = byName_.find(std::string(name));
  if (it == byName_.end()) return out;
  for (auto& r : it->second) out.push_back({r.file, r.index, 1000});
  std::stable_sort(out.begin(), out.end(),
                   [](const Match& a, const Match& b) { return a.sym().definition > b.sym().definition; });
  return out;
}

namespace {

// Greedy case-insensitive subsequence test of `q` against scope + "::" + name without building the string. This is
// exactly the condition under which fuzzyScore() can return non-zero (an exact or prefix match is a subsequence).
// `q` must already be lower-cased.
bool isSubsequenceOfQualified(std::string_view q, const Symbol& sym) {
  size_t qi = 0;
  const size_t qn = q.size();
  auto feed = [&](std::string_view part) {
    for (char ch : part) {
      if (qi == qn) return;
      if (lowerAscii(ch) == q[qi]) ++qi;
    }
  };
  if (!sym.scope.empty()) {
    feed(sym.scope);
    feed("::");
  }
  feed(sym.name);
  return qi == q.size();
}

// Per symbol: character classes of "scope::name". Consecutive symbols very often share a scope, so the scope mask
// is memoised.
std::vector<uint32_t> computeRefMasks(const FileSymbols& fs) {
  std::vector<uint32_t> out(fs.symbols.size());
  const std::string* lastScope = nullptr;
  uint32_t lastScopeMask = 0;
  for (size_t i = 0; i < fs.symbols.size(); ++i) {
    const Symbol& s = fs.symbols[i];
    uint32_t scopeMask = 0;
    if (!s.scope.empty()) {
      if (!lastScope || *lastScope != s.scope) {
        lastScopeMask = maskOf(s.scope) | charBit(':');
        lastScope = &s.scope;
      }
      scopeMask = lastScopeMask;
    }
    out[i] = maskOf(s.name) | scopeMask;
  }
  return out;
}

constexpr size_t kBucketsPerBlock = 4096;    // hash buckets per table block (~3000 names): the scan's work unit
constexpr size_t kFilesPerBlock = 8;         // files per work unit of a qualified scan
constexpr size_t kParallelMinNames = 16384;  // below this, threads cost more than they save
constexpr size_t kParallelMinFiles = 64;

}  // namespace

// Flat view of byName_ for the non-qualified scan. Walking an unordered_map node by node is a chain of dependent
// cache misses; contiguous arrays of node pointers plus per-name character masks let every thread stream through
// its slice with many independent loads in flight. The table is a list of blocks, each holding the names of a
// range of hash buckets, so it is built in parallel too (independent bucket chains) and never concatenated.
// unordered_map nodes are address-stable until erased, so it stays valid until a name is added or erased
// (namesGen_). Visiting order is irrelevant to the result: the ranking is a total order.
struct FastIndex::ScanTable {
  struct Block {
    std::vector<const NameMap::value_type*> nodes;
    std::vector<uint32_t> masks;  // maskOf(name)
  };
  size_t namesGen = 0;
  std::vector<Block> blocks;
};

std::shared_ptr<const FastIndex::ScanTable> FastIndex::prepare() const {
  std::lock_guard<std::mutex> lk(prepMu_);
  if (table_ && table_->namesGen == namesGen_) return table_;
  auto t = std::make_shared<ScanTable>();
  t->namesGen = namesGen_;
  const size_t buckets = byName_.bucket_count();
  const size_t nblocks = (buckets + kBucketsPerBlock - 1) / kBucketsPerBlock;
  t->blocks.resize(nblocks);
  std::atomic<size_t> next{0};
  const int threads = byName_.size() < kParallelMinNames
                          ? 1
                          : static_cast<int>(std::min<size_t>(hardwareThreads(searchThreads_), nblocks));
  runParallel(std::max(threads, 1), [&](int) {
    for (size_t b; (b = next++) < nblocks;) {
      auto& blk = t->blocks[b];
      const size_t end = std::min(buckets, (b + 1) * kBucketsPerBlock);
      for (size_t bucket = b * kBucketsPerBlock; bucket < end; ++bucket)
        for (auto it = byName_.cbegin(bucket); it != byName_.cend(bucket); ++it) {
          blk.nodes.push_back(&*it);
          blk.masks.push_back(maskOf(it->first));
        }
      blk.nodes.shrink_to_fit();
      blk.masks.shrink_to_fit();
    }
  });
  table_ = std::move(t);
  return table_;
}

// Fills FileInfo::masks for files that do not have them yet (all of them on the first qualified query, only
// new / re-added files afterwards). Caller holds mu_ shared. After it returns every file's masks are complete and
// stay untouched while mu_ is held, so the scan reads them without further locking.
void FastIndex::ensureFileMasks() const {
  std::lock_guard<std::mutex> g(prepMu_);  // serialises concurrent searches filling masks
  std::vector<std::pair<const FileSymbols*, const FileInfo*>> missing;
  for (auto& [key, fs] : files_) {
    const FileInfo& info = fileInfo_.at(fs.get());
    if (info.masks.size() != fs->symbols.size()) missing.push_back({fs.get(), &info});
  }
  if (missing.empty()) return;
  std::atomic<size_t> next{0};
  const int threads = missing.size() < 16 ? 1 : hardwareThreads(searchThreads_);
  runParallel(threads, [&](int) {
    for (size_t k; (k = next++) < missing.size();) missing[k].second->masks = computeRefMasks(*missing[k].first);
  });
}

void FastIndex::prepareSearch() const {
  std::shared_lock<std::shared_mutex> lk(mu_);
  (void)prepare();
  ensureFileMasks();
}

int FastIndex::fuzzyScore(std::string_view q, std::string_view c) {
  if (q.empty()) return 1;
  if (q.size() > c.size()) return 0;
  // Exact / prefix fast paths.
  bool exactCI = q.size() == c.size();
  bool prefixCI = true;
  for (size_t i = 0; i < q.size(); ++i) {
    if (lowerAscii(q[i]) != lowerAscii(c[i])) {
      exactCI = false;
      prefixCI = false;
      break;
    }
  }
  if (exactCI) return q == c ? 1000 : 950;
  if (prefixCI) return 800 - static_cast<int>(std::min<size_t>(c.size() - q.size(), 200));
  // Subsequence match with bonuses for word starts and adjacency.
  int score = 0;
  size_t ci = 0;
  int prevMatch = -2;
  for (size_t qi = 0; qi < q.size(); ++qi) {
    const char ql = lowerAscii(q[qi]);
    bool found = false;
    while (ci < c.size()) {
      if (ql == lowerAscii(c[ci])) {
        bool wordStart =
            ci == 0 || c[ci - 1] == '_' || c[ci - 1] == ':' || (isLowerAscii(c[ci - 1]) && isUpperAscii(c[ci]));
        score += 10;
        if (wordStart) score += 30;
        if (static_cast<int>(ci) == prevMatch + 1) score += 15;
        if (q[qi] == c[ci]) score += 2;
        prevMatch = static_cast<int>(ci);
        ++ci;
        found = true;
        break;
      }
      ++ci;
    }
    if (!found) return 0;
  }
  score -= static_cast<int>(std::min<size_t>(c.size(), 100)) / 4;  // shorter candidates rank higher
  return std::max(score, 1);
}

std::vector<FastIndex::Match> FastIndex::search(std::string_view query, size_t limit,
                                                const std::function<bool()>& cancelled) const {
  std::vector<Match> out;
  std::string q(query);
  // Strip whitespace.
  std::erase_if(q, [](char ch) { return ch == ' ' || ch == '\t'; });
  const bool qualified = q.find("::") != std::string::npos;
  std::shared_lock<std::shared_mutex> lk(mu_);
  if (limit == 0) return out;
  const uint32_t qmask = maskOf(q);
  std::string qLower(q);
  for (auto& ch : qLower) ch = lowerAscii(ch);

  // A candidate under consideration. `order` is the historical insertion order among equal (score, name):
  // the position in byName_[name] for plain queries, (file add sequence, symbol index) for qualified ones.
  struct Cand {
    int score;
    uint32_t index;
    uint64_t order;
    const std::string* name;
    const std::shared_ptr<const FileSymbols>* file;
  };
  // Total order identical to the historical stable_sort: score desc, name asc, then insertion order.
  auto better = [](const Cand& a, const Cand& b) {
    if (a.score != b.score) return a.score > b.score;
    if (int c = a.name->compare(*b.name)) return c < 0;
    return a.order < b.order;
  };

  // Work is cut into blocks handed out by an atomic counter (dynamic balancing + cancellation points).
  std::shared_ptr<const ScanTable> table;
  struct FileEntry {
    std::shared_ptr<const FileSymbols> file;
    uint32_t seq;
    const FileInfo* info;
  };
  std::vector<FileEntry> files;
  size_t nblocks, parallelMin, units;
  if (!qualified) {
    table = prepare();
    nblocks = table->blocks.size();
    units = byName_.size();
    parallelMin = kParallelMinNames;
  } else {
    // Qualified queries scan the symbols file by file: sequential memory instead of per-name pointer chasing, and
    // no auxiliary structure to maintain.
    ensureFileMasks();
    files.reserve(files_.size());
    for (auto& [key, fs] : files_) {
      const FileInfo& info = fileInfo_.at(fs.get());
      files.push_back({fs, info.seq, &info});
    }
    nblocks = (files.size() + kFilesPerBlock - 1) / kFilesPerBlock;
    units = files.size();
    parallelMin = kParallelMinFiles;
  }
  int threads = hardwareThreads(searchThreads_);
  if (units < parallelMin) threads = 1;
  threads = static_cast<int>(std::max<size_t>(1, std::min<size_t>(static_cast<size_t>(threads), nblocks)));
  std::vector<std::vector<Cand>> heaps(static_cast<size_t>(threads));
  std::atomic<size_t> nextBlock{0};
  std::atomic<bool> aborted{false};

  runParallel(threads, [&](int tid) {
    std::vector<Cand>& heap = heaps[static_cast<size_t>(tid)];  // max-heap under `better`: front() is the worst kept
    heap.reserve(std::min<size_t>(limit, 4096) + 1);
    std::string full;
    auto consider = [&](const Cand& c) {
      if (heap.size() < limit) {
        heap.push_back(c);
        std::push_heap(heap.begin(), heap.end(), better);
      } else if (better(c, heap.front())) {
        std::pop_heap(heap.begin(), heap.end(), better);
        heap.back() = c;
        std::push_heap(heap.begin(), heap.end(), better);
      }
    };
    for (;;) {
      if (aborted.load(std::memory_order_relaxed)) return;
      if (cancelled && cancelled()) {
        aborted = true;
        return;
      }
      const size_t b = nextBlock++;
      if (b >= nblocks) return;
      if (!qualified) {
        const auto& blk = table->blocks[b];
        for (size_t i = 0; i < blk.nodes.size(); ++i) {
          if ((qmask & ~blk.masks[i]) != 0) continue;  // the name lacks one of the query's characters
          const NameMap::value_type& node = *blk.nodes[i];
          const int s = fuzzyScore(q, node.first);
          if (s <= 0) continue;
          if (heap.size() >= limit && s + 5 < heap.front().score) continue;  // cannot beat the current worst
          const auto& refs = node.second;
          for (size_t j = 0; j < refs.size(); ++j) {
            const Ref& r = refs[j];
            consider({s + (r.file->symbols[r.index].definition ? 5 : 0), r.index, j, &node.first, &r.file});
          }
        }
      } else {
        const size_t end = std::min(files.size(), (b + 1) * kFilesPerBlock);
        for (size_t f = b * kFilesPerBlock; f < end; ++f) {
          const auto& syms = files[f].file->symbols;
          const auto& masks = files[f].info->masks;
          for (size_t i = 0; i < syms.size(); ++i) {
            if ((qmask & ~masks[i]) != 0) continue;  // "scope::name" lacks one of the query's characters
            const Symbol& sym = syms[i];
            if (!isSubsequenceOfQualified(qLower, sym)) continue;  // cheap reject before building the string
            full.assign(sym.scope);
            if (!sym.scope.empty()) full += "::";
            full += sym.name;
            const int s = fuzzyScore(q, full);
            if (s <= 0) continue;
            consider({s + (sym.definition ? 5 : 0), static_cast<uint32_t>(i),
                      (static_cast<uint64_t>(files[f].seq) << 32) | i, &sym.name, &files[f].file});
          }
        }
      }
    }
  });
  if (aborted) return out;

  std::vector<Cand> all;
  for (auto& h : heaps) all.insert(all.end(), h.begin(), h.end());
  std::sort(all.begin(), all.end(), better);
  if (all.size() > limit) all.resize(limit);
  out.reserve(all.size());
  for (const Cand& c : all) out.push_back({*c.file, c.index, c.score});
  return out;
}

}  // namespace tf
