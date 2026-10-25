#include "ui/view/WrapLayout.h"

#include <algorithm>

namespace tf::ui {

namespace {
bool isWs(char c) { return c == ' ' || c == '\t'; }
}  // namespace

void WrapLayout::bind(const TextBuffer* buf, const gfx::TextLayout* tl, int cols) {
  const int tw = tl ? tl->tabWidth() : 0;
  const int c = (buf && tl) ? std::max(0, cols) : 0;
  tl_ = tl;
  if (buf == buf_ && c == cols_ && tw == tabWidth_) return;
  buf_ = buf;
  cols_ = c;
  tabWidth_ = tw;
  reset();
}

void WrapLayout::reset() {
  entries_.clear();
  dirty_ = true;
  ++gen_;
}

size_t WrapLayout::lowerBound(int line) const {
  return static_cast<size_t>(
      std::lower_bound(entries_.begin(), entries_.end(), line, [](const Entry& e, int l) { return e.line < l; }) -
      entries_.begin());
}

int WrapLayout::availFor(const Entry& e, int row) const { return row == 0 ? cols_ : std::max(1, cols_ - e.indent); }

int WrapLayout::indentOf(const std::string& s, int& lead) const {
  const int cap = std::min(static_cast<int>(s.size()), cols_);  // more whitespace than that is past half the width
  lead = 0;
  while (lead < cap && isWs(s[static_cast<size_t>(lead)])) ++lead;
  if (lead == 0) return 0;
  const int cells =
      tl_->columnForByte(std::string_view(s.data(), static_cast<size_t>(lead)), static_cast<size_t>(lead));
  return std::min(cells, cols_ / 2);
}

// Where the row starting at byte `from` of `s` ends (= where the next row starts), or s.size() if the rest fits.
int WrapLayout::breakAt(const std::string& s, int from, int avail) const {
  const int n = static_cast<int>(s.size()), rest = n - from;
  const char* p = s.data() + from;
  int e;  // bytes of the longest prefix that fits in `avail` cells
  const int win = std::min(rest, avail + 1);
  bool plain = true;  // printable ASCII: one byte is one cell (the common case, and a fast scan for huge lines)
  for (int i = 0; i < win; ++i) plain &= static_cast<unsigned char>(p[i] - 0x20) < 0x5f;
  if (plain) {
    if (rest <= avail) return n;
    e = avail;
  } else {
    e = static_cast<int>(tl_->byteForColumn(std::string_view(p, static_cast<size_t>(rest)), avail));
    if (e >= rest) return n;
  }
  int lead = 0;
  while (lead < e && isWs(p[lead])) ++lead;
  if (lead < e) {  // the row holds a word: break at whitespace rather than inside a word
    if (p[e] == ' ') return from + e + 1 >= n ? n : from + e + 1;  // one hanging space uses the reserved cell
    for (int k = e - 1; k > lead; --k)
      if (isWs(p[k])) return from + k + 1;
  }
  if (e == 0) {  // not even one cell fits (a wide character in a one-cell row): take it anyway
    int len = 1;
    const char* end = p + rest;
    gfx::decodeUtf8(p, end, len);
    e = len;
    while (e < rest) {  // zero-width marks stay with their base character
      const char32_t cp = gfx::decodeUtf8(p + e, end, len);
      if (gfx::charCellWidth(cp) != 0) break;
      e += len;
    }
  }
  return from + e >= n ? n : from + e;
}

void WrapLayout::updateRows(Entry& e) {
  int rows;
  if (e.done) {
    rows = static_cast<int>(e.starts.size());
  } else {
    const int last = static_cast<int>(e.starts.size()) - 1, avail = availFor(e, last);
    rows = last + std::max(1, (e.size - e.starts.back() + avail - 1) / avail);
  }
  if (rows == e.rows) return;
  e.rows = rows;
  dirty_ = true;
  ++gen_;
}

void WrapLayout::scanNext(Entry& e) {
  if (e.done) return;
  const std::string& s = buf_->line(e.line);
  const int next = breakAt(s, e.starts.back(), availFor(e, static_cast<int>(e.starts.size()) - 1));
  if (next >= static_cast<int>(s.size())) e.done = true;
  else e.starts.push_back(next);
  updateRows(e);
}

void WrapLayout::scanThrough(Entry& e, int row) {
  while (!e.done && static_cast<int>(e.starts.size()) <= row) scanNext(e);
}

void WrapLayout::scanPast(Entry& e, int column) {
  while (!e.done && e.starts.back() <= column) scanNext(e);
}

void WrapLayout::trim() {
  while (!entries_.empty() && entries_.back().line >= buf_->lineCount()) {
    entries_.pop_back();
    dirty_ = true;
    ++gen_;
  }
}

void WrapLayout::rebuild() {
  if (!dirty_) return;
  pre_.resize(entries_.size() + 1);
  pre_[0] = 0;
  for (size_t i = 0; i < entries_.size(); ++i) pre_[i + 1] = pre_[i] + entries_[i].rows - 1;
  dirty_ = false;
}

WrapLayout::Entry* WrapLayout::touch(int line) {
  if (!enabled() || line < 0 || line >= buf_->lineCount()) return nullptr;
  const std::string& s = buf_->line(line);
  const int n = static_cast<int>(s.size());
  size_t i = lowerBound(line);
  if (i < entries_.size() && entries_[i].line == line) {
    if (entries_[i].size == n) return &entries_[i];
    entries_.erase(entries_.begin() + static_cast<long>(i));  // stale (the text changed behind our back)
    dirty_ = true;
    ++gen_;
  }
  if (static_cast<long long>(n) * std::max(2, tabWidth_) <= cols_) return nullptr;  // fits whatever the cells are
  const int first = breakAt(s, 0, cols_);
  if (first >= n) return nullptr;
  Entry e;
  e.line = line;
  e.size = n;
  e.indent = indentOf(s, e.lead);
  e.starts = {0, first};
  e.rows = 1;
  updateRows(e);
  entries_.insert(entries_.begin() + static_cast<long>(i), std::move(e));
  dirty_ = true;
  ++gen_;
  return &entries_[i];
}

void WrapLayout::onChange(const Document::Change& c) {
  if (!enabled() || entries_.empty()) return;
  const int s = c.removed.start.line, eOld = c.removed.end.line;
  const int k = static_cast<int>(std::count(c.newText.begin(), c.newText.end(), '\n'));
  const size_t lo = lowerBound(s);
  if (s == eOld && k == 0) {
    if (lo >= entries_.size() || entries_[lo].line != s || s >= buf_->lineCount()) return;
    Entry& e = entries_[lo];
    const std::string& text = buf_->line(s);
    const int col = c.removed.start.column;
    e.size = static_cast<int>(text.size());
    size_t keep = 0;  // rows whose break was decided entirely before the edit keep it
    if (col > e.lead) {
      // A row's break depends on the cells up to one past its width (a word running over the edge moves to the next
      // row), at most four bytes each, so everything up to start + (width + 2) * 4 counts as read.
      while (keep + 1 < e.starts.size() &&
             static_cast<long long>(e.starts[keep]) + 4LL * (availFor(e, static_cast<int>(keep)) + 2) < col)
        ++keep;
    } else {
      e.indent = indentOf(text, e.lead);
    }
    e.starts.resize(keep + 1);
    e.done = false;
    updateRows(e);
    dirty_ = true;
    ++gen_;
    return;
  }
  size_t hi = lo;
  while (hi < entries_.size() && entries_[hi].line <= eOld) ++hi;
  entries_.erase(entries_.begin() + static_cast<long>(lo), entries_.begin() + static_cast<long>(hi));
  const int delta = k - (eOld - s);
  if (delta != 0)
    for (size_t i = lo; i < entries_.size(); ++i) entries_[i].line += delta;
  dirty_ = true;
  ++gen_;
}

// ---- one line ----

int WrapLayout::rowsOf(int line) {
  Entry* e = touch(line);
  if (!e) return 1;
  while (!e->done) scanNext(*e);
  return static_cast<int>(e->starts.size());
}

int WrapLayout::rowsEstimate(int line) {
  const Entry* e = touch(line);
  return e ? e->rows : 1;
}

int WrapLayout::rowStart(int line, int row) {
  Entry* e = touch(line);
  if (!e) return 0;
  row = std::max(0, row);
  scanThrough(*e, row);
  return e->starts[std::min(static_cast<size_t>(row), e->starts.size() - 1)];
}

int WrapLayout::rowEnd(int line, int row) {
  Entry* e = touch(line);
  if (!e) return line >= 0 && buf_ && line < buf_->lineCount() ? static_cast<int>(buf_->line(line).size()) : 0;
  row = std::max(0, row);
  scanThrough(*e, row + 1);
  const size_t r = std::min(static_cast<size_t>(row), e->starts.size() - 1);
  return r + 1 < e->starts.size() ? e->starts[r + 1] : e->size;
}

int WrapLayout::rowIndent(int line, int row) {
  if (row <= 0) return 0;
  const Entry* e = touch(line);
  return e ? e->indent : 0;
}

int WrapLayout::rowOfByte(int line, int column) {
  Entry* e = touch(line);
  if (!e) return 0;
  scanPast(*e, column);
  const auto it = std::upper_bound(e->starts.begin(), e->starts.end(), column);
  return std::max(0, static_cast<int>(it - e->starts.begin()) - 1);
}

int WrapLayout::rowLastCaret(int line, int row) {
  if (!buf_ || line < 0 || line >= buf_->lineCount()) return 0;
  const std::string& s = buf_->line(line);
  const int start = rowStart(line, row), end = rowEnd(line, row);
  if (end >= static_cast<int>(s.size())) return static_cast<int>(s.size());
  const std::string_view v(s.data() + start, static_cast<size_t>(end - start));
  const int cc = tl_->columnCount(v);
  return cc <= 0 ? start : start + static_cast<int>(tl_->byteForColumn(v, cc - 1));
}

float WrapLayout::xOf(int line, int row, int column) {
  if (!buf_ || line < 0 || line >= buf_->lineCount()) return 0;
  const std::string& s = buf_->line(line);
  const int start = rowStart(line, row), end = rowEnd(line, row);
  const std::string_view v(s.data() + start, static_cast<size_t>(end - start));
  const int off = std::clamp(column - start, 0, end - start);
  const int indent = rowIndent(line, row);
  return static_cast<float>(indent) * tl_->cellWidth() + tl_->xForByte(v, static_cast<size_t>(off));
}

// ---- whole document ----

int WrapLayout::firstRowOf(int line) {
  if (!enabled()) return line;
  trim();
  rebuild();
  return line + pre_[lowerBound(line)];
}

int WrapLayout::totalRows() {
  if (!buf_) return 1;
  if (!enabled()) return buf_->lineCount();
  trim();
  rebuild();
  return buf_->lineCount() + pre_.back();
}

int WrapLayout::lineOfRow(int vrow, int* row) {
  if (!buf_) {
    if (row) *row = 0;
    return 0;
  }
  const int n = buf_->lineCount();
  vrow = std::max(0, vrow);
  int line = vrow, r = 0;
  if (!enabled()) {
    line = std::min(vrow, n - 1);
  } else {
    trim();
    rebuild();
    size_t lo = 0, hi = entries_.size();  // entries whose first row is <= vrow
    while (lo < hi) {
      const size_t mid = (lo + hi) / 2;
      if (entries_[mid].line + pre_[mid] <= vrow) lo = mid + 1;
      else hi = mid;
    }
    if (lo > 0) {
      const Entry& e = entries_[lo - 1];
      const int g = e.line + pre_[lo - 1];
      if (vrow < g + e.rows) {
        line = e.line;
        r = vrow - g;
      } else {
        line = vrow - pre_[lo];
      }
    }
    if (line >= n) {
      line = n - 1;
      r = std::max(0, rowsEstimate(line) - 1);
    }
  }
  if (row) *row = r;
  return line;
}

int WrapLayout::visualRowOf(Position p) {
  if (!buf_) return 0;
  const int line = std::clamp(p.line, 0, buf_->lineCount() - 1);
  const int col = std::clamp(p.column, 0, static_cast<int>(buf_->line(line).size()));
  return firstRowOf(line) + rowOfByte(line, col);
}

Position WrapLayout::positionAt(int vrow, float x) {
  if (!buf_ || !tl_) return {0, 0};
  int r = 0;
  const int line = lineOfRow(std::clamp(vrow, 0, totalRows() - 1), &r);
  return positionIn(line, r, x);
}

bool WrapLayout::nextRow(int& line, int& row) {
  if (!buf_ || line < 0 || line >= buf_->lineCount()) return false;
  if (rowEnd(line, row) < static_cast<int>(buf_->line(line).size())) {
    ++row;
    return true;
  }
  if (line + 1 >= buf_->lineCount()) return false;
  ++line;
  row = 0;
  return true;
}

bool WrapLayout::prevRow(int& line, int& row) {
  if (row > 0) {
    --row;
    return true;
  }
  if (line <= 0) return false;
  row = rowsOf(--line) - 1;
  return true;
}

Position WrapLayout::positionIn(int line, int r, float x) {
  if (!buf_ || !tl_) return {0, 0};
  line = std::clamp(line, 0, buf_->lineCount() - 1);
  const std::string& s = buf_->line(line);
  Entry* e = touch(line);
  if (!e) return {line, static_cast<int>(tl_->byteForX(s, x))};
  scanThrough(*e, r);
  r = static_cast<int>(std::min(static_cast<size_t>(r), e->starts.size() - 1));
  const int start = rowStart(line, r), end = rowEnd(line, r);
  const float lx = x - static_cast<float>(rowIndent(line, r)) * tl_->cellWidth();
  const std::string_view v(s.data() + start, static_cast<size_t>(end - start));
  int col = start + static_cast<int>(tl_->byteForX(v, lx));
  if (end < static_cast<int>(s.size())) col = std::min(col, rowLastCaret(line, r));
  return {line, col};
}

}  // namespace tf::ui
