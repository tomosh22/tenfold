#include "ui/gfx/TextLayout.h"

#include <algorithm>

namespace tf::gfx {

char32_t decodeUtf8(const char* p, const char* end, int& len) {
  const unsigned char c = static_cast<unsigned char>(*p);
  len = 1;
  if (c < 0x80) return c;
  int n = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC2 ? 2 : 0;
  if (c > 0xF4 || n == 0 || end - p < n) return 0xFFFD;
  char32_t cp = c & (0xFF >> (n + 1));
  for (int i = 1; i < n; ++i) {
    const unsigned char t = static_cast<unsigned char>(p[i]);
    if ((t & 0xC0) != 0x80) return 0xFFFD;
    cp = cp << 6 | (t & 0x3F);
  }
  if ((n == 3 && cp < 0x800) || (n == 4 && (cp < 0x10000 || cp > 0x10FFFF)) || (cp >= 0xD800 && cp <= 0xDFFF))
    return 0xFFFD;
  len = n;
  return cp;
}

namespace {
struct Range {
  char32_t lo, hi;
};

bool inRanges(const Range* r, size_t n, char32_t cp) {
  size_t lo = 0, hi = n;
  while (lo < hi) {
    size_t mid = (lo + hi) / 2;
    if (cp < r[mid].lo) hi = mid;
    else if (cp > r[mid].hi) lo = mid + 1;
    else return true;
  }
  return false;
}

// Sorted, non-overlapping. Zero-width: combining marks of the main scripts, ZW format characters, variation selectors.
const Range kZero[] = {{0x0300, 0x036F}, {0x0483, 0x0489}, {0x0591, 0x05BD},  {0x05BF, 0x05BF}, {0x05C1, 0x05C2},
                       {0x05C4, 0x05C5}, {0x05C7, 0x05C7}, {0x0610, 0x061A},  {0x064B, 0x065F}, {0x0670, 0x0670},
                       {0x06D6, 0x06DC}, {0x06DF, 0x06E4}, {0x06E7, 0x06E8},  {0x06EA, 0x06ED}, {0x0900, 0x0902},
                       {0x093C, 0x093C}, {0x0941, 0x0948}, {0x094D, 0x094D},  {0x0E31, 0x0E31}, {0x0E34, 0x0E3A},
                       {0x0E47, 0x0E4E}, {0x1AB0, 0x1AFF}, {0x1DC0, 0x1DFF},  {0x200B, 0x200F}, {0x202A, 0x202E},
                       {0x2060, 0x2064}, {0x20D0, 0x20FF}, {0x302A, 0x302D},  {0x3099, 0x309A}, {0xFE00, 0xFE0F},
                       {0xFE20, 0xFE2F}, {0xFEFF, 0xFEFF}, {0xE0100, 0xE01EF}};

// East Asian Wide / Fullwidth, main ranges.
const Range kWide[] = {{0x1100, 0x115F},   {0x2E80, 0x303E},   {0x3041, 0x33FF},   {0x3400, 0x4DBF},
                       {0x4E00, 0x9FFF},   {0xA000, 0xA4CF},   {0xA960, 0xA97F},   {0xAC00, 0xD7A3},
                       {0xF900, 0xFAFF},   {0xFE30, 0xFE6F},   {0xFF00, 0xFF60},   {0xFFE0, 0xFFE6},
                       {0x1F300, 0x1F64F}, {0x1F900, 0x1F9FF}, {0x20000, 0x2FFFD}, {0x30000, 0x3FFFD}};
}  // namespace

int charCellWidth(char32_t cp) {
  if (cp < 0x300) return 1;
  if (inRanges(kZero, std::size(kZero), cp)) return 0;
  if (cp >= 0x1100 && inRanges(kWide, std::size(kWide), cp)) return 2;
  return 1;
}

namespace {
enum class Kind : uint8_t { Glyph, Tab, Control };

struct Cell {
  uint32_t b0 = 0, b1 = 0;
  int col = 0, width = 1;
  char32_t cp = 0;
  Kind kind = Kind::Glyph;
  char32_t marks[3] = {};
  int nMarks = 0;
};

// Single source of truth for cell boundaries: drawing and all mappings go through here. fn(cell) returns false to
// stop. Returns the total column count when the whole line was visited (otherwise the column reached).
template <class Fn>
int forEachCell(std::string_view line, int tabWidth, Fn&& fn) {
  const char* p = line.data();
  const char* end = p + line.size();
  Cell cur;
  bool have = false;
  int col = 0;
  while (p < end) {
    int len = 1;
    const unsigned char c0 = static_cast<unsigned char>(*p);
    const char32_t cp = c0 < 0x80 ? c0 : decodeUtf8(p, end, len);
    const int w = cp < 0x80 ? 1 : charCellWidth(cp);
    const uint32_t off = static_cast<uint32_t>(p - line.data());
    if (w == 0 && have) {
      cur.b1 = off + len;
      if (cur.nMarks < 3) cur.marks[cur.nMarks++] = cp;
      p += len;
      continue;
    }
    if (have) {
      if (!fn(cur)) return col;
      col += cur.width;
    }
    cur = Cell();
    cur.b0 = off;
    cur.b1 = off + len;
    cur.col = col;
    cur.cp = cp;
    if (cp == '\t') {
      cur.kind = Kind::Tab;
      cur.width = tabWidth - col % tabWidth;
    } else if (cp < 0x20 || cp == 0x7F) {
      cur.kind = Kind::Control;
      cur.width = 2;
    } else {
      cur.width = w == 0 ? 1 : w;
    }
    have = true;
    p += len;
  }
  if (have) {
    if (!fn(cur)) return col;
    col += cur.width;
  }
  return col;
}
}  // namespace

int TextLayout::columnCount(std::string_view line) const {
  return forEachCell(line, tabWidth_, [](const Cell&) { return true; });
}

int TextLayout::columnForByte(std::string_view line, size_t byteOffset) const {
  int result = -1;
  int total = forEachCell(line, tabWidth_, [&](const Cell& c) {
    if (c.b1 > byteOffset) {
      result = c.col;
      return false;
    }
    return true;
  });
  return result >= 0 ? result : total;
}

size_t TextLayout::byteForColumn(std::string_view line, int column) const {
  size_t result = line.size();
  forEachCell(line, tabWidth_, [&](const Cell& c) {
    if (column < c.col + c.width) {
      result = c.b0;
      return false;
    }
    return true;
  });
  return column < 0 ? 0 : result;
}

float TextLayout::xForByte(std::string_view line, size_t byteOffset) const {
  return static_cast<float>(columnForByte(line, byteOffset)) * cellWidth();
}

size_t TextLayout::byteForX(std::string_view line, float x) const {
  if (x <= 0) return 0;
  const float cols = x / cellWidth();
  size_t result = line.size();
  forEachCell(line, tabWidth_, [&](const Cell& c) {
    if (cols < static_cast<float>(c.col + c.width)) {
      result = (cols - static_cast<float>(c.col)) * 2 < static_cast<float>(c.width) ? c.b0 : c.b1;
      return false;
    }
    return true;
  });
  return result;
}

void TextLayout::layoutLine(std::string_view line, float originX, float baselineY, float scrollX, float visibleWidth,
                            const std::vector<ColorSpan>& spans, uint32_t defaultColor, LineLayout& out) {
  const float adv = cellWidth();
  const float visEnd = scrollX + visibleWidth;
  const size_t firstQuad = out.quads.size();
  size_t spanIdx = 0;

  auto emit = [&](char32_t cp, int col, uint32_t color) {
    GlyphRef ref = fonts_.lookup(cp);
    const AtlasGlyph* g = atlas_.glyph(*ref.font, ref.glyph);
    if (g->w == 0) return;
    const float x = originX + static_cast<float>(col) * adv - scrollX + static_cast<float>(g->bearingX);
    const float y = baselineY - static_cast<float>(g->bearingY);
    // u0..v1 carry the atlas pixel rect for now; converted to UVs below once growth is settled.
    out.quads.push_back({x, y, x + static_cast<float>(g->w), y + static_cast<float>(g->h), static_cast<float>(g->x),
                         static_cast<float>(g->y), static_cast<float>(g->x + g->w), static_cast<float>(g->y + g->h)});
    if (!out.runs.empty() && out.runs.back().color == color &&
        out.runs.back().firstQuad + out.runs.back().count == out.quads.size() - 1)
      ++out.runs.back().count;
    else out.runs.push_back({static_cast<uint32_t>(out.quads.size() - 1), 1, color});
  };

  forEachCell(line, tabWidth_, [&](const Cell& c) {
    const float x0 = static_cast<float>(c.col) * adv;
    if (x0 >= visEnd) return false;
    if (x0 + static_cast<float>(c.width) * adv <= scrollX) return true;
    if (c.kind == Kind::Tab) return true;
    while (spanIdx < spans.size() && spans[spanIdx].end <= c.b0) ++spanIdx;
    const uint32_t color = spanIdx < spans.size() && spans[spanIdx].begin <= c.b0 ? spans[spanIdx].color : defaultColor;
    if (c.kind == Kind::Control) {
      emit('^', c.col, color);
      emit(c.cp == 0x7F ? U'?' : static_cast<char32_t>(c.cp + 64), c.col + 1, color);
      return true;
    }
    if (c.cp != ' ') emit(c.cp, c.col, color);
    for (int i = 0; i < c.nMarks; ++i) emit(c.marks[i], c.col, color);
    return true;
  });

  const float inv = 1.0f / static_cast<float>(atlas_.size());
  for (size_t i = firstQuad; i < out.quads.size(); ++i) {
    GlyphQuad& q = out.quads[i];
    q.u0 *= inv;
    q.v0 *= inv;
    q.u1 *= inv;
    q.v1 *= inv;
  }
}

}  // namespace tf::gfx
