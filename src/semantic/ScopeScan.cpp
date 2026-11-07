#include "semantic/ScopeScan.h"

#include <algorithm>
#include <climits>
#include <unordered_set>

#include "semantic/CppLexer.h"

namespace tf {

namespace {

using Toks = std::vector<Token>;

bool isP(const Token& t, std::string_view s) { return t.kind == Tok::Punct && t.text == s; }
bool isK(const Token& t, std::string_view s) { return t.kind == Tok::Keyword && t.text == s; }
bool isId(const Token& t) { return t.kind == Tok::Identifier; }
ScanPos posOf(const Token& t) { return {t.line, t.col}; }

bool isOpen(const Token& t) { return t.kind == Tok::Punct && (t.text == "(" || t.text == "[" || t.text == "{"); }
bool isClose(const Token& t) { return t.kind == Tok::Punct && (t.text == ")" || t.text == "]" || t.text == "}"); }

// Index of the token that closes the group opened at `i` (any bracket kind), or `limit` when there is none before it.
size_t matchClose(const Toks& T, size_t i, size_t limit) {
  int depth = 0;
  for (size_t k = i; k < limit; ++k) {
    if (isOpen(T[k])) ++depth;
    else if (isClose(T[k]) && --depth <= 0) return k;
  }
  return limit;
}

// At a '<': index after the matching '>', or npos when the angle group does not close before a ';', '{' or '}'.
size_t skipAngle(const Toks& T, size_t i, size_t limit) {
  int d = 0;
  for (size_t k = i; k < limit; ++k) {
    const Token& t = T[k];
    if (t.kind != Tok::Punct) continue;
    if (t.text == "<") ++d;
    else if (t.text == ">") --d;
    else if (t.text == ">>") d -= 2;
    else if (t.text == "(" || t.text == "[") k = matchClose(T, k, limit);
    else if (t.text == ";" || t.text == "{" || t.text == "}") return std::string::npos;
    if (d <= 0) return k + 1;
  }
  return std::string::npos;
}

const std::unordered_set<std::string_view>& skippedKeywords() {
  static const std::unordered_set<std::string_view> s = {
      "static", "const",    "constexpr", "consteval", "constinit", "volatile",     "thread_local", "register",
      "inline", "extern",   "mutable",   "typename",  "struct",    "class",        "union",        "enum",
      "signed", "unsigned", "friend",    "virtual",   "explicit",  "__extension__"};
  return s;
}
const std::unordered_set<std::string_view>& builtinTypes() {
  static const std::unordered_set<std::string_view> s = {"int",      "char",     "short",   "long",     "float",
                                                         "double",   "bool",     "void",    "wchar_t",  "char8_t",
                                                         "char16_t", "char32_t", "__int64", "__int128", "_Bool"};
  return s;
}

struct QName {
  std::string name;  // "a::b::C" (template arguments dropped, leading "::" dropped)
  std::string targ;  // first identifier chain in the template arguments of the last template-id
  size_t next = 0;
  bool templated = false;
};

// Reads [::] ident [<args>] { :: ident [<args>] } starting at `i` (below `e`).
bool readQName(const Toks& T, size_t i, size_t e, QName& q) {
  q = QName{};
  if (i < e && isP(T[i], "::")) ++i;
  bool any = false;
  while (i < e) {
    if (!isId(T[i])) break;
    if (!q.name.empty()) q.name += "::";
    q.name += T[i].text;
    any = true;
    ++i;
    if (i < e && isP(T[i], "<")) {
      const size_t after = skipAngle(T, i, e);
      if (after == std::string::npos) return false;
      // first identifier chain inside the arguments
      std::string chain;
      size_t k = i + 1;
      while (k + 1 < after &&
             (isK(T[k], "const") || isK(T[k], "typename") || isK(T[k], "struct") || isK(T[k], "class")))
        ++k;
      if (k + 1 < after && isP(T[k], "::")) ++k;
      while (k + 1 < after && isId(T[k])) {
        if (!chain.empty()) chain += "::";
        chain += T[k].text;
        ++k;
        if (k + 1 < after && isP(T[k], "<")) {
          const size_t inner = skipAngle(T, k, after);
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
    if (i + 1 < e && isP(T[i], "::") && (isId(T[i + 1]))) {
      ++i;
      continue;
    }
    break;
  }
  q.next = i;
  return any;
}

struct Decl {
  ScanVar v;
  size_t term = 0;  // index of the terminator token (== e when the range ends)
  bool isAuto = false;
  bool structured = false;
};

bool isTerminator(const Token& t) {
  return t.kind == Tok::Punct && (t.text == "=" || t.text == "(" || t.text == "{" || t.text == "[" || t.text == "," ||
                                  t.text == ";" || t.text == ":");
}

// Parses "type-specifiers declarator-name" at [b, e) up to the first top-level terminator. False when the tokens are
// not shaped like a declaration (they hold an operator, a call, a keyword that starts a statement...).
bool parseDecl(const Toks& T, size_t b, size_t e, Decl& d) {
  d = Decl{};
  size_t i = b;
  while (i + 1 < e && isP(T[i], "[") && isP(T[i + 1], "[")) i = matchClose(T, i, e) + 1;
  struct Item {
    enum K { Type, Builtin, Ptr } k;
    std::string name, targ;
    bool qualified = false, templated = false;
    size_t idx = 0;
  };
  std::vector<Item> items;
  bool stars = false;
  while (i < e) {
    const Token& t = T[i];
    if (t.kind == Tok::Keyword) {
      if (skippedKeywords().count(t.text)) {
        ++i;
        continue;
      }
      if (t.text == "auto") {
        items.push_back({Item::Builtin, "auto", "", false, false, i});
        d.isAuto = true;
        ++i;
        continue;
      }
      if (builtinTypes().count(t.text)) {
        items.push_back({Item::Builtin, std::string(t.text), "", false, false, i});
        ++i;
        continue;
      }
      if (t.text == "decltype" && i + 1 < e && isP(T[i + 1], "(")) {
        items.push_back({Item::Builtin, "decltype", "", false, false, i});
        d.isAuto = true;
        i = matchClose(T, i + 1, e) + 1;
        continue;
      }
      return false;
    }
    if (t.kind == Tok::Identifier || isP(t, "::")) {
      QName q;
      if (!readQName(T, i, e, q)) return false;
      items.push_back(
          {Item::Type, q.name, q.targ, q.name.find("::") != std::string::npos || isP(t, "::"), q.templated, i});
      i = q.next;
      continue;
    }
    if (t.kind == Tok::Punct) {
      if (t.text == "*") {
        items.push_back({Item::Ptr, "", "", false, false, i});
        stars = true;
        ++i;
        continue;
      }
      if (t.text == "&" || t.text == "&&" || t.text == "...") {
        ++i;
        continue;
      }
      if (isTerminator(t)) break;
    }
    return false;
  }
  d.term = i;
  if (items.empty()) return false;
  // structured binding: auto [a, b] = ...
  if (items.size() == 1 && d.isAuto && d.term < e && isP(T[d.term], "[")) {
    d.structured = true;
    return true;
  }
  const Item& nm = items.back();
  if (nm.k != Item::Type || nm.qualified || nm.templated) return false;
  const Item* ty = nullptr;
  for (size_t k = items.size() - 1; k-- > 0;)
    if (items[k].k != Item::Ptr) {
      ty = &items[k];
      break;
    }
  if (!ty) return false;
  d.v.name = nm.name;
  d.v.at = posOf(T[nm.idx]);
  if (ty->k == Item::Type) {
    d.v.type = ty->name;
    d.v.targ = ty->targ;
  }
  d.v.pointer = stars;
  return true;
}

// From `i` (an '=' / '(' / '{' / '[' terminator, or a ',' ) to the next top-level ',' below `e` (or `e`).
size_t skipInitializer(const Toks& T, size_t i, size_t e) {
  while (i < e) {
    if (isOpen(T[i])) {
      i = matchClose(T, i, e) + 1;
      continue;
    }
    if (isP(T[i], ",")) return i;
    ++i;
  }
  return e;
}

// For `auto x = new Foo(...)` / `= Foo(...)` / `= Foo{...}` / `= std::make_unique<Foo>(...)`: the type of the variable.
void deduceAuto(const Toks& T, size_t term, size_t e, ScanVar& v) {
  if (term >= e || !(isP(T[term], "=") || isP(T[term], "{"))) return;
  size_t i = term + 1;
  bool isNew = false;
  if (isP(T[term], "{")) i = term;  // `auto x{Foo(...)}` is not handled
  if (i < e && isK(T[i], "new")) {
    isNew = true;
    ++i;
  }
  QName q;
  if (i >= e || !readQName(T, i, e, q)) return;
  if (q.next >= e || !(isP(T[q.next], "(") || isP(T[q.next], "{"))) return;
  const std::string& n = q.name;
  auto endsWith = [&](std::string_view s) {
    return n.size() >= s.size() && n.compare(n.size() - s.size(), s.size(), s) == 0;
  };
  if (q.templated && (endsWith("make_unique") || endsWith("make_shared"))) {
    v.type = q.targ;
    v.pointer = true;
    return;
  }
  if (q.templated) return;
  v.type = n;
  v.pointer = isNew;
}

struct Scanner {
  const Toks& T;
  ScopeMap& m;
  std::vector<int> stack;
  std::vector<size_t> savedStmt;  // parallel to m.regions
  std::vector<bool> restoreStmt;  // parallel to m.regions
  size_t stmt = 0;
  int paren = 0;

  Scanner(const Toks& t, ScopeMap& map) : T(t), m(map) {}

  ScanRegion& top() { return m.regions[static_cast<size_t>(stack.back())]; }
  bool inBody() { return top().kind == ScanRegion::Function || top().kind == ScanRegion::Block; }

  int open(ScanRegion::Kind kind, std::string name, ScanPos begin, ScanPos nameAt, size_t i, bool restore = false) {
    ScanRegion r;
    r.kind = kind;
    r.name = std::move(name);
    r.begin = begin;
    r.end = {INT_MAX, 0};
    r.nameAt = nameAt;
    r.parent = stack.back();
    m.regions.push_back(std::move(r));
    savedStmt.push_back(stmt);
    restoreStmt.push_back(restore);
    stack.push_back(static_cast<int>(m.regions.size()) - 1);
    stmt = i + 1;
    paren = 0;
    return stack.back();
  }

  void addVar(ScanRegion& r, const Decl& d) { r.vars.push_back(d.v); }

  // Declaration statement over [b, e): registers the declared names in region `r`.
  void declStatement(ScanRegion& r, size_t b, size_t e, bool allowParen) {
    Decl d;
    if (!parseDecl(T, b, e, d)) return;
    if (d.structured) {
      const size_t close = matchClose(T, d.term, e);
      for (size_t k = d.term + 1; k < close; ++k)
        if (isId(T[k])) {
          Decl s;
          s.v.name = std::string(T[k].text);
          s.v.at = posOf(T[k]);
          addVar(r, s);
        }
      return;
    }
    if (d.term < e && isP(T[d.term], "(") && !allowParen) return;
    if (d.isAuto) deduceAuto(T, d.term, e, d.v);
    addVar(r, d);
    // more declarators: `int a = 1, *b, c[3];`
    size_t j = skipInitializer(T, d.term, e);
    while (j < e && isP(T[j], ",")) {
      ++j;
      bool ptr = false;
      while (j < e && (isP(T[j], "*") || isP(T[j], "&") || isP(T[j], "&&"))) {
        ptr = ptr || isP(T[j], "*");
        ++j;
      }
      if (j >= e || !isId(T[j])) break;
      if (j + 1 < e && !isTerminator(T[j + 1])) break;
      Decl n;
      n.v = d.v;
      n.v.name = std::string(T[j].text);
      n.v.at = posOf(T[j]);
      n.v.pointer = d.v.pointer || ptr;
      if (d.isAuto) {
        n.v.type.clear();
        n.v.targ.clear();
      }
      addVar(r, n);
      j = skipInitializer(T, j + 1, e);
    }
  }

  void paramList(ScanRegion& r, size_t open, size_t close) {
    size_t a = open + 1;
    while (a < close) {
      size_t b = a;
      int angle = 0;
      while (b < close) {
        const Token& t = T[b];
        if (isOpen(t)) {
          b = matchClose(T, b, close) + 1;
          continue;
        }
        if (isP(t, "<") && b > a && isId(T[b - 1])) ++angle;
        else if (isP(t, ">")) angle = std::max(0, angle - 1);
        else if (isP(t, ">>")) angle = std::max(0, angle - 2);
        else if (isP(t, ",") && angle == 0) break;
        ++b;
      }
      size_t e = std::min(b, close);
      size_t dflt = a;
      while (dflt < e && !isP(T[dflt], "=")) {
        if (isOpen(T[dflt])) dflt = matchClose(T, dflt, e);
        ++dflt;
      }
      Decl d;
      if (parseDecl(T, a, std::min(dflt, e), d) && !d.structured) addVar(r, d);
      a = b + 1;
    }
  }

  static bool trailingQualifier(const Token& t) {
    if (t.kind == Tok::Keyword)
      return t.text == "const" || t.text == "noexcept" || t.text == "volatile" || t.text == "mutable" ||
             t.text == "throw" || t.text == "try" || t.text == "requires" || t.text == "constexpr" ||
             t.text == "consteval" || t.text == "alignas";
    if (t.kind == Tok::Identifier) return t.text == "override" || t.text == "final" || t.text == "requires";
    if (t.kind == Tok::Punct)
      return t.text == "{" || t.text == ":" || t.text == "->" || t.text == "=" || t.text == "&" || t.text == "&&" ||
             t.text == ";" || t.text == "[" || t.text == ",";
    return t.kind == Tok::End;
  }

  // The qualified declarator name that ends at token `last` (an identifier), e.g. `Stack<T>::push`.
  std::string declaratorName(size_t first, size_t last, size_t* nameIdx) {
    std::string name;
    size_t k = last;
    *nameIdx = last;
    name = std::string(T[k].text);
    if (k > first && isP(T[k - 1], "~")) {
      name = "~" + name;
      --k;
    }
    while (k >= first + 2 && isP(T[k - 1], "::")) {
      size_t q = k - 2;
      if (isP(T[q], ">")) {  // skip back over <...>
        int d = 0;
        size_t r = q + 1;
        while (r-- > first) {
          if (isP(T[r], ">")) ++d;
          else if (isP(T[r], ">>")) d += 2;
          else if (isP(T[r], "<") && --d <= 0) break;
        }
        if (r <= first || r == static_cast<size_t>(-1) || !isId(T[r - 1])) break;
        q = r - 1;
      }
      if (!isId(T[q])) break;
      name = std::string(T[q].text) + "::" + name;
      k = q;
    }
    return name;
  }

  struct FuncHead {
    bool ok = false;
    std::string name;
    size_t nameIdx = 0, open = 0, close = 0;
  };

  FuncHead functionHead(size_t h0, size_t i) {
    FuncHead f;
    // `=` before any '(' at depth 0: a variable with an initializer, not a function
    size_t k = h0;
    while (k < i) {
      const Token& t = T[k];
      if (isK(t, "operator")) {
        // operator name: up to the parameter list
        size_t j = k + 1;
        if (j + 1 < i && isP(T[j], "(") && isP(T[j + 1], ")")) j += 2;
        else {
          while (j < i && !isP(T[j], "(")) ++j;
        }
        if (j >= i || !isP(T[j], "(")) return f;
        std::string op = "operator";
        for (size_t x = k + 1; x < j; ++x) {
          if (T[x].kind != Tok::Punct && !op.empty() && op.back() != ' ' && x == k + 1) op += ' ';
          op += T[x].text;
          if (T[x].kind != Tok::Punct && x + 1 < j && T[x + 1].kind != Tok::Punct) op += ' ';
        }
        // qualifier before the keyword
        std::string qual;
        if (k >= h0 + 2 && isP(T[k - 1], "::") && isId(T[k - 2])) {
          size_t ni;
          qual = declaratorName(h0, k - 2, &ni) + "::";
        }
        f.ok = true;
        f.name = qual + op;
        f.nameIdx = k;
        f.open = j;
        f.close = matchClose(T, j, i);
        return f;
      }
      if (isP(t, "=")) return f;
      if (isP(t, "<") && k > h0 && isId(T[k - 1])) {  // template-id in the declarator: skip its arguments
        const size_t after = skipAngle(T, k, i);
        if (after != std::string::npos) {
          k = after;
          continue;
        }
      }
      if (isP(t, "(")) {
        const size_t close = matchClose(T, k, i);
        if (close >= i) return f;
        const bool nameBefore = k > h0 && isId(T[k - 1]);
        const bool decorator = close + 1 < i && !trailingQualifier(T[close + 1]) && !isP(T[close + 1], "(");
        if (nameBefore && !decorator) {
          size_t ni;
          f.name = declaratorName(h0, k - 1, &ni);
          f.ok = true;
          f.nameIdx = ni;
          f.open = k;
          f.close = close;
          return f;
        }
        k = close + 1;
        continue;
      }
      if (isOpen(t)) {
        k = matchClose(T, k, i) + 1;
        continue;
      }
      ++k;
    }
    return f;
  }

  void classBases(ScanRegion& r, size_t colon, size_t i) {
    size_t k = colon + 1;
    while (k < i) {
      // one base specifier up to the next top-level ','
      std::string last;
      size_t a = k;
      while (k < i && !isP(T[k], ",")) {
        if (isP(T[k], "<")) {
          const size_t after = skipAngle(T, k, i);
          k = after == std::string::npos ? k + 1 : after;
          continue;
        }
        if (isOpen(T[k])) {
          k = matchClose(T, k, i) + 1;
          continue;
        }
        ++k;
      }
      QName q;
      size_t s = a;
      while (s < k && (isK(T[s], "public") || isK(T[s], "private") || isK(T[s], "protected") || isK(T[s], "virtual")))
        ++s;
      if (s < k && readQName(T, s, k, q)) r.bases.push_back(q.name);
      ++k;
    }
  }

  // The `{` at token `i`: opens a region, or skips an initializer brace. Returns the next token index.
  size_t onOpenBrace(size_t i) {
    size_t h0 = stmt;
    const Token& brace = T[i];
    const ScanPos bpos = posOf(brace);
    const bool body = inBody();
    // header = [h0, i)
    while (h0 + 1 < i && isK(T[h0], "template") && isP(T[h0 + 1], "<")) {
      const size_t after = skipAngle(T, h0 + 1, i);
      if (after == std::string::npos) break;
      h0 = after;
    }
    // unclosed '(' in the header: a lambda or a braced argument inside a call
    if (body && paren > 0) {
      const size_t close = matchClose(T, i, T.size());
      return std::min(close + 1, T.size() - 1);
    }
    // namespace
    {
      size_t k = h0;
      while (k < i && (isK(T[k], "inline") || isP(T[k], "["))) {
        if (isP(T[k], "[")) k = matchClose(T, k, i) + 1;
        else ++k;
      }
      if (!body && k < i && isK(T[k], "namespace")) {
        std::string name;
        for (size_t x = k + 1; x < i; ++x) {
          if (isP(T[x], "[")) {
            x = matchClose(T, x, i);
            continue;
          }
          if (isId(T[x])) name += (name.empty() || name.back() == ':' ? "" : "::") + std::string(T[x].text);
          else if (isP(T[x], "::")) name += "::";
          else if (isK(T[x], "inline")) continue;
          else break;
        }
        const int r = open(ScanRegion::Namespace, name.empty() ? "(anonymous)" : name, bpos, bpos, i);
        (void)r;
        return i + 1;
      }
    }
    // extern "C" {
    if (!body && h0 + 1 < i && isK(T[h0], "extern") && T[h0 + 1].kind == Tok::String && h0 + 2 == i) {
      const int r = open(ScanRegion::Namespace, "", bpos, bpos, i);
      m.regions[static_cast<size_t>(r)].transparent = true;
      return i + 1;
    }
    // enum
    {
      bool hasEnum = false;
      for (size_t k = h0; k < i && !isP(T[k], "(") && !isP(T[k], "="); ++k)
        if (isK(T[k], "enum")) hasEnum = true;
      if (hasEnum) {
        open(ScanRegion::Block, "", bpos, bpos, i);
        return i + 1;
      }
    }
    // class / struct / union
    {
      size_t ck = h0;
      bool found = false;
      for (; ck < i; ++ck) {
        if (isP(T[ck], "(") || isP(T[ck], "=") || isP(T[ck], "{")) break;
        if (isK(T[ck], "class") || isK(T[ck], "struct") || isK(T[ck], "union")) {
          found = true;
          break;
        }
      }
      if (found && !(ck > h0 && isK(T[ck - 1], "enum"))) {
        std::string name;
        Token nameTok = T[ck];
        bool anyParen = false;
        size_t k = ck + 1, colon = i;
        std::string cur;
        bool haveName = false;
        for (; k < i; ++k) {
          const Token& t = T[k];
          if (isP(t, "[")) {
            k = matchClose(T, k, i);
            continue;
          }
          if (isP(t, "(")) {
            // alignas(...) / __declspec(...) / __attribute__((...)) belong to the header; any other call is not a class
            if (k > 0 &&
                (isK(T[k - 1], "alignas") || T[k - 1].text == "__declspec" || T[k - 1].text == "__attribute__" ||
                 isK(T[k - 1], "__declspec") || isK(T[k - 1], "__attribute__") || isK(T[k - 1], "alignas"))) {
              k = matchClose(T, k, i);
              continue;
            }
            anyParen = true;
            break;
          }
          if (isP(t, ":")) {
            colon = k;
            break;
          }
          if (isP(t, "<")) {
            const size_t after = skipAngle(T, k, i);
            if (after != std::string::npos) k = after - 1;
            continue;
          }
          if (isP(t, "::")) {
            if (haveName) cur += "::";
            continue;
          }
          if (isId(t) && t.text != "final") {
            if (!haveName || (cur.size() < 2 || cur.compare(cur.size() - 2, 2, "::") != 0)) cur = std::string(t.text);
            else cur += std::string(t.text);
            haveName = true;
            nameTok = t;
            continue;
          }
          if (isId(t) && t.text == "final") continue;
        }
        if (!anyParen) {
          const int r = open(ScanRegion::Class, haveName ? cur : "(anonymous)", bpos, posOf(nameTok), i);
          if (colon < i) classBases(m.regions[static_cast<size_t>(r)], colon, i);
          return i + 1;
        }
      }
    }
    // function (outside bodies) or lambda (inside)
    if (!body) {
      FuncHead f = functionHead(h0, i);
      if (f.ok) {
        // constructor initializer list: `a{1}, b{2} {` -- the brace after a member name is an initializer
        bool initList = false;
        for (size_t k = f.close + 1; k < i; ++k) {
          if (isOpen(T[k])) {
            k = matchClose(T, k, i);
            continue;
          }
          if (isP(T[k], ":")) {
            initList = true;
            break;
          }
        }
        if (initList && i > 0 && !isP(T[i - 1], ")") && !isP(T[i - 1], "}")) {
          return std::min(matchClose(T, i, T.size()) + 1, T.size() - 1);
        }
        const int r = open(ScanRegion::Function, f.name, posOf(T[f.open]), posOf(T[f.nameIdx]), i);
        paramList(m.regions[static_cast<size_t>(r)], f.open, f.close);
        return i + 1;
      }
    }
    if (body) {
      // lambda: `[...](params) ... {`
      size_t br = h0;
      while (br < i && !isP(T[br], "[")) {
        if (isOpen(T[br])) br = matchClose(T, br, i);
        ++br;
      }
      bool hasEq = false;
      for (size_t k = h0; k < br && k < i; ++k)
        if (isP(T[k], "=")) hasEq = true;
      if (br < i) {
        const size_t rb = matchClose(T, br, i);
        if (rb < i) {
          const int r = open(ScanRegion::Block, "", bpos, bpos, i, hasEq);
          if (rb + 1 < i && isP(T[rb + 1], "(")) {
            const size_t pc = matchClose(T, rb + 1, i);
            if (pc < i) paramList(m.regions[static_cast<size_t>(r)], rb + 1, pc);
          }
          return i + 1;
        }
      }
      // braced initializer: `Foo f{...}`, `x = {...}`, `return Foo{...}`
      if (i > h0) {
        const Token& p = T[i - 1];
        if (isP(p, "=") || isP(p, ",") || isP(p, "(") || isK(p, "return") || isK(p, "co_return") || isId(p) ||
            isP(p, ">") || isP(p, "]")) {
          return std::min(matchClose(T, i, T.size()) + 1, T.size() - 1);
        }
      }
      open(ScanRegion::Block, "", bpos, bpos, i);
      return i + 1;
    }
    // initializer brace at namespace / class scope: `int x{5};`, `Foo f = {1, 2};`, `TF_MACRO {`
    if (i > h0) {
      const Token& p = T[i - 1];
      if (isP(p, "=") || isId(p) || isP(p, "]") || isP(p, ">") || isP(p, ",")) {
        return std::min(matchClose(T, i, T.size()) + 1, T.size() - 1);
      }
    }
    open(ScanRegion::Block, "", bpos, bpos, i);
    return i + 1;
  }

  void onCloseBrace(size_t i) {
    paren = 0;
    if (stack.size() <= 1) {
      stmt = i + 1;
      return;
    }
    const int r = stack.back();
    ScanRegion& reg = m.regions[static_cast<size_t>(r)];
    reg.end = posOf(T[i]);
    reg.closed = true;
    stack.pop_back();
    stmt = restoreStmt[static_cast<size_t>(r)] ? savedStmt[static_cast<size_t>(r)] : i + 1;
  }

  void usingStatement(size_t b, size_t e) {
    ScanRegion& r = top();
    if (b + 1 >= e) return;
    if (isK(T[b + 1], "namespace")) {
      QName q;
      if (readQName(T, b + 2, e, q)) r.usings.push_back({q.name, "", posOf(T[b])});
      return;
    }
    for (size_t k = b + 1; k < e; ++k)
      if (isP(T[k], "=")) return;  // alias-declaration
    size_t s = b + 1;
    if (s < e && isK(T[s], "typename")) ++s;
    QName q;
    if (!readQName(T, s, e, q)) return;
    const size_t c = q.name.rfind("::");
    if (c == std::string::npos) return;
    r.usings.push_back({q.name.substr(0, c), q.name.substr(c + 2), posOf(T[b])});
  }

  void endStatement(size_t b, size_t e) {
    if (b >= e) return;
    const Token& f = T[b];
    ScanRegion& r = top();
    if (isK(f, "using")) {
      usingStatement(b, e);
      return;
    }
    if (isK(f, "namespace") && b + 3 < e + 1 && b + 2 < e && isId(T[b + 1]) && isP(T[b + 2], "=")) {
      QName q;
      if (readQName(T, b + 3, e, q)) r.aliases.push_back({std::string(T[b + 1].text), q.name});
      return;
    }
    if (isK(f, "typedef") || isK(f, "static_assert") || isK(f, "template") || isK(f, "friend")) return;
    const bool body = inBody();
    declStatement(r, b, e, body);
  }

  void controlDecl(size_t i) {
    // if / for / while / switch / catch ( init ... )
    if (i + 1 >= T.size() || !isP(T[i + 1], "(")) return;
    const size_t close = matchClose(T, i + 1, T.size());
    if (close >= T.size()) return;
    size_t e = i + 2;
    while (e < close) {
      if (isOpen(T[e])) {
        e = matchClose(T, e, close) + 1;
        continue;
      }
      if (isP(T[e], ";") || isP(T[e], ":")) break;
      ++e;
    }
    declStatement(top(), i + 2, std::min(e, close), false);
  }

  void run() {
    stack.push_back(0);
    savedStmt.push_back(0);
    restoreStmt.push_back(false);
    const size_t n = T.size();
    size_t i = 0;
    while (i < n && T[i].kind != Tok::End) {
      const Token& t = T[i];
      if (t.kind == Tok::Preprocessor) {
        if (paren == 0) stmt = i + 1;
        ++i;
        continue;
      }
      if (t.kind == Tok::Punct) {
        if (t.text == "(") ++paren;
        else if (t.text == ")") paren = std::max(0, paren - 1);
        else if (t.text == "{") {
          i = onOpenBrace(i);
          continue;
        } else if (t.text == "}") {
          onCloseBrace(i);
        } else if (t.text == ";") {
          if (paren == 0) {
            endStatement(stmt, i);
            stmt = i + 1;
          }
        } else if (t.text == ":" && paren == 0) {
          if (i == stmt + 1 && (isK(T[stmt], "public") || isK(T[stmt], "private") || isK(T[stmt], "protected")))
            stmt = i + 1;
          else if (i > stmt && inBody() && (isK(T[stmt], "case") || isK(T[stmt], "default"))) stmt = i + 1;
        }
      } else if (t.kind == Tok::Keyword && inBody() &&
                 (t.text == "if" || t.text == "for" || t.text == "while" || t.text == "switch" || t.text == "catch")) {
        controlDecl(i);
      }
      ++i;
    }
  }
};

}  // namespace

int ScopeMap::regionAt(ScanPos p) const {
  if (regions.size() <= 1) return 0;
  // last region whose begin is before p
  size_t lo = 1, hi = regions.size();
  while (lo < hi) {
    const size_t mid = (lo + hi) / 2;
    if (regions[mid].begin < p) lo = mid + 1;
    else hi = mid;
  }
  int r = static_cast<int>(lo) - 1;
  if (r < 1) return 0;
  while (r > 0) {
    const ScanRegion& g = regions[static_cast<size_t>(r)];
    if (g.begin < p && p < g.end) return r;
    r = g.parent;
  }
  return 0;
}

ScopeMap scanScopes(std::string_view text, bool isC, size_t maxBytes) {
  ScopeMap m;
  ScanRegion root;
  root.kind = ScanRegion::Namespace;
  root.end = {INT_MAX, 0};
  root.parent = -1;
  m.regions.push_back(std::move(root));
  if (text.size() > maxBytes) return m;
  const Toks T = CppLexer(text, isC).all();
  Scanner s(T, m);
  s.run();
  return m;
}

}  // namespace tf
