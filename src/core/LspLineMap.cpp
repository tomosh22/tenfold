#include "core/LspLineMap.h"

#include <algorithm>

namespace tf {

std::shared_ptr<const LspLineMap> LspLineMap::build(const TextBuffer& b) {
  if (!b.hasSeparators()) return nullptr;
  const std::vector<int>& sl = b.separatorLines();  // ascending lines that continue the previous line
  auto m = std::make_shared<LspLineMap>();
  int sepBefore = 0;
  for (size_t i = 0; i < sl.size();) {
    // A run: lines sl[i]-1 .. sl[j-1] (consecutive continuation lines).
    size_t j = i + 1;
    while (j < sl.size() && sl[j] == sl[j - 1] + 1) ++j;
    Run r;
    r.editorStart = sl[i] - 1;
    r.count = static_cast<int>(j - i) + 1;
    r.serverLine = r.editorStart - sepBefore;
    r.firstSeg = static_cast<int>(m->segs_.size());
    r.sepBefore = sepBefore;
    int u8 = 0, u16 = 0;
    for (int k = 0; k < r.count; ++k) {
      const int line = r.editorStart + k;
      Seg s;
      s.u8 = u8;
      s.u16 = u16;
      s.bytes = static_cast<int>(b.line(line).size());
      s.units = TextBuffer::utf8ToUtf16Column(b.line(line), s.bytes);
      m->segs_.push_back(s);
      u8 += s.bytes + static_cast<int>(TextBuffer::terminatorText(b.endingAfter(line)).size());
      u16 += s.units + 1;
    }
    m->runs_.push_back(r);
    sepBefore += r.count - 1;
    i = j;
  }
  return m;
}

Position LspLineMap::toServer(Position p, const std::vector<std::string>& lines, bool utf16) const {
  if (lines.empty()) return p;
  p.line = std::clamp(p.line, 0, static_cast<int>(lines.size()) - 1);
  const std::string& text = lines[static_cast<size_t>(p.line)];
  p.column = std::clamp(p.column, 0, static_cast<int>(text.size()));
  const int col = utf16 ? TextBuffer::utf8ToUtf16Column(text, p.column) : p.column;
  auto it = std::upper_bound(runs_.begin(), runs_.end(), p.line,
                             [](int line, const Run& r) { return line < r.editorStart; });
  if (it == runs_.begin()) return {p.line, col};
  const Run& r = *(it - 1);
  if (p.line >= r.editorStart + r.count) return {p.line - (r.sepBefore + r.count - 1), col};
  const Seg& s = segs_[static_cast<size_t>(r.firstSeg + (p.line - r.editorStart))];
  return {r.serverLine, (utf16 ? s.u16 : s.u8) + col};
}

Position LspLineMap::fromServer(Position q, const std::vector<std::string>& lines, bool utf16) const {
  if (q.line < 0) return {0, 0};
  q.column = std::max(q.column, 0);
  auto it = std::upper_bound(runs_.begin(), runs_.end(), q.line,
                             [](int line, const Run& r) { return line < r.serverLine; });
  int line = q.line, col = q.column;
  if (it != runs_.begin()) {
    const Run& r = *(it - 1);
    if (q.line == r.serverLine) {
      const auto first = segs_.begin() + r.firstSeg;
      const auto last = first + r.count;
      // The last segment that starts at or before the column.
      auto sit = std::upper_bound(first, last, q.column,
                                  [&](int c, const Seg& s) { return c < (utf16 ? s.u16 : s.u8); });
      const Seg& s = *(sit - 1);
      line = r.editorStart + static_cast<int>(sit - 1 - first);
      col = q.column - (utf16 ? s.u16 : s.u8);
      col = std::min(col, utf16 ? s.units : s.bytes);  // past the content: inside or after the separator
    } else {
      line = q.line + r.sepBefore + r.count - 1;
    }
  }
  if (line >= static_cast<int>(lines.size())) return {line, col};
  const std::string& text = lines[static_cast<size_t>(line)];
  return {line, utf16 ? TextBuffer::utf16ToUtf8Column(text, col) : std::min(col, static_cast<int>(text.size()))};
}

int LspLineMap::serverLineCount(int editorLines) const {
  int seps = 0;
  for (const Run& r : runs_) seps += r.count - 1;
  return editorLines - seps;
}

}  // namespace tf
