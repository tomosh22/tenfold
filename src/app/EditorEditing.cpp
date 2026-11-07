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
  for (Document::BatchEdit& e : edits)
    e.text = withDocBreaks(doc, doc.buffer().clamp(e.range.start).line, std::move(e.text));
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
    const bool expand = editing_.formatBracesOnEnterSplit && postBlanks < post.size() && opener != '\0' &&
                        opensBlock(code) && post[postBlanks] == closerOf(opener);
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
  const bool opener = c == '(' || c == '[' || c == '{', quote = c == '"' || c == '\'';
  // Which characters this keystroke pairs / skips over / wraps with, per the 10x settings.
  const bool pairOpener =
      (c == '{' && editing_.autoCloseBrace) || ((c == '(' || c == '[') && editing_.autoCloseBracket);
  const bool pairCloser =
      (c == '}' && editing_.autoCloseBrace) || ((c == ')' || c == ']') && editing_.autoCloseBracket);
  const bool pairQuote = quote && editing_.autoCloseQuotes;
  const bool wrap = (opener && editing_.bracketSelectedText) || pairQuote;
  const bool dedent = editing_.autoIndent && editing_.autoIndentBraces && c == '}';
  if (!pairOpener && !pairCloser && !pairQuote && !wrap && !dedent) {
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
      if (wrap) {  // wrap the selection and keep it selected
        e.text = std::string(1, c) + b.textInRangeRaw(e.range) + closerOf(c);
        e.literal = true;
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
      if ((pairCloser || pairQuote) && next == c && !(quote && prev == '\\')) {
        skip = true;  // typing over the closer that is already there
        e.range = {p, {p.line, p.column + 1}};
      } else if (pairOpener && nextOpen) {
        e.text = std::string(1, c) + closerOf(c);
        e.headOffset = 1;
      } else if (pairQuote && prev != '\\' && !isIdentCh(prev) && prev != c && nextOpen) {
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

void Editor::toggleLineComment() { lineComments(LineCommentMode::Toggle); }
void Editor::commentLines() { lineComments(LineCommentMode::Comment); }
void Editor::uncommentLines() { lineComments(LineCommentMode::Uncomment); }

// Toggle: uncomments when every non-blank line has "//", else comments them all. Comment: always adds "// " (at the
// minimum indentation). Uncomment: removes "//" (and one following space) from the lines that have it.
void Editor::lineComments(LineCommentMode mode) {
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
  const bool remove = mode == LineCommentMode::Uncomment || (mode == LineCommentMode::Toggle && all);
  std::vector<Document::BatchEdit> edits;
  for (int l : lines) {
    const std::string& s = b.line(l);
    const size_t ind = leadingBlanks(s);
    if (remove) {
      if (s.compare(ind, 2, "//") != 0) continue;
      const size_t len = s.compare(ind + 2, 1, " ") == 0 ? 3 : 2;  // "// " or "//"
      edits.push_back({{{l, static_cast<int>(ind)}, {l, static_cast<int>(ind + len)}}, {}});
    } else {
      edits.push_back({{{l, static_cast<int>(minIndent)}, {l, static_cast<int>(minIndent)}}, "// "});
    }
  }
  runLineEdits(std::move(edits));
}

// ToggleComment (10x): a selection follows CommentBlockUsesLineComments (true: line comments, false: /* */ around it);
// carets without a selection always toggle line comments. With several carets the block form needs every caret to
// have a selection.
void Editor::toggleComment() {
  if (!current_ || carets_.empty()) return;
  const bool allSelected = std::all_of(carets_.begin(), carets_.end(), [](const Selection& s) { return !s.empty(); });
  if (allSelected && !editing_.commentBlockUsesLineComments) toggleBlockComment();
  else toggleLineComment();
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
    const std::string t = b.textInRangeRaw(r);
    CaretEdit e;
    e.range = r;
    e.literal = true;
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
      e.text = b.textInRangeRaw(r);
      e.literal = true;
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

LineEnding Editor::newLineEndingAt(const Document& doc, int line) const {
  if (!editing_.replicateNewLine) return editing_.defaultLineEnding;
  return doc.buffer().endingForNewLine(line, editing_.defaultLineEnding);
}

std::string Editor::withDocBreaks(const Document& doc, int line, std::string text) const {
  if (TextBuffer::countBreaks(text) == 0) return text;
  return TextBuffer::withLineEnding(text, newLineEndingAt(doc, line));
}

void Editor::setLineEnding(Document& doc, LineEnding e) {
  const bool cur = &doc == current_;
  if (cur) breakTyping();
  doc.beginGroup(cur ? carets_ : std::vector<Selection>{});
  const bool was = inEdit_;
  inEdit_ = true;
  doc.convertLineEndings(e);
  inEdit_ = was;
  doc.endGroup(cur ? carets_ : std::vector<Selection>{});
}

bool Editor::saveWithLineEnding(Document& doc, LineEnding e, std::string* error) {
  setLineEnding(doc, e);
  return save(doc, error);
}

bool Editor::saveWithEncoding(Document& doc, EncodingChoice c, std::optional<LineEnding> ending, std::string* error,
                              UnencodableReport* refused) {
  UnencodableReport rep = doc.findUnencodable(c);
  if (ending && isUnicodeSeparator(*ending) && rep.encoding == Encoding::Windows1252 && doc.buffer().lineCount() > 1) {
    // The requested line ends (NEL / LS / PS) are characters Windows-1252 does not have; report the first one.
    const std::string_view t = TextBuffer::terminatorText(*ending);
    UnencodableChar ch;
    ch.codePoint = t.size() == 2 ? 0x85u : t[2] == '\xA8' ? 0x2028u : 0x2029u;
    ch.column = static_cast<int>(doc.buffer().line(0).size());
    ch.text = std::string(t);
    rep.chars.insert(rep.chars.begin(), std::move(ch));
    rep.total += static_cast<size_t>(doc.buffer().lineCount() - 1);
  }
  if (!rep.empty()) {  // refuse before touching anything: encoding, text and line endings stay as they are
    if (error) *error = doc.path().filename().string() + " was not saved: " + describeUnencodable(rep);
    noteSaveRefused(doc, rep);
    if (refused) *refused = std::move(rep);
    return false;
  }
  const bool cur = &doc == current_;
  if (cur) breakTyping();
  const bool dropsBom = doc.buffer().line(0).compare(0, 3, "\xEF\xBB\xBF") == 0 && c != EncodingChoice::Utf8Signature;
  doc.beginGroup(cur ? carets_ : std::vector<Selection>{});
  const bool was = inEdit_;
  inEdit_ = true;
  doc.applyEncodingChoice(c);
  if (cur && dropsBom) {  // the signature left line 1: carets behind it move three bytes left
    auto shift = [](Position& p) {
      if (p.line == 0) p.column = std::max(0, p.column - 3);
    };
    for (Selection& s : carets_) {
      shift(s.anchor);
      shift(s.head);
    }
  }
  if (ending) doc.convertLineEndings(*ending);
  inEdit_ = was;
  doc.endGroup(cur ? carets_ : std::vector<Selection>{});
  if (cur) clampCarets();
  if (doc.isUntitled()) return true;  // nothing to write yet: saveAs writes it in this encoding
  return save(doc, error, refused);
}

// ---------------------------------------------------------------------------
// 10x editing commands (W8a)
// ---------------------------------------------------------------------------

namespace {

struct BracketPair {
  Position open, close;
};

// Every matched ( ) [ ] { } pair of the buffer, ignoring brackets in comments, string and character literals (a quote
// that is not closed ends at the end of its line). Mismatched closers are skipped.
std::vector<BracketPair> bracketPairs(const TextBuffer& b, bool bracesOnly) {
  std::vector<BracketPair> pairs;
  std::vector<std::pair<char, Position>> stack;
  bool block = false;
  for (int l = 0; l < b.lineCount(); ++l) {
    const std::string& s = b.line(l);
    char quote = 0;
    for (size_t i = 0; i < s.size(); ++i) {
      const char c = s[i];
      const char n = i + 1 < s.size() ? s[i + 1] : '\0';
      if (block) {
        if (c == '*' && n == '/') block = false, ++i;
      } else if (quote) {
        if (c == '\\') ++i;
        else if (c == quote) quote = 0;
      } else if (c == '/' && n == '/') {
        break;
      } else if (c == '/' && n == '*') {
        block = true;
        ++i;
      } else if (c == '"' || c == '\'') {
        quote = c;
      } else if (c == '{' || (!bracesOnly && (c == '(' || c == '['))) {
        stack.push_back({c, {l, static_cast<int>(i)}});
      } else if (c == '}' || (!bracesOnly && (c == ')' || c == ']'))) {
        if (!stack.empty() && closerOf(stack.back().first) == c) {
          pairs.push_back({stack.back().second, {l, static_cast<int>(i)}});
          stack.pop_back();
        }
      }
    }
  }
  return pairs;
}

}  // namespace

std::string Editor::cutLines() {
  if (!current_) return {};
  const TextBuffer& b = current_->buffer();
  std::string out;
  for (const auto& [first, last] : selectedLineBlocks())
    for (int l = first; l <= last; ++l) out += b.line(l) + "\n";
  deleteLines();
  return out;
}

void Editor::makeSelectionUppercase() { changeSelectionCase(true); }
void Editor::makeSelectionLowercase() { changeSelectionCase(false); }

// Only ASCII letters change (multi-byte characters are left alone); the length stays, so every selection survives.
void Editor::changeSelectionCase(bool upper) {
  if (!current_ || carets_.empty()) return;
  const TextBuffer& b = current_->buffer();
  std::vector<CaretEdit> edits;
  bool changes = false;
  for (const Selection& s : carets_) {
    CaretEdit e;
    e.range = s.range();
    if (!s.empty()) {
      e.text = b.textInRangeRaw(e.range);
      e.literal = true;
      const std::string before = e.text;
      for (char& c : e.text) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x80) c = static_cast<char>(upper ? std::toupper(u) : std::tolower(u));
      }
      changes = changes || e.text != before;
      e.anchorOffset = s.reversed() ? e.text.size() : 0;
      e.headOffset = s.reversed() ? 0 : e.text.size();
    }
    edits.push_back(std::move(e));
  }
  if (!changes) return;
  breakTyping();
  applyCaretEdits(std::move(edits), false);
}

void Editor::deleteToEndOfLine() {
  if (!current_ || carets_.empty()) return;
  const TextBuffer& b = current_->buffer();
  std::vector<CaretEdit> edits;
  bool changes = false;
  for (const Selection& s : carets_) {
    const Range r = s.range();
    CaretEdit e;
    e.range = {r.start, {r.end.line, static_cast<int>(b.line(r.end.line).size())}};
    changes = changes || e.range.start != e.range.end;
    edits.push_back(std::move(e));
  }
  if (!changes) return;
  breakTyping();
  applyCaretEdits(std::move(edits), false);
}

void Editor::insertLineAbove() {
  if (!current_ || carets_.empty()) return;
  breakTyping();
  const TextBuffer& b = current_->buffer();
  std::vector<CaretEdit> edits;
  std::set<int> usedLines;
  for (const Selection& s : carets_) {
    const int l = s.head.line;
    CaretEdit e;
    if (usedLines.insert(l).second) {
      const std::string ind = editing_.autoIndent ? b.line(l).substr(0, leadingBlanks(b.line(l))) : std::string();
      e.range = {{l, 0}, {l, 0}};
      e.text = ind + "\n";
      e.headOffset = ind.size();
    } else {  // a second caret on the same line stays where it is
      e.range = {s.head, s.head};
      e.headOffset = 0;
    }
    edits.push_back(std::move(e));
  }
  applyCaretEdits(std::move(edits), false);
}

void Editor::insertLineBelow() {
  if (!current_ || carets_.empty()) return;
  breakTyping();
  const TextBuffer& b = current_->buffer();
  std::vector<CaretEdit> edits;
  std::set<int> usedLines;
  for (const Selection& s : carets_) {
    const int l = s.max().line;
    const std::string& line = b.line(l);
    const Position eol{l, static_cast<int>(line.size())};
    CaretEdit e;
    if (usedLines.insert(l).second) {
      std::string ind = editing_.autoIndent ? line.substr(0, leadingBlanks(line)) : std::string();
      if (editing_.autoIndent && opensBlock(codeOnly(line))) ind += indentUnitFor(*current_);
      e.range = {eol, eol};
      e.text = "\n" + ind;
    } else {
      e.range = {s.head, s.head};
      e.headOffset = 0;
    }
    edits.push_back(std::move(e));
  }
  applyCaretEdits(std::move(edits), false);
}

void Editor::selectCurrentWord() {
  if (!current_ || carets_.empty()) return;
  breakTyping();
  const TextBuffer& b = current_->buffer();
  std::vector<Selection> next = carets_;
  bool changed = false;
  for (Selection& s : next) {
    const auto w = b.wordAt(s.head);
    if (w.text.empty()) continue;
    s = {w.range.start, w.range.end};
    changed = true;
  }
  if (changed) setSelections(std::move(next));
}

// For each selection the smallest candidate that strictly contains it: per enclosing pair, first the text between the
// brackets, then the pair including them; repeated calls therefore grow outward.
void Editor::growSelectionToBrackets(bool bracesOnly) {
  if (!current_ || carets_.empty()) return;
  breakTyping();
  std::vector<BracketPair> pairs = bracketPairs(current_->buffer(), bracesOnly);
  std::sort(pairs.begin(), pairs.end(), [](const BracketPair& a, const BracketPair& b) { return b.open < a.open; });
  std::vector<Selection> next = carets_;
  bool changed = false;
  for (Selection& s : next) {
    const Range sel = s.range();
    for (const BracketPair& p : pairs) {
      const Range inner{{p.open.line, p.open.column + 1}, p.close};
      const Range outer{p.open, {p.close.line, p.close.column + 1}};
      bool done = false;
      for (const Range& c : {inner, outer})
        if (c.start <= sel.start && sel.end <= c.end && (c.start != sel.start || c.end != sel.end)) {
          s = {c.start, c.end};
          changed = done = true;
          break;
        }
      if (done) break;
    }
  }
  if (changed) setSelections(std::move(next));
}

void Editor::selectCurrentScope() { growSelectionToBrackets(true); }
void Editor::selectCurrentBracketScope() { growSelectionToBrackets(false); }

bool Editor::moveToMatchingBracket() {
  if (!current_ || carets_.empty()) return false;
  breakTyping();
  const std::vector<BracketPair> pairs = bracketPairs(current_->buffer(), false);
  std::map<Position, Position> partner;
  for (const BracketPair& p : pairs) {
    partner[p.open] = p.close;
    partner[p.close] = p.open;
  }
  std::vector<Selection> next = carets_;
  bool moved = false;
  for (Selection& s : next) {
    for (int back = 0; back < 2; ++back) {  // the bracket after the caret first, then the one before it
      const Position at{s.head.line, s.head.column - back};
      if (at.column < 0) continue;
      const auto it = partner.find(at);
      if (it == partner.end()) continue;
      s = caretAt({it->second.line, it->second.column + back});
      moved = true;
      break;
    }
  }
  if (moved) setSelections(std::move(next));
  return moved;
}

bool Editor::saveAll(std::string* error, std::vector<SaveRefusal>* refused) {
  bool ok = true;
  for (Document* d : documents()) {
    if (!d->dirty()) continue;
    std::string err;
    UnencodableReport rep;
    if (save(*d, &err, &rep)) continue;
    if (!rep.empty() && refused) {  // reported through `refused` instead of `error`
      refused->push_back({d->path(), std::move(rep)});
      ok = false;
      continue;
    }
    if (ok && error) *error = err.empty() ? "cannot save " + pathUtf8(d->path().filename()) : err;
    ok = false;
  }
  return ok;
}

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

bool Editor::alignCarets() {
  if (!current_ || carets_.size() < 2) return false;
  breakTyping();
  const TextBuffer& b = current_->buffer();
  const int tw = std::max(1, editing_.tabWidth);
  const auto visualColumn = [&](Position p) {
    const std::string& l = b.line(p.line);
    int col = 0;
    for (int i = 0; i < p.column && i < static_cast<int>(l.size()); ++i) {
      const unsigned char c = static_cast<unsigned char>(l[static_cast<size_t>(i)]);
      if (c == '\t') col += tw - col % tw;
      else if ((c & 0xC0) != 0x80) ++col;  // one column per code point
    }
    return col;
  };
  int target = 0;
  for (const Selection& s : carets_) target = std::max(target, visualColumn(s.head));
  std::vector<CaretEdit> edits(carets_.size());
  std::vector<size_t> order(carets_.size());  // carets_ has the primary first; the leftmost caret of a line moves
  for (size_t i = 0; i < order.size(); ++i) order[i] = i;
  std::sort(order.begin(), order.end(), [&](size_t a, size_t c) { return carets_[a].head < carets_[c].head; });
  std::set<int> usedLines;
  bool any = false;
  for (const size_t i : order) {
    const Position h = carets_[i].head;
    edits[i].range = {h, h};
    const int pad = target - visualColumn(h);
    if (usedLines.insert(h.line).second && pad > 0) {
      edits[i].text.assign(static_cast<size_t>(pad), ' ');
      any = true;
    }
  }
  if (!any) return false;
  applyCaretEdits(std::move(edits), false);
  return true;
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

bool Editor::gotoBookmarkInFile(bool forward) {
  if (!current_) return false;
  const auto line = forward ? bookmarks_.nextInFile(cursor_.file, cursor_.pos.line)
                            : bookmarks_.prevInFile(cursor_.file, cursor_.pos.line);
  return line && gotoBookmarkItem({cursor_.file, *line});
}

size_t Editor::removeBookmarksInFile() {
  if (!current_) return 0;
  const size_t n = bookmarks_.removeFile(current_->path());
  saveBookmarks();
  return n;
}

void Editor::saveBookmarks() {
  if (bookmarks_.dirty()) bookmarks_.save(ws_.cacheDir() / "bookmarks.json", ws_.root());
}

}  // namespace tf
