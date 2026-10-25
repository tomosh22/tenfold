#include "ui/view/Highlighter.h"

#include <algorithm>
#include <cctype>
#include <cstring>

#include "semantic/CppLexer.h"

namespace tf::ui {

namespace {
constexpr uint32_t kNone = 0xFFFFFFFFu;
bool identStart(unsigned char c) { return std::isalpha(c) || c == '_' || c >= 0x80; }
bool identChar(unsigned char c) { return std::isalnum(c) || c == '_' || c >= 0x80; }
bool oneOf(std::string_view w, std::initializer_list<const char*> l) {
  for (const char* s : l)
    if (w == s) return true;
  return false;
}
}  // namespace

bool LexState::operator==(const LexState& o) const {
  return mode == o.mode && dlen == o.dlen && std::memcmp(delim, o.delim, dlen) == 0;
}

LexState lexLine(std::string_view s, LexState st, std::vector<LexSpan>& out) {
  const size_t n = s.size();
  size_t i = 0;
  auto add = [&](size_t b, size_t e, Lex k) {
    if (e <= b) return;
    if (k == Lex::Operator && !out.empty() && out.back().kind == k && out.back().end == b)
      out.back().end = static_cast<uint32_t>(e);
    else out.push_back({static_cast<uint32_t>(b), static_cast<uint32_t>(e), k});
  };
  const bool backslash = n && s[n - 1] == '\\';
  switch (st.mode) {
    case LexState::Block: {
      const size_t j = s.find("*/");
      if (j == std::string_view::npos) return add(0, n, Lex::Comment), st;
      add(0, j + 2, Lex::Comment);
      i = j + 2;
      break;
    }
    case LexState::Raw: {
      const std::string close = ")" + std::string(st.delim, st.dlen) + "\"";
      const size_t j = s.find(close);
      if (j == std::string_view::npos) return add(0, n, Lex::String), st;
      add(0, j + close.size(), Lex::String);
      i = j + close.size();
      break;
    }
    case LexState::LineComment: add(0, n, Lex::Comment); return backslash ? st : LexState{};
    case LexState::Directive: add(0, n, Lex::Preprocessor); return backslash ? st : LexState{};
    case LexState::None: break;
  }
  bool lineStart = st.mode == LexState::None;
  auto quoted = [&](size_t from, size_t q) {  // string / char literal whose opening quote is at q
    size_t j = q + 1;
    while (j < n && s[j] != s[q]) j += s[j] == '\\' ? 2 : 1;
    j = std::min(n, j + 1);
    add(from, j, s[q] == '"' ? Lex::String : Lex::Char);
    return j;
  };
  while (i < n) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v') {
      ++i;
      continue;
    }
    if (c == '/' && i + 1 < n && s[i + 1] == '/') {
      add(i, n, Lex::Comment);
      return backslash ? LexState{LexState::LineComment, 0, {}} : LexState{};
    }
    if (c == '/' && i + 1 < n && s[i + 1] == '*') {
      const size_t j = s.find("*/", i + 2);
      if (j == std::string_view::npos) {
        add(i, n, Lex::Comment);
        return LexState{LexState::Block, 0, {}};
      }
      add(i, j + 2, Lex::Comment);
      i = j + 2;
      continue;
    }
    const bool directive = c == '#' && lineStart;
    lineStart = false;
    if (directive) {  // up to the end of the line or a trailing comment
      size_t j = i;
      while (j < n && !(s[j] == '/' && j + 1 < n && (s[j + 1] == '/' || s[j + 1] == '*'))) ++j;
      add(i, j, Lex::Preprocessor);
      i = j;
      if (j >= n) return backslash ? LexState{LexState::Directive, 0, {}} : LexState{};
    } else if (c == '"' || c == '\'') {
      i = quoted(i, i);
    } else if (std::isdigit(c) || (c == '.' && i + 1 < n && std::isdigit(static_cast<unsigned char>(s[i + 1])))) {
      const bool hex = c == '0' && i + 1 < n && (s[i + 1] == 'x' || s[i + 1] == 'X');
      size_t j = i;
      while (j < n) {
        const unsigned char d = static_cast<unsigned char>(s[j]);
        const int prev = j > i ? std::tolower(static_cast<unsigned char>(s[j - 1])) : 0;
        const bool sign = (d == '+' || d == '-') && prev == (hex ? 'p' : 'e');
        const bool sep = d == '\'' && j + 1 < n && std::isalnum(static_cast<unsigned char>(s[j + 1]));
        if (!(std::isalnum(d) || d == '.' || d == '_' || sep || sign)) break;
        ++j;
      }
      add(i, j, Lex::Number);
      i = j;
    } else if (identStart(c)) {
      size_t j = i;
      while (j < n && identChar(static_cast<unsigned char>(s[j]))) ++j;
      const std::string_view w = s.substr(i, j - i);
      if (j < n && s[j] == '"' && oneOf(w, {"R", "LR", "uR", "UR", "u8R"})) {  // raw string R"delim( ... )delim"
        size_t k = j + 1;
        while (k < n && s[k] != '(' && k - j <= 17) ++k;
        if (k < n && s[k] == '(' && k - j - 1 <= 16) {
          LexState rs{LexState::Raw, static_cast<uint8_t>(k - j - 1), {}};
          std::memcpy(rs.delim, s.data() + j + 1, rs.dlen);
          const std::string close = ")" + std::string(rs.delim, rs.dlen) + "\"";
          const size_t e = s.find(close, k + 1);
          if (e == std::string_view::npos) return add(i, n, Lex::String), rs;
          add(i, e + close.size(), Lex::String);
          i = e + close.size();
          continue;
        }
      }
      if (j < n && (s[j] == '"' || s[j] == '\'') && oneOf(w, {"L", "u", "U", "u8"})) {  // encoding prefix
        i = quoted(i, j);
        continue;
      }
      add(i, j, CppLexer::isKeyword(w) ? Lex::Keyword : Lex::Identifier);
      i = j;
    } else {
      add(i, i + 1, std::strchr("(){}[];,.", c) ? Lex::Punct : Lex::Operator);
      ++i;
    }
  }
  return LexState{};
}

// ---- cache ----

void Highlighter::reset() {
  e_.assign(buf_ ? static_cast<size_t>(buf_->lineCount()) : 0, Entry{});
  consistent_ = 0;
  provValid_ = pending_ = false;
}

void Highlighter::attach(const TextBuffer* b) {
  buf_ = b;
  reset();
  sem_.clear();
}

void Highlighter::relex(Entry& e, int line, const LexState& begin) {
  e.begin = begin;
  e.spans.clear();
  e.end = lexLine(buf_->line(line), begin, e.spans);
  e.valid = true;
}

bool Highlighter::advance(int target, int& budget) {
  while (consistent_ <= target) {
    Entry& e = e_[static_cast<size_t>(consistent_)];
    const LexState b = consistent_ ? e_[static_cast<size_t>(consistent_ - 1)].end : LexState{};
    if (!(e.valid && e.begin == b)) {
      if (budget <= 0) return false;
      --budget;
      relex(e, consistent_, b);
    }
    ++consistent_;
  }
  return true;
}

bool Highlighter::ensureExact(int line) {
  if (!buf_ || e_.size() != static_cast<size_t>(buf_->lineCount())) return false;
  int budget = kSyncBudget;
  return advance(std::min(line, buf_->lineCount() - 1), budget);
}

const std::vector<LexSpan>* Highlighter::spansOf(int line) const {
  if (line < consistent_) return &e_[static_cast<size_t>(line)].spans;
  if (provValid_ && line >= provBase_ && line < provBase_ + static_cast<int>(prov_.size()))
    return &prov_[static_cast<size_t>(line - provBase_)].spans;
  return nullptr;
}

void Highlighter::prepare(int first, int last) {
  if (!buf_) return;
  const int n = buf_->lineCount();
  if (e_.size() != static_cast<size_t>(n)) reset();  // changed behind our back: start over
  last = std::min(last, n);
  if (first >= last) return;
  int budget = kSyncBudget;
  if (advance(last - 1, budget)) {
    provValid_ = pending_ = false;
    return;
  }
  wantLast_ = last;
  pending_ = true;
  if (provValid_ && first >= provBase_ && last <= provBase_ + static_cast<int>(prov_.size())) return;
  provBase_ = std::max(consistent_, first - kLookback);
  prov_.assign(static_cast<size_t>(last - provBase_), Entry{});
  LexState st = provBase_ == consistent_ && consistent_ ? e_[static_cast<size_t>(consistent_ - 1)].end : LexState{};
  for (int i = provBase_; i < last; ++i) {
    Entry& e = prov_[static_cast<size_t>(i - provBase_)];
    relex(e, i, st);
    st = e.end;
  }
  provValid_ = true;
}

bool Highlighter::step(int budgetLines) {
  if (!pending_ || !buf_ || e_.size() != static_cast<size_t>(buf_->lineCount())) return false;
  if (!advance(std::min(wantLast_, buf_->lineCount()) - 1, budgetLines)) return false;
  pending_ = provValid_ = false;
  return true;
}

void Highlighter::onChange(const Document::Change& c) {
  const int s = c.removed.start.line, eOld = c.removed.end.line;
  const int k = static_cast<int>(std::count(c.newText.begin(), c.newText.end(), '\n'));
  if (!e_.empty() && s < static_cast<int>(e_.size())) {
    if (eOld > s)
      e_.erase(e_.begin() + s + 1,
               e_.begin() + static_cast<long>(std::min<size_t>(e_.size(), static_cast<size_t>(eOld) + 1)));
    if (k > 0) e_.insert(e_.begin() + s + 1, static_cast<size_t>(k), Entry{});
    for (int i = s; i <= s + k && i < static_cast<int>(e_.size()); ++i) e_[static_cast<size_t>(i)].valid = false;
    consistent_ = std::min(consistent_, s);
    provValid_ = false;
  }
  if (sem_.empty()) return;
  // Semantic tokens: spans on untouched lines stay (shifted by the line delta); a single-line edit keeps the spans
  // before and after the edited columns on its line (shifted by the column delta) and drops the ones it touches; a
  // multi-line edit drops every span on the lines it replaced. Dropped spans get len 0 (no memmove while typing).
  auto lineLess = [](const TokenSpan& t, int l) { return t.line < l; };
  const auto lo = std::lower_bound(sem_.begin(), sem_.end(), s, lineLess);
  const auto hi = std::lower_bound(lo, sem_.end(), eOld + 1, lineLess);
  if (s == eOld && k == 0) {
    const int a = c.removed.start.column, b = c.removed.end.column;
    const int shift = static_cast<int>(c.newText.size()) - (b - a);
    const bool extends = !c.newText.empty() && identChar(static_cast<unsigned char>(c.newText[0]));
    for (auto it = lo; it != hi; ++it) {
      const int end = it->col + it->len;
      if (end < a || (end == a && !extends)) continue;  // before the edit (touching it only if it cannot extend)
      if (it->col > b || (it->col == b && b > a)) it->col += shift;  // after it
      else it->len = 0;
    }
    return;
  }
  const auto tail = sem_.erase(lo, hi);
  const int delta = k - (eOld - s);
  if (delta != 0)
    for (auto it = tail; it != sem_.end(); ++it) it->line += delta;
}

// ---- colours ----

uint32_t Highlighter::idx(gfx::Color c) {
  for (size_t i = 0; i < pal_.size(); ++i)
    if (pal_[i].r == c.r && pal_[i].g == c.g && pal_[i].b == c.b && pal_[i].a == c.a) return static_cast<uint32_t>(i);
  pal_.push_back(c);
  return static_cast<uint32_t>(pal_.size() - 1);
}

void Highlighter::setTheme(const Theme& t) {
  theme_ = t;
  pal_.clear();
  semIdx_.clear();
  textIdx_ = idx(t.text);
  static const char* names[9] = {"keyword", "identifier",   "number",   "string",     "char",
                                 "comment", "preprocessor", "operator", "punctuation"};
  for (int i = 0; i < 9; ++i) kindIdx_[i] = idx(t.tokenColor(names[i]));
}

void Highlighter::setSemanticTokens(std::vector<TokenSpan> spans, std::vector<std::string> types,
                                    std::vector<std::string> modifiers) {
  auto less = [](const TokenSpan& a, const TokenSpan& b) { return a.line != b.line ? a.line < b.line : a.col < b.col; };
  if (!std::is_sorted(spans.begin(), spans.end(), less)) std::stable_sort(spans.begin(), spans.end(), less);
  sem_ = std::move(spans);
  types_ = std::move(types);
  modifiers_ = std::move(modifiers);
  semIdx_.clear();
}

// Palette index for a semantic token, or kNone when the theme has nothing for its type (e.g. "comment" for inactive
// code, "unknown"): those leave the lexical colour alone.
uint32_t Highlighter::semanticColor(const TokenSpan& s) {
  const uint64_t key = (static_cast<uint64_t>(s.modifiers) << 16) | s.type;
  auto it = semIdx_.find(key);
  if (it != semIdx_.end()) return it->second;
  uint32_t r = kNone;
  if (s.type < types_.size()) {
    const std::string& type = types_[s.type];
    std::vector<std::string> mods;
    for (size_t b = 0; b < modifiers_.size() && b < 32; ++b)
      if (s.modifiers & (1u << b)) mods.push_back(modifiers_[b]);
    bool known = false;
    for (std::string t = type; !t.empty() && !known; t = Theme::tokenFallback(t)) known = theme_.tokens.count(t) > 0;
    if (known) r = idx(theme_.tokenColor(type, mods));
  }
  return semIdx_[key] = r;
}

const std::vector<gfx::ColorSpan>& Highlighter::colorSpans(int line) {
  out_.clear();
  const std::vector<LexSpan>* lex = spansOf(line);
  if (!lex) return out_;
  auto lineLess = [](const TokenSpan& t, int l) { return t.line < l; };
  size_t si = static_cast<size_t>(std::lower_bound(sem_.begin(), sem_.end(), line, lineLess) - sem_.begin());
  auto live = [&](size_t i) { return i < sem_.size() && sem_[i].line == line; };
  for (const LexSpan& l : *lex) {
    const uint32_t kind = kindIdx_[static_cast<int>(l.kind)];
    while (live(si) && static_cast<uint32_t>(sem_[si].col + sem_[si].len) <= l.begin) ++si;
    if (l.kind != Lex::Identifier || !live(si)) {
      out_.push_back({l.begin, l.end, kind});
      continue;
    }
    uint32_t cur = l.begin;
    for (size_t i = si; live(i) && static_cast<uint32_t>(sem_[i].col) < l.end; ++i) {
      const TokenSpan& t = sem_[i];
      if (t.len <= 0) continue;
      const uint32_t b = std::max<uint32_t>(l.begin, static_cast<uint32_t>(t.col)),
                     e = std::min<uint32_t>(l.end, static_cast<uint32_t>(t.col + t.len));
      if (e <= b || b < cur) continue;
      const uint32_t c = semanticColor(t);
      if (c == kNone) continue;
      if (b > cur) out_.push_back({cur, b, kind});
      out_.push_back({b, e, c});
      cur = e;
    }
    if (cur < l.end) out_.push_back({cur, l.end, kind});
  }
  return out_;
}

}  // namespace tf::ui
