#include "semantic/FastOverload.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <numeric>
#include <set>

#include "semantic/CppLexer.h"

namespace tf {

namespace {

using Cat = OvType::Cat;
using Toks = std::vector<Token>;

constexpr int kInf = 100;  // an unbounded rank

bool isP(const Token& t, std::string_view s) { return t.kind == Tok::Punct && t.text == s; }
bool isK(const Token& t, std::string_view s) { return t.kind == Tok::Keyword && t.text == s; }
bool isId(const Token& t) { return t.kind == Tok::Identifier; }
bool isIdentChar(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || static_cast<unsigned char>(c) >= 0x80;
}

std::string_view lastComp(std::string_view s) {
  const size_t q = s.rfind("::");
  return q == std::string_view::npos ? s : s.substr(q + 2);
}

// ---------------------------------------------------------------------------------------------------------------
// builtin types
// ---------------------------------------------------------------------------------------------------------------

bool isBuiltinWord(std::string_view w) {
  static const std::set<std::string_view> s = {"int",     "char",  "short",   "long",    "float",    "double",
                                               "bool",    "void",  "wchar_t", "char8_t", "char16_t", "char32_t",
                                               "__int64", "_Bool", "signed",  "unsigned"};
  return s.count(w) > 0;
}

// "unsigned long long int" -> "unsigned long long"; "" for anything else.
std::string canonPrim(std::string_view words) {
  int longs = 0, shorts = 0;
  bool un = false, sg = false, ch = false, in = false, db = false, fl = false, bl = false, vd = false;
  std::string special;
  size_t i = 0;
  while (i < words.size()) {
    while (i < words.size() && words[i] == ' ') ++i;
    size_t j = i;
    while (j < words.size() && words[j] != ' ') ++j;
    const std::string_view w = words.substr(i, j - i);
    i = j;
    if (w.empty()) continue;
    if (w == "unsigned") un = true;
    else if (w == "signed") sg = true;
    else if (w == "long") ++longs;
    else if (w == "short") ++shorts;
    else if (w == "char") ch = true;
    else if (w == "int") in = true;
    else if (w == "double") db = true;
    else if (w == "float") fl = true;
    else if (w == "bool" || w == "_Bool") bl = true;
    else if (w == "void") vd = true;
    else if (w == "wchar_t" || w == "char8_t" || w == "char16_t" || w == "char32_t") special = std::string(w);
    else if (w == "__int64") longs = 2;
    else return {};
  }
  if (vd) return "void";
  if (bl) return "bool";
  if (!special.empty()) return special;
  if (fl) return "float";
  if (db) return longs ? "long double" : "double";
  if (ch) return un ? "unsigned char" : sg ? "signed char" : "char";
  (void)in;
  if (shorts) return un ? "unsigned short" : "short";
  if (longs >= 2) return un ? "unsigned long long" : "long long";
  if (longs == 1) return un ? "unsigned long" : "long";
  return un || sg || in ? (un ? "unsigned int" : "int") : std::string();
}

bool promotesToInt(const std::string& p) {
  return p == "bool" || p == "char" || p == "signed char" || p == "unsigned char" || p == "short" ||
         p == "unsigned short" || p == "wchar_t" || p == "char8_t" || p == "char16_t";
}

// The std:: names that are aliases of builtin types (not classes).
bool isStdAliasName(std::string_view last) {
  static const std::set<std::string_view> s = {
      "size_t",       "ssize_t",   "ptrdiff_t", "intptr_t",    "uintptr_t",    "nullptr_t",    "max_align_t",
      "int8_t",       "int16_t",   "int32_t",   "int64_t",     "uint8_t",      "uint16_t",     "uint32_t",
      "uint64_t",     "wint_t",    "off_t",     "time_t",      "clock_t",      "intmax_t",     "uintmax_t",
      "sig_atomic_t", "mbstate_t", "fpos_t",    "int_fast8_t", "int_least8_t", "uint_fast8_t", "uint_least8_t"};
  return s.count(last) > 0;
}

std::string rawOf(const OvType& t) {
  std::string r;
  switch (t.kind) {
    case OvType::Unknown: r = "?"; break;
    case OvType::Prim: r = "P:" + t.name; break;
    case OvType::Named: r = "N:" + std::string(lastComp(t.name)) + (t.targ.empty() ? "" : "<" + t.targ + ">"); break;
    case OvType::NullPtr: r = "nullptr"; break;
    case OvType::Generic: r = "G:" + t.name; break;
  }
  if (t.constBase) r += " const";
  r += std::string(t.ptr, '*');
  if (t.ref) r += t.ref == 1 ? "&" : "&&";
  return r;
}

// ---------------------------------------------------------------------------------------------------------------
// token helpers
// ---------------------------------------------------------------------------------------------------------------

// Index of the closer of the bracket opened at `i` (any kind closes any kind), or `limit`.
size_t matchFwd(const Toks& T, size_t i, size_t limit) {
  int d = 0;
  for (size_t k = i; k < limit; ++k) {
    if (T[k].kind != Tok::Punct) continue;
    const std::string_view s = T[k].text;
    if (s == "(" || s == "[" || s == "{") ++d;
    else if ((s == ")" || s == "]" || s == "}") && --d == 0) return k;
  }
  return limit;
}

// Index of the opener of the bracket closed at `i`, or npos.
size_t matchBack(const Toks& T, size_t i) {
  int d = 0;
  for (size_t k = i + 1; k-- > 0;) {
    if (T[k].kind != Tok::Punct) continue;
    const std::string_view s = T[k].text;
    if (s == ")" || s == "]" || s == "}") ++d;
    else if ((s == "(" || s == "[" || s == "{") && --d == 0) return k;
  }
  return std::string::npos;
}

// At a '<': index after the matching '>' (tolerating nested brackets), or npos.
size_t skipAngleFwd(const Toks& T, size_t i, size_t limit) {
  int d = 0;
  for (size_t k = i; k < limit; ++k) {
    const Token& t = T[k];
    if (t.kind != Tok::Punct) continue;
    if (t.text == "<") ++d;
    else if (t.text == ">") --d;
    else if (t.text == ">>") d -= 2;
    else if (t.text == "(" || t.text == "[") k = matchFwd(T, k, limit);
    else if (t.text == ";" || t.text == "{" || t.text == "}") return std::string::npos;
    if (d <= 0) return k + 1;
  }
  return std::string::npos;
}

// At a '>' (index i): the index of the matching '<', or npos.
size_t skipAngleBack(const Toks& T, size_t i) {
  int d = 0;
  for (size_t k = i + 1; k-- > 0;) {
    const Token& t = T[k];
    if (t.kind != Tok::Punct) continue;
    if (t.text == ">") ++d;
    else if (t.text == ">>") d += 2;
    else if (t.text == "<" && --d <= 0) return k;
    else if (t.text == ";" || t.text == "{" || t.text == "}") return std::string::npos;
  }
  return std::string::npos;
}

// A qualified name [::] id [<args>] { :: id [<args>] } at `i` (below `e`): the name with template arguments dropped,
// the first identifier chain in the last template-id's arguments, the index after it.
struct QName {
  std::string name, targ;
  bool templated = false;
  size_t next = 0;
};
bool readQName(const Toks& T, size_t i, size_t e, QName& q) {
  q = QName{};
  if (i < e && isP(T[i], "::")) ++i;
  bool any = false;
  while (i < e && isId(T[i])) {
    if (!q.name.empty()) q.name += "::";
    q.name += T[i].text;
    any = true;
    ++i;
    if (i < e && isP(T[i], "<")) {
      const size_t after = skipAngleFwd(T, i, e);
      if (after == std::string::npos) return false;
      std::string chain;
      size_t k = i + 1;
      while (k + 1 < after &&
             (isK(T[k], "const") || isK(T[k], "typename") || isK(T[k], "struct") || isK(T[k], "class")))
        ++k;
      while (k + 1 < after && isId(T[k])) {
        if (!chain.empty()) chain += "::";
        chain += T[k].text;
        ++k;
        if (k + 1 < after && isP(T[k], "<")) {
          const size_t inner = skipAngleFwd(T, k, after);
          if (inner == std::string::npos) break;
          k = inner;
        }
        if (k + 1 < after && isP(T[k], "::")) ++k;
        else break;
      }
      q.targ = chain;
      q.templated = true;
      i = after;
    }
    if (i + 1 < e && isP(T[i], "::") && isId(T[i + 1])) {
      ++i;
      continue;
    }
    break;
  }
  q.next = i;
  return any;
}

// The declared type spelled by the tokens [b, e) of a parameter / a cast: type specifiers, `*`, `&`, `&&`. With
// `wantName` an identifier after the type is the parameter's name (skipped). `pack` is set for `...`. A shape this does
// not read leaves the type Unknown. `tparams` are the template parameter names (Generic types).
struct TypeRead {
  OvType ty;
  bool hasDefault = false;
  bool pack = false;
};
TypeRead readType(const Toks& T, size_t b, size_t e, const std::vector<std::string>& tparams, bool wantName) {
  TypeRead r;
  bool bad = false, star = false, haveType = false, isAuto = false;
  std::string words, sign;
  std::string name, targ;
  bool templated = false;
  size_t i = b;
  while (i + 1 < e && isP(T[i], "[") && isP(T[i + 1], "[")) i = std::min(matchFwd(T, i, e) + 1, e);
  while (i < e) {
    const Token& t = T[i];
    if (t.kind == Tok::Keyword) {
      if (t.text == "const") {
        if (!star) r.ty.constBase = true;
        else r.ty.constPtr = true;
      } else if (t.text == "volatile" || t.text == "register" || t.text == "mutable" || t.text == "struct" ||
                 t.text == "class" || t.text == "union" || t.text == "enum" || t.text == "constexpr") {
        // elaborated type specifier / storage: nothing to record
      } else if (t.text == "signed" || t.text == "unsigned") {
        sign = std::string(t.text);
        haveType = true;
      } else if (t.text == "auto") {
        isAuto = true;
        haveType = true;
      } else if (isBuiltinWord(t.text)) {
        if (!words.empty()) words += ' ';
        words += t.text;
        haveType = true;
      } else {
        bad = true;  // decltype, typename, operator, this, ...
      }
      ++i;
      continue;
    }
    if (t.kind == Tok::Identifier || isP(t, "::")) {
      if (haveType && !star && (isId(t))) {
        // a declarator name after the type
        if (!wantName) bad = true;
        ++i;
        // `[N]` after the name: an array parameter is a pointer
        while (i < e && isP(T[i], "[")) {
          ++r.ty.ptr;
          i = std::min(matchFwd(T, i, e) + 1, e);
        }
        continue;
      }
      if (haveType && star && isId(t)) {
        if (!wantName) bad = true;
        ++i;
        while (i < e && isP(T[i], "[")) {
          ++r.ty.ptr;
          i = std::min(matchFwd(T, i, e) + 1, e);
        }
        continue;
      }
      QName q;
      if (!readQName(T, i, e, q)) {
        bad = true;
        break;
      }
      name = q.name;
      targ = q.targ;
      templated = q.templated;
      haveType = true;
      i = q.next;
      continue;
    }
    if (t.kind == Tok::Punct) {
      if (t.text == "*") {
        ++r.ty.ptr;
        star = true;
        r.ty.constPtr = false;
      } else if (t.text == "&") {
        r.ty.ref = 1;
      } else if (t.text == "&&") {
        r.ty.ref = 2;
      } else if (t.text == "...") {
        r.pack = true;
      } else if (t.text == "=") {
        r.hasDefault = true;
        break;
      } else if (t.text == "[") {
        ++r.ty.ptr;
        i = std::min(matchFwd(T, i, e), e);
      } else {
        bad = true;  // `(` function pointer, `<` ...
        break;
      }
      ++i;
      continue;
    }
    bad = true;
    break;
  }
  if (bad || !haveType) {
    r.ty = OvType{};
    r.ty.raw = "?";
    return r;
  }
  const uint8_t ptr = r.ty.ptr;
  const uint8_t ref = r.ty.ref;
  const bool cb = r.ty.constBase, cp = r.ty.constPtr;
  r.ty = OvType{};
  r.ty.ptr = ptr;
  r.ty.ref = ref;
  r.ty.constBase = cb;
  r.ty.constPtr = cp;
  if (isAuto) {
    r.ty.kind = OvType::Generic;
    r.ty.name = "auto";
  } else if (!words.empty() || (!sign.empty() && name.empty())) {
    std::string w = sign.empty() ? words : sign + (words.empty() ? "" : " " + words);
    std::string c = canonPrim(w);
    if (c.empty() || !name.empty()) {
      r.ty = OvType{};
    } else {
      r.ty.kind = OvType::Prim;
      r.ty.name = c;
    }
  } else if (!name.empty()) {
    if (!templated && name.find("::") == std::string::npos &&
        std::find(tparams.begin(), tparams.end(), name) != tparams.end()) {
      r.ty.kind = OvType::Generic;
    } else {
      r.ty.kind = OvType::Named;
      r.ty.targ = targ;
    }
    r.ty.name = name;
  }
  r.ty.raw = rawOf(r.ty);
  if (r.ty.kind == OvType::Unknown) r.ty.raw = "?";
  return r;
}

// ---------------------------------------------------------------------------------------------------------------
// signatures
// ---------------------------------------------------------------------------------------------------------------

// A token that makes a template's viability depend on more than the argument types (SFINAE, constraints).
bool isConstraintToken(const Token& t) {
  if (isK(t, "requires") || isK(t, "decltype") || isK(t, "concept")) return true;
  if (t.kind != Tok::Identifier) return false;
  return t.text == "enable_if" || t.text == "enable_if_t" || t.text == "void_t" || t.text == "conditional_t" ||
         t.text == "requires" || t.text.starts_with("is_") || t.text.ends_with("_v") || t.text == "declval" ||
         t.text == "invoke_result_t" || t.text == "result_of_t";
}

// Tokens before the declaration at `off`: finds `template <...>` and `static` of the declaration the name belongs to.
void readDeclPrefix(std::string_view text, size_t off, const std::vector<size_t>& lineStarts, int line, bool isC,
                    OvSignature& sig) {
  const size_t first = static_cast<size_t>(std::max(0, line - 4));
  size_t from = first < lineStarts.size() ? lineStarts[first] : off;
  if (off - std::min(off, from) > 8192) from = off - 8192;  // very long lines: a bounded window
  if (from >= off) return;
  std::string win(text.substr(from, off - from));
  Toks T = CppLexer(win, isC).all();
  if (!T.empty() && T.back().kind == Tok::End) T.pop_back();
  // the declaration starts after the last `;`, `{`, `}`, access label colon or directive
  size_t start = 0;
  for (size_t k = 0; k < T.size(); ++k) {
    const Token& t = T[k];
    if (t.kind == Tok::Preprocessor || isP(t, ";") || isP(t, "{") || isP(t, "}")) start = k + 1;
    else if (isP(t, ":") && k > 0 &&
             (isK(T[k - 1], "public") || isK(T[k - 1], "private") || isK(T[k - 1], "protected") || isId(T[k - 1])) &&
             k >= 1 && (isK(T[k - 1], "public") || isK(T[k - 1], "private") || isK(T[k - 1], "protected")))
      start = k + 1;
  }
  size_t i = start;
  std::vector<std::vector<std::string>> headers;  // the names each `template <...>` header declares
  while (i < T.size()) {
    if (isK(T[i], "template") && i + 1 < T.size() && isP(T[i + 1], "<")) {
      const size_t after = skipAngleFwd(T, i + 1, T.size());
      if (after == std::string::npos) break;
      headers.emplace_back();
      // names: the identifier before ',' / '=' / '>' of each entry (a `class T` / `typename T` / `int N` entry)
      size_t k = i + 2;
      const size_t end = after - 1;
      size_t entryStart = k;
      int d = 0;
      auto flush = [&](size_t eEnd) {
        std::string nm;
        for (size_t q = entryStart; q < eEnd; ++q) {
          if (isP(T[q], "=")) break;
          if (isId(T[q])) nm = std::string(T[q].text);
        }
        if (!nm.empty()) headers.back().push_back(nm);
      };
      for (; k < end; ++k) {
        if (isP(T[k], "<") || isP(T[k], "(")) ++d;
        else if ((isP(T[k], ">") || isP(T[k], ")")) && d > 0) --d;
        else if (isP(T[k], ",") && d == 0) {
          flush(k);
          entryStart = k + 1;
        }
      }
      flush(end);
      i = after;
      continue;
    }
    if (isK(T[i], "static")) sig.isStatic = true;
    if (isConstraintToken(T[i])) sig.constrained = true;
    ++i;
  }
  // The parameters of the function's own header are deduced from the call. An out-of-line member of a class template
  // (`template <class T> void Stack<T>::push(T&&)`) is declared under the class's header (T is fixed by the class, a
  // `T&&` is no forwarding reference); only a further, inner header belongs to the function.
  const bool qualified = !T.empty() && isP(T.back(), "::");
  if (qualified && !headers.empty()) sig.classTParams = headers.front();
  if (qualified ? headers.size() >= 2 : !headers.empty()) {
    sig.isTemplate = true;
    sig.tparams = headers.back();
  }
}

// After the `operator` keyword at T[i - 1]: the index past the operator-function-id / conversion type, at the
// parameter list's '('.
size_t skipOperatorId(const Toks& T, size_t i) {
  if (i + 1 < T.size() && isP(T[i], "(") && isP(T[i + 1], ")")) {
    i += 2;
  } else if (i + 1 < T.size() && isP(T[i], "[") && isP(T[i + 1], "]")) {
    i += 2;
  } else if (i < T.size() && (isK(T[i], "new") || isK(T[i], "delete"))) {
    ++i;
    if (i + 1 < T.size() && isP(T[i], "[") && isP(T[i + 1], "]")) i += 2;
  } else if (i < T.size() && T[i].kind == Tok::String) {
    ++i;
    if (i < T.size() && isId(T[i])) ++i;
  } else if (i < T.size() && T[i].kind == Tok::Punct && T[i].text != "(" && T[i].text != "::") {
    ++i;
  } else {  // conversion function: the type up to the parameter list
    int angle = 0;
    while (i < T.size()) {
      if (isP(T[i], "<")) ++angle;
      else if (isP(T[i], ">")) angle = std::max(0, angle - 1);
      else if (isP(T[i], ">>")) angle = std::max(0, angle - 2);
      else if (angle == 0 && (isP(T[i], "(") || isP(T[i], ";") || isP(T[i], "{"))) break;
      ++i;
    }
  }
  return i;
}

}  // namespace

OvSignature parseSignature(std::string_view text, int line, int col, std::string_view name, bool isC) {
  OvSignature sig;
  // line starts
  std::vector<size_t> ls{0};
  for (size_t k = 0; k < text.size(); ++k)
    if (text[k] == '\n') ls.push_back(k + 1);
  if (line < 0 || static_cast<size_t>(line) >= ls.size() || col < 0) return sig;
  const size_t off = ls[static_cast<size_t>(line)] + static_cast<size_t>(col);
  if (off >= text.size()) return sig;
  const bool isOperator = name.starts_with("operator");
  const std::string_view want = isOperator ? std::string_view("operator") : name;
  if (want.empty() || text.compare(off, want.size(), want) != 0) return sig;
  std::string win(text.substr(off, std::min<size_t>(text.size() - off, 6000)));
  Toks T = CppLexer(win, isC).all();
  if (!T.empty() && T.back().kind == Tok::End) T.pop_back();
  if (T.empty()) return sig;
  size_t i = 0;
  if (isOperator) {
    if (!isK(T[0], "operator")) return sig;
    i = skipOperatorId(T, 1);
  } else {
    if (name.starts_with("~")) {  // destructors have no parameters to tell apart
      sig.ok = true;
      return sig;
    }
    i = 1;
    if (i < T.size() && isP(T[i], "<")) {
      const size_t after = skipAngleFwd(T, i, T.size());
      if (after == std::string::npos) return sig;
      i = after;
    }
  }
  if (i >= T.size() || !isP(T[i], "(")) return sig;
  const size_t open = i;
  const size_t close = matchFwd(T, open, T.size());
  if (close >= T.size() || !isP(T[close], ")")) return sig;

  // prefix: template header, static
  readDeclPrefix(text, off, ls, line, isC, sig);

  // parameters
  {
    size_t a = open + 1;
    while (a < close) {
      size_t b = a;
      int angle = 0;
      while (b < close) {
        const Token& t = T[b];
        if (t.kind == Tok::Punct && (t.text == "(" || t.text == "[" || t.text == "{")) {
          b = matchFwd(T, b, close) + 1;
          continue;
        }
        if (isP(t, "<") && b > a && isId(T[b - 1])) ++angle;
        else if (isP(t, ">")) angle = std::max(0, angle - 1);
        else if (isP(t, ">>")) angle = std::max(0, angle - 2);
        else if (isP(t, ",") && angle == 0) break;
        ++b;
      }
      const size_t e = std::min(b, close);
      if (e == a + 1 && isP(T[a], "...")) {
        sig.cVariadic = true;
      } else if (e > a) {
        TypeRead r = readType(T, a, e, sig.tparams, true);
        OvParam p;
        p.ty = std::move(r.ty);
        p.hasDefault = r.hasDefault;
        if (r.pack) sig.pack = true;
        // a default value after `=`
        for (size_t k = a; k < e; ++k)
          if (isP(T[k], "=")) p.hasDefault = true;
        // a trailing `...` after a named pack
        if (isP(T[e - 1], "...")) sig.pack = true;
        sig.params.push_back(std::move(p));
      }
      a = b + 1;
    }
  }
  if (sig.params.size() == 1 && sig.params[0].ty.kind == OvType::Prim && sig.params[0].ty.name == "void" &&
      sig.params[0].ty.ptr == 0)
    sig.params.clear();

  for (size_t k = 0; k < close; ++k)
    if (isConstraintToken(T[k])) sig.constrained = true;
  // trailing qualifiers
  for (size_t k = close + 1; k < T.size(); ++k) {
    const Token& t = T[k];
    if (isConstraintToken(t)) sig.constrained = true;
    if (isK(t, "const")) sig.isConst = true;
    else if (isP(t, "&")) sig.refQual = 1;
    else if (isP(t, "&&")) sig.refQual = 2;
    else if (isK(t, "noexcept") || isK(t, "volatile") || isK(t, "throw")) {
      if (k + 1 < T.size() && isP(T[k + 1], "(")) k = matchFwd(T, k + 1, T.size());
    } else if (isId(t) && (t.text == "override" || t.text == "final")) {
      // nothing
    } else if (isP(t, "=") && k + 1 < T.size() && isK(T[k + 1], "default")) {
      sig.isDefaulted = true;
      break;
    } else {
      break;
    }
  }
  sig.ok = true;
  return sig;
}

FileSigs::FileSigs(std::string text, bool isC) : text_(std::move(text)), isC_(isC) {
  lineStarts_.push_back(0);
  for (size_t k = 0; k < text_.size(); ++k)
    if (text_[k] == '\n') lineStarts_.push_back(k + 1);
}

std::shared_ptr<const OvSignature> FileSigs::at(int line, int col, std::string_view name) const {
  const auto key = std::make_pair(line, col);
  {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = sigs_.find(key);
    if (it != sigs_.end()) return it->second;
  }
  auto sig = std::make_shared<OvSignature>(parseSignature(text_, line, col, name, isC_));
  std::lock_guard<std::mutex> lk(mu_);
  if (sigs_.size() > 4096) sigs_.clear();
  sigs_[key] = sig;
  return sig;
}

bool FileSigs::classHasConversionOperator(int line, int col) const {
  const auto key = std::make_pair(line, col);
  {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = conv_.find(key);
    if (it != conv_.end()) return it->second;
  }
  bool result = true;  // unknown: yes
  if (line >= 0 && static_cast<size_t>(line) < lineStarts_.size() && col >= 0) {
    const size_t off = lineStarts_[static_cast<size_t>(line)] + static_cast<size_t>(col);
    if (off < text_.size()) {
      std::string win(std::string_view(text_).substr(off, std::min<size_t>(text_.size() - off, 60000)));
      Toks T = CppLexer(win, isC_).all();
      size_t k = 0;
      while (k < T.size() && !isP(T[k], "{") && !isP(T[k], ";")) ++k;
      if (k < T.size() && isP(T[k], "{")) {
        const size_t close = matchFwd(T, k, T.size());
        if (close < T.size()) {
          result = false;
          for (size_t q = k + 1; q < close; ++q) {
            if (!isK(T[q], "operator") || q + 1 >= close) continue;
            const Token& n = T[q + 1];
            const bool functionId = n.kind == Tok::Punct || n.kind == Tok::String || isK(n, "new") || isK(n, "delete");
            if (!functionId) result = true;  // `operator bool`, `operator std::string`, `operator const char*`
          }
        }
      }
    }
  }
  std::lock_guard<std::mutex> lk(mu_);
  conv_[key] = result;
  return result;
}

std::vector<std::string> FileSigs::templateParamsBefore(int line, int col) const {
  if (line < 0 || static_cast<size_t>(line) >= lineStarts_.size() || col < 0) return {};
  const size_t off = lineStarts_[static_cast<size_t>(line)] + static_cast<size_t>(col);
  if (off >= text_.size()) return {};
  OvSignature tmp;
  readDeclPrefix(text_, off, lineStarts_, line, isC_, tmp);
  return tmp.tparams;
}

std::shared_ptr<const FileSigs> SigCache::get(const std::string& key, uint64_t a, uint64_t b,
                                              const std::function<std::shared_ptr<const FileSigs>()>& build) {
  const bool cache = a != 0 || b != 0;
  if (cache) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = map_.find(key);
    if (it != map_.end() && it->second.a == a && it->second.b == b) {
      it->second.tick = ++tick_;
      return it->second.sigs;
    }
  }
  std::shared_ptr<const FileSigs> built = build();
  if (cache && built) {
    std::lock_guard<std::mutex> lk(mu_);
    if (auto old = map_.find(key); old != map_.end()) {
      bytes_ -= std::min(bytes_, old->second.sigs->size());
      map_.erase(old);
    }
    while (!map_.empty() && (map_.size() >= kMaxEntries || bytes_ + built->size() > kMaxBytes)) {
      auto oldest = map_.begin();
      for (auto it = map_.begin(); it != map_.end(); ++it)
        if (it->second.tick < oldest->second.tick) oldest = it;
      bytes_ -= std::min(bytes_, oldest->second.sigs->size());
      map_.erase(oldest);
    }
    bytes_ += built->size();
    map_[key] = {a, b, built, ++tick_};
  }
  return built;
}

void SigCache::clear() {
  std::lock_guard<std::mutex> lk(mu_);
  map_.clear();
  bytes_ = 0;
}

// ---------------------------------------------------------------------------------------------------------------
// what the index and the files say about the named types
// ---------------------------------------------------------------------------------------------------------------

namespace {

enum class NK { Class, Enum, Opaque };

struct NamedInfo {
  NK kind = NK::Opaque;
  bool project = false;              // a class the index knows (its bases and members are visible)
  std::vector<std::string> closure;  // qualified names of the class and its bases
  bool convOp = true;                // declares / inherits a conversion function (true when unknown)
  bool hasCtor1 = true;              // has a constructor callable with one argument (true when unknown)
};

struct Ctx {
  const ScopeEnv& env;
  const CaretInfo& ci;
  std::map<std::string, NamedInfo> cache;

  Ctx(const ScopeEnv& e, const CaretInfo& c) : env(e), ci(c) {}

  NamedInfo& info(const OvType& t) {
    auto it = cache.find(t.name);
    if (it != cache.end()) return it->second;
    NamedInfo ni;
    const std::string last(lastComp(t.name));
    bool anyClass = false, anyEnum = false, anyTypedef = false;
    std::vector<FastIndex::Match> ctors;
    for (const FastIndex::Match& m : env.index->lookup(last)) {
      switch (m.sym().kind) {
        case SymbolKind::Class:
        case SymbolKind::Struct:
        case SymbolKind::Union: anyClass = true; break;
        case SymbolKind::Enum: anyEnum = true; break;
        case SymbolKind::Typedef: anyTypedef = true; break;
        case SymbolKind::Constructor: ctors.push_back(m); break;
        default: break;
      }
    }
    if (anyClass && !anyEnum) {
      ni.kind = NK::Class;
      ni.closure = classScopesOfQualifier(env, ci, t.name);
      ni.project = !ni.closure.empty();
    } else if (anyEnum && !anyClass) {
      ni.kind = NK::Enum;
    } else if (!anyClass && !anyEnum && !anyTypedef && t.name.starts_with("std::") && !isStdAliasName(last)) {
      ni.kind = NK::Class;  // a standard library class: nothing is known about its members
    }
    if (ni.project) {
      ni.hasCtor1 = false;
      for (const FastIndex::Match& m : ctors) {
        const Symbol& s = m.sym();
        if (s.scope != ni.closure[0]) continue;
        const int lo = s.minArity >= 0 ? s.minArity : s.arity;
        if (s.arity < 0 || (lo <= 1 && (s.variadic || s.arity >= 1))) ni.hasCtor1 = true;
      }
      ni.convOp = false;
      for (const std::string& scope : ni.closure) {
        bool found = false;
        for (const FastIndex::Match& m : env.index->lookup(std::string(lastComp(scope)))) {
          const Symbol& s = m.sym();
          if (!(s.kind == SymbolKind::Class || s.kind == SymbolKind::Struct || s.kind == SymbolKind::Union) ||
              s.qualified() != scope)
            continue;
          found = true;
          auto sigs = env.sigsOf ? env.sigsOf(m.file->file) : nullptr;
          if (!sigs || sigs->classHasConversionOperator(s.line, s.col)) ni.convOp = true;
        }
        if (!found) ni.convOp = true;
      }
    }
    return cache.emplace(t.name, std::move(ni)).first->second;
  }

  // The classes of the std library that a functional cast `std::X(...)` constructs (not functions).
  static bool isStdClassName(std::string_view last) {
    static const std::set<std::string_view> s = {
        "string",        "wstring",       "u8string", "u16string", "u32string", "string_view",
        "wstring_view",  "vector",        "deque",    "list",      "set",       "map",
        "unordered_map", "unordered_set", "array",    "optional",  "path"};
    return s.count(last) > 0;
  }
};

bool sameNamed(const OvType& a, const OvType& b) {
  if (lastComp(a.name) != lastComp(b.name)) return false;
  const bool qa = a.name.find("::") != std::string::npos, qb = b.name.find("::") != std::string::npos;
  if (qa && qb && a.name != b.name && !a.name.ends_with("::" + b.name) && !b.name.ends_with("::" + a.name))
    return false;
  if (!a.targ.empty() && !b.targ.empty() && a.targ != b.targ) return false;
  return true;
}

// ---------------------------------------------------------------------------------------------------------------
// conversion ranks
// ---------------------------------------------------------------------------------------------------------------

// A bound on the rank of converting one argument to one parameter: 0 exact, 1 exact with a qualification added or a
// worse reference binding, 2 promotion, 3 conversion, 4 user-defined conversion, 5 ellipsis. `certain` = the
// conversion is certainly viable; `bad` = it certainly is not. [lo, hi] hold the rank when viable.
struct Pos {
  int lo = 0, hi = 0;
  bool certain = true;
  bool bad = false;
};
Pos fixedPos(int r) { return {r, r, true, false}; }
Pos maybePos(int lo, int hi) { return {lo, hi, false, false}; }
Pos badPos() { return {kInf, kInf, false, true}; }

bool isConstObj(const OvType& t) { return t.ptr == 0 ? t.constBase : t.constPtr; }

Pos ptrToPtr(Ctx& cx, const OvType& A, const OvType& P) {
  // base level
  const bool toVoid = P.kind == OvType::Prim && P.name == "void" && P.ptr == 1;
  Pos base;
  if (toVoid && A.ptr >= 1) {
    base = fixedPos(A.kind == OvType::Prim && A.name == "void" && A.ptr == 1 ? 0 : 3);
  } else if (A.ptr != P.ptr) {
    if (A.kind == OvType::Named && cx.info(A).kind == NK::Opaque) return maybePos(0, 4);
    return badPos();
  } else if (A.kind == OvType::Prim && P.kind == OvType::Prim) {
    base = A.name == P.name ? fixedPos(0) : badPos();
  } else if (A.kind == OvType::Named && P.kind == OvType::Named) {
    if (sameNamed(A, P)) {
      base = fixedPos(0);
    } else {
      NamedInfo& ia = cx.info(A);
      NamedInfo& ip = cx.info(P);
      if (ia.kind == NK::Class && ip.kind == NK::Class) {
        bool derived = false;
        for (const std::string& c : ia.closure) derived = derived || lastComp(c) == lastComp(P.name);
        if (derived) base = fixedPos(3);
        else if (ia.project && ip.project) base = badPos();
        else base = maybePos(3, 3);
      } else if (ia.kind == NK::Opaque || ip.kind == NK::Opaque) {
        base = maybePos(0, 4);
      } else {
        base = badPos();
      }
    }
  } else {  // a builtin and a named type
    const OvType& n = A.kind == OvType::Named ? A : P;
    base = cx.info(n).kind == NK::Opaque ? maybePos(0, 4) : badPos();
  }
  if (base.bad) return base;
  if (A.constBase && !P.constBase && !toVoid) return badPos();
  if (A.constBase && !P.constBase && toVoid) return badPos();
  if (!A.constBase && P.constBase && base.lo == 0 && base.hi == 0) { base.lo = base.hi = 1; }
  return base;
}

Pos primConv(const OvType& A, const OvType& P) {
  if (A.name == "void" || P.name == "void") return badPos();
  if (A.name == P.name) return fixedPos(0);
  if (P.name == "int" && promotesToInt(A.name)) return fixedPos(2);
  if (P.name == "double" && A.name == "float") return fixedPos(2);
  return fixedPos(3);
}

// Converting a value (cv and references aside).
Pos convertValue(Ctx& cx, const OvType& A, const OvType& P) {
  if (P.kind == OvType::Generic) {
    if (P.ptr == 0) return fixedPos(0);
    if (A.kind == OvType::NullPtr) return badPos();
    return A.ptr >= P.ptr ? fixedPos(0) : badPos();
  }
  if (A.kind == OvType::NullPtr) {
    if (P.ptr > 0) return fixedPos(3);
    if (P.kind == OvType::Prim) return badPos();
    NamedInfo& ip = cx.info(P);
    if (ip.kind == NK::Enum) return badPos();
    return ip.project && !ip.hasCtor1 ? badPos() : maybePos(4, 4);
  }
  const bool aPtr = A.ptr > 0, pPtr = P.ptr > 0;
  if (aPtr && pPtr) return ptrToPtr(cx, A, P);
  if (!aPtr && pPtr) {
    if (A.kind == OvType::Prim) return A.zeroLit ? fixedPos(3) : badPos();
    NamedInfo& ia = cx.info(A);
    if (ia.kind == NK::Enum) return badPos();
    if (ia.project && !ia.convOp) return badPos();
    return maybePos(4, 4);
  }
  if (aPtr && !pPtr) {
    if (P.kind == OvType::Prim) return P.name == "bool" ? fixedPos(3) : badPos();
    NamedInfo& ip = cx.info(P);
    if (ip.kind == NK::Enum) return badPos();
    if (ip.project && !ip.hasCtor1) return badPos();
    return ip.kind == NK::Opaque ? maybePos(0, 4) : maybePos(4, 4);
  }
  if (A.kind == OvType::Prim && P.kind == OvType::Prim) return primConv(A, P);
  if (A.kind == OvType::Prim && P.kind == OvType::Named) {
    NamedInfo& ip = cx.info(P);
    if (ip.kind == NK::Enum) return badPos();
    if (ip.kind == NK::Opaque) return maybePos(0, 4);
    return ip.project && !ip.hasCtor1 ? badPos() : maybePos(4, 4);
  }
  if (A.kind == OvType::Named && P.kind == OvType::Prim) {
    NamedInfo& ia = cx.info(A);
    if (ia.kind == NK::Enum) return maybePos(2, 3);  // an unscoped enum converts, a scoped one does not
    if (ia.kind == NK::Opaque) return maybePos(0, 4);
    return ia.project && !ia.convOp ? badPos() : maybePos(4, 4);
  }
  if (A.kind == OvType::Named && P.kind == OvType::Named) {
    if (sameNamed(A, P)) return fixedPos(0);
    NamedInfo& ia = cx.info(A);
    NamedInfo& ip = cx.info(P);
    if (ia.kind == NK::Enum && ip.kind == NK::Enum) return badPos();
    if (ia.kind == NK::Class && ip.kind == NK::Enum) return badPos();
    if (ia.kind == NK::Opaque || ip.kind == NK::Opaque) return maybePos(0, 4);
    if (ia.kind == NK::Class && ip.kind == NK::Class) {
      for (const std::string& c : ia.closure)
        if (lastComp(c) == lastComp(P.name)) return fixedPos(3);
      if (ia.project && ip.project && !ip.hasCtor1 && !ia.convOp) return badPos();
      return maybePos(3, 4);
    }
    // an enum to a class: a converting constructor
    return ip.project && !ip.hasCtor1 ? badPos() : maybePos(4, 4);
  }
  return maybePos(0, kInf);
}

Pos argPos(Ctx& cx, const OvType& A, const OvType& P) {
  if (P.kind == OvType::Unknown) return maybePos(0, kInf);
  if (A.kind == OvType::Unknown) {
    if (P.kind == OvType::Generic && P.ptr == 0 && P.ref != 1) return fixedPos(0);
    return maybePos(0, kInf);
  }
  Pos v = convertValue(cx, A, P);
  if (v.bad || !P.ref) return v;
  const bool pConst = P.ptr == 0 ? P.constBase : P.constPtr;
  const bool aConst = isConstObj(A);
  const bool identity = v.lo == 0 && v.hi == 0;
  if (P.ref == 1 && !pConst) {
    // a non-const lvalue reference binds an lvalue of the same (or a derived) type that is not const
    if (P.kind == OvType::Generic) {
      if (A.cat == Cat::PRValue || A.cat == Cat::XValue) return badPos();
      if (A.cat == Cat::Unknown) v.certain = false;
      return v;
    }
    if (A.cat == Cat::PRValue || A.cat == Cat::XValue) return badPos();
    if (A.cat == Cat::Unknown) v.certain = false;
    if (aConst) return badPos();
    if (!identity) {
      const bool derived = v.lo == 3 && v.hi == 3 && A.kind == OvType::Named && P.kind == OvType::Named;
      if (derived) return v;
      if (!v.certain && A.kind == OvType::Named && P.kind == OvType::Named) return v;
      return badPos();
    }
    return v;
  }
  if (P.ref == 1) {  // `const T &` (or `T *const &`): binds anything convertible
    if (identity) {
      int add = 0;
      if (A.cat == Cat::LValue) add = aConst ? 0 : 1;
      else if (A.cat == Cat::PRValue || A.cat == Cat::XValue) add = 1;
      else v.hi = 1;
      v.lo += add;
      v.hi += add;
    } else if (v.lo >= 3) {
      ++v.lo;  // a conversion makes a temporary the reference binds to: worse than an rvalue reference doing so
      ++v.hi;
    }
    return v;
  }
  // `T &&`
  if (P.kind == OvType::Generic) return v;  // forwarding reference
  if (A.cat == Cat::LValue) {
    if (identity) return badPos();
    return v;  // a conversion makes a temporary
  }
  if (A.cat == Cat::Unknown && identity) v.certain = false;
  return v;
}

// The implicit object parameter of a member function against the receiver.
Pos objPos(const OvType& obj, const OvSignature& sig) {
  if (obj.kind == OvType::Unknown) return maybePos(0, kInf);
  const bool oc = obj.constBase;
  if (oc && !sig.isConst) return badPos();
  Pos r = fixedPos(0);
  if (sig.refQual == 1 && (obj.cat == Cat::PRValue || obj.cat == Cat::XValue)) return badPos();
  if (sig.refQual == 2 && obj.cat == Cat::LValue) return badPos();
  if ((sig.refQual == 1 || sig.refQual == 2) && obj.cat == Cat::Unknown) r.certain = false;
  int add = 0;
  if (!oc && sig.isConst) add += 1;
  if ((obj.cat == Cat::PRValue || obj.cat == Cat::XValue) && sig.refQual != 2) add += 1;
  r.lo += add;
  r.hi += add;
  return r;
}

// ---------------------------------------------------------------------------------------------------------------
// types of expressions
// ---------------------------------------------------------------------------------------------------------------

struct Val {
  bool known = false;
  OvType ty;
};

OvType tyOfVar(const ScanVar& v) {
  OvType t;
  if (!v.prim.empty()) {
    const std::string c = canonPrim(v.prim);
    if (c.empty()) return t;
    t.kind = OvType::Prim;
    t.name = c;
  } else if (!v.type.empty()) {
    if (v.pointer && v.ptrDepth == 0) return t;  // a smart pointer / auto from make_unique: shape unknown
    t.kind = OvType::Named;
    t.name = v.type;
    t.targ = v.targ;
    t.targExact = v.targExact;
  } else {
    return t;
  }
  t.ptr = v.ptrDepth;
  t.constBase = v.constBase;
  t.constPtr = v.ptrDepth > 0 && v.constTop;
  t.cat = Cat::LValue;
  t.raw = rawOf(t);
  return t;
}

}  // namespace

namespace {

class Typer {
 public:
  explicit Typer(Ctx& cx) : cx_(cx) {}

  // The type of the expression in [b, e) when it is one unary / postfix expression; unknown otherwise.
  Val ofRange(const Toks& T, size_t b, size_t e) {
    if (e <= b || nest_ > 6) return {};
    ++nest_;
    size_t i = b;
    Val v = unary(T, i, e);
    --nest_;
    return i == e ? v : Val{};
  }

  // One unary expression starting at `i` (advances `i`); unknown when it is not one this reads.
  Val unary(const Toks& T, size_t& i, size_t e) {
    if (i >= e || depth_ > 40) return {};
    struct Depth {
      int& d;
      explicit Depth(int& x) : d(x) { ++d; }
      ~Depth() { --d; }
    } guard(depth_);
    const Token& t = T[i];
    if (t.kind == Tok::Punct) {
      if (t.text == "-" || t.text == "+" || t.text == "!" || t.text == "&" || t.text == "*" || t.text == "~") {
        const std::string op(t.text);
        ++i;
        Val v = unary(T, i, e);
        if (!v.known) return {};
        Val r;
        if (op == "&") {
          if (v.ty.cat != Cat::LValue || v.ty.ptr >= 200) return {};
          r = v;
          r.ty.ptr = static_cast<uint8_t>(v.ty.ptr + 1);
          r.ty.constPtr = false;
          r.ty.cat = Cat::PRValue;
          r.ty.zeroLit = false;
          r.ty.raw.clear();
          return r;
        }
        if (op == "*") {
          if (v.ty.ptr == 0) return {};
          r = v;
          r.ty.ptr = static_cast<uint8_t>(v.ty.ptr - 1);
          r.ty.cat = Cat::LValue;
          r.ty.zeroLit = false;
          if (r.ty.ptr == 0) r.ty.constPtr = false;
          return r;
        }
        if (v.ty.kind != OvType::Prim || v.ty.ptr != 0) return {};
        r.known = true;
        r.ty.kind = OvType::Prim;
        r.ty.cat = Cat::PRValue;
        if (op == "!") {
          r.ty.name = "bool";
        } else if (promotesToInt(v.ty.name)) {
          r.ty.name = "int";
        } else {
          r.ty.name = v.ty.name;
        }
        return r;
      }
      if (t.text == "++" || t.text == "--") return {};
    }
    if (isK(t, "new")) return newExpr(T, i, e);
    if (isK(t, "sizeof") || isK(t, "alignof") || isK(t, "typeid") || isK(t, "co_await")) return {};
    return postfix(T, i, e);
  }

 private:
  Ctx& cx_;
  int depth_ = 0;  // prefix operators in a row
  int nest_ = 0;   // parenthesised / argument nesting

  static Val val(OvType t) {
    Val v;
    v.known = true;
    t.raw.clear();
    v.ty = std::move(t);
    return v;
  }

  Val newExpr(const Toks& T, size_t& i, size_t e) {
    ++i;  // new
    size_t b = i;
    while (i < e && !isP(T[i], "(") && !isP(T[i], "{") && !isP(T[i], "[") && !isP(T[i], ";")) ++i;
    TypeRead r = readType(T, b, i, {}, false);
    if (r.ty.kind != OvType::Prim && r.ty.kind != OvType::Named) return {};
    // skip the initializer / array bound
    if (i < e && (isP(T[i], "(") || isP(T[i], "{") || isP(T[i], "["))) i = std::min(matchFwd(T, i, e) + 1, e);
    OvType t = r.ty;
    t.ref = 0;
    t.ptr = static_cast<uint8_t>(t.ptr + 1);
    t.constPtr = false;
    t.cat = Cat::PRValue;
    return val(std::move(t));
  }

  Val literal(const Toks& T, size_t& i, size_t e) {
    const Token& t = T[i];
    ScanVar sv;
    if (t.kind == Tok::String) {
      // adjacent string literals concatenate
      size_t j = i;
      bool ok = true;
      while (j < e && T[j].kind == Tok::String) {
        ok = ok && literalVarType(T[j].text, true, false, sv);
        ++j;
      }
      if (!ok) {
        i = j;
        return {};
      }
      i = j;
    } else {
      if (!literalVarType(t.text, false, t.kind == Tok::Char, sv)) {
        ++i;
        return {};
      }
      ++i;
    }
    OvType ty;
    ty.kind = OvType::Prim;
    ty.name = canonPrim(sv.prim);
    if (ty.name.empty()) return {};
    ty.ptr = sv.ptrDepth;
    ty.constBase = sv.constBase;
    ty.cat = Cat::PRValue;
    if (t.kind == Tok::Number && ty.ptr == 0) {
      size_t k = 0;
      while (k < t.text.size() && t.text[k] == '0') ++k;
      ty.zeroLit = k > 0 && t.text.find_first_not_of("0uUlL") == std::string_view::npos && ty.name != "double";
    }
    return val(std::move(ty));
  }

  // `name` (qualified, possibly with template arguments) names a class the index (or the std whitelist) knows and
  // nothing else: a construction `name(...)` / `name{...}` yields a prvalue of it.
  Val construction(const QName& q) {
    OvType t;
    t.kind = OvType::Named;
    t.name = q.name;
    t.targ = q.targ;
    t.cat = Cat::PRValue;
    const std::string last(lastComp(q.name));
    bool cls = false, other = false;
    for (const FastIndex::Match& m : cx_.env.index->lookup(last)) {
      switch (m.sym().kind) {
        case SymbolKind::Class:
        case SymbolKind::Struct:
        case SymbolKind::Union: cls = true; break;
        case SymbolKind::Constructor: break;
        default: other = true; break;
      }
    }
    if (cls && !other) return val(std::move(t));
    if (!cls && !other && q.name.starts_with("std::") && Ctx::isStdClassName(last)) return val(std::move(t));
    return {};
  }

  // The type of the variable / enumerator named `q` (no call).
  Val variable(const QName& q) {
    if (q.name.find("::") == std::string::npos) {
      if (auto v = resolveVariableAt(cx_.env, cx_.ci, q.name)) {
        Val r = val(tyOfVar(*v));
        r.known = r.ty.kind != OvType::Unknown;
        return r;
      }
      return {};
    }
    const std::string last(lastComp(q.name));
    const std::string qual = q.name.substr(0, q.name.size() - last.size() - 2);
    // a scoped enumerator `Flags::A`
    const auto hits = cx_.env.index->lookup(last);
    size_t enumerators = 0, others = 0;
    std::string enumScope;
    for (const FastIndex::Match& m : hits) {
      const Symbol& s = m.sym();
      if (s.kind == SymbolKind::Enumerator && (lastComp(s.scope) == lastComp(qual))) {
        ++enumerators;
        enumScope = s.scope;
      } else if (s.kind != SymbolKind::Enumerator) {
        ++others;
      }
    }
    if (enumerators > 0 && others == 0) {
      OvType t;
      t.kind = OvType::Named;
      t.name = enumScope;
      t.cat = Cat::PRValue;
      return val(std::move(t));
    }
    if (auto v = memberVarOf(cx_.env, cx_.ci, qual, last)) {
      Val r = val(tyOfVar(*v));
      r.known = r.ty.kind != OvType::Unknown;
      return r;
    }
    return {};
  }

  Val postfix(const Toks& T, size_t& i, size_t e) {
    Val v = primary(T, i, e);
    for (;;) {
      if (i >= e) return v;
      const Token& t = T[i];
      if ((isP(t, ".") || isP(t, "->")) && i + 1 < e && isId(T[i + 1])) {
        const bool arrow = t.text == "->";
        const std::string member(T[i + 1].text);
        i += 2;
        if (i < e && isP(T[i], "<")) {  // obj.template f<int>(...) / member template
          const size_t after = skipAngleFwd(T, i, e);
          if (after == std::string::npos) return {};
          i = after;
        }
        if (i < e && isP(T[i], "(")) {  // a method call: its result type is not known
          i = std::min(matchFwd(T, i, e) + 1, e);
          v = {};
          continue;
        }
        if (!v.known || v.ty.kind != OvType::Named) {
          v = {};
          continue;
        }
        if (arrow ? v.ty.ptr != 1 : v.ty.ptr != 0) {
          v = {};
          continue;
        }
        const bool objConst = v.ty.constBase;
        auto field = memberVarOf(cx_.env, cx_.ci, v.ty.name, member);
        if (!field) {
          v = {};
          continue;
        }
        Val r = val(tyOfVar(*field));
        r.known = r.ty.kind != OvType::Unknown;
        if (r.known && objConst && r.ty.ptr == 0) r.ty.constBase = true;
        else if (r.known && objConst) r.ty.constPtr = true;
        v = r;
        continue;
      }
      if (isP(t, "[")) {
        const size_t close = matchFwd(T, i, e);
        if (close >= e) return {};
        i = close + 1;
        if (v.known && v.ty.ptr > 0) {
          v.ty.ptr = static_cast<uint8_t>(v.ty.ptr - 1);
          v.ty.cat = Cat::LValue;
          v.ty.zeroLit = false;
          if (v.ty.ptr == 0) v.ty.constPtr = false;
        } else {
          v = {};
        }
        continue;
      }
      if (isP(t, "(")) {
        i = std::min(matchFwd(T, i, e) + 1, e);
        v = {};
        continue;
      }
      if (isP(t, "++") || isP(t, "--")) {
        ++i;
        if (v.known && v.ty.kind == OvType::Prim && v.ty.ptr == 0) v.ty.cat = Cat::PRValue;
        else v = {};
        continue;
      }
      return v;  // a binary operator or anything else: the caller sees the rest
    }
  }

  Val primary(const Toks& T, size_t& i, size_t e) {
    const Token& t = T[i];
    if (t.kind == Tok::Number || t.kind == Tok::Char || t.kind == Tok::String) return literal(T, i, e);
    if (isK(t, "true") || isK(t, "false")) {
      ++i;
      OvType ty;
      ty.kind = OvType::Prim;
      ty.name = "bool";
      ty.cat = Cat::PRValue;
      return val(std::move(ty));
    }
    if (isK(t, "nullptr")) {
      ++i;
      OvType ty;
      ty.kind = OvType::NullPtr;
      ty.cat = Cat::PRValue;
      return val(std::move(ty));
    }
    if (isK(t, "this")) {
      ++i;
      return {};
    }
    if (isP(t, "(")) {
      const size_t close = matchFwd(T, i, e);
      if (close >= e) {
        i = e;
        return {};
      }
      Val inner = ofRange(T, i + 1, close);
      i = close + 1;
      return inner;
    }
    if (t.kind == Tok::Keyword) {
      // casts: static_cast<T>(x) ...
      if (t.text == "static_cast" || t.text == "const_cast" || t.text == "reinterpret_cast" ||
          t.text == "dynamic_cast") {
        if (i + 1 < e && isP(T[i + 1], "<")) {
          const size_t after = skipAngleFwd(T, i + 1, e);
          if (after != std::string::npos && after < e && isP(T[after], "(")) {
            TypeRead r = readType(T, i + 2, after - 1, {}, false);
            const size_t close = matchFwd(T, after, e);
            i = std::min(close + 1, e);
            if (r.ty.kind != OvType::Prim && r.ty.kind != OvType::Named) return {};
            OvType ty = r.ty;
            ty.cat = ty.ref == 1 ? Cat::LValue : ty.ref == 2 ? Cat::XValue : Cat::PRValue;
            ty.ref = 0;
            return val(std::move(ty));
          }
        }
        i = e;
        return {};
      }
      // functional cast with a builtin type: int(x), double{}, unsigned(n)
      if (isBuiltinWord(t.text)) {
        size_t j = i;
        std::string words;
        while (j < e && T[j].kind == Tok::Keyword && isBuiltinWord(T[j].text)) {
          if (!words.empty()) words += ' ';
          words += T[j].text;
          ++j;
        }
        if (j < e && (isP(T[j], "(") || isP(T[j], "{"))) {
          i = std::min(matchFwd(T, j, e) + 1, e);
          OvType ty;
          ty.kind = OvType::Prim;
          ty.name = canonPrim(words);
          if (ty.name.empty() || ty.name == "void") return {};
          ty.cat = Cat::PRValue;
          return val(std::move(ty));
        }
      }
      i = e;
      return {};
    }
    if (isId(t) || isP(t, "::")) {
      QName q;
      if (!readQName(T, i, e, q)) {
        i = e;
        return {};
      }
      i = q.next;
      if (i < e && (isP(T[i], "(") || isP(T[i], "{"))) {
        const size_t close = matchFwd(T, i, e);
        const size_t open = i;
        i = std::min(close + 1, e);
        if (isP(T[open], "(") && (q.name == "std::move" || q.name == "move") && close < e) {
          Val inner = ofRange(T, open + 1, close);
          if (!inner.known) return {};
          inner.ty.cat = Cat::XValue;
          inner.ty.zeroLit = false;
          return inner;
        }
        return construction(q);
      }
      return variable(q);
    }
    i = e;
    return {};
  }
};

// ---------------------------------------------------------------------------------------------------------------
// the call at the caret
// ---------------------------------------------------------------------------------------------------------------

struct CallText {
  std::string src;  // the text from the word's end on (the caret's line, then the next lines)
  Toks toks;
  size_t open = 0, close = 0;
  std::vector<std::pair<size_t, size_t>> args;  // token ranges
  std::vector<OvType> targs;                    // explicit template arguments `f<int, T *>(`
  bool hasTargs = false;
};

// Splits the tokens between the brackets at `open` / `close` at top-level commas (template argument lists with commas
// are skipped over). The ranges are token indexes.
bool splitArgs(const Toks& T, size_t open, size_t close, std::vector<std::pair<size_t, size_t>>& args) {
  size_t a = open + 1;
  if (a == close) return true;
  for (size_t k = a; k <= close; ++k) {
    if (k == close) {
      args.emplace_back(a, k);
      break;
    }
    const Token& t = T[k];
    if (t.kind == Tok::Punct && (t.text == "(" || t.text == "[" || t.text == "{")) {
      k = matchFwd(T, k, close);
      continue;
    }
    if (isP(t, "<") && k > a && isId(T[k - 1])) {
      const size_t after = skipAngleFwd(T, k, close);
      if (after != std::string::npos && after <= close &&
          (after == close || isP(T[after], "(") || isP(T[after], "{") || isP(T[after], "::") || isP(T[after], ",") ||
           isP(T[after], ")"))) {
        k = after - 1;
        continue;
      }
    }
    if (isP(t, ",")) {
      args.emplace_back(a, k);
      a = k + 1;
    }
  }
  return true;
}

// Splits the call whose name ends at column `wordEnd` of the caret's line. False when no call follows or the
// arguments cannot be split reliably.
bool lexCall(const CaretInfo& ci, size_t wordEnd, CallText& c) {
  if (wordEnd > ci.text.size()) return false;
  c.src.assign(ci.text.substr(wordEnd, 6000));
  int line = ci.at.line;
  for (int extra = 0; extra < 8 && c.src.size() < 6000 && ci.lineAt && line + 1 < ci.lineCount; ++extra) {
    ++line;
    c.src += '\n';
    c.src.append(ci.lineAt(line), 0, 2000);
  }
  c.toks = CppLexer(c.src, false).all();
  if (!c.toks.empty() && c.toks.back().kind == Tok::End) c.toks.pop_back();
  const Toks& T = c.toks;
  size_t i = 0;
  if (ci.word == "operator") i = skipOperatorId(T, 0);  // `a.operator()(1.0)`: the call's parentheses follow the id
  if (i < T.size() && isP(T[i], "<")) {
    const size_t after = skipAngleFwd(T, i, T.size());
    if (after == std::string::npos) return false;
    // explicit template arguments
    size_t a = i + 1;
    const size_t end = after - 1;
    size_t k = a;
    int d = 0;
    for (; k <= end; ++k) {
      if (k < end && (isP(T[k], "<") || isP(T[k], "("))) ++d;
      else if (k < end && (isP(T[k], ">") || isP(T[k], ")")) && d > 0) --d;
      if (k == end || (isP(T[k], ",") && d == 0)) {
        TypeRead r = readType(T, a, k, {}, false);
        c.targs.push_back(r.ty);
        a = k + 1;
      }
    }
    c.hasTargs = true;
    i = after;
  }
  if (i >= T.size() || !isP(T[i], "(")) return false;
  c.open = i;
  c.close = matchFwd(T, i, T.size());
  if (c.close >= T.size() || !isP(T[c.close], ")")) return false;
  return splitArgs(T, c.open, c.close, c.args);
}

// Index of the first token of the postfix expression that ends before token `end`, or npos.
size_t postfixStart(const Toks& T, size_t end) {
  size_t j = end;
  for (int guard = 0; guard < 64; ++guard) {
    if (j == 0) return std::string::npos;
    const Token& t = T[j - 1];
    if (isP(t, ")") || isP(t, "]")) {
      const size_t o = matchBack(T, j - 1);
      if (o == std::string::npos) return std::string::npos;
      j = o;
      if (isP(t, ")") && j > 0 && isP(T[j - 1], ">")) {
        const size_t lt = skipAngleBack(T, j - 1);
        if (lt == std::string::npos) return std::string::npos;
        j = lt;
      }
      if (j == 0) return isP(t, ")") ? 0 : std::string::npos;
      const Token& p = T[j - 1];
      if (isId(p) || isP(p, ")") || isP(p, "]") || isK(p, "this") ||
          (p.kind == Tok::Keyword && (isBuiltinWord(p.text) || p.text.ends_with("_cast"))))
        continue;
      if (isP(t, ")")) return j;  // a parenthesised expression
      return std::string::npos;
    }
    const bool atom = isId(t) || t.kind == Tok::Number || t.kind == Tok::String || t.kind == Tok::Char ||
                      isK(t, "this") || isK(t, "true") || isK(t, "false") || isK(t, "nullptr") ||
                      (t.kind == Tok::Keyword && (isBuiltinWord(t.text) || t.text.ends_with("_cast")));
    if (!atom) return std::string::npos;
    --j;
    if (j > 0 && (isP(T[j - 1], "::") || isP(T[j - 1], ".") || isP(T[j - 1], "->"))) {
      --j;
      if (j == 0 && isP(T[j], "::")) return 0;  // `::name`
      continue;
    }
    return j;
  }
  return std::string::npos;
}

// The receiver of the member call whose name is token `wordIdx` of the line's tokens.
OvType receiverOf(Ctx& cx, const Toks& line, size_t wordIdx) {
  OvType unknown;
  if (wordIdx < 2) return unknown;
  const Token& op = line[wordIdx - 1];
  if (!(isP(op, ".") || isP(op, "->"))) return unknown;
  const bool arrow = op.text == "->";
  const size_t start = postfixStart(line, wordIdx - 1);
  if (start == std::string::npos) return unknown;
  Typer ty(cx);
  Val v = ty.ofRange(line, start, wordIdx - 1);
  if (!v.known || v.ty.kind != OvType::Named) return unknown;
  OvType obj = v.ty;
  if (arrow) {
    if (obj.ptr != 1) return unknown;
    obj.ptr = 0;
    obj.constPtr = false;
    obj.cat = Cat::LValue;
  } else if (obj.ptr != 0) {
    return unknown;
  }
  return obj;
}

bool isFunctionKind(SymbolKind k) {
  return k == SymbolKind::Function || k == SymbolKind::Method || k == SymbolKind::Constructor;
}

struct Cand {
  size_t index = 0;  // into the matches
  std::shared_ptr<const OvSignature> sig;
  bool usable = false;
  bool bad = false;
  bool certain = true;
  bool tmpl = false;
  std::vector<Pos> pos;
  std::vector<std::string> raws;
  std::vector<std::string> bases;  // raws without the reference marks: the same underlying conversion
};

// Substitutes explicit template arguments into a copy of the signature's parameter types.
OvSignature substituted(const OvSignature& sig, const std::vector<OvType>& targs) {
  OvSignature out = sig;
  for (OvParam& p : out.params) {
    if (p.ty.kind != OvType::Generic) continue;
    for (size_t k = 0; k < sig.tparams.size() && k < targs.size(); ++k) {
      if (p.ty.name != sig.tparams[k]) continue;
      const OvType& a = targs[k];
      if (a.kind == OvType::Unknown) {
        p.ty = OvType{};
        p.ty.raw = "?";
        break;
      }
      OvType t = a;
      t.ptr = static_cast<uint8_t>(t.ptr + p.ty.ptr);
      t.ref = p.ty.ref ? p.ty.ref : t.ref;
      t.constBase = t.constBase || (p.ty.ptr == 0 && p.ty.constBase);
      t.raw = rawOf(t);
      p.ty = std::move(t);
      break;
    }
  }
  return out;
}

bool dominates(const Cand& x, const Cand& y) {
  if (!x.usable || !y.usable || x.bad || (x.sig && x.sig->constrained)) return false;
  bool strict = false, allEqual = true, differ = false;
  for (size_t i = 0; i < x.pos.size() && i < y.pos.size(); ++i) {
    if (x.raws[i] == y.raws[i] && x.raws[i] != "?") continue;
    differ = true;
    // an uncertain conversion is no hindrance when both candidates ask for the same one (only the reference differs)
    if (!x.pos[i].certain && !(x.bases[i] == y.bases[i] && x.bases[i] != "?")) return false;
    const Pos &a = x.pos[i], &b = y.pos[i];
    if (a.hi <= b.lo) {
      if (a.hi < b.lo) strict = true;
      if (!(a.lo == a.hi && b.lo == b.hi && a.hi == b.lo && b.certain)) allEqual = false;
      continue;
    }
    return false;
  }
  if (strict) return true;
  if (!allEqual || !differ || !y.certain || y.bad) return false;
  // equal conversions all the way: a non-template beats a template; `T *` is more specialised than `T`
  if (!x.tmpl && y.tmpl) return true;
  if (x.tmpl && y.tmpl && x.sig && y.sig && x.sig->params.size() == y.sig->params.size()) {
    bool anyMore = false, anyLess = false;
    for (size_t i = 0; i < x.sig->params.size(); ++i) {
      const OvType &a = x.sig->params[i].ty, &b = y.sig->params[i].ty;
      if (a.kind == OvType::Generic && b.kind == OvType::Generic) {
        if (a.ptr > b.ptr) anyMore = true;
        if (a.ptr < b.ptr) anyLess = true;
      }
    }
    return anyMore && !anyLess;
  }
  return false;
}

// `raw` of a parameter type without its reference marks and its `const`: what the conversion is to, whatever way the
// parameter binds it.
std::string baseRaw(const OvType& t) {
  OvType b = t;
  b.ref = 0;
  b.constBase = false;
  return rawOf(b);
}

// The type spelled by the first template argument of a variable's declared type ("int", "unsigned long",
// "std::string").
OvType typeOfTarg(const std::string& targ) {
  OvType t;
  bool builtin = !targ.empty();
  size_t i = 0;
  while (i < targ.size()) {
    size_t j = targ.find(' ', i);
    if (j == std::string::npos) j = targ.size();
    builtin = builtin && isBuiltinWord(std::string_view(targ).substr(i, j - i));
    i = j + 1;
  }
  const std::string prim = builtin ? canonPrim(targ) : std::string();
  if (!prim.empty() && prim != "void") {
    t.kind = OvType::Prim;
    t.name = prim;
  } else if (!builtin) {
    t.kind = OvType::Named;
    t.name = targ;
  }
  t.raw = rawOf(t);
  return t;
}

// The name of the first template parameter of the class a member belongs to: from the out-of-line definition's header,
// else from the header before the class itself.
std::string classParam0(Ctx& cx, const FastIndex::Match& m, const OvSignature& sg) {
  if (!sg.classTParams.empty()) return sg.classTParams[0];
  const Symbol& s = m.sym();
  if (s.scope.empty()) return {};
  for (const FastIndex::Match& cm : cx.env.index->lookup(std::string(lastComp(s.scope)))) {
    const Symbol& cs = cm.sym();
    if (!(cs.kind == SymbolKind::Class || cs.kind == SymbolKind::Struct) || cs.qualified() != s.scope) continue;
    auto sigs = cx.env.sigsOf ? cx.env.sigsOf(cm.file->file) : nullptr;
    if (!sigs) continue;
    const std::vector<std::string> names = sigs->templateParamsBefore(cs.line, cs.col);
    if (!names.empty()) return names[0];
  }
  return {};
}

// A copy of `sig` with the class template parameter `name` replaced by `arg` in the parameter types.
OvSignature substitutedClass(const OvSignature& sig, const std::string& name, const OvType& arg) {
  OvSignature out = sig;
  if (arg.kind == OvType::Unknown) return out;
  for (OvParam& p : out.params) {
    if (p.ty.kind != OvType::Named || p.ty.name != name || !p.ty.targ.empty()) continue;
    OvType t = arg;
    t.ptr = static_cast<uint8_t>(t.ptr + p.ty.ptr);
    t.ref = p.ty.ref;
    t.constBase = t.constBase || (p.ty.ptr == 0 && p.ty.constBase);
    t.constPtr = p.ty.constPtr;
    t.raw = rawOf(t);
    p.ty = std::move(t);
  }
  return out;
}

struct Item {
  size_t idx = 0;
  bool reversed = false;  // a rewritten comparison candidate: the operands swapped
};

// Scores `matches` (the candidates `which`) against the receiver and the arguments and returns the survivors. With
// `opMode` the arguments are the operands of an operator, args[0] being the object of a member candidate (and
// `receiver` unused); `allBadIsEmpty` returns nothing when every candidate is certainly not viable (else they all
// stay).
std::vector<Item> selectBest(Ctx& cx, const std::vector<FastIndex::Match>& matches, const std::vector<Item>& which,
                             const OvType* receiver, const std::vector<OvType>& args, const std::vector<OvType>* targs,
                             bool opMode = false, bool allBadIsEmpty = false) {
  std::vector<Cand> cands;
  for (const Item& it : which) {
    Cand c;
    c.index = it.idx;
    const FastIndex::Match& m = matches[it.idx];
    const Symbol& s = m.sym();
    auto sigs = cx.env.sigsOf ? cx.env.sigsOf(m.file->file) : nullptr;
    if (sigs && isFunctionKind(s.kind)) c.sig = sigs->at(s.line, s.col, s.name);
    if (c.sig && c.sig->ok && !c.sig->pack) {
      const OvSignature* sg = c.sig.get();
      if (targs && !targs->empty() && sg->isTemplate) {
        c.sig = std::make_shared<OvSignature>(substituted(*sg, *targs));
        sg = c.sig.get();
      }
      std::vector<OvType> as = args;
      if (it.reversed && as.size() == 2) std::swap(as[0], as[1]);
      c.usable = true;
      c.tmpl = sg->isTemplate;
      const bool member = s.kind == SymbolKind::Method && !sg->isStatic;
      // a member of a class template called through `Stack<int> s`: the class's first parameter is the argument
      if (!opMode && receiver && member && receiver->kind == OvType::Named && receiver->targExact &&
          !receiver->targ.empty()) {
        const std::string tp = classParam0(cx, m, *sg);
        if (!tp.empty()) {
          c.sig = std::make_shared<OvSignature>(substitutedClass(*sg, tp, typeOfTarg(receiver->targ)));
          sg = c.sig.get();
        }
      }
      auto push = [&](Pos p, std::string raw, std::string base) {
        c.pos.push_back(p);
        c.raws.push_back(std::move(raw));
        c.bases.push_back(std::move(base));
      };
      auto objRaw = [&]() {
        return std::string("obj") + (sg->isConst ? "c" : "") + (sg->refQual ? (sg->refQual == 1 ? "&" : "&&") : "");
      };
      const size_t shift = opMode && member ? 1 : 0;  // the object takes args[0]
      if (opMode && member) {
        const OvType& obj = as.empty() ? OvType{} : as[0];
        push(obj.kind == OvType::Prim || obj.kind == OvType::NullPtr || obj.ptr > 0 ? badPos() : objPos(obj, *sg),
             objRaw(), "obj");
      }
      for (size_t i = shift; i < as.size(); ++i) {
        const size_t pi = i - shift;
        if (pi < sg->params.size()) {
          const OvType& pt = sg->params[pi].ty;
          push(argPos(cx, as[i], pt), pt.raw, baseRaw(pt));
        } else if (sg->cVariadic) {
          push(fixedPos(5), "...", "...");
        } else {
          push(badPos(), "?", "?");
        }
      }
      if (!opMode) {
        // too few arguments for the parameters without a default
        size_t required = sg->params.size();
        while (required > 0 && sg->params[required - 1].hasDefault) --required;
        if (args.size() < required) push(badPos(), "n", "n");
      }
      if (!opMode && receiver) {
        Pos p = member ? objPos(*receiver, *sg) : fixedPos(0);
        if (member && receiver->kind == OvType::Named) {
          // a member of another class than the receiver's (or its bases)
          NamedInfo& ni = cx.info(*receiver);
          if (ni.project && std::find(ni.closure.begin(), ni.closure.end(), s.scope) == ni.closure.end()) p = badPos();
        }
        push(p, objRaw(), "obj");
      }
      for (const Pos& p : c.pos) {
        c.bad = c.bad || p.bad;
        c.certain = c.certain && p.certain && !p.bad;
      }
    }
    cands.push_back(std::move(c));
  }
  // certainly non-viable candidates go, unless that would leave nothing
  std::vector<size_t> alive;
  for (size_t i = 0; i < cands.size(); ++i)
    if (!cands[i].bad) alive.push_back(i);
  if (alive.empty()) {
    if (allBadIsEmpty) return {};
    alive.resize(cands.size());
    std::iota(alive.begin(), alive.end(), size_t{0});
  }
  std::vector<Item> out;
  for (size_t i : alive) {
    bool beaten = false;
    for (size_t j : alive)
      if (i != j && dominates(cands[j], cands[i])) {
        beaten = true;
        break;
      }
    if (!beaten) out.push_back(which[i]);
  }
  return out.empty() ? which : out;
}

}  // namespace

std::vector<FastIndex::Match> narrowByOverload(const ScopeEnv& env, const CaretInfo& ci,
                                               std::vector<FastIndex::Match> matches) {
  if (matches.size() < 2 || !env.index || !env.sigsOf) return matches;
  std::vector<size_t> fn;
  for (size_t i = 0; i < matches.size(); ++i)
    if (isFunctionKind(matches[i].sym().kind) && matches[i].sym().kind != SymbolKind::Constructor) fn.push_back(i);
  if (fn.size() < 2) return matches;
  // A prototype and its definition are not overloads of each other: only when some name has two prototypes or two
  // definitions (the candidates already fit the call's argument count) can the argument types tell candidates apart.
  // The common case, a function declared in a header and defined in a source file, costs nothing here.
  {
    std::map<std::string, std::pair<int, int>> groups;  // name -> (prototypes, definitions)
    bool several = false;
    for (size_t i : fn) {
      const Symbol& sy = matches[i].sym();
      auto& g = groups[sy.name];
      (sy.definition ? g.second : g.first) += 1;
      several = several || g.first > 1 || g.second > 1;
    }
    if (!several) return matches;
  }
  CallText call;
  if (!lexCall(ci, ci.wordEnd, call)) return matches;
  Ctx cx(env, ci);
  Typer typer(cx);
  std::vector<OvType> args;
  for (const auto& [b, e] : call.args) {
    Val v = typer.ofRange(call.toks, b, e);
    args.push_back(v.known ? v.ty : OvType{});
  }
  // the receiver of a member call
  OvType receiver;
  bool haveReceiver = false;
  if (accessKindOf(ci) != AccessKind::None) {
    // the text before the word (a bounded window on very long lines), up to and including the word
    const size_t from = ci.wordStart > 2048 ? ci.wordStart - 2048 : 0;
    const std::string_view pre =
        ci.text.substr(from, std::min<size_t>(ci.text.size() - from, ci.wordStart - from + 64));
    Toks line = CppLexer(pre, false).all();
    if (!line.empty() && line.back().kind == Tok::End) line.pop_back();
    for (size_t k = 0; k < line.size(); ++k)
      if (line[k].line == 0 && static_cast<size_t>(line[k].col) + from == ci.wordStart &&
          (isId(line[k]) || isK(line[k], "operator"))) {
        receiver = receiverOf(cx, line, k);
        haveReceiver = receiver.kind != OvType::Unknown;
        break;
      }
  }
  bool anyKnown = haveReceiver;
  for (const OvType& a : args) anyKnown = anyKnown || a.kind != OvType::Unknown;
  if (!anyKnown) return matches;
  std::vector<Item> which;
  for (size_t i : fn) which.push_back({i, false});
  const std::vector<Item> kept =
      selectBest(cx, matches, which, haveReceiver ? &receiver : nullptr, args, call.hasTargs ? &call.targs : nullptr);
  if (kept.size() == fn.size()) return matches;
  std::set<size_t> drop(fn.begin(), fn.end());
  for (const Item& k : kept) drop.erase(k.idx);
  std::vector<FastIndex::Match> out;
  for (size_t i = 0; i < matches.size(); ++i)
    if (!drop.count(i)) out.push_back(std::move(matches[i]));
  return out;
}

namespace {

// Binary operator precedence (larger binds tighter); -1 when `s` is not a binary operator.
int binPrec(std::string_view s, bool* rightAssoc = nullptr) {
  if (rightAssoc) *rightAssoc = false;
  if (s == "->*" || s == ".*") return 14;
  if (s == "*" || s == "/" || s == "%") return 13;
  if (s == "+" || s == "-") return 12;
  if (s == "<<" || s == ">>") return 11;
  if (s == "<=>") return 10;
  if (s == "<" || s == ">" || s == "<=" || s == ">=") return 9;
  if (s == "==" || s == "!=") return 8;
  if (s == "&") return 7;
  if (s == "^") return 6;
  if (s == "|") return 5;
  if (s == "&&") return 4;
  if (s == "||") return 3;
  if (s == "=" || s == "+=" || s == "-=" || s == "*=" || s == "/=" || s == "%=" || s == "^=" || s == "&=" ||
      s == "|=" || s == "<<=" || s == ">>=" || s == "?") {
    if (rightAssoc) *rightAssoc = true;
    return 2;
  }
  if (s == ",") return 1;
  return -1;
}

// A token that ends a value (so a following operator is binary / postfix).
bool valueEnd(const Token& t) {
  return isId(t) || t.kind == Tok::Number || t.kind == Tok::String || t.kind == Tok::Char || isP(t, ")") ||
         isP(t, "]") || isK(t, "this") || isK(t, "true") || isK(t, "false") || isK(t, "nullptr");
}

// Is the caret's line entered inside a /* block comment */? Scans up to 60 lines above it (strings, character
// literals and `//` comments respected); a comment that opens earlier is not seen.
bool insideBlockComment(const CaretInfo& ci) {
  if (!ci.lineAt) return false;
  bool inBlock = false;
  for (int l = std::max(0, ci.at.line - 60); l < ci.at.line && l < ci.lineCount; ++l) {
    const std::string& s = ci.lineAt(l);
    for (size_t i = 0; i < s.size(); ++i) {
      if (inBlock) {
        if (s[i] == '*' && i + 1 < s.size() && s[i + 1] == '/') {
          inBlock = false;
          ++i;
        }
      } else if (s[i] == '/' && i + 1 < s.size() && s[i + 1] == '*') {
        inBlock = true;
        ++i;
      } else if (s[i] == '/' && i + 1 < s.size() && s[i + 1] == '/') {
        break;
      } else if (s[i] == '"' || s[i] == '\'') {
        const char q = s[i];
        for (++i; i < s.size() && s[i] != q; ++i)
          if (s[i] == '\\') ++i;
      }
    }
  }
  return inBlock;
}

// Keeps the candidates the caret's file can see: those in a reachable file, and definitions elsewhere whose
// declaration is in one (the same rule as narrowByScope's step 5). Nothing is dropped when no include graph is known.
void keepReachable(const ScopeEnv& env, const CaretInfo& ci, std::vector<FastIndex::Match>& cands) {
  if (cands.size() < 2 || !env.index) return;
  std::vector<std::filesystem::path> quoteDirs, angleDirs;
  if (env.includeDirsOf) env.includeDirsOf(ci.file, quoteDirs, angleDirs);
  auto reach = env.index->reachableFrom(ci.file, quoteDirs, angleDirs);
  if (!reach) return;
  auto visible = [&](const FastIndex::Match& m) { return reach->count(FastIndex::pathKey(m.file->file)) > 0; };
  auto key = [](const Symbol& s) {
    return s.scope + "\x01" + s.name + "\x01" + std::to_string(static_cast<int>(s.arity));
  };
  std::set<std::string> declared;
  size_t nVisible = 0;
  for (const FastIndex::Match& m : cands)
    if (visible(m)) {
      ++nVisible;
      if (!m.sym().definition) declared.insert(key(m.sym()));
    }
  if (nVisible == 0 || nVisible == cands.size()) return;
  std::erase_if(cands, [&](const FastIndex::Match& m) {
    return !visible(m) && !(m.sym().definition && declared.count(key(m.sym())) > 0);
  });
}

bool arityAccepts(const Symbol& s, size_t n) {
  if (s.arity < 0) return true;
  const int lo = s.minArity >= 0 ? s.minArity : s.arity;
  const int k = static_cast<int>(n);
  return k >= lo && (s.variadic || k <= s.arity);
}

OvType primType(const char* name) {
  OvType t;
  t.kind = OvType::Prim;
  t.name = name;
  t.cat = Cat::PRValue;
  return t;
}

// The namespaces of the operand classes (argument-dependent lookup).
std::set<std::string> adlScopes(Ctx& cx, const std::vector<OvType>& args) {
  std::set<std::string> out;
  for (const OvType& a : args) {
    if (a.kind != OvType::Named) continue;
    for (const std::string& c : cx.info(a).closure) {
      const size_t q = c.rfind("::");
      out.insert(q == std::string::npos ? std::string() : c.substr(0, q));
    }
    const size_t q = a.name.rfind("::");  // an enum: the scope it is written in
    if (cx.info(a).kind == NK::Enum && q != std::string::npos) out.insert(a.name.substr(0, q));
  }
  return out;
}

std::vector<FastIndex::Match> udlMatches(Ctx& cx, const Token& tok, const Token* sfx) {
  std::string_view t = tok.text;
  std::vector<OvType> args;
  std::string suffix;
  if (sfx) {  // `"abc"` `_name`: the lexer keeps the suffix apart
    if (!sfx->text.starts_with("_")) return {};
    suffix = std::string(sfx->text);
  } else if (tok.kind == Tok::Number) {
    const size_t us = t.rfind('_');
    if (us == std::string_view::npos || us == 0) return {};
    suffix = std::string(t.substr(us));
    t = t.substr(0, us);
  } else if (tok.kind == Tok::String || tok.kind == Tok::Char) {
    const size_t q = t.rfind(tok.kind == Tok::String ? '"' : '\'');
    if (q == std::string_view::npos || q + 1 >= t.size() || t[q + 1] != '_') return {};
    suffix = std::string(t.substr(q + 1));
    t = t.substr(0, q + 1);
  } else {
    return {};
  }
  if (tok.kind == Tok::Number) {
    ScanVar sv;
    if (!literalVarType(t, false, false, sv)) return {};
    const std::string c = canonPrim(sv.prim);
    if (c == "float" || c == "double" || c == "long double") args.push_back(primType("long double"));
    else args.push_back(primType("unsigned long long"));
  } else if (tok.kind == Tok::String) {
    OvType p = primType("char");
    p.ptr = 1;
    p.constBase = true;
    args.push_back(p);
    args.push_back(primType("unsigned long"));
  } else {
    args.push_back(primType("char"));
  }
  for (char c : suffix)
    if (!isIdentChar(c)) return {};
  std::vector<FastIndex::Match> cands;
  for (const FastIndex::Match& m : cx.env.index->lookup("operator\"\"" + suffix))
    if (m.sym().kind == SymbolKind::Function) cands.push_back(m);
  if (cands.empty()) return {};
  const std::vector<std::string> scopes = visibleScopesAt(cx.env, cx.ci);
  std::erase_if(cands, [&](const FastIndex::Match& m) {
    return std::find(scopes.begin(), scopes.end(), m.sym().scope) == scopes.end();
  });
  keepReachable(cx.env, cx.ci, cands);
  std::erase_if(cands, [&](const FastIndex::Match& m) { return !arityAccepts(m.sym(), args.size()); });
  if (cands.size() < 2) return cands;
  std::vector<Item> items;
  for (size_t i = 0; i < cands.size(); ++i) items.push_back({i, false});
  std::vector<FastIndex::Match> out;
  for (const Item& it : selectBest(cx, cands, items, nullptr, args, nullptr, false, false))
    out.push_back(cands[it.idx]);
  return out;
}

}  // namespace

std::set<std::string> adlScopesOfCall(const ScopeEnv& env, const CaretInfo& ci) {
  if (!env.index) return {};
  CallText call;
  if (!lexCall(ci, ci.wordEnd, call)) return {};
  Ctx cx(env, ci);
  Typer typer(cx);
  std::vector<OvType> args;
  for (const auto& [b, e] : call.args) {
    Val v = typer.ofRange(call.toks, b, e);
    args.push_back(v.known ? v.ty : OvType{});
  }
  return adlScopes(cx, args);
}

std::vector<FastIndex::Match> operatorTokenMatches(const ScopeEnv& env, const CaretInfo& ci) {
  if (!env.index || !env.sigsOf || ci.wordStart >= ci.text.size() || ci.text.size() > 4096 || insideBlockComment(ci))
    return {};
  Toks line = CppLexer(ci.text, false).all();
  if (!line.empty() && line.back().kind == Tok::End) line.pop_back();
  size_t opIdx = std::string::npos;
  for (size_t k = 0; k < line.size(); ++k) {
    const size_t b = static_cast<size_t>(line[k].col);
    if (line[k].line == 0 && b <= ci.wordStart && ci.wordStart < b + line[k].text.size()) {
      opIdx = k;
      break;
    }
  }
  if (opIdx == std::string::npos) return {};
  const Token& op = line[opIdx];
  Ctx cx(env, ci);
  if (op.kind == Tok::Number || op.kind == Tok::String || op.kind == Tok::Char) {
    const Token* sfx = nullptr;
    if (opIdx + 1 < line.size() && isId(line[opIdx + 1]) &&
        line[opIdx + 1].col == op.col + static_cast<int>(op.text.size()))
      sfx = &line[opIdx + 1];
    return udlMatches(cx, op, sfx);
  }
  if (op.kind != Tok::Punct) return {};

  const std::string_view o = op.text;
  const bool prevValue = opIdx > 0 && valueEnd(line[opIdx - 1]);
  enum class Form { Binary, Prefix, Postfix, Subscript, Call, Arrow } form;
  bool ra = false;
  const int prec = binPrec(o, &ra);
  if (o == "[") form = Form::Subscript;
  else if (o == "(") form = Form::Call;
  else if (o == "->") form = Form::Arrow;
  else if (o == "++" || o == "--") form = prevValue ? Form::Postfix : Form::Prefix;
  else if (o == "!" || o == "~") form = Form::Prefix;
  else if ((o == "+" || o == "-" || o == "*" || o == "&") && !prevValue) form = Form::Prefix;
  else if (prec > 1 && o != "?" && o != "&&" && o != "||" && o != "->*" && o != ".*") form = Form::Binary;
  else return {};
  if (form != Form::Prefix && !prevValue) return {};
  if (form == Form::Binary && prec < 0) return {};

  Typer typer(cx);
  auto leftOperand = [&](size_t& startOut) -> Val {
    startOut = postfixStart(line, opIdx);
    if (startOut == std::string::npos) return {};
    return typer.ofRange(line, startOut, opIdx);
  };
  // Is the left operand just the postfix expression (no tighter operator before it)? `declaration` is set when the
  // tokens before it look like a declaration's type.
  auto leftIsPlain = [&](size_t start, bool* declaration) -> bool {
    if (start == std::string::npos) return false;  // no postfix expression before the operator
    if (start == 0) return true;
    const Token& x = line[start - 1];
    if (isP(x, "(") || isP(x, "[") || isP(x, "{") || isP(x, ",") || isP(x, ";") || isP(x, "}") || isP(x, "?") ||
        isP(x, ":") || isK(x, "return") || isK(x, "throw") || isK(x, "else") || isK(x, "do") || isK(x, "case") ||
        isK(x, "co_return") || isK(x, "co_yield"))
      return true;
    if (x.kind == Tok::Punct) {
      const int xp = binPrec(x.text);
      const bool xBinary = start >= 2 && valueEnd(line[start - 2]);
      if (xp > 0 && xBinary) return xp < prec || (ra && xp <= prec);
      return false;  // a prefix operator: `-a + b`
    }
    if (declaration) *declaration = true;
    return false;
  };

  std::vector<OvType> args;
  std::vector<std::string> names;
  bool membersOnly = false;
  auto leftVal = [&](bool needPlain) -> Val {
    size_t start = 0;
    Val v = leftOperand(start);
    if (needPlain) {
      bool decl = false;
      if (!leftIsPlain(start, &decl)) {
        if (decl && o == "=") return Val{false, {}};
        return {};
      }
    }
    return v;
  };
  auto rightVal = [&]() -> Val {
    size_t i = opIdx + 1;
    Val v = typer.unary(line, i, line.size());
    if (i < line.size() && line[i].kind == Tok::Punct) {
      bool xra = false;
      const int xp = binPrec(line[i].text, &xra);
      if (xp > 0 && (xp > prec || (ra && xp >= prec))) return {};
    }
    return v;
  };
  auto unwrap = [](const Val& v) { return v.known ? v.ty : OvType{}; };

  if (form == Form::Binary) {
    if (o == "=") {
      size_t st = 0;
      Val l = leftOperand(st);
      bool decl = false;
      if (!leftIsPlain(st, &decl) || decl) return {};
      args.push_back(unwrap(l));
    } else {
      args.push_back(unwrap(leftVal(true)));
    }
    args.push_back(unwrap(rightVal()));
    names.push_back("operator" + std::string(o));
    if (o == "<" || o == ">" || o == "<=" || o == ">=") names.push_back("operator<=>");
    if (o == "==" || o == "!=") names.push_back("operator<=>");
    if (o == "!=") names.push_back("operator==");  // C++20: `a != b` is `!(a == b)`
  } else if (form == Form::Prefix) {
    args.push_back(unwrap(rightVal()));
    names.push_back("operator" + std::string(o));
  } else if (form == Form::Postfix) {
    args.push_back(unwrap(leftVal(false)));
    args.push_back(primType("int"));
    names.push_back("operator" + std::string(o));
  } else {
    // subscript / call / arrow: the left operand is an object of class type
    Val l = leftVal(false);
    if (!l.known || l.ty.kind != OvType::Named || l.ty.ptr != 0) return {};
    args.push_back(l.ty);
    membersOnly = true;
    if (form == Form::Arrow) {
      names.push_back("operator->");
    } else {
      const size_t close = matchFwd(line, opIdx, line.size());
      if (close >= line.size() || !(isP(line[close], form == Form::Call ? ")" : "]"))) return {};
      std::vector<std::pair<size_t, size_t>> ranges;
      splitArgs(line, opIdx, close, ranges);
      for (const auto& [b, e] : ranges) args.push_back(unwrap(typer.ofRange(line, b, e)));
      names.push_back(form == Form::Call ? "operator()" : "operator[]");
    }
  }
  // a user-declared operator needs an operand of class or enumeration type
  bool classOperand = false;
  for (const OvType& a : args) classOperand = classOperand || (a.kind == OvType::Named && a.ptr == 0);
  if (!classOperand) return {};

  // candidates by name
  std::vector<FastIndex::Match> cands;
  for (const std::string& n : names)
    for (const FastIndex::Match& m : env.index->lookup(n))
      if (m.sym().kind == SymbolKind::Method || (m.sym().kind == SymbolKind::Function && !membersOnly))
        cands.push_back(m);
  if (cands.empty()) return {};
  const OvType& left = args[0];
  const bool rewritable =
      args.size() == 2 && (o == "==" || o == "!=" || o == "<" || o == ">" || o == "<=" || o == ">=" || o == "<=>");
  const std::vector<std::string> visible = visibleScopesAt(env, ci);
  const std::set<std::string> adl = adlScopes(cx, args);
  std::erase_if(cands, [&](const FastIndex::Match& m) {
    const Symbol& s = m.sym();
    // `<=>` stands in for `==` / `!=` only when defaulted
    if (s.name == "operator<=>" && (o == "==" || o == "!=")) {
      auto sigs = env.sigsOf(m.file->file);
      auto sg = sigs ? sigs->at(s.line, s.col, s.name) : nullptr;
      if (!sg || !sg->ok || !sg->isDefaulted) return true;
    }
    if (s.kind == SymbolKind::Method) {
      // the object's class (or a base): the left operand, or the right one of a rewritten comparison
      auto fits = [&](const OvType& obj) {
        if (obj.kind == OvType::Unknown) return true;
        if (obj.kind != OvType::Named || obj.ptr > 0) return false;
        NamedInfo& ni = cx.info(obj);
        if (ni.kind != NK::Class || !ni.project)
          return false;  // a library class: the project declares none of its members
        return std::find(ni.closure.begin(), ni.closure.end(), s.scope) != ni.closure.end();
      };
      const bool rewrittenName = rewritable && (s.name == "operator==" || s.name == "operator<=>");
      if (!(fits(left) || (rewrittenName && args.size() == 2 && fits(args[1])))) return true;
      return !arityAccepts(s, args.size() - 1);
    }
    if (std::find(visible.begin(), visible.end(), s.scope) == visible.end() && !adl.count(s.scope)) return true;
    return !arityAccepts(s, args.size());
  });
  keepReachable(env, ci, cands);
  if (cands.empty()) return {};
  std::vector<Item> items;
  for (size_t i = 0; i < cands.size(); ++i) {
    items.push_back({i, false});
    const std::string& n = cands[i].sym().name;
    if (rewritable && (n == "operator==" || n == "operator<=>")) items.push_back({i, true});
  }
  const std::vector<Item> kept = selectBest(cx, cands, items, nullptr, args, nullptr, true, true);
  std::vector<FastIndex::Match> out;
  std::set<size_t> seen;
  for (const Item& it : kept)
    if (seen.insert(it.idx).second) out.push_back(cands[it.idx]);
  return out;
}

}  // namespace tf
