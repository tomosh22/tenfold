#include "semantic/FastScope.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdint>
#include <set>

namespace tf {

namespace {

bool isIdentCh(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$' || static_cast<unsigned char>(c) >= 0x80;
}

std::string_view lastComponent(std::string_view s) {
  const size_t q = s.rfind("::");
  return q == std::string_view::npos ? s : s.substr(q + 2);
}

std::string joinScope(const std::string& a, const std::string& b) {
  if (a.empty()) return b;
  if (b.empty()) return a;
  return a + "::" + b;
}

// "a::b::c" -> {"a::b::c", "a::b", "a", ""}
std::vector<std::string> prefixVariants(std::string path) {
  std::vector<std::string> out;
  for (;;) {
    out.push_back(path);
    if (path.empty()) break;
    const size_t q = path.rfind("::");
    path = q == std::string::npos ? std::string() : path.substr(0, q);
  }
  return out;
}

bool isClassKind(SymbolKind k) { return k == SymbolKind::Class || k == SymbolKind::Struct || k == SymbolKind::Union; }
bool isFunctionKind(SymbolKind k) {
  return k == SymbolKind::Function || k == SymbolKind::Method || k == SymbolKind::Constructor;
}

std::string pathOf(const ScopeMap& m, int r) {
  std::vector<const std::string*> parts;
  for (; r > 0; r = m.regions[static_cast<size_t>(r)].parent) {
    const ScanRegion& g = m.regions[static_cast<size_t>(r)];
    if ((g.kind == ScanRegion::Namespace && !g.transparent) || g.kind == ScanRegion::Class) parts.push_back(&g.name);
  }
  std::string s;
  for (size_t i = parts.size(); i-- > 0;) {
    if (!s.empty()) s += "::";
    s += *parts[i];
  }
  return s;
}

std::string expandAlias(const ScopeMap& m, int region, std::string qual) {
  const size_t q = qual.find("::");
  const std::string first = qual.substr(0, q);
  for (int r = region; r >= 0; r = m.regions[static_cast<size_t>(r)].parent) {
    for (const ScanAlias& a : m.regions[static_cast<size_t>(r)].aliases)
      if (a.alias == first) return a.target + (q == std::string::npos ? "" : qual.substr(q));
    if (r == 0) break;
  }
  return qual;
}

// The classes the index knows under the spelling `spelled` ("Vec2", "geo::Vec2").
std::vector<FastIndex::Match> classesNamed(const FastIndex& idx, std::string_view spelled) {
  if (spelled.starts_with("::")) spelled.remove_prefix(2);
  std::vector<FastIndex::Match> out;
  if (spelled.empty()) return out;
  for (auto& m : idx.lookup(lastComponent(spelled))) {
    const Symbol& s = m.sym();
    if (!isClassKind(s.kind)) continue;
    const std::string q = s.qualified();
    if (q == spelled || (q.size() > spelled.size() && q.ends_with(spelled) && q[q.size() - spelled.size() - 1] == ':'))
      out.push_back(std::move(m));
  }
  return out;
}

// Narrows classes that share a name to the one the written name means at `ctx` (the caret's namespace / class path):
// a class declared in an enclosing scope of the caret (the innermost such scope wins), else the classes sharing the
// longest namespace prefix with it, the least nested first.
void preferNear(std::vector<FastIndex::Match>& cls, const std::string& ctx) {
  if (cls.size() < 2) return;
  const std::vector<std::string> chain = prefixVariants(ctx);  // innermost first, "" last
  for (const std::string& scope : chain) {
    std::vector<FastIndex::Match> hit;
    for (auto& c : cls)
      if (c.sym().scope == scope) hit.push_back(c);
    if (!hit.empty()) {
      cls = std::move(hit);
      return;
    }
  }
  auto common = [&](const std::string& scope) {
    size_t n = 0;
    while (n < scope.size() && n < ctx.size() && scope[n] == ctx[n]) ++n;
    while (n > 0 && n < scope.size() && !(scope[n] == ':' && n + 1 < scope.size())) --n;
    return n;
  };
  size_t best = 0;
  for (auto& c : cls) best = std::max(best, common(c.sym().scope));
  std::erase_if(cls, [&](const FastIndex::Match& c) { return common(c.sym().scope) < best; });
}

const ScanRegion* classRegionOf(const ScopeMap& m, const Symbol& s) {
  for (const ScanRegion& g : m.regions)
    if (g.kind == ScanRegion::Class && g.nameAt.line == s.line && g.nameAt.col == s.col) return &g;
  return nullptr;
}

// level 0: the classes' own qualified names; level n: their n-th generation of bases.
std::vector<std::vector<std::string>> classLevels(const ScopeEnv& env, std::vector<FastIndex::Match> cls,
                                                  const std::string& ctx, int maxDepth = 6) {
  std::vector<std::vector<std::string>> out;
  std::set<std::string> seen;
  for (int depth = 0; depth <= maxDepth && !cls.empty(); ++depth) {
    std::vector<std::string> names;
    std::vector<FastIndex::Match> next;
    for (const FastIndex::Match& c : cls) {
      const std::string q = c.sym().qualified();
      if (!seen.insert(q).second) continue;
      names.push_back(q);
      std::shared_ptr<const ScopeMap> sm = env.mapOf ? env.mapOf(c.file->file) : nullptr;
      if (!sm) continue;
      const ScanRegion* reg = classRegionOf(*sm, c.sym());
      if (!reg) continue;
      for (const std::string& b : reg->bases) {
        std::vector<FastIndex::Match> bc = classesNamed(*env.index, b);
        preferNear(bc, c.sym().scope.empty() ? ctx : c.sym().scope);
        for (auto& x : bc) next.push_back(std::move(x));
      }
    }
    if (!names.empty()) out.push_back(std::move(names));
    cls = std::move(next);
  }
  return out;
}

// Members of an anonymous struct / union (and entities of an unnamed namespace) are found in the scope around it.
std::string stripAnonymous(const std::string& s) {
  constexpr std::string_view anon = "(anonymous)";
  std::string_view v = s;
  for (;;) {
    if (v == anon) return {};
    if (v.size() > anon.size() + 2 && v.ends_with(anon) && v.substr(v.size() - anon.size() - 2, 2) == "::")
      v = v.substr(0, v.size() - anon.size() - 2);
    else break;
  }
  return std::string(v);
}

struct Level {
  std::vector<std::string> scopes;
  bool isClass = false;
  bool contains(const std::string& s) const { return std::find(scopes.begin(), scopes.end(), s) != scopes.end(); }
  bool has(const std::string& s) const {
    if (contains(s)) return true;
    if (s.find("(anonymous)") == std::string::npos) return false;
    const std::string t = stripAnonymous(s);
    return t != s && contains(t);
  }
};

bool isSmartPointer(std::string_view last) {
  return last == "unique_ptr" || last == "shared_ptr" || last == "weak_ptr" || last == "optional" ||
         last == "scoped_ptr" || last == "intrusive_ptr" || last == "auto_ptr" || last == "unique_ref";
}
bool isIndexable(std::string_view last) {
  return last == "vector" || last == "array" || last == "deque" || last == "span";
}

// Levels of the lookup chain at the caret, innermost first, ending with the global scope.
std::vector<Level> buildLevels(const ScopeEnv& env, const CaretInfo& ci, int region) {
  const ScopeMap& m = *ci.map;
  std::vector<Level> levels;
  auto addUsings = [&](const ScanRegion& g, int r, Level& lv) {
    const std::string base = pathOf(m, r);
    for (const ScanUsing& u : g.usings) {
      if (ci.at < u.at) continue;
      if (!u.name.empty() && u.name != ci.word) continue;
      const std::string ns = expandAlias(m, r, u.ns);
      for (const std::string& v : prefixVariants(base)) lv.scopes.push_back(joinScope(v, ns));
    }
  };
  for (int r = region; r > 0; r = m.regions[static_cast<size_t>(r)].parent) {
    const ScanRegion& g = m.regions[static_cast<size_t>(r)];
    Level lv;
    std::vector<std::vector<std::string>> bases;
    switch (g.kind) {
      case ScanRegion::Block: break;
      case ScanRegion::Function: {
        const size_t q = g.name.rfind("::");
        if (q != std::string::npos && q > 0 && !g.name.starts_with("operator")) {
          const std::string prefix = expandAlias(m, g.parent, g.name.substr(0, q));
          for (const std::string& v : prefixVariants(pathOf(m, g.parent))) lv.scopes.push_back(joinScope(v, prefix));
          std::vector<FastIndex::Match> cls;
          for (const std::string& s : lv.scopes)
            for (auto& c : classesNamed(*env.index, s))
              if (c.sym().qualified() == s) cls.push_back(std::move(c));
          if (!cls.empty()) {
            lv.isClass = true;
            auto cl = classLevels(env, std::move(cls), pathOf(m, g.parent));
            if (!cl.empty()) lv.scopes = cl[0];
            for (size_t i = 1; i < cl.size(); ++i) bases.push_back(std::move(cl[i]));
          }
        }
        break;
      }
      case ScanRegion::Class: {
        const std::string p = pathOf(m, r);
        lv.scopes.push_back(p);
        lv.isClass = true;
        std::vector<FastIndex::Match> cls;
        for (auto& c : classesNamed(*env.index, p))
          if (c.sym().qualified() == p) cls.push_back(std::move(c));
        auto cl = classLevels(env, std::move(cls), pathOf(m, g.parent));
        for (size_t i = 1; i < cl.size(); ++i) bases.push_back(std::move(cl[i]));
        break;
      }
      case ScanRegion::Namespace:
        if (!g.transparent) lv.scopes.push_back(pathOf(m, r));
        break;
    }
    addUsings(g, g.kind == ScanRegion::Function || g.kind == ScanRegion::Block ? g.parent : r, lv);
    if (!lv.scopes.empty()) levels.push_back(std::move(lv));
    for (auto& b : bases) {
      Level bl;
      bl.scopes = std::move(b);
      bl.isClass = true;
      levels.push_back(std::move(bl));
    }
  }
  Level global;
  global.scopes.push_back("");
  addUsings(m.regions[0], 0, global);
  levels.push_back(std::move(global));
  return levels;
}

// The variable named `name` visible at the caret: locals and parameters of the enclosing bodies first, then the fields
// of enclosing classes and the variables of enclosing namespaces (as written in this file).
struct VarHit {
  const ScanVar* v = nullptr;
  int region = 0;
};
VarHit lookupVar(const ScopeMap& m, int region, ScanPos caret, const std::string& name, bool bodiesOnly) {
  for (int r = region; r >= 0; r = m.regions[static_cast<size_t>(r)].parent) {
    const ScanRegion& g = m.regions[static_cast<size_t>(r)];
    const bool body = g.kind == ScanRegion::Function || g.kind == ScanRegion::Block;
    if (!bodiesOnly || body) {
      for (size_t i = g.vars.size(); i-- > 0;) {
        const ScanVar& v = g.vars[i];
        if (v.name != name) continue;
        if (body && caret < v.at) continue;
        return {&v, r};
      }
    }
    if (r == 0) break;
  }
  return {};
}

// A field `name` of one of `cls`' closure (classes then their bases): its declared type.
const ScanVar* fieldOf(const ScopeEnv& env, const std::vector<FastIndex::Match>& cls, const std::string& name,
                       std::shared_ptr<const ScopeMap>* keep, const std::string& ctx) {
  std::vector<FastIndex::Match> cur = cls;
  std::set<std::string> seen;
  for (int depth = 0; depth < 6 && !cur.empty(); ++depth) {
    std::vector<FastIndex::Match> next;
    for (const FastIndex::Match& c : cur) {
      if (!seen.insert(c.sym().qualified()).second) continue;
      std::shared_ptr<const ScopeMap> sm = env.mapOf ? env.mapOf(c.file->file) : nullptr;
      if (!sm) continue;
      const ScanRegion* reg = classRegionOf(*sm, c.sym());
      if (!reg) continue;
      for (const ScanVar& v : reg->vars)
        if (v.name == name) {
          *keep = sm;
          return &v;
        }
      for (const std::string& b : reg->bases) {
        auto bc = classesNamed(*env.index, b);
        preferNear(bc, c.sym().scope.empty() ? ctx : c.sym().scope);
        for (auto& x : bc) next.push_back(std::move(x));
      }
    }
    cur = std::move(next);
  }
  return nullptr;
}

// Skips blanks and /* comments */ backwards from column `p`.
size_t skipSpacesBack(std::string_view t, size_t p) {
  for (;;) {
    while (p > 0 && (t[p - 1] == ' ' || t[p - 1] == '\t')) --p;
    if (p < 2 || t[p - 1] != '/' || t[p - 2] != '*') return p;
    const size_t open = t.rfind("/*", p - 2);
    if (open == std::string_view::npos) return p;
    p = open;
  }
}

// Skips blanks and /* comments */ forwards from column `p`.
size_t skipSpacesFwd(std::string_view t, size_t p) {
  for (;;) {
    while (p < t.size() && (t[p] == ' ' || t[p] == '\t')) ++p;
    if (p + 1 >= t.size() || t[p] != '/' || t[p + 1] != '*') return p;
    const size_t close = t.find("*/", p + 2);
    if (close == std::string_view::npos) return p;
    p = close + 2;
  }
}

// Position (column) of the member-access operator before the word, npos if none. `arrow` tells which.
size_t accessOpPos(const CaretInfo& ci, bool* arrow) {
  std::string_view t = ci.text;
  size_t p = skipSpacesBack(t, ci.wordStart);
  if (p > 0 && t[p - 1] == '~') p = skipSpacesBack(t, p - 1);
  if (p >= 2 && t[p - 1] == '>' && t[p - 2] == '-') {
    *arrow = true;
    return p - 2;
  }
  if (p >= 1 && t[p - 1] == '.' && !(p >= 2 && t[p - 2] == '.')) {
    // a number like `1.f` is no access
    size_t q = p - 1;
    size_t s = q;
    while (s > 0 && isIdentCh(t[s - 1])) --s;
    if (s < q && std::isdigit(static_cast<unsigned char>(t[s]))) return std::string::npos;
    *arrow = false;
    return p - 1;
  }
  return std::string::npos;
}

struct Receiver {
  std::vector<std::string> segs;  // base first
  std::vector<bool> arrowAfter;   // arrowAfter[k]: the operator after segs[k] is `->`
  std::vector<bool> indexed;      // segs[k] is followed by [..] before its operator
};

bool parseReceiver(std::string_view t, size_t opPos, bool lastArrow, Receiver& rc) {
  std::vector<std::string> segs;
  std::vector<bool> arrows, idx;
  size_t end = opPos;
  bool arrow = lastArrow;
  for (int guard = 0; guard < 8; ++guard) {
    size_t e = skipSpacesBack(t, end);
    bool indexed = false;
    if (e > 0 && t[e - 1] == ']') {  // arr[i].x
      int d = 0;
      size_t k = e;
      while (k > 0) {
        --k;
        if (t[k] == ']') ++d;
        else if (t[k] == '[' && --d == 0) break;
      }
      if (d != 0) return false;
      e = skipSpacesBack(t, k);
      indexed = true;
    }
    size_t s = e;
    while (s > 0 && isIdentCh(t[s - 1])) --s;
    if (s == e) return false;
    if (std::isdigit(static_cast<unsigned char>(t[s]))) return false;
    segs.emplace_back(t.substr(s, e - s));
    arrows.push_back(arrow);
    idx.push_back(indexed);
    // what precedes the identifier
    size_t p = skipSpacesBack(t, s);
    if (p >= 2 && t[p - 1] == '>' && t[p - 2] == '-') {
      arrow = true;
      end = p - 2;
    } else if (p >= 1 && t[p - 1] == '.' && !(p >= 2 && t[p - 2] == '.')) {
      arrow = false;
      end = p - 1;
    } else if (p >= 2 && t[p - 1] == ':' && t[p - 2] == ':') {
      return false;
    } else {
      break;
    }
    // the operator must follow an identifier / ']' (not a call result)
    const size_t q = skipSpacesBack(t, end);
    if (q == 0 || !(isIdentCh(t[q - 1]) || t[q - 1] == ']')) return false;
  }
  std::reverse(segs.begin(), segs.end());
  std::reverse(arrows.begin(), arrows.end());
  std::reverse(idx.begin(), idx.end());
  rc.segs = std::move(segs);
  rc.arrowAfter = std::move(arrows);
  rc.indexed = std::move(idx);
  return true;
}

// The class matches a declared type names after unwrapping pointers; empty when the type is unknown.
std::vector<FastIndex::Match> classesOfType(const ScopeEnv& env, const ScanVar& v, bool arrowAfter, bool indexed,
                                            const std::string& ctx) {
  std::string t = v.type;
  if (t.empty()) return {};
  const std::string_view last = lastComponent(t);
  if (indexed && isIndexable(last)) {
    t = v.targ;
  } else if (isSmartPointer(last)) {
    if (!arrowAfter) return {};  // members of the smart pointer itself
    t = v.targ;
  } else if (!v.targ.empty()) {
    // another template: its members are the template's own (unless the index knows a class of that name)
  }
  if (t.empty()) return {};
  auto cls = classesNamed(*env.index, t);
  preferNear(cls, ctx);
  return cls;
}

}  // namespace

bool isSourceFileName(const std::filesystem::path& p) {
  std::string e = p.extension().string();
  for (char& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return e == ".c" || e == ".cc" || e == ".cpp" || e == ".cxx" || e == ".c++" || e == ".cp" || e == ".m" ||
         e == ".mm" || e == ".cu";
}

AccessKind accessKindOf(const CaretInfo& ci) {
  bool arrow = false;
  const size_t p = accessOpPos(ci, &arrow);
  if (p == std::string::npos) return AccessKind::None;
  return arrow ? AccessKind::Arrow : AccessKind::Dot;
}

std::optional<ScanVar> findLocal(const CaretInfo& ci) {
  if (!ci.map || ci.map->regions.size() <= 1) return std::nullopt;
  const int r = ci.map->regionAt(ci.at);
  if (r <= 0) return std::nullopt;
  VarHit h = lookupVar(*ci.map, r, ci.at, ci.word, true);
  if (!h.v) return std::nullopt;
  return *h.v;
}

namespace {
// The number of comma-separated arguments of the list opened at column `p` of the caret's line (`(` or `{`).
int countArgsFrom(const CaretInfo& ci, size_t p) {
  std::string_view t = ci.text;
  const char closeCh = t[p] == '(' ? ')' : '}';
  int depth = 0, commas = 0;
  bool token = false;
  bool onlyVoid = true;
  std::string firstWord;
  int lineNo = ci.at.line;
  std::string_view cur = t;
  size_t i = p + 1;
  size_t budget = 4000;
  int lines = 0;
  bool inBlock = false;
  for (;;) {
    if (i >= cur.size()) {
      if (++lines > 8 || !ci.lineAt || lineNo + 1 >= ci.lineCount) return -1;
      ++lineNo;
      cur = ci.lineAt(lineNo);
      i = 0;
      continue;
    }
    if (budget-- == 0) return -1;
    const char c = cur[i];
    if (inBlock) {
      if (c == '*' && i + 1 < cur.size() && cur[i + 1] == '/') {
        inBlock = false;
        ++i;
      }
      ++i;
      continue;
    }
    if (c == '/' && i + 1 < cur.size() && cur[i + 1] == '/') {
      i = cur.size();
      continue;
    }
    if (c == '/' && i + 1 < cur.size() && cur[i + 1] == '*') {
      inBlock = true;
      i += 2;
      continue;
    }
    if (c == '"' || c == '\'') {
      token = true;
      onlyVoid = false;
      ++i;
      while (i < cur.size() && cur[i] != c) i += cur[i] == '\\' ? 2 : 1;
      ++i;
      continue;
    }
    if (c == '(' || c == '[' || c == '{') {
      ++depth;
      token = true;
      onlyVoid = false;
    } else if (c == ')' || c == ']' || c == '}') {
      if (depth == 0) {
        if (c != closeCh) return -1;
        break;
      }
      --depth;
    } else if (c == ',' && depth == 0) {
      ++commas;
      onlyVoid = false;
    } else if (c == '<' && depth == 0 && i > 0 && (isIdentCh(cur[i - 1]) || cur[i - 1] == ' ')) {
      // may open template arguments (`std::map<int, int> m`), may be a comparison: either way the commas are unsure
      for (size_t k = i + 1; k < cur.size(); ++k)
        if (cur[k] == '>' || cur[k] == ',') return -1;
      return -1;
    } else if (!std::isspace(static_cast<unsigned char>(c))) {
      token = true;
      if (isIdentCh(c)) {
        size_t s = i;
        while (i < cur.size() && isIdentCh(cur[i])) ++i;
        if (firstWord.empty() && depth == 0) firstWord = std::string(cur.substr(s, i - s));
        else onlyVoid = false;
        continue;
      }
      onlyVoid = false;
    }
    ++i;
  }
  if (!token) return 0;
  if (onlyVoid && firstWord == "void" && commas == 0) return 0;
  return commas + 1;
}
}  // namespace

int callArgCount(const CaretInfo& ci) {
  std::string_view t = ci.text;
  size_t p = skipSpacesFwd(t, ci.wordEnd);
  if (p < t.size() && t[p] == '<') {
    int d = 0;
    size_t k = p;
    for (; k < t.size(); ++k) {
      if (t[k] == '<') ++d;
      else if (t[k] == '>' && --d == 0) break;
      else if (t[k] == ';' || t[k] == '(' || t[k] == '{') return -1;
    }
    if (k >= t.size()) return -1;
    p = skipSpacesFwd(t, k + 1);
  }
  if (p >= t.size() || t[p] != '(') return -1;
  return countArgsFrom(ci, p);
}

std::vector<std::string> classScopesOfQualifier(const ScopeEnv& env, const CaretInfo& ci,
                                                const std::string& qualifier) {
  std::string q = qualifier;
  int region = 0;
  if (ci.map) region = ci.map->regionAt(ci.at);
  if (ci.map) q = expandAlias(*ci.map, region, q);
  auto cls = classesNamed(*env.index, q);
  preferNear(cls, ci.map ? pathOf(*ci.map, region) : std::string());
  std::vector<std::string> out;
  for (auto& lv : classLevels(env, std::move(cls), ci.map ? pathOf(*ci.map, region) : std::string()))
    for (auto& s : lv) out.push_back(std::move(s));
  return out;
}

std::vector<FastIndex::Match> narrowByScope(const ScopeEnv& env, const CaretInfo& caret,
                                            std::vector<FastIndex::Match> matches) {
  if (matches.empty() || !env.index) return matches;
  // Code nested more than kMaxScopeDepth regions deep (generated or pathological) is not scope-resolved: the chain
  // would cost more than the answer is worth.
  constexpr int kMaxScopeDepth = 48;
  CaretInfo ci = caret;
  if (ci.map) {
    int depth = 0;
    for (int r = ci.map->regionAt(ci.at); r > 0 && depth <= kMaxScopeDepth;
         r = ci.map->regions[static_cast<size_t>(r)].parent)
      ++depth;
    if (depth > kMaxScopeDepth) ci.map = nullptr;
  }
  // Keeps the matches `pred` accepts, unless that would leave none.
  auto keepIf = [&](auto pred) {
    std::vector<char> keep(matches.size());
    size_t kept = 0;
    for (size_t i = 0; i < matches.size(); ++i) kept += (keep[i] = pred(matches[i])) ? 1 : 0;
    if (kept == 0 || kept == matches.size()) return;
    size_t w = 0;
    for (size_t i = 0; i < matches.size(); ++i)
      if (keep[i]) matches[w++] = std::move(matches[i]);
    matches.resize(w);
  };
  const std::filesystem::path& here = ci.file;
  auto sameFile = [&](const FastIndex::Match& m) { return m.file->file == here; };

  // 1. file-local entities (`static`, anonymous namespace) are visible in their own file; a source file's are not
  //    offered elsewhere (a header's might be included).
  auto fileLocal = [](const FastIndex::Match& m) {
    const Symbol& s = m.sym();
    return s.fileLocal || s.scope.find("(anonymous)") != std::string::npos;
  };
  keepIf([&](const FastIndex::Match& m) { return !fileLocal(m) || sameFile(m) || !isSourceFileName(m.file->file); });
  {
    bool localHere = false;
    for (auto& m : matches) localHere = localHere || (fileLocal(m) && sameFile(m));
    // a file-local entity of the caret's own file hides what other files declare (a redeclaration would not compile)
    if (localHere && ci.qualifier.empty() && !ci.globalQualifier && accessKindOf(ci) == AccessKind::None)
      keepIf([&](const FastIndex::Match& m) { return sameFile(m) && fileLocal(m); });
  }

  const AccessKind acc = accessKindOf(ci);
  const int region = ci.map ? ci.map->regionAt(ci.at) : 0;
  const std::string ctxPath = ci.map ? pathOf(*ci.map, region) : std::string();

  if (acc != AccessKind::None) {
    // 2. member access: only members; and the receiver's class (with its bases) when its type is lexically known
    keepIf([](const FastIndex::Match& m) {
      return m.sym().kind == SymbolKind::Method || m.sym().kind == SymbolKind::Field;
    });
    if (ci.map && matches.size() > 1) {
      bool arrow = false;
      const size_t op = accessOpPos(ci, &arrow);
      Receiver rc;
      if (op != std::string::npos && parseReceiver(ci.text, op, arrow, rc)) {
        std::vector<FastIndex::Match> cls;
        bool known = false;
        if (rc.segs[0] == "this") {
          // the enclosing class
          std::vector<Level> lv = buildLevels(env, ci, region);
          for (const Level& l : lv)
            if (l.isClass) {
              for (const std::string& s : l.scopes)
                for (auto& c : classesNamed(*env.index, s))
                  if (c.sym().qualified() == s) cls.push_back(std::move(c));
              break;
            }
          known = !cls.empty();
        } else {
          VarHit h = lookupVar(*ci.map, region, ci.at, rc.segs[0], false);
          const ScanVar* var = h.v;
          std::shared_ptr<const ScopeMap> keep;
          if (!var) {
            // a field of an enclosing class (possibly declared in another file, or inherited)
            std::vector<Level> lv = buildLevels(env, ci, region);
            for (const Level& l : lv) {
              if (!l.isClass) continue;
              std::vector<FastIndex::Match> own;
              for (const std::string& s : l.scopes)
                for (auto& c : classesNamed(*env.index, s))
                  if (c.sym().qualified() == s) own.push_back(std::move(c));
              if ((var = fieldOf(env, own, rc.segs[0], &keep, ctxPath))) break;
            }
          }
          for (size_t k = 0; var; ++k) {
            cls = classesOfType(env, *var, rc.arrowAfter[k], rc.indexed[k], ctxPath);
            if (cls.empty() || k + 1 >= rc.segs.size()) break;
            std::shared_ptr<const ScopeMap> keep2;
            var = fieldOf(env, cls, rc.segs[k + 1], &keep2, ctxPath);
            keep = keep2;
            if (!var) cls.clear();
          }
          known = !cls.empty();
        }
        if (known) {
          // the most derived class that has the member hides the same name in its bases
          for (auto& lv : classLevels(env, std::move(cls), ctxPath)) {
            Level level;
            level.scopes = lv;
            bool hit = false;
            for (auto& m : matches) hit = hit || level.has(m.sym().scope);
            if (!hit) continue;
            keepIf([&](const FastIndex::Match& m) { return level.has(m.sym().scope); });
            break;
          }
        }
      }
    }
  } else if (ci.globalQualifier) {
    keepIf([](const FastIndex::Match& m) { return m.sym().scope.empty(); });
  } else if (ci.qualifier.empty() && ci.map && matches.size() > 1) {
    // 3. unqualified: the caret's scope chain, innermost first (a name found in an inner scope hides outer ones)
    std::vector<Level> levels = buildLevels(env, ci, region);
    auto rankOf = [&](const FastIndex::Match& m) -> int {
      if (m.sym().kind == SymbolKind::Macro) return -1;
      for (size_t i = 0; i < levels.size(); ++i)
        if (levels[i].has(m.sym().scope)) return static_cast<int>(i);
      return INT32_MAX;
    };
    int best = INT32_MAX;
    for (auto& m : matches) {
      const int r = rankOf(m);
      if (r >= 0) best = std::min(best, r);
    }
    if (best != INT32_MAX) keepIf([&](const FastIndex::Match& m) { return rankOf(m) <= best; });
  }

  // 3b. what kind of name the position can mean
  {
    auto isTag = [](SymbolKind k) {
      return k == SymbolKind::Class || k == SymbolKind::Struct || k == SymbolKind::Union || k == SymbolKind::Enum;
    };
    if (ci.afterTagKeyword) {
      keepIf([&](const FastIndex::Match& m) { return isTag(m.sym().kind); });
    } else if (matches.size() > 1 && acc == AccessKind::None) {
      std::string_view t = ci.text;
      size_t p = skipSpacesFwd(t, ci.wordEnd);
      if (p < t.size() && t[p] == '(') {
        // a function (not a constructor) hides a class of the same name in its scope: `Stat(...)` calls the function
        std::set<std::string> fnScopes;
        for (auto& m : matches)
          if (m.sym().kind == SymbolKind::Function || m.sym().kind == SymbolKind::Method)
            fnScopes.insert(m.sym().scope);
        if (!fnScopes.empty())
          keepIf([&](const FastIndex::Match& m) { return !isTag(m.sym().kind) || !fnScopes.count(m.sym().scope); });
      }
      if (p < t.size() && t[p] == '<' && matches.size() > 1) {
        // explicit template arguments name a template: `get<int>(x)`
        const size_t close = t.find('>', p);
        if (close != std::string_view::npos && skipSpacesFwd(t, close + 1) < t.size() &&
            t[skipSpacesFwd(t, close + 1)] == '(')
          keepIf([](const FastIndex::Match& m) { return m.sym().isTemplate; });
      }
    }
  }

  // 4. the call's argument count against the overloads' parameter counts
  if (matches.size() > 1) {
    const int n = callArgCount(ci);
    if (n >= 0) {
      keepIf([&](const FastIndex::Match& m) {
        const Symbol& s = m.sym();
        if (!isFunctionKind(s.kind) || s.arity < 0) return true;
        const int lo = s.minArity >= 0 ? s.minArity : s.arity;
        return n >= lo && (s.variadic || n <= s.arity);
      });
    }
  }

  // 5. the include graph: what the caret's file can see (a definition outside it stays when its prototype is inside)
  if (matches.size() > 1) {
    bool multiFile = false;
    for (auto& m : matches) multiFile = multiFile || m.file != matches[0].file;
    if (multiFile) {
      std::vector<std::filesystem::path> quoteDirs, angleDirs;
      if (env.includeDirsOf) env.includeDirsOf(here, quoteDirs, angleDirs);
      if (auto reach = env.index->reachableFrom(here, quoteDirs, angleDirs)) {
        std::unordered_map<const FileSymbols*, bool> seen;  // one path key per candidate file
        auto visible = [&](const FastIndex::Match& m) {
          auto [it, fresh] = seen.try_emplace(m.file.get(), false);
          if (fresh) it->second = reach->count(FastIndex::pathKey(m.file->file)) > 0;
          return it->second;
        };
        auto pairKey = [](const Symbol& s) {
          return s.scope + "\x01" + s.name + "\x01" + std::to_string(static_cast<int>(s.arity)) + "\x01" +
                 (isFunctionKind(s.kind) ? "f" : "v");
        };
        // a definition in a file the caret cannot see stays when its prototype / declaration is visible
        auto pairable = [](const Symbol& s) {
          return s.definition &&
                 (isFunctionKind(s.kind) || s.kind == SymbolKind::Variable || s.kind == SymbolKind::Field);
        };
        size_t nVisible = 0, nHiddenDefs = 0;
        for (auto& m : matches) {
          if (visible(m)) ++nVisible;
          else if (pairable(m.sym())) ++nHiddenDefs;
        }
        if (nVisible > 0 && nVisible < matches.size()) {
          std::set<std::string> keys;
          if (nHiddenDefs > 0)
            for (auto& m : matches)
              if (visible(m)) keys.insert(pairKey(m.sym()));
          keepIf([&](const FastIndex::Match& m) {
            return visible(m) || (pairable(m.sym()) && keys.count(pairKey(m.sym())) > 0);
          });
        }
      }
    }
  }
  return matches;
}

std::shared_ptr<const ScopeMap> ScopeCache::get(const std::string& key, uint64_t a, uint64_t b,
                                                const std::function<std::shared_ptr<const ScopeMap>()>& build) {
  const bool cache = a != 0 || b != 0;
  if (cache) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = map_.find(key);
    if (it != map_.end() && it->second.a == a && it->second.b == b) {
      it->second.tick = ++tick_;
      return it->second.map;
    }
  }
  std::shared_ptr<const ScopeMap> built = build();
  if (cache && built) {
    // bounded by entries and by size (regions and variables), least recently used first out
    size_t weight = built->regions.size();
    for (const ScanRegion& r : built->regions) weight += r.vars.size();
    std::lock_guard<std::mutex> lk(mu_);
    if (auto old = map_.find(key); old != map_.end()) {
      weight_ -= old->second.weight;
      map_.erase(old);
    }
    while (!map_.empty() && (map_.size() >= 48 || weight_ + weight > kMaxWeight)) {
      auto oldest = map_.begin();
      for (auto it = map_.begin(); it != map_.end(); ++it)
        if (it->second.tick < oldest->second.tick) oldest = it;
      weight_ -= oldest->second.weight;
      map_.erase(oldest);
    }
    weight_ += weight;
    map_[key] = {a, b, built, ++tick_, weight};
  }
  return built;
}

void ScopeCache::clear() {
  std::lock_guard<std::mutex> lk(mu_);
  map_.clear();
  weight_ = 0;
}

}  // namespace tf
