#include "ui/view/RowBlocks.h"

#include <algorithm>

namespace tf::ui {

void RowBlocks::set(std::vector<Block> blocks) {
  std::stable_sort(blocks.begin(), blocks.end(), [](const Block& a, const Block& b) { return a.at < b.at; });
  blocks_ = std::move(blocks);
  tops_.clear();
  extra_ = 0;
  for (Block& b : blocks_) {
    b.at = std::max(0, b.at);
    b.rows = std::max(1, b.rows);
    tops_.push_back(b.at + extra_);
    extra_ += b.rows;
  }
}

int RowBlocks::display(int w) const {
  int shift = 0;
  for (const Block& b : blocks_) {
    if (b.at > w) break;
    shift += b.rows;
  }
  return w + shift;
}

int RowBlocks::top(size_t i) const { return tops_[i]; }

int RowBlocks::wrapRow(int d, int* block, int* offset) const {
  if (block) *block = -1;
  int shift = 0;
  for (size_t i = 0; i < blocks_.size(); ++i) {
    if (d < tops_[i]) break;
    if (d < tops_[i] + blocks_[i].rows) {
      if (block) *block = static_cast<int>(i);
      if (offset) *offset = d - tops_[i];
      return blocks_[i].at;
    }
    shift += blocks_[i].rows;
  }
  return d - shift;
}

size_t RowBlocks::firstBlockFrom(int d) const {
  size_t i = 0;
  while (i < blocks_.size() && tops_[i] + blocks_[i].rows <= d) ++i;
  return i;
}

}  // namespace tf::ui
