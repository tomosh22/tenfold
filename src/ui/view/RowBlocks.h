#pragma once
#include <vector>

namespace tf::ui {

// Display rows with reserved blocks (C3c inline diff): the map between the rows WrapLayout counts ("wrap rows", one
// per visual row of document text) and the rows the view lays out, which also hold blocks of decoration rows that are
// not part of the document. A block sits before wrap row `at` (at == the wrap row count = after the last text row)
// and takes `rows` display rows; wrap rows from `at` on move down by that many. WrapLayout itself is untouched: it
// never learns about blocks, so its row model (and everything that is O(log n) in it) stays exactly as it was.
//
// Pure value logic, no widgets: the view rebuilds it when the blocks or the wrap map changed. With no blocks every
// function is the identity.
class RowBlocks {
 public:
  struct Block {
    int at = 0;    // wrap row the block precedes
    int rows = 0;  // display rows it occupies (>= 1)
  };
  // Blocks sorted by `at` (a stable sort is applied; equal `at` keep their order, i.e. stack top to bottom).
  void set(std::vector<Block> blocks);
  bool empty() const { return blocks_.empty(); }
  size_t size() const { return blocks_.size(); }
  const Block& block(size_t i) const { return blocks_[i]; }
  int extraRows() const { return extra_; }  // total rows of all blocks

  // Display row of wrap row `w` (blocks that precede it are counted).
  int display(int w) const;
  // Display row at which block `i` starts.
  int top(size_t i) const;
  // Wrap row shown at display row `d`. When `d` lies inside block i, returns the wrap row the block precedes, sets
  // *block = i and *offset = d - top(i); else *block = -1.
  int wrapRow(int d, int* block = nullptr, int* offset = nullptr) const;
  // Index of the first block with top(i) + rows > d (blocks ending at or before row d are skipped); size() if none.
  size_t firstBlockFrom(int d) const;

 private:
  std::vector<Block> blocks_;
  std::vector<int> tops_;
  int extra_ = 0;
};

}  // namespace tf::ui
