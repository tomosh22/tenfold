#pragma once
#include <string_view>

#include "ui/gfx/TextLayout.h"

namespace tf::ui {

// Document geometry for a text view: where lines, the gutter and carets sit, and the byte <-> x mapping of one line.
// All x <-> byte rules (tabs, wide characters, zero-width marks) come from gfx::TextLayout; nothing here re-derives
// them. Lines and y positions here are plain line / row counts: with soft wrap on, the view maps lines to visual rows
// with WrapLayout first and passes row numbers where this class says "line".
class Layout {
 public:
  explicit Layout(const gfx::TextLayout& tl) : tl_(&tl) {}

  float lineHeight() const { return tl_->lineHeight(); }
  float cellWidth() const { return tl_->cellWidth(); }

  // Line-number gutter: at least three digits plus a cell of padding on both sides; text starts half a cell later.
  float gutterWidth(int lineCount) const;
  float textOffset(int lineCount) const { return gutterWidth(lineCount) + cellWidth() / 2; }

  // Version-control change strip (C3c): a narrow bar between the line numbers and the text. It lies in the padding
  // cell the gutter already reserves right of the numbers (numbers end one cell before gutterWidth), so it adds no
  // width and no text moves when version control becomes active; gutterWidth() stays the one place that decides how
  // wide everything left of the text is. x is relative to the view's left edge.
  float diffStripWidth() const;
  float diffStripX(int lineCount) const { return gutterWidth(lineCount) - diffStripWidth() - 2; }

  // Vertical position of a line relative to the top of the view; the line under a y in the view (clamped to the
  // document); and the line range [first, last) visible in a view `heightPx` tall scrolled by `scrollY`.
  float lineTop(int line, float scrollY) const { return static_cast<float>(line) * lineHeight() - scrollY; }
  int lineAt(float yInView, float scrollY, int lineCount) const;
  void visibleLines(float scrollY, float heightPx, int lineCount, int& first, int& last) const;

  // Line-local x (from the start of the line, unscrolled) <-> byte offset in `line`.
  float xForByte(std::string_view line, size_t byte) const { return tl_->xForByte(line, byte); }
  size_t byteForX(std::string_view line, float x) const { return tl_->byteForX(line, x < 0 ? 0 : x); }
  int columnForByte(std::string_view line, size_t byte) const { return tl_->columnForByte(line, byte); }
  float lineWidth(std::string_view line) const { return static_cast<float>(tl_->columnCount(line)) * cellWidth(); }

 private:
  const gfx::TextLayout* tl_;
};

}  // namespace tf::ui
