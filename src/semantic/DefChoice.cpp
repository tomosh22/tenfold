#include "semantic/DefChoice.h"

#include <cctype>
#include <set>

namespace tf::defchoice {
namespace {

bool isIdentStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool isIdentChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }
bool isSpace(char c) { return c == ' ' || c == '\t'; }

size_t skipWs(std::string_view s, size_t i) {
  while (i < s.size() && isSpace(s[i])) ++i;
  return i;
}

bool isBuiltinType(std::string_view w) {
  static const std::set<std::string_view> k = {"int",     "char",    "short",    "long",     "unsigned", "signed",
                                               "float",   "double",  "bool",     "void",     "auto",     "decltype",
                                               "wchar_t", "char8_t", "char16_t", "char32_t", "__int64",  "size_t"};
  return k.count(w) != 0;
}

bool isQualifierWord(std::string_view w) {
  return w == "const" || w == "volatile" || w == "struct" || w == "class" || w == "union" || w == "enum" ||
         w == "typename" || w == "constexpr" || w == "static" || w == "inline" || w == "extern";
}

// Index just past the '>' that closes the '<' at `open`; npos when the line ends first (or a ';' comes first).
size_t skipAngles(std::string_view s, size_t open) {
  int depth = 0;
  for (size_t i = open; i < s.size(); ++i) {
    if (s[i] == '<') ++depth;
    else if (s[i] == '>') {
      if (--depth == 0) return i + 1;
    } else if (s[i] == ';' || s[i] == '{') return std::string_view::npos;
  }
  return std::string_view::npos;
}

// Reads a (possibly qualified, possibly templated) name starting at `i`; leaves `i` after it.
struct QualName {
  std::string name, qualifier;
  int col = -1;
  bool ok = false;
};
QualName readQualName(std::string_view s, size_t& i) {
  QualName q;
  i = skipWs(s, i);
  if (i + 1 < s.size() && s[i] == ':' && s[i + 1] == ':') i += 2;
  for (;;) {
    i = skipWs(s, i);
    if (i >= s.size() || !isIdentStart(s[i])) return q;
    const size_t b = i;
    while (i < s.size() && isIdentChar(s[i])) ++i;
    if (!q.name.empty()) q.qualifier += (q.qualifier.empty() ? "" : "::") + q.name;
    q.name = std::string(s.substr(b, i - b));
    q.col = static_cast<int>(b);
    size_t j = skipWs(s, i);
    if (j < s.size() && s[j] == '<') {
      const size_t e = skipAngles(s, j);
      if (e == std::string_view::npos) return q;
      i = e;
      j = skipWs(s, i);
    }
    if (j + 1 < s.size() && s[j] == ':' && s[j + 1] == ':') {
      i = j + 2;
      continue;
    }
    q.ok = true;
    return q;
  }
}

struct Tok {
  std::string text;
  bool word = false;
};
std::vector<Tok> tokenize(std::string_view s) {
  std::vector<Tok> t;
  for (size_t i = 0; i < s.size();) {
    const char c = s[i];
    if (isSpace(c) || c == '\n') {
      ++i;
    } else if (isIdentChar(c)) {
      size_t b = i;
      while (i < s.size() && isIdentChar(s[i])) ++i;
      t.push_back({std::string(s.substr(b, i - b)), true});
    } else if (c == ':' && i + 1 < s.size() && s[i + 1] == ':') {
      t.push_back({"::", false});
      i += 2;
    } else if (c == '.' && s.substr(i, 3) == "...") {
      t.push_back({"...", false});
      i += 3;
    } else {
      t.push_back({std::string(1, c), false});
      ++i;
    }
  }
  return t;
}

std::string joinTokens(const std::vector<Tok>& t, size_t from, size_t to) {
  std::string out;
  for (size_t i = from; i < to; ++i) {
    if (!out.empty() && t[i].word && t[i - 1].word) out += ' ';
    out += t[i].text;
  }
  return out;
}

// One parameter, default argument and name removed.
std::string normalizeParam(std::string_view param) {
  std::vector<Tok> t = tokenize(param);
  int depth = 0;
  for (size_t i = 0; i < t.size(); ++i) {  // default argument
    const std::string& x = t[i].text;
    if (x == "<" || x == "(" || x == "[" || x == "{") ++depth;
    else if (x == ">" || x == ")" || x == "]" || x == "}") --depth;
    else if (x == "=" && depth <= 0) {
      t.resize(i);
      break;
    }
  }
  if (t.size() >= 2 && t.back().word && !isBuiltinType(t.back().text) && !isQualifierWord(t.back().text) &&
      t[t.size() - 2].text != "::") {
    // The last word is a parameter name unless everything before it is a qualifier (`const Foo`, `struct Foo`).
    bool onlyQualifiers = true;
    for (size_t i = 0; i + 1 < t.size(); ++i)
      if (!(t[i].word && isQualifierWord(t[i].text))) onlyQualifiers = false;
    if (!onlyQualifiers) t.pop_back();
  }
  return joinTokens(t, 0, t.size());
}

std::vector<std::string> splitParams(std::string_view inner) {
  std::vector<std::string> out;
  int depth = 0;
  size_t b = 0;
  for (size_t i = 0; i <= inner.size(); ++i) {
    const char c = i < inner.size() ? inner[i] : ',';
    if (c == '<' || c == '(' || c == '[' || c == '{') ++depth;
    else if (c == '>' || c == ')' || c == ']' || c == '}') --depth;
    else if (c == ',' && depth <= 0) {
      out.push_back(normalizeParam(inner.substr(b, i - b)));
      b = i + 1;
    }
  }
  if (out.size() == 1 && out[0].empty()) out.clear();
  if (out.size() == 1 && out[0] == "void") out.clear();
  return out;
}

}  // namespace

std::string blankComments(std::string_view line) {
  std::string out(line);
  for (size_t i = 0; i < out.size(); ++i) {
    if (out[i] == '"' || out[i] == '\'') {  // a string or character literal
      const char q = out[i];
      for (++i; i < out.size() && out[i] != q; ++i)
        if (out[i] == '\\') ++i;
    } else if (out[i] == '/' && i + 1 < out.size() && out[i + 1] == '/') {
      for (; i < out.size(); ++i) out[i] = ' ';
    } else if (out[i] == '/' && i + 1 < out.size() && out[i + 1] == '*') {
      const size_t e = out.find("*/", i + 2);
      const size_t end = e == std::string::npos ? out.size() : e + 2;
      for (; i < end; ++i) out[i] = ' ';
      --i;
    }
  }
  return out;
}

int constructPoint(std::string_view line, int wordEnd) {
  if (wordEnd < 0 || static_cast<size_t>(wordEnd) > line.size()) return -1;
  size_t i = skipWs(line, static_cast<size_t>(wordEnd));
  if (i < line.size() && line[i] == '<') {
    const size_t e = skipAngles(line, i);
    if (e == std::string_view::npos) return -1;
    i = skipWs(line, e);
  }
  if (i < line.size() && (line[i] == '(' || line[i] == '{')) return static_cast<int>(i);
  if (i < line.size() && isIdentStart(line[i])) {  // Widget w(2);  Widget w{2};
    size_t j = i;
    while (j < line.size() && isIdentChar(line[j])) ++j;
    static const std::set<std::string_view> kNot = {"const",    "volatile", "noexcept", "override", "final",
                                                    "operator", "requires", "throw",    "and",      "or",
                                                    "not",      "mutable",  "try",      "alignas"};
    if (kNot.count(line.substr(i, j - i))) return -1;
    j = skipWs(line, j);
    if (j < line.size() && (line[j] == '(' || line[j] == '{')) return static_cast<int>(j);
  }
  return -1;
}

AliasForm aliasForm(std::string_view line) {
  size_t i = skipWs(line, 0);
  auto word = [&](std::string_view w) {
    if (line.substr(i, w.size()) != w) return false;
    return i + w.size() >= line.size() || !isIdentChar(line[i + w.size()]);
  };
  if (word("template")) {
    i = skipWs(line, i + 8);
    if (i < line.size() && line[i] == '<') {
      const size_t e = skipAngles(line, i);
      if (e == std::string_view::npos) return AliasForm::None;
      i = skipWs(line, e);
    }
  }
  if (word("typedef")) return AliasForm::Typedef;
  if (!word("using")) return AliasForm::None;
  i = skipWs(line, i + 5);
  if (word("namespace") || word("enum")) return AliasForm::None;
  if (word("typename")) i = skipWs(line, i + 8);
  if (i >= line.size() || !(isIdentStart(line[i]) || line[i] == ':')) return AliasForm::None;
  size_t j = i;
  while (j < line.size() && isIdentChar(line[j])) ++j;
  j = skipWs(line, j);
  if (j < line.size() && line[j] == '=' && (j + 1 >= line.size() || line[j + 1] != '=')) return AliasForm::Using;
  return AliasForm::UsingDecl;
}

std::optional<AliasRhs> aliasRhs(std::string_view line, int nameCol) {
  const AliasForm form = aliasForm(line);
  AliasRhs r;
  if (form == AliasForm::Using) {
    const size_t eq = line.find('=');
    if (eq == std::string_view::npos) return std::nullopt;
    size_t i = eq + 1;
    for (;;) {  // leading keywords
      i = skipWs(line, i);
      size_t j = i;
      while (j < line.size() && isIdentChar(line[j])) ++j;
      if (j > i && isQualifierWord(line.substr(i, j - i))) i = j;
      else break;
    }
    QualName q = readQualName(line, i);
    if (!q.ok || isBuiltinType(q.name)) return std::nullopt;
    r.col = q.col;
    r.name = q.name;
    r.qualifier = q.qualifier;
    return r;
  }
  if (form == AliasForm::Typedef) {
    const size_t tp = line.find("typedef");
    if (tp == std::string_view::npos || nameCol <= 0 || static_cast<size_t>(nameCol) <= tp + 7) return std::nullopt;
    std::string_view before = line.substr(tp + 7, static_cast<size_t>(nameCol) - (tp + 7));
    if (before.find_first_of("({}") != std::string_view::npos) return std::nullopt;
    size_t e = before.size();
    for (;;) {  // trailing blanks, '*', '&', const / volatile
      while (e > 0 && (isSpace(before[e - 1]) || before[e - 1] == '*' || before[e - 1] == '&')) --e;
      size_t b = e;
      while (b > 0 && isIdentChar(before[b - 1])) --b;
      const std::string_view w = before.substr(b, e - b);
      if (w == "const" || w == "volatile") e = b;
      else break;
    }
    if (e == 0) return std::nullopt;
    if (before[e - 1] == '>') {  // Foo<int> Bar
      int depth = 0;
      size_t i = e;
      while (i > 0) {
        --i;
        if (before[i] == '>') ++depth;
        else if (before[i] == '<' && --depth == 0) break;
      }
      if (depth != 0) return std::nullopt;
      e = i;
      while (e > 0 && isSpace(before[e - 1])) --e;
    }
    size_t b = e;
    while (b > 0 && isIdentChar(before[b - 1])) --b;
    if (b == e) return std::nullopt;
    r.name = std::string(before.substr(b, e - b));
    if (isBuiltinType(r.name)) return std::nullopt;
    r.col = static_cast<int>(tp + 7 + b);
    size_t q = b;  // qualifier: a::b::Name
    while (q >= 2 && before[q - 1] == ':' && before[q - 2] == ':') {
      size_t qe = q - 2, qb = qe;
      while (qb > 0 && isIdentChar(before[qb - 1])) --qb;
      if (qb == qe) break;
      r.qualifier = std::string(before.substr(qb, qe - qb)) + (r.qualifier.empty() ? "" : "::" + r.qualifier);
      q = qb;
    }
    return r;
  }
  return std::nullopt;
}

std::string usingQualifier(std::string_view line) {
  // The using-declaration may follow other declarations on the line (`struct D : B { using B::B; };`).
  for (size_t u = line.find("using"); u != std::string_view::npos; u = line.find("using", u + 1)) {
    if (u > 0 && isIdentChar(line[u - 1])) continue;
    const std::string_view rest = line.substr(u);
    if (aliasForm(rest) != AliasForm::UsingDecl) continue;
    size_t i = skipWs(rest, 5);
    if (rest.substr(i, 8) == "typename") i = skipWs(rest, i + 8);
    QualName q = readQualName(rest, i);
    if (!q.ok || q.qualifier.empty()) continue;
    const size_t cut = q.qualifier.rfind("::");
    const std::string last = cut == std::string::npos ? q.qualifier : q.qualifier.substr(cut + 2);
    if (last != q.name) continue;  // only `using Base::Base;`, the inherited constructors
    return q.qualifier;
  }
  return {};
}

std::vector<std::string> paramTypes(std::string_view line, int openParen, bool* ok) {
  if (ok) *ok = false;
  if (openParen < 0 || static_cast<size_t>(openParen) >= line.size() || line[static_cast<size_t>(openParen)] != '(')
    return {};
  int depth = 0;
  for (size_t i = static_cast<size_t>(openParen); i < line.size(); ++i) {
    if (line[i] == '(') ++depth;
    else if (line[i] == ')' && --depth == 0) {
      if (ok) *ok = true;
      return splitParams(line.substr(static_cast<size_t>(openParen) + 1, i - static_cast<size_t>(openParen) - 1));
    }
  }
  return {};
}

std::vector<std::string> signatureParamTypes(std::string_view arcana, bool* ok) {
  if (ok) *ok = false;
  // The second quoted string is the function type.
  size_t q1 = arcana.find('\'');
  if (q1 == std::string_view::npos) return {};
  size_t q2 = arcana.find('\'', q1 + 1);
  if (q2 == std::string_view::npos) return {};
  size_t q3 = arcana.find('\'', q2 + 1);
  if (q3 == std::string_view::npos) return {};
  size_t q4 = arcana.find('\'', q3 + 1);
  if (q4 == std::string_view::npos) return {};
  const std::string_view fn = arcana.substr(q3 + 1, q4 - q3 - 1);
  const size_t open = fn.find('(');
  if (open == std::string_view::npos) return {};
  return paramTypes(fn, static_cast<int>(open), ok);
}

std::string signatureText(std::string_view line, int nameCol) {
  if (nameCol < 0 || static_cast<size_t>(nameCol) >= line.size()) return {};
  size_t i = static_cast<size_t>(nameCol);
  while (i < line.size() && (isIdentChar(line[i]) || line[i] == '~')) ++i;
  std::string name(line.substr(static_cast<size_t>(nameCol), i - static_cast<size_t>(nameCol)));
  if (i < line.size() && line[i] == '(') {
    bool ok = false;
    std::vector<std::string> p = paramTypes(line, static_cast<int>(i), &ok);
    if (!ok) return name + "(...)";
    name += "(";
    for (size_t k = 0; k < p.size(); ++k) name += (k ? ", " : "") + p[k];
    name += ")";
  }
  return name;
}

}  // namespace tf::defchoice
