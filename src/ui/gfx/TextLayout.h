#pragma once
#include <cstdint>
#include <string_view>
#include <vector>

#include "ui/gfx/Atlas.h"
#include "ui/gfx/Font.h"
#include "ui/gfx/Renderer.h"

namespace tf::gfx {

// Colour assignment for a byte range of the line (half-open). Spans must be sorted and non-overlapping; bytes not
// covered use the default colour index.
struct ColorSpan {
  uint32_t begin, end;
  uint32_t color;
};

// Consecutive quads sharing one colour index: quads[firstQuad, firstQuad + count).
struct ColorRun {
  uint32_t firstQuad, count, color;
};

struct LineLayout {
  std::vector<GlyphQuad> quads;
  std::vector<ColorRun> runs;
  void clear() {
    quads.clear();
    runs.clear();
  }
};

// Monospace cell layout, no shaping. Rules (shared by drawing and by every x <-> byte mapping):
//  * Input is UTF-8. An invalid byte becomes U+FFFD occupying one cell and consuming exactly one byte.
//  * Tab advances to the next multiple of tabWidth columns (an empty cell, nothing drawn).
//  * East Asian Wide/Fullwidth characters (compact table of the main ranges) take 2 cells, glyph at the left cell.
//  * Zero-width characters (combining marks, ZWJ/ZWSP, variation selectors) belong to the previous cell: their glyphs
//    are drawn at that cell's origin, their bytes join that cell, and the caret can never sit between them. A
//    zero-width character at line start (or after nothing) gets its own one-cell slot.
//  * Control characters (< 0x20 except tab, and 0x7f) are shown as two cells "^X" ('^' plus letter, DEL is "^?").
//  * Glyph colours come from the span covering the cell's first byte.
// Only cells overlapping the visible window are emitted. Not thread-safe.
class TextLayout {
 public:
  TextLayout(FontStack& fonts, Atlas& atlas, int tabWidth = 4) : fonts_(fonts), atlas_(atlas), tabWidth_(tabWidth) {}

  void setTabWidth(int w) { tabWidth_ = w < 1 ? 1 : w; }
  int tabWidth() const { return tabWidth_; }
  float cellWidth() const { return fonts_.metrics().advance; }
  float lineHeight() const { return fonts_.metrics().lineHeight; }

  // Appends the visible part of `line` to `out`. The line's column 0 is drawn at originX; the viewport shows line-local
  // x in [scrollX, scrollX + visibleWidth). baselineY is the pen baseline in pixels. Glyphs partly inside are emitted
  // whole (the renderer scissors). UVs are filled in last, with the atlas size after any growth during this call.
  void layoutLine(std::string_view line, float originX, float baselineY, float scrollX, float visibleWidth,
                  const std::vector<ColorSpan>& spans, uint32_t defaultColor, LineLayout& out);

  // Number of columns the line occupies.
  int columnCount(std::string_view line) const;
  // Column of the cell containing byteOffset (offsets inside a cell snap to its start; >= size gives columnCount).
  int columnForByte(std::string_view line, size_t byteOffset) const;
  // Byte offset of the cell starting at or containing `column` (>= columnCount gives line.size()).
  size_t byteForColumn(std::string_view line, int column) const;
  // Line-local x of the caret position before byteOffset. xForByte == columnForByte * cellWidth.
  float xForByte(std::string_view line, size_t byteOffset) const;
  // Hit test: nearest cell boundary (a valid caret byte offset) for a line-local x; x past the end gives line.size().
  size_t byteForX(std::string_view line, float x) const;

 private:
  FontStack& fonts_;
  Atlas& atlas_;
  int tabWidth_;
};

// Exposed for tests.
char32_t decodeUtf8(const char* p, const char* end, int& len);  // invalid -> U+FFFD, len 1
int charCellWidth(char32_t cp);                                 // 0 zero-width, 1, or 2

}  // namespace tf::gfx
