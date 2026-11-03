#include "corpus/Corpus.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>

namespace tf::corpus {

namespace fs = std::filesystem;

namespace {

bool isNameChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.'; }
bool isIdentChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || (c & 0x80); }

std::string trim(std::string_view s) {
  size_t b = 0, e = s.size();
  while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
  while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
  return std::string(s.substr(b, e - b));
}

// One anchor comment at `at` (pointing at "/*@"): its name, and the offset just past "*/"; npos when there is none.
size_t parseAnchorComment(std::string_view line, size_t at, std::string& name) {
  if (line.compare(at, 3, "/*@") != 0) return std::string::npos;
  size_t i = at + 3;
  size_t b = i;
  while (i < line.size() && isNameChar(line[i])) ++i;
  if (i == b || line.compare(i, 2, "*/") != 0) return std::string::npos;
  name.assign(line.substr(b, i - b));
  return i + 2;
}

int tokenLength(std::string_view line, size_t at) {
  size_t i = at;
  if (i < line.size() && line[i] == '~') ++i;
  size_t b = i;
  while (i < line.size() && isIdentChar(line[i])) ++i;
  if (i == b) return 1;  // punctuation: one character
  return static_cast<int>(i - at);
}

std::string where(const std::string& file, int line) { return file + ":" + std::to_string(line) + ": "; }

}  // namespace

std::vector<std::string> anchorNamesIn(std::string_view text) {
  std::vector<std::string> out;
  size_t p = 0;
  while ((p = text.find("/*@", p)) != std::string_view::npos) {
    std::string name;
    size_t end = parseAnchorComment(text, p, name);
    if (end == std::string::npos) {
      p += 3;
      continue;
    }
    out.push_back(name);
    p = end;
  }
  return out;
}

void scanAnchorsInText(const std::string& relFile, std::string_view text, AnchorMap& out,
                       std::vector<std::string>& errors) {
  int lineNo = 0;
  size_t pos = 0;
  while (pos <= text.size()) {
    size_t nl = text.find('\n', pos);
    std::string_view line = text.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
    size_t p = 0;
    while ((p = line.find("/*@", p)) != std::string_view::npos) {
      std::string name;
      size_t end = parseAnchorComment(line, p, name);
      if (end == std::string::npos) {
        p += 3;
        continue;
      }
      // The token follows, after blanks and further anchor comments (several names for one token).
      std::vector<std::string> names{name};
      size_t t = end;
      for (;;) {
        while (t < line.size() && (line[t] == ' ' || line[t] == '\t')) ++t;
        std::string more;
        size_t e2 = t < line.size() ? parseAnchorComment(line, t, more) : std::string::npos;
        if (e2 == std::string::npos) break;
        names.push_back(more);
        t = e2;
      }
      if (t >= line.size() || line[t] == '\r') {
        errors.push_back(where(relFile, lineNo + 1) + "anchor '" + name + "' has no token after it on its line");
      } else {
        for (auto& n : names) {
          Anchor a;
          a.name = n;
          a.file = relFile;
          a.line = lineNo;
          a.col = static_cast<int>(t);
          a.len = tokenLength(line, t);
          auto [it, fresh] = out.emplace(n, a);
          if (!fresh)
            errors.push_back(where(relFile, lineNo + 1) + "duplicate anchor '" + n + "' (first at " + it->second.file +
                             ":" + std::to_string(it->second.line + 1) + ")");
        }
      }
      p = std::max(t, p + 1);
    }
    if (nl == std::string_view::npos) break;
    pos = nl + 1;
    ++lineNo;
  }
}

bool scanAnchors(const fs::path& dir, AnchorMap& out, std::vector<std::string>& errors) {
  const size_t before = errors.size();
  std::vector<fs::path> files;
  std::error_code ec;
  for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
       !ec && it != end; it.increment(ec)) {
    const std::string name = it->path().filename().string();
    if (it->is_directory(ec)) {
      if (name == ".tenfold" || name == ".git") it.disable_recursion_pending();
      continue;
    }
    if (!it->is_regular_file(ec)) continue;
    if (name == kExpectFileName || name == "compile_commands.json") continue;
    files.push_back(it->path());
  }
  std::sort(files.begin(), files.end());
  for (auto& f : files) {
    std::ifstream in(f, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string text = ss.str();
    if (text.size() > (8u << 20)) continue;
    if (text.substr(0, 8192).find('\0') != std::string::npos) continue;  // binary
    scanAnchorsInText(fs::relative(f, dir, ec).generic_string(), text, out, errors);
  }
  return errors.size() == before;
}

const char* toString(Kind k) {
  switch (k) {
    case Kind::Def: return "def";
    case Kind::Decl: return "decl";
    case Kind::Typedef: return "typedef";
    case Kind::Impl: return "impl";
    case Kind::Refs: return "refs";
    case Kind::Hover: return "hover";
    case Kind::Sym: return "sym";
    case Kind::Rename: return "rename";
  }
  return "?";
}

const char* toString(Rel r) {
  switch (r) {
    case Rel::None: return "";
    case Rel::Arrow: return "->";
    case Rel::Eq: return "=";
    case Rel::Ge: return ">=";
    case Rel::Contains: return "~";
    case Rel::Has: return "\xE2\x88\x8B";  // U+220B
  }
  return "?";
}

const char* toString(Status s) {
  switch (s) {
    case Status::Pass: return "PASS";
    case Status::Fail: return "FAIL";
    case Status::XFail: return "XFAIL";
    case Status::XPass: return "XPASS";
    case Status::Skip: return "SKIP";
  }
  return "?";
}

Status classify(bool xfail, bool ok) {
  if (xfail) return ok ? Status::XPass : Status::XFail;
  return ok ? Status::Pass : Status::Fail;
}

bool unescapeQuoted(std::string_view body, std::string& out) {
  out.clear();
  for (size_t i = 0; i < body.size(); ++i) {
    if (body[i] != '\\') {
      out += body[i];
      continue;
    }
    if (++i >= body.size()) return false;
    switch (body[i]) {
      case '"': out += '"'; break;
      case '\\': out += '\\'; break;
      case 'n': out += '\n'; break;
      case 't': out += '\t'; break;
      default: return false;
    }
  }
  return true;
}

// ---------------------------------------------------------------------------
// Expectation parser
// ---------------------------------------------------------------------------

namespace {

struct Tok {
  std::string text;
  bool quoted = false;
  bool isBar() const { return !quoted && text == "|"; }
};

// Words, quoted strings ("..." with \" \\ \n \t) and '|' (always a token of its own). A word starting with '#'
// starts a comment: the rest of the line is returned in `comment`. False with `err` on a bad string.
bool tokenize(std::string_view s, std::vector<Tok>& toks, std::string& comment, std::string& err) {
  size_t i = 0;
  while (i < s.size()) {
    char c = s[i];
    if (std::isspace(static_cast<unsigned char>(c))) {
      ++i;
    } else if (c == '#') {
      comment = trim(s.substr(i + 1));
      return true;
    } else if (c == '|') {
      toks.push_back({"|", false});
      ++i;
    } else if (c == '"') {
      size_t j = i + 1;
      while (j < s.size() && s[j] != '"') j += s[j] == '\\' ? 2 : 1;
      if (j >= s.size()) {
        err = "unterminated string";
        return false;
      }
      std::string body;
      if (!unescapeQuoted(s.substr(i + 1, j - i - 1), body)) {
        err = "bad escape in string (use \\\" \\\\ \\n \\t)";
        return false;
      }
      toks.push_back({body, true});
      i = j + 1;
    } else {
      size_t j = i;
      while (j < s.size() && !std::isspace(static_cast<unsigned char>(s[j])) && s[j] != '|' && s[j] != '"') ++j;
      toks.push_back({std::string(s.substr(i, j - i)), false});
      i = j;
    }
  }
  return true;
}

bool isIdentifier(const std::string& s) {
  if (s.empty() || std::isdigit(static_cast<unsigned char>(s[0]))) return false;
  return std::all_of(s.begin(), s.end(),
                     [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; });
}

struct Parser {
  const std::string& label;
  const AnchorMap& anchors;
  std::set<std::string> virtualAnchors;  // introduced by `edit` lines so far
  std::vector<Item>& out;
  std::vector<std::string>& errors;
  long openRename = -1;  // index in `out` of the rename whose indented lines are being read

  void error(int line, const std::string& msg) { errors.push_back(where(label, line) + msg); }

  bool known(const std::string& name) const { return anchors.count(name) || virtualAnchors.count(name); }

  bool anchorOk(int line, const Tok& t) {
    if (t.quoted || t.isBar()) {
      error(line, "expected an anchor name, got '" + t.text + "'");
      return false;
    }
    if (known(t.text)) return true;
    error(line, "unknown anchor '" + t.text + "'");
    return false;
  }

  void closeRename() {
    if (openRename < 0) return;
    const Item& r = out[static_cast<size_t>(openRename)];
    if (r.renamed.empty()) error(r.line, "rename needs indented lines listing the anchors that must be renamed");
    openRename = -1;
  }

  void line(int no, const std::string& raw, bool indented) {
    std::string t = trim(raw);
    if (t.empty() || t[0] == '#') return;
    if (indented) {
      if (openRename < 0) return error(no, "unexpected indented line (only a rename block has them)");
      std::vector<Tok> toks;
      std::string comment, err;
      if (!tokenize(t, toks, comment, err)) return error(no, err);
      for (auto& k : toks)
        if (anchorOk(no, k)) out[static_cast<size_t>(openRename)].renamed.push_back(k.text);
      return;
    }
    closeRename();
    parseLine(no, t);
  }

  void parseDirective(Item& it, const std::string& first, const std::string& rest) {
    const int no = it.line;
    if (first == "config") {
      std::string name = rest;
      size_t h = name.find(" #");
      if (h != std::string::npos) name = trim(name.substr(0, h));
      if (name.empty()) return error(no, "config needs a configuration name, e.g. config Debug|x64");
      it.type = Item::Type::Config;
      it.arg = name;
      out.push_back(std::move(it));
      return;
    }
    std::vector<Tok> toks;
    std::string comment, err;
    if (!tokenize(rest, toks, comment, err)) return error(no, err);
    if (first == "open") {
      if (toks.size() != 1 || toks[0].isBar()) return error(no, "usage: open <file>");
      it.type = Item::Type::Open;
      it.arg = toks[0].text;
      out.push_back(std::move(it));
      return;
    }
    // edit
    if (toks.size() != 2 || !toks[1].quoted || toks[0].isBar())
      return error(no, "usage: edit <file>:<line>:<col> \"text\"");
    const std::string& spec = toks[0].text;
    size_t c2 = spec.rfind(':');
    size_t c1 = c2 == std::string::npos || c2 == 0 ? std::string::npos : spec.rfind(':', c2 - 1);
    int l = 0, c = 0;
    try {
      if (c1 == std::string::npos || c1 == 0) throw 1;
      l = std::stoi(spec.substr(c1 + 1, c2 - c1 - 1));
      c = std::stoi(spec.substr(c2 + 1));
    } catch (...) { return error(no, "usage: edit <file>:<line>:<col> \"text\" (1-based line and column)"); }
    if (l < 1 || c < 1) return error(no, "edit line and column are 1-based");
    it.type = Item::Type::Edit;
    it.arg = spec.substr(0, c1);
    it.editLine = l;
    it.editCol = c;
    it.editText = toks[1].text;
    for (auto& n : anchorNamesIn(it.editText)) {
      if (known(n)) return error(no, "edit introduces duplicate anchor '" + n + "'");
      virtualAnchors.insert(n);
    }
    out.push_back(std::move(it));
  }

  void parseLine(int no, const std::string& t) {
    Item it;
    it.line = no;
    it.text = t;
    size_t sp = t.find_first_of(" \t");
    const std::string first = t.substr(0, sp);
    if (first == "config" || first == "open" || first == "edit")
      return parseDirective(it, first, sp == std::string::npos ? std::string() : trim(t.substr(sp)));

    std::vector<Tok> toks;
    std::string comment, err;
    if (!tokenize(t, toks, comment, err)) return error(no, err);
    size_t i = 0;
    while (i < toks.size() && !toks[i].quoted && (toks[i].text == "fast" || toks[i].text == "xfail")) {
      bool& flag = toks[i].text == "fast" ? it.fast : it.xfail;
      if (flag) return error(no, "'" + toks[i].text + "' given twice");
      flag = true;
      ++i;
    }
    if (it.xfail) {
      if (comment.empty()) return error(no, "xfail needs a reason: xfail <expectation> # why it is wrong today");
      it.reason = comment;
    }
    if (i >= toks.size()) return error(no, "missing expectation after the prefix");
    const std::string kw = toks[i++].text;
    static const std::map<std::string, Kind> kinds = {
        {"def", Kind::Def},   {"decl", Kind::Decl},   {"typedef", Kind::Typedef}, {"impl", Kind::Impl},
        {"refs", Kind::Refs}, {"hover", Kind::Hover}, {"sym", Kind::Sym},         {"rename", Kind::Rename}};
    auto kit = kinds.find(kw);
    if (kit == kinds.end()) {
      if (kw == "config" || kw == "open" || kw == "edit") return error(no, "'" + kw + "' cannot follow fast/xfail");
      return error(no,
                   "unknown expectation '" + kw + "' (def decl typedef impl refs hover sym rename config open edit)");
    }
    it.kind = kit->second;
    if (it.fast && (it.kind == Kind::Refs || it.kind == Kind::Rename))
      return error(no, kw + " has no fast tier (it is answered by clangd only)");

    auto next = [&](const char* what) -> const Tok* {
      if (i >= toks.size()) {
        error(no, std::string("missing ") + what);
        return nullptr;
      }
      return &toks[i++];
    };

    if (it.kind == Kind::Sym) {
      const Tok* q = next("query: sym \"query\" -> Anchor");
      if (!q) return;
      if (!q->quoted) return error(no, "sym query must be in double quotes");
      it.query = q->text;
    } else {
      const Tok* a = next("anchor");
      if (!a || !anchorOk(no, *a)) return;
      it.anchor = a->text;
    }

    if (it.kind == Kind::Rename) {
      const Tok* n = next("new name: rename Anchor newName");
      if (!n) return;
      if (n->quoted || !isIdentifier(n->text)) return error(no, "rename target '" + n->text + "' is not an identifier");
      it.query = n->text;
      if (i != toks.size()) return error(no, "unexpected text after the new name");
      out.push_back(std::move(it));
      openRename = static_cast<long>(out.size()) - 1;
      return;
    }

    const Tok* r = next("relation (->, =, >=, ~, \xE2\x88\x8B)");
    if (!r) return;
    const std::string& rs = r->text;
    if (r->quoted) return error(no, "expected a relation, got a string");
    if (rs == "->") it.rel = Rel::Arrow;
    else if (rs == "=") it.rel = Rel::Eq;
    else if (rs == ">=") it.rel = Rel::Ge;
    else if (rs == "~") it.rel = Rel::Contains;
    else if (rs == "\xE2\x88\x8B" || rs == "has") it.rel = Rel::Has;
    else return error(no, "unknown relation '" + rs + "' (-> = >= ~ \xE2\x88\x8B has)");

    bool relOk = false;
    switch (it.kind) {
      case Kind::Def:
      case Kind::Decl:
      case Kind::Typedef: relOk = it.rel == Rel::Arrow; break;
      case Kind::Impl: relOk = it.rel == Rel::Arrow || it.rel == Rel::Eq || it.rel == Rel::Ge; break;
      case Kind::Refs: relOk = it.rel == Rel::Eq || it.rel == Rel::Ge; break;
      case Kind::Hover: relOk = it.rel == Rel::Contains; break;
      case Kind::Sym: relOk = it.rel == Rel::Arrow || it.rel == Rel::Has; break;
      case Kind::Rename: break;
    }
    if (!relOk) return error(no, kw + " does not take '" + rs + "'");

    if (it.kind == Kind::Hover) {
      if (i >= toks.size()) return error(no, "hover needs quoted text: hover Anchor ~ \"text\"");
      for (; i < toks.size(); ++i) {
        if (!toks[i].quoted) return error(no, "hover text must be in double quotes");
        it.texts.push_back(toks[i].text);
      }
      out.push_back(std::move(it));
      return;
    }

    // Operands: alternatives separated by '|' (def, decl, typedef, sym ->), else a blank-separated set.
    const bool alternatives = it.rel == Rel::Arrow && it.kind != Kind::Impl;
    if (i >= toks.size()) return error(no, "missing anchors after '" + rs + "'");
    if (toks.size() - i == 1 && !toks[i].quoted && toks[i].text == "none") {
      if (it.rel == Rel::Ge || it.rel == Rel::Has) return error(no, "'none' makes no sense after '" + rs + "'");
      it.none = true;
      out.push_back(std::move(it));
      return;
    }
    bool expectAnchor = true;
    for (; i < toks.size(); ++i) {
      const Tok& k = toks[i];
      if (k.isBar()) {
        if (!alternatives) return error(no, kw + " lists anchors separated by blanks, not '|'");
        if (expectAnchor) return error(no, "misplaced '|'");
        expectAnchor = true;
        continue;
      }
      if (alternatives && !expectAnchor) return error(no, kw + " takes one anchor or alternatives separated by '|'");
      if (!anchorOk(no, k)) return;
      it.targets.push_back(k.text);
      expectAnchor = false;
    }
    if (alternatives && expectAnchor) return error(no, "missing anchor after '|'");
    out.push_back(std::move(it));
  }
};

}  // namespace

bool parseExpectations(std::string_view text, const std::string& fileLabel, const AnchorMap& anchors,
                       std::vector<Item>& out, std::vector<std::string>& errors) {
  const size_t before = errors.size();
  out.clear();
  Parser ps{fileLabel, anchors, {}, out, errors};
  int no = 0;
  size_t pos = 0;
  if (text.size() >= 3 && text.compare(0, 3, "\xEF\xBB\xBF") == 0) pos = 3;
  while (pos <= text.size()) {
    size_t nl = text.find('\n', pos);
    std::string_view ln = text.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
    if (!ln.empty() && ln.back() == '\r') ln.remove_suffix(1);
    ++no;
    const bool indented = !ln.empty() && (ln[0] == ' ' || ln[0] == '\t');
    ps.line(no, std::string(ln), indented);
    if (nl == std::string_view::npos) break;
    pos = nl + 1;
  }
  ps.closeRename();
  return errors.size() == before;
}

// ---------------------------------------------------------------------------
// Comparison
// ---------------------------------------------------------------------------

bool locMatchesAnchor(const Loc& loc, const Anchor& a) {
  if (loc.file != a.file) return false;
  if (loc.line == a.line && loc.col >= a.col && loc.col < a.col + std::max(1, a.len)) return true;
  if (loc.endLine >= 0) {
    auto notAfter = [](int l1, int c1, int l2, int c2) { return l1 < l2 || (l1 == l2 && c1 <= c2); };
    return notAfter(loc.line, loc.col, a.line, a.col) && notAfter(a.line, a.col, loc.endLine, loc.endCol) &&
           !(a.line == loc.endLine && a.col == loc.endCol);
  }
  return false;
}

std::string describeLoc(const Loc& loc, const AnchorMap& anchors) {
  std::string s = loc.file + ":" + std::to_string(loc.line + 1) + ":" + std::to_string(loc.col + 1);
  const Anchor* exact = nullptr;
  const Anchor* nearest = nullptr;
  long nearD = 0;
  for (auto& [n, a] : anchors) {
    if (a.file != loc.file) continue;
    if (locMatchesAnchor(loc, a)) {
      if (!exact || (a.line == loc.line && a.col == loc.col)) exact = &a;  // prefer the one starting exactly here
      continue;
    }
    long d = std::labs(static_cast<long>(a.line) - loc.line) * 10000 + std::labs(static_cast<long>(a.col) - loc.col);
    if (!nearest || d < nearD) {
      nearest = &a;
      nearD = d;
    }
  }
  if (exact) return s + " [" + exact->name + "]";
  if (nearest) return s + " (near " + nearest->name + ")";
  return s;
}

std::string describeLocs(const std::vector<Loc>& locs, const AnchorMap& anchors, size_t limit) {
  if (locs.empty()) return "none";
  std::string s;
  for (size_t i = 0; i < locs.size() && i < limit; ++i) s += (i ? ", " : "") + describeLoc(locs[i], anchors);
  if (locs.size() > limit) s += ", ... (+" + std::to_string(locs.size() - limit) + " more)";
  return s;
}

bool sameSpot(const Loc& a, const Loc& b) { return a.file == b.file && a.line == b.line; }

bool sameAnswer(const std::vector<Loc>& a, const std::vector<Loc>& b) {
  auto key = [](const Loc& l) { return l.file + ":" + std::to_string(l.line); };
  std::set<std::string> sa, sb;
  for (auto& l : a) sa.insert(key(l));
  for (auto& l : b) sb.insert(key(l));
  return sa == sb;
}

namespace {

std::string join(const std::vector<std::string>& v, const char* sep) {
  std::string s;
  for (size_t i = 0; i < v.size(); ++i) s += (i ? sep : "") + v[i];
  return s;
}

const Anchor* find(const AnchorMap& m, const std::string& n) {
  auto it = m.find(n);
  return it == m.end() ? nullptr : &it->second;
}

bool landsOnAny(const Loc& l, const std::vector<std::string>& names, const AnchorMap& anchors) {
  for (auto& n : names)
    if (const Anchor* a = find(anchors, n); a && locMatchesAnchor(l, *a)) return true;
  return false;
}

bool anyLandsOn(const std::vector<Loc>& locs, const std::string& name, const AnchorMap& anchors) {
  const Anchor* a = find(anchors, name);
  return a && std::any_of(locs.begin(), locs.end(), [&](const Loc& l) { return locMatchesAnchor(l, *a); });
}

}  // namespace

Verdict checkLocations(const Item& it, const std::vector<Loc>& actual, const AnchorMap& anchors) {
  Verdict v;
  v.actual = describeLocs(actual, anchors);
  if (it.none) {
    v.expected = "none";
    v.ok = actual.empty();
    return v;
  }
  if (it.kind == Kind::Sym) {
    if (it.rel == Rel::Arrow) {
      v.expected = "first: " + join(it.targets, " | ");
      v.ok = !actual.empty() && landsOnAny(actual.front(), it.targets, anchors);
    } else {
      v.expected = "contains " + join(it.targets, " ");
      v.ok = std::all_of(it.targets.begin(), it.targets.end(),
                         [&](const std::string& n) { return anyLandsOn(actual, n, anchors); });
    }
    return v;
  }
  if (it.rel == Rel::Arrow && it.kind != Kind::Impl) {
    // def / decl / typedef: the answer is non-empty and every location lands on one of the alternatives.
    v.expected = join(it.targets, " | ");
    v.ok = !actual.empty() &&
           std::all_of(actual.begin(), actual.end(), [&](const Loc& l) { return landsOnAny(l, it.targets, anchors); });
    return v;
  }
  // Sets (refs, impl): exactly (= and impl ->) or at least (>=) these anchors.
  const bool exact = it.rel != Rel::Ge;
  v.expected = std::string(exact ? "exactly {" : "at least {") + join(it.targets, " ") + "}";
  v.ok = std::all_of(it.targets.begin(), it.targets.end(),
                     [&](const std::string& n) { return anyLandsOn(actual, n, anchors); });
  if (exact)
    for (auto& l : actual)
      if (!landsOnAny(l, it.targets, anchors)) v.ok = false;
  return v;
}

Verdict checkHover(const Item& it, const std::string& hoverText) {
  Verdict v;
  std::string exp;
  v.ok = true;
  for (auto& t : it.texts) {
    exp += (exp.empty() ? "\"" : " + \"") + t + "\"";
    if (hoverText.find(t) == std::string::npos) v.ok = false;
  }
  v.expected = "contains " + exp;
  std::string shown = hoverText;
  for (char& c : shown)
    if (c == '\n' || c == '\r') c = ' ';
  if (shown.size() > 160) shown = shown.substr(0, 157) + "...";
  v.actual = hoverText.empty() ? "(no hover text)" : "\"" + shown + "\"";
  return v;
}

Verdict checkRename(const Item& it, const std::map<std::string, std::string>& before,
                    const std::map<std::string, std::string>& after) {
  Verdict v;
  v.expected = "{" + join(it.renamed, " ") + "} -> " + it.query;
  std::vector<std::string> notRenamed, unexpected;
  for (auto& n : it.renamed) {
    auto a = after.find(n);
    if (a == after.end() || a->second != it.query)
      notRenamed.push_back(n + "(" + (a == after.end() ? "lost" : a->second) + ")");
  }
  for (auto& [n, tok] : after) {
    if (tok != it.query) continue;
    if (std::find(it.renamed.begin(), it.renamed.end(), n) != it.renamed.end()) continue;
    auto b = before.find(n);
    if (b != before.end() && b->second != it.query) unexpected.push_back(n);
  }
  v.ok = notRenamed.empty() && unexpected.empty();
  std::string act;
  if (!notRenamed.empty()) act += "not renamed: " + join(notRenamed, " ");
  if (!unexpected.empty()) act += std::string(act.empty() ? "" : "; ") + "also renamed: " + join(unexpected, " ");
  v.actual = v.ok ? "renamed as expected" : act;
  return v;
}

LatencyStats computeStats(std::vector<double> ms) {
  LatencyStats s;
  s.n = ms.size();
  if (ms.empty()) return s;
  std::sort(ms.begin(), ms.end());
  const size_t n = ms.size();
  s.median = n % 2 ? ms[n / 2] : (ms[n / 2 - 1] + ms[n / 2]) / 2;
  size_t rank = static_cast<size_t>(std::ceil(0.95 * static_cast<double>(n)));
  s.p95 = ms[std::min(n, std::max<size_t>(1, rank)) - 1];
  s.max = ms.back();
  return s;
}

}  // namespace tf::corpus
