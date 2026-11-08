#include "semantic/PpExpr.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <vector>

namespace tf::pp {

namespace {

bool isIdStart(unsigned char c) { return std::isalpha(c) || c == '_' || c == '$' || c >= 0x80; }
bool isIdChar(unsigned char c) { return std::isalnum(c) || c == '_' || c == '$' || c >= 0x80; }

// ---- values
// ----------------------------------------------------------------------------------------------------------

struct Value {
  bool known = false;
  bool u = false;  // unsigned (uintmax_t)
  uint64_t b = 0;  // two's complement bits
};

Value known(uint64_t bits, bool u = false) { return {true, u, bits}; }
Value unknownValue() { return {}; }
Value fromTri(Tri t) { return t == Tri::Unknown ? unknownValue() : known(t == Tri::True ? 1 : 0); }
Tri truth(const Value& v) { return v.known ? triOf(v.b != 0) : Tri::Unknown; }

// ---- tokens
// ----------------------------------------------------------------------------------------------------------

enum class Op : uint8_t {
  LNot,
  BNot,
  Plus,
  Minus,
  Mul,
  Div,
  Mod,
  Shl,
  Shr,
  Lt,
  Le,
  Gt,
  Ge,
  Eq,
  Ne,
  BAnd,
  BXor,
  BOr,
  LAnd,
  LOr,
  Quest,
  Colon,
  LParen,
  RParen,
  Comma,
  Assign
};

struct PTok {
  enum Kind : uint8_t { Num, Ident, Operator, Str, Other, End } kind = End;
  Op op = Op::LNot;
  std::string_view text;
  Value value;  // Num (known = false: a number that cannot be evaluated: floating point, bad suffix, too big)
};

constexpr size_t kMaxTokens = 4096;
constexpr int kMaxDepth = 200;

int digitValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return 99;
}

// An integer constant (pp-number text); unknown value for floating point, bad suffixes and values over 64 bits.
Value parseNumber(std::string_view t) {
  std::string s;
  s.reserve(t.size());
  for (char c : t)
    if (c != '\'') s += c;
  size_t i = 0;
  int base = 10;
  if (s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
    base = 16;
    i = 2;
  } else if (s.size() >= 2 && s[0] == '0' && (s[1] == 'b' || s[1] == 'B')) {
    base = 2;
    i = 2;
  } else if (s.size() >= 2 && s[0] == '0') {
    base = 8;
    i = 1;
  }
  uint64_t v = 0;
  size_t digits = 0;
  bool overflow = false;
  for (; i < s.size(); ++i) {
    const int d = digitValue(s[i]);
    // a hex digit letter is never a suffix; in the other bases the first letter ends the digits
    if (d >= base) break;
    if (__builtin_mul_overflow(v, static_cast<uint64_t>(base), &v) ||
        __builtin_add_overflow(v, static_cast<uint64_t>(d), &v))
      overflow = true;
    ++digits;
  }
  if (digits == 0 && !(base == 8 && s == "0")) {
    if (!(base == 8 && i == 1)) return unknownValue();
  }
  std::string suffix = s.substr(i);
  for (char& c : suffix) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  bool u = false;
  if (suffix.empty()) {
  } else if (suffix == "u" || suffix == "ul" || suffix == "lu" || suffix == "ull" || suffix == "llu" ||
             suffix == "uz" || suffix == "zu") {
    u = true;
  } else if (suffix == "l" || suffix == "ll" || suffix == "z") {
  } else {
    return unknownValue();  // floating point (`1.5`, `1e3`, `0x1p3`), user-defined suffix, junk
  }
  if (overflow) return unknownValue();
  if (!u && v > static_cast<uint64_t>(INT64_MAX)) u = true;
  return known(v, u);
}

// 'a', '\n', '\x41', L'a' ... ASCII only; anything else is Unknown.
Value parseChar(std::string_view t) {
  size_t i = 0;
  while (i < t.size() && t[i] != '\'') ++i;
  if (i >= t.size() || t.back() != '\'' || t.size() < i + 3) return unknownValue();
  std::string_view body = t.substr(i + 1, t.size() - i - 2);
  if (body.empty()) return unknownValue();
  uint64_t v = 0;
  if (body[0] != '\\') {
    if (body.size() != 1 || static_cast<unsigned char>(body[0]) >= 0x80) return unknownValue();
    v = static_cast<unsigned char>(body[0]);
  } else {
    if (body.size() < 2) return unknownValue();
    switch (body[1]) {
      case 'n': v = 10; break;
      case 't': v = 9; break;
      case 'r': v = 13; break;
      case 'a': v = 7; break;
      case 'b': v = 8; break;
      case 'f': v = 12; break;
      case 'v': v = 11; break;
      case '\\': v = '\\'; break;
      case '\'': v = '\''; break;
      case '"': v = '"'; break;
      case '?': v = '?'; break;
      case 'x': {
        if (body.size() < 3 || body.size() > 4) return unknownValue();
        for (size_t k = 2; k < body.size(); ++k) {
          const int d = digitValue(body[k]);
          if (d > 15) return unknownValue();
          v = v * 16 + static_cast<uint64_t>(d);
        }
        break;
      }
      default: {
        if (body[1] < '0' || body[1] > '7' || body.size() > 4) return unknownValue();
        for (size_t k = 1; k < body.size(); ++k) {
          if (body[k] < '0' || body[k] > '7') return unknownValue();
          v = v * 8 + static_cast<uint64_t>(body[k] - '0');
        }
      }
    }
  }
  if (v >= 0x80) return unknownValue();  // signedness of plain char is implementation-defined
  return known(v);
}

// Tokens of an expression; false when it has more than kMaxTokens.
bool tokenize(std::string_view s, std::vector<PTok>& out) {
  size_t i = 0;
  auto push = [&](PTok t) {
    out.push_back(t);
    return out.size() <= kMaxTokens;
  };
  auto op = [&](Op o, size_t n) {
    PTok t;
    t.kind = PTok::Operator;
    t.op = o;
    t.text = s.substr(i, n);
    i += n;
    return push(t);
  };
  while (i < s.size()) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v') {
      ++i;
      continue;
    }
    const char c1 = i + 1 < s.size() ? s[i + 1] : '\0';
    if (isIdStart(c)) {
      size_t e = i + 1;
      while (e < s.size() && isIdChar(static_cast<unsigned char>(s[e]))) ++e;
      const std::string_view word = s.substr(i, e - i);
      if (e < s.size() && (s[e] == '\'' || s[e] == '"') &&
          (word == "L" || word == "u" || word == "U" || word == "u8")) {
        // prefixed character / string literal
        const char q = s[e];
        size_t k = e + 1;
        while (k < s.size() && s[k] != q) k += s[k] == '\\' ? 2 : 1;
        k = std::min(k + 1, s.size());
        PTok t;
        t.kind = q == '\'' ? PTok::Num : PTok::Str;
        t.text = s.substr(i, k - i);
        if (q == '\'') t.value = parseChar(t.text);
        i = k;
        if (!push(t)) return false;
        continue;
      }
      struct Alt {
        std::string_view w;
        Op o;
      };
      static constexpr Alt kAlt[] = {{"and", Op::LAnd},  {"or", Op::LOr},   {"not", Op::LNot},   {"bitand", Op::BAnd},
                                     {"bitor", Op::BOr}, {"xor", Op::BXor}, {"compl", Op::BNot}, {"not_eq", Op::Ne}};
      bool alt = false;
      for (const Alt& a : kAlt)
        if (a.w == word) {
          PTok t;
          t.kind = PTok::Operator;
          t.op = a.o;
          t.text = word;
          i = e;
          if (!push(t)) return false;
          alt = true;
          break;
        }
      if (alt) continue;
      PTok t;
      t.kind = PTok::Ident;
      t.text = word;
      i = e;
      if (!push(t)) return false;
      continue;
    }
    if (std::isdigit(c) || (c == '.' && std::isdigit(static_cast<unsigned char>(c1)))) {
      size_t e = i + 1;
      while (e < s.size()) {
        const char ch = s[e];
        if ((ch == '+' || ch == '-') && (s[e - 1] == 'e' || s[e - 1] == 'E' || s[e - 1] == 'p' || s[e - 1] == 'P')) {
          ++e;
        } else if (isIdChar(static_cast<unsigned char>(ch)) || ch == '.' || ch == '\'') {
          ++e;
        } else {
          break;
        }
      }
      PTok t;
      t.kind = PTok::Num;
      t.text = s.substr(i, e - i);
      t.value = parseNumber(t.text);
      i = e;
      if (!push(t)) return false;
      continue;
    }
    if (c == '\'') {
      size_t k = i + 1;
      while (k < s.size() && s[k] != '\'') k += s[k] == '\\' ? 2 : 1;
      k = std::min(k + 1, s.size());
      PTok t;
      t.kind = PTok::Num;
      t.text = s.substr(i, k - i);
      t.value = parseChar(t.text);
      i = k;
      if (!push(t)) return false;
      continue;
    }
    if (c == '"') {
      size_t k = i + 1;
      while (k < s.size() && s[k] != '"') k += s[k] == '\\' ? 2 : 1;
      k = std::min(k + 1, s.size());
      PTok t;
      t.kind = PTok::Str;
      t.text = s.substr(i, k - i);
      i = k;
      if (!push(t)) return false;
      continue;
    }
    bool ok = true;
    switch (c) {
      case '!': ok = c1 == '=' ? op(Op::Ne, 2) : op(Op::LNot, 1); break;
      case '~': ok = op(Op::BNot, 1); break;
      case '+': ok = op(Op::Plus, 1); break;
      case '-': ok = op(Op::Minus, 1); break;
      case '*': ok = op(Op::Mul, 1); break;
      case '/': ok = op(Op::Div, 1); break;
      case '%': ok = op(Op::Mod, 1); break;
      case '<': ok = c1 == '<' ? op(Op::Shl, 2) : (c1 == '=' ? op(Op::Le, 2) : op(Op::Lt, 1)); break;
      case '>': ok = c1 == '>' ? op(Op::Shr, 2) : (c1 == '=' ? op(Op::Ge, 2) : op(Op::Gt, 1)); break;
      case '=': ok = c1 == '=' ? op(Op::Eq, 2) : op(Op::Assign, 1); break;
      case '&': ok = c1 == '&' ? op(Op::LAnd, 2) : op(Op::BAnd, 1); break;
      case '|': ok = c1 == '|' ? op(Op::LOr, 2) : op(Op::BOr, 1); break;
      case '^': ok = op(Op::BXor, 1); break;
      case '?': ok = op(Op::Quest, 1); break;
      case ':': ok = op(Op::Colon, 1); break;
      case '(': ok = op(Op::LParen, 1); break;
      case ')': ok = op(Op::RParen, 1); break;
      case ',': ok = op(Op::Comma, 1); break;
      default: {
        PTok t;
        t.kind = PTok::Other;
        t.text = s.substr(i, 1);
        ++i;
        ok = push(t);
      }
    }
    if (!ok) return false;
  }
  return true;
}

// ---- parser / evaluator
// ----------------------------------------------------------------------------------------------

int precedence(Op o) {
  switch (o) {
    case Op::LOr: return 1;
    case Op::LAnd: return 2;
    case Op::BOr: return 3;
    case Op::BXor: return 4;
    case Op::BAnd: return 5;
    case Op::Eq:
    case Op::Ne: return 6;
    case Op::Lt:
    case Op::Le:
    case Op::Gt:
    case Op::Ge: return 7;
    case Op::Shl:
    case Op::Shr: return 8;
    case Op::Plus:
    case Op::Minus: return 9;
    case Op::Mul:
    case Op::Div:
    case Op::Mod: return 10;
    default: return 0;
  }
}

Value binary(Op o, const Value& a, const Value& b) {
  if (o == Op::LAnd) return fromTri(triAnd(truth(a), truth(b)));
  if (o == Op::LOr) return fromTri(triOr(truth(a), truth(b)));
  if (!a.known || !b.known) return unknownValue();
  const bool u = a.u || b.u;  // usual arithmetic conversions
  const uint64_t x = a.b, y = b.b;
  const int64_t sx = static_cast<int64_t>(x), sy = static_cast<int64_t>(y);
  auto cmp = [&](bool us, bool ss) { return known((u ? us : ss) ? 1 : 0); };
  switch (o) {
    case Op::Mul: return known(x * y, u);
    case Op::Plus: return known(x + y, u);
    case Op::Minus: return known(x - y, u);
    case Op::Div:
      if (y == 0) return unknownValue();
      if (u) return known(x / y, true);
      if (sx == INT64_MIN && sy == -1) return unknownValue();
      return known(static_cast<uint64_t>(sx / sy));
    case Op::Mod:
      if (y == 0) return unknownValue();
      if (u) return known(x % y, true);
      if (sx == INT64_MIN && sy == -1) return unknownValue();
      return known(static_cast<uint64_t>(sx % sy));
    case Op::Shl:
    case Op::Shr: {
      // the result has the type of the left operand; a negative or too large count is undefined
      if (!b.u && sy < 0) return unknownValue();
      if (y >= 64) return unknownValue();
      if (o == Op::Shl) return known(x << y, a.u);
      if (a.u) return known(x >> y, true);
      return known(static_cast<uint64_t>(sx >> y));
    }
    case Op::Lt: return cmp(x < y, sx < sy);
    case Op::Le: return cmp(x <= y, sx <= sy);
    case Op::Gt: return cmp(x > y, sx > sy);
    case Op::Ge: return cmp(x >= y, sx >= sy);
    case Op::Eq: return known(x == y ? 1 : 0);
    case Op::Ne: return known(x != y ? 1 : 0);
    case Op::BAnd: return known(x & y, u);
    case Op::BXor: return known(x ^ y, u);
    case Op::BOr: return known(x | y, u);
    default: return unknownValue();
  }
}

class Parser {
 public:
  Parser(const std::vector<PTok>& toks, const MacroLookup& lookup, const HasIncludeFn* hasInclude)
      : t_(toks), lookup_(lookup), hasInclude_(hasInclude) {}

  // nullopt = the expression is malformed (callers answer Unknown)
  std::optional<Value> run() {
    if (t_.empty()) return std::nullopt;
    Value v = ternary();
    if (bad_ || i_ < t_.size()) return std::nullopt;
    return v;
  }

 private:
  const PTok* peek() const { return i_ < t_.size() ? &t_[i_] : nullptr; }
  bool isOp(Op o) const {
    const PTok* t = peek();
    return t && t->kind == PTok::Operator && t->op == o;
  }
  bool enter() {
    if (++depth_ > kMaxDepth) bad_ = true;
    return !bad_;
  }

  Value ternary() {
    if (!enter()) return unknownValue();
    Value c = binaryExpr(1);
    if (!bad_ && isOp(Op::Quest)) {
      ++i_;
      Value a = ternary();
      if (!isOp(Op::Colon)) {
        bad_ = true;
        --depth_;
        return unknownValue();
      }
      ++i_;
      Value b = ternary();
      --depth_;
      if (bad_) return unknownValue();
      if (c.known) {
        Value r = c.b != 0 ? a : b;
        // the result has the common type of both branches
        if (r.known && (a.u || b.u)) r.u = true;
        return r;
      }
      if (a.known && b.known && a.b == b.b) return known(a.b, a.u || b.u);
      return unknownValue();
    }
    --depth_;
    return c;
  }

  Value binaryExpr(int minPrec) {
    Value lhs = unary();
    while (!bad_) {
      const PTok* t = peek();
      if (!t || t->kind != PTok::Operator) break;
      const int p = precedence(t->op);
      if (p == 0 || p < minPrec) break;
      const Op o = t->op;
      ++i_;
      Value rhs = binaryExpr(p + 1);
      lhs = binary(o, lhs, rhs);
    }
    return lhs;
  }

  Value unary() {
    if (!enter()) return unknownValue();
    Value r = unaryInner();
    --depth_;
    return r;
  }

  Value unaryInner() {
    const PTok* t = peek();
    if (!t) {
      bad_ = true;
      return unknownValue();
    }
    if (t->kind == PTok::Operator) {
      switch (t->op) {
        case Op::LNot: {
          ++i_;
          Value v = unary();
          return fromTri(triNot(truth(v)));
        }
        case Op::BNot: {
          ++i_;
          Value v = unary();
          return v.known ? known(~v.b, v.u) : v;
        }
        case Op::Plus: ++i_; return unary();
        case Op::Minus: {
          ++i_;
          Value v = unary();
          return v.known ? known(0 - v.b, v.u) : v;
        }
        case Op::LParen: {
          ++i_;
          Value v = ternary();
          if (!isOp(Op::RParen)) {
            bad_ = true;
            return unknownValue();
          }
          ++i_;
          return v;
        }
        default: bad_ = true; return unknownValue();
      }
    }
    if (t->kind == PTok::Num) {
      ++i_;
      return t->value;
    }
    if (t->kind == PTok::Ident) return identifier();
    bad_ = true;  // string literal or stray character
    return unknownValue();
  }

  // Skips a balanced `( ... )` starting at the current token; false when it is not balanced.
  bool skipParens() {
    if (!isOp(Op::LParen)) return false;
    int depth = 0;
    while (i_ < t_.size()) {
      if (t_[i_].kind == PTok::Operator) {
        if (t_[i_].op == Op::LParen) ++depth;
        else if (t_[i_].op == Op::RParen && --depth == 0) {
          ++i_;
          return true;
        }
      }
      ++i_;
    }
    return false;
  }

  Value identifier() {
    const std::string_view name = t_[i_].text;
    ++i_;
    if (name == "defined") {
      bool paren = false;
      if (isOp(Op::LParen)) {
        paren = true;
        ++i_;
      }
      const PTok* id = peek();
      if (!id || id->kind != PTok::Ident) {
        bad_ = true;
        return unknownValue();
      }
      const std::string_view target = id->text;
      ++i_;
      if (paren) {
        if (!isOp(Op::RParen)) {
          bad_ = true;
          return unknownValue();
        }
        ++i_;
      }
      switch (lookup_(target).kind) {
        case MacroState::Kind::Undefined: return known(0);
        case MacroState::Kind::Defined:
        case MacroState::Kind::Int: return known(1);
        default: return unknownValue();
      }
    }
    if (name == "true") return known(1);
    if (name == "false") return known(0);
    if (name == "__has_include" && isOp(Op::LParen)) {
      const size_t open = i_;
      if (!skipParens()) {
        bad_ = true;
        return unknownValue();
      }
      if (!hasInclude_ || !*hasInclude_) return unknownValue();
      const char* b = t_[open].text.data() + 1;
      const char* e = t_[i_ - 1].text.data();
      std::string_view raw(b, static_cast<size_t>(e - b));
      while (!raw.empty() && (raw.front() == ' ' || raw.front() == '\t')) raw.remove_prefix(1);
      while (!raw.empty() && (raw.back() == ' ' || raw.back() == '\t')) raw.remove_suffix(1);
      if (raw.size() >= 3 && ((raw.front() == '"' && raw.back() == '"') || (raw.front() == '<' && raw.back() == '>')))
        return fromTri((*hasInclude_)(raw.substr(1, raw.size() - 2), raw.front() == '<'));
      return unknownValue();
    }
    if (isOp(Op::LParen)) {
      // `__has_include(...)`, `__has_builtin(x)`, a function-like macro invocation: the value is not known
      if (!skipParens()) { bad_ = true; }
      return unknownValue();
    }
    const MacroState st = lookup_(name);
    switch (st.kind) {
      case MacroState::Kind::Undefined: return known(0);  // an identifier that is not a macro is 0
      case MacroState::Kind::Int: return known(static_cast<uint64_t>(st.value), st.isUnsigned);
      default: return unknownValue();
    }
  }

  const std::vector<PTok>& t_;
  const MacroLookup& lookup_;
  const HasIncludeFn* hasInclude_;
  size_t i_ = 0;
  int depth_ = 0;
  bool bad_ = false;
};

std::optional<Value> evaluate(std::string_view expr, const MacroLookup& lookup, const HasIncludeFn* hasInclude) {
  std::vector<PTok> toks;
  if (!tokenize(expr, toks)) return std::nullopt;
  return Parser(toks, lookup, hasInclude).run();
}

}  // namespace

MacroState mergeStates(const MacroState& a, const MacroState& b) {
  using K = MacroState::Kind;
  if (a == b) return a;
  const bool da = a.kind == K::Defined || a.kind == K::Int, db = b.kind == K::Defined || b.kind == K::Int;
  if (da && db) return MacroState::defined();
  return MacroState::unknown();
}

Tri evalCondition(std::string_view expr, const MacroLookup& lookup, const HasIncludeFn* hasInclude) {
  const std::optional<Value> v = evaluate(expr, lookup, hasInclude);
  return v ? truth(*v) : Tri::Unknown;
}

MacroState evalValue(std::string_view expr, const MacroLookup& lookup, const HasIncludeFn* hasInclude) {
  const std::optional<Value> v = evaluate(expr, lookup, hasInclude);
  if (!v || !v->known) return MacroState::unknown();
  return MacroState::integer(static_cast<int64_t>(v->b), v->u);
}

std::string stripComments(std::string_view t) {
  std::string out;
  out.reserve(t.size());
  size_t i = 0;
  while (i < t.size()) {
    const char c = t[i];
    if (c == '\\' && i + 1 < t.size() && (t[i + 1] == '\n' || t[i + 1] == '\r')) {
      i += (t[i + 1] == '\r' && i + 2 < t.size() && t[i + 2] == '\n') ? 3 : 2;
      continue;
    }
    if (c == '/' && i + 1 < t.size() && t[i + 1] == '*') {
      const size_t e = t.find("*/", i + 2);
      out += ' ';
      if (e == std::string_view::npos) break;
      i = e + 2;
      continue;
    }
    if (c == '/' && i + 1 < t.size() && t[i + 1] == '/') break;
    if (c == '"' || c == '\'') {
      out += c;
      ++i;
      while (i < t.size() && t[i] != c && t[i] != '\n' && t[i] != '\r') {
        if (t[i] == '\\' && i + 1 < t.size() && t[i + 1] != '\n' && t[i + 1] != '\r') out += t[i++];
        out += t[i++];
      }
      if (i < t.size() && t[i] == c) out += t[i++];
      continue;
    }
    out += c;
    ++i;
  }
  return out;
}

Directive parseDirective(std::string_view text) {
  Directive d;
  const std::string s = stripComments(text);
  size_t i = 0;
  auto blank = [&](size_t k) { return k < s.size() && (s[k] == ' ' || s[k] == '\t' || s[k] == '\f' || s[k] == '\v'); };
  while (blank(i)) ++i;
  if (i >= s.size() || s[i] != '#') return d;
  ++i;
  while (blank(i)) ++i;
  size_t e = i;
  while (e < s.size() && std::isalpha(static_cast<unsigned char>(s[e]))) ++e;
  const std::string_view name = std::string_view(s).substr(i, e - i);
  if (name.empty()) return d;
  using K = Directive::Kind;
  struct Entry {
    std::string_view n;
    K k;
  };
  static constexpr Entry kNames[] = {{"if", K::If},         {"ifdef", K::Ifdef},     {"ifndef", K::Ifndef},
                                     {"elif", K::Elif},     {"elifdef", K::Elifdef}, {"elifndef", K::Elifndef},
                                     {"else", K::Else},     {"endif", K::Endif},     {"define", K::Define},
                                     {"undef", K::Undef},   {"include", K::Include}, {"include_next", K::Include},
                                     {"import", K::Include}};
  d.kind = K::Other;
  for (const Entry& en : kNames)
    if (en.n == name) d.kind = en.k;
  // the name must end here: `#ifdefX` is not an #ifdef
  if (e < s.size() && isIdChar(static_cast<unsigned char>(s[e]))) {
    d.kind = K::Other;
    return d;
  }
  size_t a = e;
  while (blank(a)) ++a;
  size_t z = s.size();
  while (z > a && (std::isspace(static_cast<unsigned char>(s[z - 1])))) --z;
  d.arg = s.substr(a, z - a);
  return d;
}

std::string firstIdentifier(std::string_view arg) {
  size_t i = 0;
  while (i < arg.size() && (arg[i] == ' ' || arg[i] == '\t')) ++i;
  if (i >= arg.size() || !isIdStart(static_cast<unsigned char>(arg[i]))) return {};
  size_t e = i + 1;
  while (e < arg.size() && isIdChar(static_cast<unsigned char>(arg[e]))) ++e;
  return std::string(arg.substr(i, e - i));
}

std::optional<DefineParts> parseDefine(std::string_view arg) {
  DefineParts p;
  p.name = firstIdentifier(arg);
  if (p.name.empty()) return std::nullopt;
  size_t i = 0;
  while (i < arg.size() && (arg[i] == ' ' || arg[i] == '\t')) ++i;
  i += p.name.size();
  if (i < arg.size() && arg[i] == '(') {
    p.functionLike = true;
    const size_t close = arg.find(')', i);
    i = close == std::string_view::npos ? arg.size() : close + 1;
  }
  while (i < arg.size() && (arg[i] == ' ' || arg[i] == '\t')) ++i;
  size_t z = arg.size();
  while (z > i && (arg[z - 1] == ' ' || arg[z - 1] == '\t')) --z;
  p.body = std::string(arg.substr(i, z - i));
  return p;
}

}  // namespace tf::pp
