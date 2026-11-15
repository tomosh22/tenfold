#include "semantic/MacroExpand.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <tuple>

namespace tf::mx {

// ---------------------------------------------------------------------------------------------------------------------
// Templates
// ---------------------------------------------------------------------------------------------------------------------

namespace {

bool isIdStart(char c) {
  return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '$' || static_cast<unsigned char>(c) >= 0x80;
}
bool isIdChar(char c) { return isIdStart(c) || std::isdigit(static_cast<unsigned char>(c)); }

std::string trim(std::string_view s) {
  size_t b = 0, e = s.size();
  while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
  while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
  return std::string(s.substr(b, e - b));
}

bool isIdentifier(std::string_view s) {
  if (s.empty() || !isIdStart(s[0])) return false;
  for (char c : s)
    if (!isIdChar(c)) return false;
  return true;
}

}  // namespace

namespace {

// `raw` (a directive token's text) with comments and line continuations turned into blanks, same length, line breaks
// kept: offsets, lines and columns stay those of the file.
std::string maskDirective(std::string_view raw) {
  std::string m(raw);
  const size_t n = m.size();
  auto nl = [&](size_t k) { return k < n && (m[k] == '\n' || m[k] == '\r'); };
  for (size_t i = 0; i < n;) {
    const char c = m[i];
    if (c == '"' || c == '\'') {
      ++i;
      while (i < n && m[i] != c && !nl(i)) i += (m[i] == '\\' && i + 1 < n && !nl(i + 1)) ? 2 : 1;
      if (i < n && m[i] == c) ++i;
    } else if (c == '/' && i + 1 < n && m[i + 1] == '*') {
      size_t e = i + 2;
      while (e < n && !(m[e] == '*' && e + 1 < n && m[e + 1] == '/')) ++e;
      e = std::min(e + 2, n);
      for (size_t k = i; k < e; ++k)
        if (!nl(k)) m[k] = ' ';
      i = e;
    } else if (c == '/' && i + 1 < n && m[i + 1] == '/') {
      size_t e = i;
      while (e < n) {  // to the end of the logical line
        if (m[e] == '\\' && nl(e + 1)) {
          m[e] = ' ';
          e += 2;
          if (e < n && m[e - 1] == '\r' && m[e] == '\n') ++e;
          continue;
        }
        if (nl(e)) break;
        m[e] = ' ';
        ++e;
      }
      i = e;
    } else if (c == '\\' && nl(i + 1)) {
      m[i] = ' ';
      i += 2;
    } else {
      ++i;
    }
  }
  return m;
}

}  // namespace

void scanTemplates(const std::vector<Token>& toks, FileSymbols& out) {
  out.templates.clear();
  for (const Token& t : toks) {
    if (t.kind != Tok::Preprocessor) continue;
    if (t.text.find("define") == std::string_view::npos) continue;  // cheap reject
    std::string m = maskDirective(t.text);
    size_t i = 0;
    auto blank = [&](size_t k) {
      return k < m.size() &&
             (m[k] == ' ' || m[k] == '\t' || m[k] == '\f' || m[k] == '\v' || m[k] == '\n' || m[k] == '\r');
    };
    while (blank(i)) ++i;
    if (i >= m.size() || m[i] != '#') continue;
    ++i;
    while (blank(i)) ++i;
    if (m.compare(i, 6, "define") != 0) continue;
    i += 6;
    if (i < m.size() && isIdChar(m[i])) continue;
    while (blank(i)) ++i;
    size_t e = i;
    if (e >= m.size() || !isIdStart(m[e])) continue;
    while (e < m.size() && isIdChar(m[e])) ++e;
    MacroTemplate tpl;
    tpl.name = m.substr(i, e - i);
    tpl.objectLike = e >= m.size() || m[e] != '(';
    const size_t close = tpl.objectLike ? e - 1 : m.find(')', e);
    if (close == std::string::npos) continue;
    const std::string params = tpl.objectLike ? std::string() : m.substr(e + 1, close - e - 1);
    bool ok = true;
    if (!trim(params).empty()) {
      size_t b = 0;
      for (;;) {
        const size_t c = params.find(',', b);
        const std::string p = trim(std::string_view(params).substr(b, c == std::string::npos ? c : c - b));
        if (p == "...") {
          tpl.variadic = true;
          if (c != std::string::npos) ok = false;  // `...` must be last
        } else if (isIdentifier(p)) {
          tpl.params.push_back(p);
        } else {
          ok = false;  // GNU named variadic (`args...`) or garbage
        }
        if (!ok || c == std::string::npos) break;
        b = c + 1;
      }
    }
    if (!ok || tpl.params.size() > kMaxParams) continue;
    // The body: lex what follows the parameter list, everything before it blanked (so positions are the file's).
    // (a `;` stands where the parameter list ended: a body that starts with `#` must not lex as a directive)
    for (size_t k = 0; k <= close; ++k)
      if (m[k] != '\n' && m[k] != '\r') m[k] = k == close ? ';' : ' ';
    CppLexer lx(m);
    bool bad = false, named = false, sentinel = true;
    for (;;) {
      const Token b = lx.next();
      if (sentinel) {
        sentinel = false;
        continue;
      }
      if (b.kind == Tok::End) break;
      if (b.kind == Tok::Preprocessor) {
        bad = true;
        break;
      }
      named = named || b.kind == Tok::Identifier || b.kind == Tok::Keyword;
      if (!tpl.body.empty()) tpl.body += ' ';
      tpl.body += b.text;
      const int dl = b.line, col = b.line == 0 ? t.col + b.col : b.col;
      tpl.pos.push_back(dl > 255 || col > 65535 || col < 0
                            ? 0xFFFFFFFFu
                            : (static_cast<uint32_t>(dl) << 16) | static_cast<uint32_t>(col));
      if (tpl.body.size() > (tpl.objectLike ? kMaxObjectBodyBytes : kMaxBodyBytes)) {
        bad = true;
        break;
      }
    }
    if (bad || tpl.body.empty() || (tpl.objectLike && !named)) continue;
    tpl.line = t.line;
    if (out.templates.size() >= kMaxTemplatesPerFile) break;
    out.templates.push_back(std::move(tpl));
  }
}

// ---------------------------------------------------------------------------------------------------------------------
// Expansion
// ---------------------------------------------------------------------------------------------------------------------

namespace {

struct XTok {
  Tok kind = Tok::Punct;
  std::string text;
  int line = 0;
  int col = 0;
  const std::filesystem::path* file = nullptr;  // the file the token is spelled in; null: the calling file
  bool is(std::string_view s) const { return text == s; }
};
using XVec = std::vector<XTok>;

struct Ctx {
  Ctx(const Resolver& r, const Limits& l, const MacroCall& c) : resolve(r), lim(l), call(c) {}
  const Resolver& resolve;
  const Limits& lim;
  const MacroCall& call;
  int expansions = 0;
  bool failed = false;
  // Charges `n` tokens of work against the shared budget; false (and failed) when it is spent.
  bool charge(size_t n) {
    if (!lim.budget) return true;
    if (*lim.budget < n) {
      *lim.budget = 0;
      failed = true;
      return false;
    }
    *lim.budget -= n;
    return true;
  }
  std::vector<const MacroTemplate*> active;
};

Tok kindOfText(const std::string& text) {
  if (text.empty()) return Tok::Punct;
  const char c = text[0];
  if (std::isdigit(static_cast<unsigned char>(c))) return Tok::Number;
  if (isIdStart(c)) return CppLexer::isKeyword(text) ? Tok::Keyword : Tok::Identifier;
  if (c == '"') return Tok::String;
  if (c == '\'') return Tok::Char;
  return Tok::Punct;
}

// Lexes `text`, a source fragment whose first character is at (line0, col0); `fixed` places every token at that
// position instead (a macro body: the caller then positions the tokens). Sets `bad` on a directive token.
XVec lexFragment(std::string_view text, int line0, int col0, bool fixed, bool& bad) {
  XVec out;
  // A body starts a "line": `#x` would lex as a directive. A sentinel token in front keeps `#` and `##` operators.
  const std::string prefixed = fixed ? "; " + std::string(text) : std::string(text);
  CppLexer lx(prefixed);
  bool first = true;
  for (;;) {
    const Token t = lx.next();
    if (t.kind == Tok::End) break;
    if (t.kind == Tok::Preprocessor) {
      bad = true;
      break;
    }
    if (fixed && first) {
      first = false;
      continue;
    }
    XTok x;
    x.kind = t.kind;
    x.text = std::string(t.text);
    if (fixed) {
      x.line = line0;
      x.col = col0;
    } else {
      x.line = line0 + t.line;
      x.col = t.line == 0 ? col0 + t.col : t.col;
    }
    out.push_back(std::move(x));
    if (out.size() > 8192) {
      bad = true;
      break;
    }
  }
  return out;
}

XTok atCall(const Ctx& cx, Tok kind, std::string text) {
  XTok x;
  x.kind = kind;
  x.text = std::move(text);
  x.line = cx.call.line;
  x.col = cx.call.col;
  return x;
}

// `in[open]` is '(' : the raw arguments up to the matching ')' (split at top-level commas), and the index of ')'.
bool collectArgs(const XVec& in, size_t open, std::vector<XVec>& args, size_t& close) {
  args.clear();
  args.emplace_back();
  int paren = 0;
  for (size_t k = open; k < in.size(); ++k) {
    const XTok& t = in[k];
    if (t.kind == Tok::Punct) {
      if (t.text == "(") {
        if (++paren == 1) continue;
      } else if (t.text == ")") {
        if (--paren == 0) {
          close = k;
          return true;
        }
      } else if (t.text == "," && paren == 1) {
        args.emplace_back();
        continue;
      }
    }
    args.back().push_back(t);
  }
  return false;
}

XVec expandTokens(const XVec& in, Ctx& cx, int depth);

// The tokens the invocation of `t` with raw arguments `rawArgs` stands for, after substitution (before rescanning).
XVec substitute(const MacroTemplate& t, const std::filesystem::path* tfile, std::vector<XVec> rawArgs, Ctx& cx,
                int depth) {
  XVec out;
  const size_t np = t.params.size();
  if (np == 1 && rawArgs.size() == 1 && rawArgs[0].empty()) {  // `M()` for a one-parameter macro: an empty argument
  } else if (np == 0 && !t.variadic && rawArgs.size() == 1 && rawArgs[0].empty()) {
    rawArgs.clear();  // `M()` for `M()`: no arguments
  }
  while (rawArgs.size() < np) rawArgs.emplace_back();
  // __VA_ARGS__: the arguments after the named ones, with their commas
  XVec va;
  bool haveVa = false;
  if (t.variadic) {
    for (size_t k = np; k < rawArgs.size(); ++k) {
      if (k > np) va.push_back(atCall(cx, Tok::Punct, ","));
      for (const XTok& x : rawArgs[k]) va.push_back(x);
      haveVa = haveVa || !rawArgs[k].empty();
    }
  }
  bool bad = false;
  XVec body = lexFragment(t.body, cx.call.line, cx.call.col, true, bad);
  if (bad) {
    cx.failed = true;
    return out;
  }
  // A token written in the body is spelled in the macro's file (a name declared there is where clangd lands).
  if (body.size() == t.pos.size()) {
    for (size_t k = 0; k < body.size(); ++k) {
      if (t.pos[k] == 0xFFFFFFFFu) continue;
      body[k].line = t.line + static_cast<int>(t.pos[k] >> 16);
      body[k].col = static_cast<int>(t.pos[k] & 0xFFFFu);
      body[k].file = tfile;
    }
  }
  std::vector<XVec> expanded(np);
  std::vector<char> haveExpanded(np, 0);
  XVec vaExpanded;
  bool haveVaExpanded = false;
  auto paramIndex = [&](const XTok& x) -> int {
    if (x.kind != Tok::Identifier) return -1;
    for (size_t k = 0; k < np; ++k)
      if (t.params[k] == x.text) return static_cast<int>(k);
    if (t.variadic && x.text == "__VA_ARGS__") return static_cast<int>(np);
    return -1;
  };
  auto rawOf = [&](int k) -> const XVec& {
    return static_cast<size_t>(k) == np ? va : rawArgs[static_cast<size_t>(k)];
  };
  auto expandedOf = [&](int k) -> const XVec& {
    if (static_cast<size_t>(k) == np) {
      if (!haveVaExpanded) {
        vaExpanded = expandTokens(va, cx, depth + 1);
        haveVaExpanded = true;
      }
      return vaExpanded;
    }
    const size_t u = static_cast<size_t>(k);
    if (!haveExpanded[u]) {
      expanded[u] = expandTokens(rawArgs[u], cx, depth + 1);
      haveExpanded[u] = 1;
    }
    return expanded[u];
  };
  auto stringify = [&](const XVec& a) {
    std::string s = "\"";
    for (size_t k = 0; k < a.size(); ++k) {
      if (k > 0 && isIdChar(a[k - 1].text.back()) && isIdChar(a[k].text.front())) s += ' ';
      for (char c : a[k].text) {
        if (c == '"' || c == '\\') s += '\\';
        s += c;
      }
    }
    return atCall(cx, Tok::String, s + "\"");
  };
  auto paste = [&](XVec& left, const XVec& right) {
    if (right.empty()) return;
    if (left.empty()) {
      left = right;
      return;
    }
    left.back().text += right.front().text;
    left.back().kind = kindOfText(left.back().text);
    if (left.back().kind == Tok::Punct && left.back().text.size() > 1) {  // `+` ## `=`: one operator token or not
      bool b = false;
      const XVec re = lexFragment(left.back().text, 0, 0, false, b);
      if (re.size() != 1) left.back().kind = Tok::Punct;
    }
    left.back().line = cx.call.line;
    left.back().col = cx.call.col;
    left.back().file = nullptr;
    for (size_t k = 1; k < right.size(); ++k) left.push_back(right[k]);
  };
  // Substitutes B[b, e) into `out`.
  std::function<void(size_t, size_t)> run;
  run = [&](size_t b, size_t e) {
    for (size_t i = b; i < e && !cx.failed;) {
      const XTok& x = body[i];
      if (x.kind == Tok::Identifier && x.text == "__VA_OPT__" && t.variadic && i + 1 < e && body[i + 1].is("(")) {
        int d = 0;
        size_t k = i + 1;
        for (; k < e; ++k) {
          if (body[k].is("(")) ++d;
          else if (body[k].is(")") && --d == 0) break;
        }
        if (k >= e) {
          cx.failed = true;
          return;
        }
        if (haveVa) run(i + 2, k);
        i = k + 1;
        continue;
      }
      if (x.kind == Tok::Punct && x.text == "#" && i + 1 < e && paramIndex(body[i + 1]) >= 0) {
        out.push_back(stringify(rawOf(paramIndex(body[i + 1]))));
        i += 2;
        continue;
      }
      if (x.kind == Tok::Punct && x.text == "##") {
        ++i;
        continue;
      }
      const int pi = paramIndex(x);
      const bool pasteNext = i + 1 < e && body[i + 1].is("##");
      XVec cur;
      if (pi >= 0) cur = pasteNext ? rawOf(pi) : expandedOf(pi);
      else cur.push_back(x);
      ++i;
      while (i < e && body[i].is("##")) {
        ++i;
        if (i >= e) break;
        XVec right;
        const int rp = paramIndex(body[i]);
        if (rp >= 0) right = rawOf(rp);
        else right.push_back(body[i]);
        ++i;
        paste(cur, right);
      }
      for (XTok& c : cur) out.push_back(std::move(c));
      if (out.size() > cx.lim.maxTokens) {
        cx.failed = true;
        return;
      }
    }
  };
  if (!cx.charge(body.size() + 1)) return out;
  run(0, body.size());
  cx.charge(out.size());
  return out;
}

XVec expandTokens(const XVec& in, Ctx& cx, int depth) {
  XVec out;
  for (size_t i = 0; i < in.size() && !cx.failed; ++i) {
    const XTok& x = in[i];
    if (x.kind == Tok::Identifier && x.text == "__LINE__") {
      out.push_back(atCall(cx, Tok::Number, std::to_string(cx.call.line + 1)));
      continue;
    }
    if (x.kind == Tok::Identifier && !(i + 1 < in.size() && in[i + 1].is("(")) && depth < cx.lim.maxDepth) {
      const Resolved r = cx.resolve(x.text, kNoArgs);  // an object-like macro
      if (r.tpl && std::find(cx.active.begin(), cx.active.end(), r.tpl) == cx.active.end() &&
          cx.expansions < cx.lim.maxExpansions) {
        ++cx.expansions;
        XVec sub = substitute(*r.tpl, r.file, {}, cx, depth);
        cx.active.push_back(r.tpl);
        XVec re = expandTokens(sub, cx, depth + 1);
        cx.active.pop_back();
        for (XTok& t : re) out.push_back(std::move(t));
        if (out.size() > cx.lim.maxTokens) cx.failed = true;
        continue;
      }
    }
    if (x.kind == Tok::Identifier && i + 1 < in.size() && in[i + 1].is("(") && depth < cx.lim.maxDepth) {
      std::vector<XVec> args;
      size_t close = 0;
      if (collectArgs(in, i + 1, args, close)) {
        const size_t argc = (args.size() == 1 && args[0].empty()) ? 0 : args.size();
        const Resolved r = cx.resolve(x.text, argc);
        const MacroTemplate* t = r.tpl;
        if (t && std::find(cx.active.begin(), cx.active.end(), t) == cx.active.end() &&
            cx.expansions < cx.lim.maxExpansions) {
          ++cx.expansions;
          XVec sub = substitute(*t, r.file, std::move(args), cx, depth);
          cx.active.push_back(t);
          XVec re = expandTokens(sub, cx, depth + 1);
          cx.charge(re.size());
          cx.active.pop_back();
          for (XTok& r : re) out.push_back(std::move(r));
          if (out.size() > cx.lim.maxTokens) cx.failed = true;
          i = close;
          continue;
        }
      }
    }
    out.push_back(x);
    if (out.size() > cx.lim.maxTokens) cx.failed = true;
  }
  return out;
}

// The expansion of `call`; empty and `ok` false when it does not expand or breaks a limit.
XVec expandToTokens(const MacroCall& call, const Resolver& resolve, const Limits& limits, bool& ok) {
  ok = false;
  Ctx cx(resolve, limits, call);
  const size_t argc = (call.args.size() == 1 && call.args[0].text.find_first_not_of(" \t\r\n") == std::string::npos)
                          ? 0
                          : call.args.size();
  const Resolved rm = resolve(call.name, argc);
  const MacroTemplate* t = rm.tpl;
  if (!t) return {};
  std::vector<XVec> args;
  for (const MacroArg& a : call.args) {
    bool bad = false;
    XVec v = lexFragment(a.text, a.line, a.col, false, bad);
    if (bad || !cx.charge(v.size() + 1)) return {};
    args.push_back(std::move(v));
  }
  ++cx.expansions;
  XVec sub = substitute(*t, rm.file, std::move(args), cx, 0);
  cx.active.push_back(t);
  XVec out = expandTokens(sub, cx, 1);
  cx.active.pop_back();
  if (cx.failed || out.size() > limits.maxTokens) return {};
  if (call.withContext) {  // `int CAT(a, b) = 1;`: the expansion read in its statement
    bool bad = false;
    XVec pre = lexFragment(call.pre.text, call.pre.line, call.pre.col, false, bad);
    XVec post = lexFragment(call.post.text, call.post.line, call.post.col, false, bad);
    if (bad || pre.empty()) return {};
    for (XTok& x : out) pre.push_back(std::move(x));
    for (XTok& x : post) pre.push_back(std::move(x));
    pre.push_back(atCall(cx, Tok::Punct, ";"));
    out = std::move(pre);
    if (out.size() > limits.maxTokens) return {};
  }
  // braces and parentheses must balance: a macro that opens a scope another one closes is not expanded
  int brace = 0, paren = 0, bracket = 0;
  for (const XTok& x : out) {
    if (x.kind == Tok::Preprocessor) return {};
    if (x.kind != Tok::Punct) continue;
    if (x.text == "{") ++brace;
    else if (x.text == "}") --brace;
    else if (x.text == "(") ++paren;
    else if (x.text == ")") --paren;
    else if (x.text == "[") ++bracket;
    else if (x.text == "]") --bracket;
    if (brace < 0 || paren < 0 || bracket < 0) return {};
  }
  if (brace != 0 || paren != 0 || bracket != 0) return {};
  ok = true;
  return out;
}

}  // namespace

std::vector<Expanded> expandCall(const MacroCall& call, const Resolver& resolve, const Limits& limits) {
  bool ok = false;
  const XVec toks = expandToTokens(call, resolve, limits, ok);
  if (!ok || toks.empty()) return {};
  std::vector<Token> tt;
  tt.reserve(toks.size() + 1);
  for (size_t k = 0; k < toks.size(); ++k) {
    Token t;
    t.kind = toks[k].kind;
    t.text = toks[k].text;
    t.line = 0;
    t.col = static_cast<int>(k);  // the index: maps a symbol back to the token's real position
    tt.push_back(t);
  }
  std::vector<Symbol> syms = extractFromTokens(std::move(tt), call.scope, call.scopeKind);
  std::vector<Expanded> out;
  std::set<std::tuple<std::string, std::string, int, int, int, const std::filesystem::path*>> seen;
  for (Symbol& s : syms) {
    if (s.col < 0 || static_cast<size_t>(s.col) >= toks.size()) continue;
    const XTok& at = toks[static_cast<size_t>(s.col)];
    s.line = at.line;
    s.col = at.col;
    if (!seen.emplace(s.name, s.scope, s.line, s.col, static_cast<int>(s.kind), at.file).second) continue;
    Expanded e;
    e.sym = std::move(s);
    if (at.file) e.file = *at.file;
    out.push_back(std::move(e));
  }
  return out;
}

std::string expandCallText(const MacroCall& call, const Resolver& resolve, const Limits& limits) {
  bool ok = false;
  const XVec toks = expandToTokens(call, resolve, limits, ok);
  std::string s;
  for (const XTok& x : toks) {
    if (!s.empty()) s += ' ';
    s += x.text;
  }
  return s;
}

}  // namespace tf::mx
