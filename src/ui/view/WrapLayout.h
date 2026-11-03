#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/Document.h"
#include "ui/gfx/TextLayout.h"

namespace tf::ui {

// Soft word wrap (B2c): the map between document lines and visual rows of one text view.
//
// A line is cut into rows of at most `cols` cells (tabs, wide characters and control characters count as
// gfx::TextLayout counts them, measured from the start of each row). A row ends after the last space or tab that
// fits (one more space may hang past the edge, into the cell the view reserves); a word that does not fit on a row of
// its own is cut at the last cell that fits. Continuation rows are indented by the leading whitespace of the line,
// at most half the width. Rows are positions in the line: a caret at the boundary between two rows belongs to the
// later one, so the last caret position of a row that is not the line's last is the start of its last cell.
//
// Cost model: nothing is computed for a line until somebody asks about it. Lines that fit on one row are not stored at
// all; a line that wraps keeps its row starts, computed front to back only as far as asked (a 50 MB single line is
// scanned only up to the rows being shown or the caret), plus a row count that is an estimate until the whole line has
// been scanned. Lines nobody has asked about count as one row. An edit throws away the rows of the changed line from
// the edit on (rows before it cannot change) and shifts the rest; there is never a pass over the document.
//
// With cols <= 0 (wrap off) every line is exactly one row and all the functions reduce to the identity mapping.
//
// Hidden lines (W8f, code folding): setHidden() takes disjoint ranges of lines that have no rows at all. Every
// whole-document function (firstRowOf, totalRows, lineOfRow, visualRowOf, nextRow, prevRow) then works on the
// visible lines only, with or without wrapping; a hidden line maps to the first row of the next visible line.
// Per-line functions (rowsOf, rowStart, xOf ...) still answer for a hidden line, they are just never asked for one by
// the view. Costs: a binary search over the ranges per query, and rebuild() visits the wrapped lines once per change.
// Not thread-safe; mutating state is hidden behind const-less methods only.
class WrapLayout {
 public:
  // Binds the buffer and the cell metrics and sets the wrap width. Anything that changes the rows (another buffer,
  // another width, another tab width) drops all cached rows. cols <= 0 or a null buffer / layout turns wrapping off.
  void bind(const TextBuffer* buf, const gfx::TextLayout* tl, int cols);
  void reset();  // drops every cached row (keeps the binding)
  bool enabled() const { return cols_ > 0 && buf_ && tl_; }
  int cols() const { return cols_; }
  // Changes whenever a row count or the set of wrapped lines changes (cheap staleness test for derived data).
  uint32_t generation() const { return gen_; }

  // Document change (Document::addListener): keeps the rows the edit cannot have touched.
  void onChange(const Document::Change& c);

  // ---- hidden lines (folded regions) ----
  struct HiddenRange {
    int first = 0, last = 0;  // inclusive line numbers
  };
  // Replaces the hidden lines; `ranges` must be sorted and disjoint (the fold model merges them). Not touched by
  // bind() / reset(): hiding belongs to the fold model, not to the width.
  void setHidden(std::vector<HiddenRange> ranges);
  bool hasHidden() const { return !hid_.empty(); }
  bool isHidden(int line) const;
  int hiddenTotal() const { return hiddenTotal_; }
  int hiddenBefore(int line) const;  // number of hidden lines with an index below `line`
  // The visible line at or after / before `line` (lineCount() / -1 when there is none).
  int nextVisible(int line) const;
  int prevVisible(int line) const;
  // The hidden range holding `line`, if any.
  std::optional<HiddenRange> hiddenRangeOf(int line) const;

  // ---- one line ----
  // Exact number of rows (scans the whole line). rowsEstimate: what is known so far, never scans past the first row.
  int rowsOf(int line);
  int rowsEstimate(int line);
  // Byte range [rowStart, rowEnd) of a row (row is clamped to the line's rows); rowEnd of the last row is the line
  // length. rowIndent: continuation indent in cells (0 for the first row).
  int rowStart(int line, int row);
  int rowEnd(int line, int row);
  int rowIndent(int line, int row);
  // The row of a line that contains the caret position `column` (a boundary belongs to the later row).
  int rowOfByte(int line, int column);
  // Largest caret column that stays on the row (the line length for the last row, else the start of its last cell).
  int rowLastCaret(int line, int row);
  // x in pixels from the text origin of the caret position `column`, which must lie on `row` (indent included).
  float xOf(int line, int row, int column);

  // ---- whole document ----
  int firstRowOf(int line);  // visual row index of a line's first row
  int totalRows();
  // Line holding visual row `vrow` (clamped to the document); *row = its index within the line.
  int lineOfRow(int vrow, int* row = nullptr);
  int visualRowOf(Position p);
  // Caret position nearest to x pixels (from the text origin) on visual row `vrow` (clamped); never past the row.
  Position positionAt(int vrow, float x);
  // The same for row `row` of `line` (both clamped).
  Position positionIn(int line, int row, float x);
  // Steps one visual row down / up from (line, row), across lines; false (and no change) at the end / start of the
  // document. Only looks at the rows it passes, so walking a screenful never scans the rest of a huge line.
  bool nextRow(int& line, int& row);
  bool prevRow(int& line, int& row);

 private:
  struct Entry {
    int line = 0;
    int size = 0;                // line length when the rows were computed (staleness check)
    int indent = 0;              // continuation indent in cells
    int lead = 0;                // leading whitespace bytes that decide the indent (an edit inside them redoes it)
    int rows = 1;                // known (done) or estimated row count
    bool done = false;           // starts holds every row and the last one ends at the line end
    std::vector<int> starts{0};  // row starts known so far; the last row's end is unknown unless done
  };

  Entry* touch(int line);  // the entry of a wrapped line, or null for a line that fits one row
  struct Hid {
    int first, last, before;  // `before`: hidden lines in the ranges ahead of this one
  };
  int lineOfVisible(int v) const;  // the line holding the v-th visible line
  size_t lowerBound(int line) const;
  int availFor(const Entry& e, int row) const;
  int breakAt(const std::string& line, int from, int avail) const;
  int indentOf(const std::string& line, int& lead) const;
  void scanNext(Entry& e);
  void scanThrough(Entry& e, int row);  // makes starts.size() > row (or done)
  void scanPast(Entry& e, int column);  // makes the row containing `column` known to its end
  void updateRows(Entry& e);
  void trim();
  void rebuild();

  const TextBuffer* buf_ = nullptr;
  const gfx::TextLayout* tl_ = nullptr;
  int cols_ = 0, tabWidth_ = 0;
  uint32_t gen_ = 1;
  std::vector<Entry> entries_;  // wrapped lines only, sorted by line
  // pre_[i] = rows beyond the first of the visible entries_[0, i); valid when !dirty_.
  std::vector<int> pre_;
  std::vector<int> base_;  // base_[i]: first row of entries_[i] (meaningless for hidden entries)
  std::vector<Hid> hid_;
  int hiddenTotal_ = 0;
  bool dirty_ = true;
};

}  // namespace tf::ui
