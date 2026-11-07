#include "ui/view/LineAnchors.h"

#include <algorithm>

namespace tf::ui {

void LineAnchors::bind(const TextBuffer* buf, const gfx::TextLayout* tl) {
  const int tw = tl ? tl->tabWidth() : 0;
  if (buf != buf_ || tw != tabWidth_) entries_.clear();
  buf_ = buf;
  tl_ = tl;
  tabWidth_ = tw;
}

size_t LineAnchors::lowerBound(int line) const {
  return static_cast<size_t>(
      std::lower_bound(entries_.begin(), entries_.end(), line, [](const Entry& e, int l) { return e.line < l; }) -
      entries_.begin());
}

LineAnchors::Entry* LineAnchors::entry(int line) {
  if (!buf_ || !tl_ || line < 0 || line >= buf_->lineCount()) return nullptr;
  const size_t n = buf_->line(line).size();
  const size_t i = lowerBound(line);
  const bool have = i < entries_.size() && entries_[i].line == line;
  if (have && entries_[i].size != n) {  // the text changed behind our back
    entries_.erase(entries_.begin() + static_cast<long>(i));
    return entry(line);
  }
  if (have) return &entries_[i];
  if (n < kMinLine) return nullptr;
  Entry e;
  e.line = line;
  e.size = n;
  entries_.insert(entries_.begin() + static_cast<long>(i), std::move(e));
  return &entries_[i];
}

void LineAnchors::extend(Entry& e, size_t limitByte) {
  while (!e.done && e.a.back().byte <= limitByte) {
    const size_t before = e.a.size();
    tl_->collectAnchors(buf_->line(e.line), e.a.back(), limitByte, kStride, e.a);
    if (e.a.size() == before) {  // no further cell boundary: the end of the line
      e.cols = tl_->columnCount(buf_->line(e.line), e.a.back());
      e.done = true;
    }
  }
}

gfx::Anchor LineAnchors::atByte(int line, size_t byte) {
  Entry* e = entry(line);
  if (!e) return {};
  extend(*e, byte);
  const auto it =
      std::upper_bound(e->a.begin(), e->a.end(), byte, [](size_t b, const gfx::Anchor& a) { return b < a.byte; });
  return *(it - 1);
}

gfx::Anchor LineAnchors::atColumn(int line, int col) {
  Entry* e = entry(line);
  if (!e || col <= 0) return {};
  for (;;) {
    if (e->done || e->a.back().col > col) break;
    const size_t before = e->a.size();
    // Extend by one batch; the byte limit is generous (every cell has at least one byte, so col <= byte offset).
    extend(*e, e->a.back().byte + kStride);
    if (e->a.size() == before) break;
  }
  const auto it =
      std::upper_bound(e->a.begin(), e->a.end(), col, [](int c, const gfx::Anchor& a) { return c < a.col; });
  return *(it - 1);
}

int LineAnchors::columnCount(int line) {
  Entry* e = entry(line);
  if (!e) return buf_ && line >= 0 && line < buf_->lineCount() ? tl_->columnCount(buf_->line(line)) : 0;
  extend(*e, e->size);
  return e->cols;
}

const std::vector<gfx::Anchor>* LineAnchors::anchorsOf(int line) const {
  const size_t i = lowerBound(line);
  return i < entries_.size() && entries_[i].line == line ? &entries_[i].a : nullptr;
}

void LineAnchors::onChange(const Document::Change& c) {
  if (entries_.empty()) return;
  const int s = c.removed.start.line, eOld = c.removed.end.line;
  const int k = static_cast<int>(std::count(c.newText.begin(), c.newText.end(), '\n'));
  const size_t lo = lowerBound(s);
  size_t hi = lo;
  while (hi < entries_.size() && entries_[hi].line <= eOld) ++hi;
  // The first line keeps the anchors wholly before the edit (the edit may sit inside a UTF-8 sequence's neighbour).
  size_t from = lo;
  if (lo < hi && entries_[lo].line == s) {
    Entry& e = entries_[lo];
    const size_t col = static_cast<size_t>(std::max(0, c.removed.start.column));
    while (e.a.size() > 1 && e.a.back().byte + 4 > col) e.a.pop_back();
    e.done = false;
    e.size = s < buf_->lineCount() ? buf_->line(s).size() : 0;
    from = lo + 1;
    if (e.size < kMinLine) {
      entries_.erase(entries_.begin() + static_cast<long>(lo));
      --hi;
      from = lo;
    }
  }
  entries_.erase(entries_.begin() + static_cast<long>(from), entries_.begin() + static_cast<long>(hi));
  const int delta = k - (eOld - s);
  if (delta != 0)
    for (size_t i = from; i < entries_.size(); ++i) entries_[i].line += delta;
}

}  // namespace tf::ui
