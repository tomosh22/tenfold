#include "ui/view/InlayMap.h"

#include <algorithm>

namespace tf::ui {

void InlayMap::set(const std::vector<LspInlayHint>& hints, const gfx::TextLayout& tl) {
  items_.clear();
  items_.reserve(hints.size());
  for (const LspInlayHint& h : hints) {
    if (h.label.empty()) continue;
    Item it;
    it.line = h.pos.line;
    it.col = h.pos.column;
    it.text = h.label;
    it.prefix = h.kind == 2;
    it.cells = tl.columnCount(it.text);
    items_.push_back(std::move(it));
  }
  std::stable_sort(items_.begin(), items_.end(), [](const Item& a, const Item& b) {
    return a.line != b.line ? a.line < b.line : a.col < b.col;
  });
}

void InlayMap::onChange(const Document::Change& c) {
  if (items_.empty()) return;
  const int first = c.removed.start.line, last = c.removed.end.line;
  std::vector<Item> kept;
  kept.reserve(items_.size());
  for (Item& it : items_) {
    if (it.line < first) kept.push_back(std::move(it));
    else if (it.line > last) {
      it.line += c.lineDelta;
      kept.push_back(std::move(it));
    }  // else: a touched line
  }
  items_ = std::move(kept);
}

std::pair<size_t, size_t> InlayMap::ofLine(int line) const {
  const auto lo = std::lower_bound(items_.begin(), items_.end(), line, [](const Item& i, int l) { return i.line < l; });
  auto hi = lo;
  while (hi != items_.end() && hi->line == line) ++hi;
  return {static_cast<size_t>(lo - items_.begin()), static_cast<size_t>(hi - items_.begin())};
}

bool InlayMap::onRow(const Item& it, int rs, int re, int lineLen) {
  if (it.col < rs || it.col > re) return false;
  if (rs > 0 && it.col == rs && !it.prefix) return false;  // belongs to the end of the row before
  if (re < lineLen && it.col == re && it.prefix) return false;  // belongs to the start of the next row
  return true;
}

int InlayMap::shiftCells(int line, int rs, int re, int lineLen, int col, bool end) const {
  const auto [a, b] = ofLine(line);
  int s = 0;
  for (size_t i = a; i < b; ++i) {
    const Item& h = items_[i];
    if (h.col > col) break;
    if (h.col == col && (end || !h.prefix)) continue;
    if (onRow(h, rs, re, lineLen)) s += h.width();
  }
  return s;
}

}  // namespace tf::ui
