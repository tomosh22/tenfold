// Editing conveniences of the Editor (B8): auto-indent, auto-close, comments, line commands, find / replace in the
// current document and bookmarks. Everything edits through Document::replaceMany (one batched, single-undo-step pass)
// or applyCaretEdits, so multi-caret behaviour and undo grouping are the same as for typing.
#include <algorithm>
#include <cctype>
#include <cstring>
#include <limits>
#include <map>
#include <optional>
#include <set>

#include "app/Editor.h"
#include "core/PathUtil.h"

namespace tf {

namespace fs = std::filesystem;

namespace {

bool isBlankCh(char c) { return c == ' ' || c == '\t'; }
bool isIdentCh(char c) {
  const unsigned char u = static_cast<unsigned char>(c);
  return std::isalnum(u) || u == '_' || u == '$' || u >= 0x80;
}

size_t leadingBlanks(const std::string& l, size_t from = 0) {
  size_t i = from;
  while (i < l.size() && isBlankCh(l[i])) ++i;
  return i - from;
}
bool isBlankLine(const std::string& l) { return leadingBlanks(l) == l.size(); }
std::string trimRight(const std::string& l) {
  size_t e = l.size();
  while (e > 0 && isBlankCh(l[e - 1])) --e;
  return l.substr(0, e);
}
bool endsWith(const std::string& s, const char* suffix) {
  const size_t n = std::char_traits<char>::length(suffix);
  return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

char closerOf(char open) { return open == '(' ? ')' : open == '[' ? ']' : open == '{' ? '}' : open; }

// `line` with string / character literals and a trailing // comment blanked out (same length).
std::string codeOnly(const std::string& line) {
  std::string out = line;
  char quote = 0;
  for (size_t i = 0; i < out.size(); ++i) {
    const char c = out[i];
    if (quote) {
      if (c == '\\' && i + 1 < out.size()) out[i + 1] = ' ';
      else if (c == quote) quote = 0;
      out[i] = ' ';
    } else if (c == '"' || c == '\'') {
      quote = c;
      out[i] = ' ';
    } else if (c == '/' && i + 1 < out.size() && out[i + 1] == '/') {
      std::fill(out.begin() + static_cast<long>(i), out.end(), ' ');
      break;
    }
  }
  return out;
}

// The indentation of the line holding the '{' that the '}' about to be typed at the start of line `from` closes.
std::optional<std::string> openBraceIndent(const TextBuffer& b, int from) {
  int depth = 1;
  for (int l = from - 1; l >= 0 && l >= from - 5000; --l) {
    const std::string code = codeOnly(b.line(l));
    for (size_t i = code.size(); i-- > 0;) {
      if (code[i] == '}') ++depth;
      else if (code[i] == '{' && --depth == 0) return b.line(l).substr(0, leadingBlanks(b.line(l)));
    }
  }
  return std::nullopt;
}

// "Continuation": the (right-trimmed) line ends with a binary operator, so the statement goes on.
bool endsWithOperator(const std::string& code) {
  const std::string t = trimRight(code);
  if (t.empty() || t[leadingBlanks(t)] == '#') return false;
  if (endsWith(t, "++") || endsWith(t, "--") || endsWith(t, "*/")) return false;
  if (endsWith(t, "<<")) return true;
  return std::string("+-*/%&|^=?").find(t.back()) != std::string::npos;
}
bool opensBlock(const std::string& code) {
  const std::string t = trimRight(code);
  return !t.empty() && (t.back() == '{' || t.back() == '(' || t.back() == '[');
}

// Maps `p` (old coordinates) through sorted, disjoint edits whose replacement ranges are `now`. A position inside a
// replaced range moves to the start of its replacement. At a pure insertion exactly at `p` the position ends up after
// the inserted text, or before it with `stickBefore` (the start of a selection, so it keeps covering the insertion).
Position mapThrough(const std::vector<Range>& old, const std::vector<Range>& now, Position p, bool stickBefore) {
  const auto passed = [&](const Range& r) { return r.end < p || (r.end == p && !(stickBefore && r.start == r.end)); };
  const size_t last = static_cast<size_t>(std::partition_point(old.begin(), old.end(), passed) - old.begin());
  if (last < old.size() && old[last].start < p) return now[last].start;
  if (last == 0) return p;
  const Position o = old[last - 1].end, n = now[last - 1].end;
  if (p.line == o.line) return {n.line, n.column + (p.column - o.column)};
  return {p.line + (n.line - o.line), p.column};
}

}  // namespace

// ---------------------------------------------------------------------------
// Plumbing
// ---------------------------------------------------------------------------

// Merged, inclusive line ranges the carets touch. A selection that ends at column 0 of a later line does not touch
// that line.
std::vector<std::pair<int, int>> Editor::selectedLineBlocks() const {
  std::vector<std::pair<int, int>> blocks;
  for (const Selection& s : carets_) {
    const Range r = s.range();
    int last = r.end.line;
    if (!s.empty() && r.end.column == 0 && last > r.start.line) --last;
    blocks.push_back({r.start.line, last});
  }
  std::sort(blocks.begin(), blocks.end());
  std::vector<std::pair<int, int>> out;
  for (const auto& b : blocks) {
    if (!out.empty() && b.first <= out.back().second + 1) out.back().second = std::max(out.back().second, b.second);
    else out.push_back(b);
  }
  return out;
}

void Editor::runLineEdits(std::vector<Document::BatchEdit> edits, const CaretFn& carets) {
  if (!current_ || edits.empty()) return;
  breakTyping();
  Document& doc = *current_;
  std::vector<Range> old;
  old.reserve(edits.size());
  for (const auto& e : edits) old.push_back(e.range);
  doc.beginGroup(carets_);
  inEdit_ = true;
  const std::vector<Range> now = doc.replaceMany(std::move(edits));
  inEdit_ = false;
  size_t primary = 0;
  std::vector<Selection> next;
  if (carets) {
    next = carets(now, primary);
  } else {
    for (const Selection& s : carets_) {
      const bool empty = s.empty();
      next.push_back({mapThrough(old, now, s.anchor, !empty && !s.reversed()),
                      mapThrough(old, now, s.head, !empty && s.reversed())});
    }
  }
  if (next.empty()) next.push_back(caretAt(cursor_.pos));
  carets_ = std::move(next);
  wants_.assign(carets_.size(), -1);
  clampCarets();
  normalizeCarets(primary);
  doc.endGroup(carets_);
  noteEdit();
}

// ---------------------------------------------------------------------------
// Indentation
// ---------------------------------------------------------------------------

// Tabs vs. spaces and the most common indentation step among the first lines. Comment continuation lines (" * ...")
// and blank lines do not vote.
std::string Editor::indentUnitFor(const Document& doc) const {
  if (!editing_.detectIndent) return editing_.indentUnit;
  const TextBuffer& b = doc.buffer();
  int tabs = 0, spaces = 0, prev = 0;
  std::map<int, int> steps;
  for (int i = 0; i < b.lineCount() && i < 400; ++i) {
    const std::string& l = b.line(i);
    if (isBlankLine(l)) continue;
    const size_t ind = leadingBlanks(l);
    if (l[ind] == '*') continue;
    if (l[0] == '\t') {
      ++tabs;
      prev = 0;
    } else if (l[0] == ' ') {
      ++spaces;
      if (static_cast<int>(ind) > prev) ++steps[static_cast<int>(ind) - prev];
      prev = static_cast<int>(ind);
    } else {
      prev = 0;
    }
  }
  if (tabs > 0 && tabs >= spaces) return "\t";
  int best = 0, count = 0;
  for (const auto& [step, n] : steps)
    if (step >= 2 && step <= 8 && n > count) best = step, count = n;
  return best ? std::string(static_cast<size_t>(best), ' ') : editing_.indentUnit;
}

void Editor::insertNewline() {
  if (!current_ || carets_.empty()) return;
  if (!editing_.autoIndent) {
    insertText("\n");
    return;
  }
  const std::string unit = indentUnitFor(*current_);
  const TextBuffer& b = current_->buffer();
  std::vector<CaretEdit> edits;
  edits.reserve(carets_.size());
  for (const Selection& s : carets_) {
    Range r = s.range();
    const std::string& first = b.line(r.start.line);
    const std::string& lastLine = b.line(r.end.line);
    const std::string pre = first.substr(0, static_cast<size_t>(r.start.column));
    const std::string post = lastLine.substr(static_cast<size_t>(r.end.column));
    const std::string code = codeOnly(trimRight(pre));
    const std::string ind = first.substr(0, std::min(leadingBlanks(first), pre.size()));
    // Does the line above (the previous non-blank one) end with an operator, i.e. is this line a continuation?
    bool aboveOp = false;
    for (int l = r.start.line - 1; l >= 0; --l) {
      if (isBlankLine(b.line(l))) continue;
      aboveOp = endsWithOperator(codeOnly(b.line(l)));
      break;
    }
    std::string next = ind;
    if (opensBlock(code)) next += unit;
    else if (endsWithOperator(code) && !aboveOp) next += unit;
    else if (aboveOp && endsWith(trimRight(code), ";") && ind.size() >= unit.size() &&
             ind.compare(ind.size() - unit.size(), unit.size(), unit) == 0)
      next.erase(next.size() - unit.size());  // the continued statement ended
    std::string text = "\n" + next;
    CaretEdit e;
    const size_t postBlanks = leadingBlanks(post);
    const char opener = trimRight(pre).empty() ? '\0' : trimRight(pre).back();
    const bool expand =
        postBlanks < post.size() && opener != '\0' && opensBlock(code) && post[postBlanks] == closerOf(opener);
    if (expand) {
      e.headOffset = text.size();
      text += "\n" + ind;
    }
    r.start.column = static_cast<int>(trimRight(pre).size());
    r.end.column += static_cast<int>(postBlanks);
    e.range = r;
    e.text = std::move(text);
    edits.push_back(std::move(e));
  }
  typeEdits(std::move(edits), "\n");
}

void Editor::typeText(const std::string& text) {
  if (!current_ || carets_.empty()) return;
  const char c = text.size() == 1 ? text[0] : '\0';
  const bool opener = c == '(' || c == '[' || c == '{', quote = c == '"' || c == '\'',
             closer = c == ')' || c == ']' || c == '}';
  const bool pairs = editing_.autoClosePairs && (opener || quote || closer);
  const bool dedent = editing_.autoIndent && c == '}';
  if (!pairs && !dedent) {
    insertText(text);
    return;
  }
  const TextBuffer& b = current_->buffer();
  std::vector<CaretEdit> edits;
  edits.reserve(carets_.size());
  bool allSkip = true;
  for (const Selection& s : carets_) {
    CaretEdit e{s.range(), text};
    bool skip = false;
    if (!s.empty()) {
      if (pairs && (opener || quote)) {  // wrap the selection and keep it selected
        e.text = std::string(1, c) + b.textInRange(e.range) + closerOf(c);
        e.anchorOffset = s.reversed() ? e.text.size() - 1 : 1;
        e.headOffset = s.reversed() ? 1 : e.text.size() - 1;
      }
    } else {
      const Position p = s.head;
      const std::string& line = b.line(p.line);
      const char next = p.column < static_cast<int>(line.size()) ? line[static_cast<size_t>(p.column)] : '\0';
      const char prev = p.column > 0 ? line[static_cast<size_t>(p.column) - 1] : '\0';
      const bool nextOpen =
          next == '\0' || std::isspace(static_cast<unsigned char>(next)) || std::strchr(")]};,", next);
      if (pairs && (closer || quote) && next == c && !(quote && prev == '\\')) {
        skip = true;  // typing over the closer that is already there
        e.range = {p, {p.line, p.column + 1}};
      } else if (pairs && opener && nextOpen) {
        e.text = std::string(1, c) + closerOf(c);
        e.headOffset = 1;
      } else if (pairs && quote && prev != '\\' && !isIdentCh(prev) && prev != c && nextOpen) {
        e.text = std::string(2, c);
        e.headOffset = 1;
      } else if (dedent && static_cast<size_t>(p.column) <= leadingBlanks(line)) {
        // '}' as the first non-blank character: line up with the line of the '{' it closes.
        if (const auto ind = openBraceIndent(b, p.line)) {
          e.range = {{p.line, 0}, p};
          e.text = *ind + "}";
        }
      }
    }
    allSkip = allSkip && skip;
    edits.push_back(std::move(e));
  }
  if (allSkip) {  // only caret movement: no edit, no undo step
    breakTyping();
    for (Selection& s : carets_) s = caretAt({s.head.line, s.head.column + 1});
    wants_.assign(carets_.size(), -1);
    normalizeCarets();
    return;
  }
  typeEdits(std::move(edits), text);
}

void Editor::insertTab() {
  if (!current_ || carets_.empty()) return;
  for (const Selection& s : carets_)
    if (!s.empty() && s.min().line != s.max().line) {
      indentLines();
      return;
    }
  insertText(indentUnitFor(*current_));
}

void Editor::indentLines() {
  if (!current_) return;
  const std::string unit = indentUnitFor(*current_);
  const TextBuffer& b = current_->buffer();
  std::vector<Document::BatchEdit> edits;
  for (const auto& [first, last] : selectedLineBlocks())
    for (int l = first; l <= last; ++l)
      if (!b.line(l).empty()) edits.push_back({{{l, 0}, {l, 0}}, unit});
  runLineEdits(std::move(edits));
}

void Editor::outdentLines() {
  if (!current_) return;
  const std::string unit = indentUnitFor(*current_);
  const size_t width = unit == "\t" ? 4 : unit.size();
  const TextBuffer& b = current_->buffer();
  std::vector<Document::BatchEdit> edits;
  for (const auto& [first, last] : selectedLineBlocks())
    for (int l = first; l <= last; ++l) {
      const std::string& s = b.line(l);
      size_t n = 0;
      if (!s.empty() && s[0] == '\t') n = 1;
      else
        while (n < s.size() && n < width && s[n] == ' ') ++n;
      if (n) edits.push_back({{{l, 0}, {l, static_cast<int>(n)}}, {}});
    }
  runLineEdits(std::move(edits));
}

// ---------------------------------------------------------------------------
// Comments
// ---------------------------------------------------------------------------

void Editor::toggleLineComment() {
  if (!current_) return;
  const TextBuffer& b = current_->buffer();
  std::vector<int> lines;
  for (const auto& [first, last] : selectedLineBlocks())
    for (int l = first; l <= last; ++l)
      if (!isBlankLine(b.line(l))) lines.push_back(l);
  if (lines.empty()) return;
  bool all = true;
  size_t minIndent = std::string::npos;
  for (int l : lines) {
    const std::string& s = b.line(l);
    const size_t ind = leadingBlanks(s);
    all = all && s.compare(ind, 2, "//") == 0;
    minIndent = std::min(minIndent, ind);
  }
  std::vector<Document::BatchEdit> edits;
  for (int l : lines) {
    const std::string& s = b.line(l);
    const size_t ind = leadingBlanks(s);
    if (all) {
      const size_t len = s.compare(ind + 2, 1, " ") == 0 ? 3 : 2;  // "// " or "//"
      edits.push_back({{{l, static_cast<int>(ind)}, {l, static_cast<int>(ind + len)}}, {}});
    } else {
      edits.push_back({{{l, static_cast<int>(minIndent)}, {l, static_cast<int>(minIndent)}}, "// "});
    }
  }
  runLineEdits(std::move(edits));
}

void Editor::toggleBlockComment() {
  if (!current_ || carets_.empty()) return;
  breakTyping();
  const TextBuffer& b = current_->buffer();
  std::vector<CaretEdit> edits;
  std::set<int> usedLines;  // two bare carets on one line comment it once
  for (const Selection& s : carets_) {
    Range r = s.range();
    bool whole = false;
    if (s.empty()) {  // a caret: the text of its line
      const std::string& l = b.line(r.start.line);
      const size_t from = leadingBlanks(l), to = trimRight(l).size();
      if (from >= to || !usedLines.insert(r.start.line).second) {
        edits.push_back({r, {}});
        continue;
      }
      r = {{r.start.line, static_cast<int>(from)}, {r.start.line, static_cast<int>(to)}};
      whole = true;
    }
    const std::string t = b.textInRange(r);
    CaretEdit e;
    e.range = r;
    if (t.size() >= 4 && t.compare(0, 2, "/*") == 0 && endsWith(t, "*/")) e.text = t.substr(2, t.size() - 4);
    else e.text = "/*" + t + "*/";
    if (!whole) {  // the result stays selected
      e.anchorOffset = s.reversed() ? e.text.size() : 0;
      e.headOffset = s.reversed() ? 0 : e.text.size();
    }
    edits.push_back(std::move(e));
  }
  applyCaretEdits(std::move(edits), false);
}

// ---------------------------------------------------------------------------
// Line commands
// ---------------------------------------------------------------------------

void Editor::duplicate() {
  if (!current_ || carets_.empty()) return;
  breakTyping();
  const TextBuffer& b = current_->buffer();
  std::vector<CaretEdit> edits;
  std::set<int> usedLines;
  for (const Selection& s : carets_) {
    const Range r = s.range();
    CaretEdit e;
    if (!s.empty()) {  // the copy goes right behind the selection and becomes the selection
      e.range = {r.end, r.end};
      e.text = b.textInRange(r);
      e.anchorOffset = s.reversed() ? e.text.size() : 0;
      e.headOffset = s.reversed() ? 0 : e.text.size();
    } else if (usedLines.insert(s.head.line).second) {  // the line below, caret at the same column
      const std::string& l = b.line(s.head.line);
      e.range = {{s.head.line, static_cast<int>(l.size())}, {s.head.line, static_cast<int>(l.size())}};
      e.text = "\n" + l;
      e.headOffset = 1 + static_cast<size_t>(s.head.column);
    } else {
      e.range = {s.head, s.head};
      e.headOffset = 0;
    }
    edits.push_back(std::move(e));
  }
  applyCaretEdits(std::move(edits), false);
}

void Editor::moveLines(bool down) {
  if (!current_) return;
  const TextBuffer& b = current_->buffer();
  const int lastLine = b.lineCount() - 1;
  const auto blocks = selectedLineBlocks();
  std::vector<Document::BatchEdit> edits;
  std::vector<bool> moved(blocks.size(), false);
  for (size_t i = 0; i < blocks.size(); ++i) {
    const auto [a, z] = blocks[i];
    if (down ? z >= lastLine : a <= 0) continue;
    std::string block;
    for (int l = a; l <= z; ++l) block += (l > a ? "\n" : "") + b.line(l);
    if (down) {
      edits.push_back({{{a, 0}, {z + 1, static_cast<int>(b.line(z + 1).size())}}, b.line(z + 1) + "\n" + block});
    } else {
      edits.push_back({{{a - 1, 0}, {z, static_cast<int>(b.line(z).size())}}, block + "\n" + b.line(a - 1)});
    }
    moved[i] = true;
  }
  const std::vector<Selection> old = carets_;
  const int delta = down ? 1 : -1;
  runLineEdits(std::move(edits), [&](const std::vector<Range>&, size_t& primary) {
    std::vector<Selection> next;
    for (const Selection& s : old) {
      const int line = s.min().line;
      size_t k = 0;
      while (k < blocks.size() && !(line >= blocks[k].first && line <= blocks[k].second)) ++k;
      const int d = k < blocks.size() && moved[k] ? delta : 0;
      next.push_back({{s.anchor.line + d, s.anchor.column}, {s.head.line + d, s.head.column}});
    }
    primary = 0;
    return next;
  });
}

void Editor::joinLines() {
  if (!current_) return;
  const TextBuffer& b = current_->buffer();
  std::set<int> joins;  // line l is joined with l + 1
  for (const Selection& s : carets_) {
    const Range r = s.range();
    for (int l = r.start.line; l <= std::max(r.start.line, r.end.line - 1); ++l)
      if (l + 1 < b.lineCount()) joins.insert(l);
  }
  std::vector<Document::BatchEdit> edits;
  for (int l : joins) {
    const std::string& left = b.line(l);
    const std::string& right = b.line(l + 1);
    const size_t lead = leadingBlanks(right);
    const bool space = !left.empty() && !isBlankCh(left.back()) && lead < right.size();
    edits.push_back({{{l, static_cast<int>(left.size())}, {l + 1, static_cast<int>(lead)}}, space ? " " : ""});
  }
  runLineEdits(std::move(edits));
}

void Editor::deleteLines() {
  if (!current_) return;
  const TextBuffer& b = current_->buffer();
  const int n = b.lineCount();
  const auto blocks = selectedLineBlocks();
  std::vector<Document::BatchEdit> edits;
  std::vector<Position> where;  // per block: the caret after the deletion (line in the new buffer)
  size_t primary = 0;
  int removed = 0;
  for (size_t i = 0; i < blocks.size(); ++i) {
    const auto [a, z] = blocks[i];
    int col = std::numeric_limits<int>::max();
    for (const Selection& s : carets_)
      if (s.head.line >= a && s.head.line <= z) {
        col = std::min(col, s.head.column);
        if (&s == &carets_[0]) primary = i;
      }
    if (z + 1 < n) {
      edits.push_back({{{a, 0}, {z + 1, 0}}, {}});
      where.push_back({a - removed, col});
    } else if (a > 0) {
      edits.push_back({{{a - 1, static_cast<int>(b.line(a - 1).size())}, {z, static_cast<int>(b.line(z).size())}}, {}});
      where.push_back({a - 1 - removed, col});
    } else {
      edits.push_back({{{0, 0}, {z, static_cast<int>(b.line(z).size())}}, {}});
      where.push_back({0, 0});
    }
    removed += z - a + 1;
  }
  runLineEdits(std::move(edits), [&](const std::vector<Range>&, size_t& prim) {
    std::vector<Selection> next;
    for (const Position& p : where) next.push_back(caretAt(current_->buffer().clamp(p)));
    prim = primary;
    return next;
  });
}

bool Editor::gotoLine(int line, int column, std::string* error) {
  if (!current_) {
    if (error) *error = "no document";
    return false;
  }
  const TextBuffer& b = current_->buffer();
  const int l = std::clamp(line, 1, b.lineCount()) - 1;
  const int c = std::clamp(column, 1, static_cast<int>(b.line(l).size()) + 1) - 1;
  return gotoLocation({current_->path(), {l, c}}, error, "line " + std::to_string(l + 1));
}

void Editor::setLineEnding(Document& doc, LineEnding e) { doc.setLineEnding(e); }

// ---------------------------------------------------------------------------
// Find / replace in the current document
// ---------------------------------------------------------------------------

std::vector<Range> Editor::findInDocument(const SearchOptions& opts, std::string* error) {
  std::vector<Range> out;
  if (!current_ || opts.query.empty()) return out;
  SearchInput in;  // TextSearch's matcher over the in-memory text, via an override for a synthetic file
  in.root = "tf-find";
  in.files = {in.root / "doc"};
  in.overrides[pathKey("doc", nativePathStyle())] = current_->buffer().text();
  std::vector<SearchMatch> found;
  const SearchSummary sum = TextSearch::run(std::move(in), opts, &found);
  if (!sum.error.empty() && error) *error = sum.error;
  std::sort(found.begin(), found.end(), [](const SearchMatch& a, const SearchMatch& b) {
    return a.line != b.line ? a.line < b.line : a.column < b.column;
  });
  out.reserve(found.size());
  for (const SearchMatch& m : found) out.push_back({{m.line, m.column}, {m.line, m.column + m.length}});
  return out;
}

size_t Editor::replaceAll(const SearchOptions& opts, const std::string& replacement, std::string* error) {
  SearchOptions o = opts;
  o.maxResults = static_cast<size_t>(1) << 30;
  const std::vector<Range> hits = findInDocument(o, error);
  std::vector<Document::BatchEdit> edits;
  edits.reserve(hits.size());
  for (const Range& r : hits) edits.push_back({r, replacement});
  runLineEdits(std::move(edits));
  return hits.size();
}

// ---------------------------------------------------------------------------
// Bookmarks
// ---------------------------------------------------------------------------

bool Editor::toggleBookmark() {
  if (!current_ || carets_.empty()) return false;
  const bool now = bookmarks_.toggle(current_->path(), carets_[0].head.line);
  saveBookmarks();
  return now;
}

bool Editor::gotoBookmarkItem(const Bookmarks::Item& b) {
  return gotoLocation({b.file, {b.line, 0}}, nullptr, "bookmark");
}

bool Editor::gotoBookmark(bool forward) {
  if (!current_) return false;
  const auto item =
      forward ? bookmarks_.next(cursor_.file, cursor_.pos.line) : bookmarks_.prev(cursor_.file, cursor_.pos.line);
  return item && gotoBookmarkItem(*item);
}

void Editor::saveBookmarks() {
  if (bookmarks_.dirty()) bookmarks_.save(ws_.cacheDir() / "bookmarks.json", ws_.root());
}

}  // namespace tf
