#include "core/TextBuffer.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace tf {

namespace {
bool isIdentChar(unsigned char c) {
  return std::isalnum(c) || c == '_' || c >= 0x80;  // treat non-ASCII as identifier-ish
}
}  // namespace

namespace {
using linebreak::BreakFinder;

bool hasBreak(std::string_view t) {
  if (t.size() <= 32) {  // keystrokes and short pastes: a plain loop beats library calls
    for (size_t i = 0; i < t.size(); ++i) {
      const char c = t[i];
      if (c == '\n' || c == '\r') return true;
      if (static_cast<unsigned char>(c) >= 0xC2 && linebreak::separatorLen(t, i)) return true;
    }
    return false;
  }
  return std::memchr(t.data(), '\n', t.size()) != nullptr || std::memchr(t.data(), '\r', t.size()) != nullptr ||
         linebreak::hasSeparator(t);
}

size_t countBreaks(std::string_view t) {
  size_t n = 0, len = 0;
  BreakFinder bf(t);
  for (size_t at = bf.next(0, len); at != std::string_view::npos; at = bf.next(at + len, len)) ++n;
  return n;
}

LineEnding kindOf(std::string_view t, size_t at, size_t len) {
  const unsigned char c = static_cast<unsigned char>(t[at]);
  if (c == 0xC2) return LineEnding::NEL;
  if (c == 0xE2) return static_cast<unsigned char>(t[at + 2]) == 0xA8 ? LineEnding::LS : LineEnding::PS;
  return len == 2 ? LineEnding::CRLF : c == '\n' ? LineEnding::LF : LineEnding::CR;
}

// Every break of `t` is of kind `e`.
bool allBreaksAre(std::string_view t, LineEnding e) {
  size_t len = 0;
  BreakFinder bf(t);
  for (size_t at = bf.next(0, len); at != std::string_view::npos; at = bf.next(at + len, len))
    if (kindOf(t, at, len) != e) return false;
  return true;
}
}  // namespace

int TextBuffer::countBreaks(std::string_view text) { return static_cast<int>(tf::countBreaks(text)); }

void TextBuffer::setText(std::string_view text) {
  lines_.clear();
  ends_.clear();
  statsValid_ = false;
  sepValid_ = false;
  baseSet_ = false;
  bool have = false;
  LineEnding first = LineEnding::LF;
  size_t start = 0, len = 0;
  BreakFinder bf(text);
  while (true) {
    const size_t at = bf.next(start, len);
    if (at == std::string_view::npos) {
      lines_.emplace_back(text.substr(start));
      break;
    }
    lines_.emplace_back(text.substr(start, at - start));
    const LineEnding k = kindOf(text, at, len);
    if (k != first || !have) {  // rare: the first break, or a different kind
      if (!have) {
        have = true;
        first = k;
      } else if (ends_.empty()) {
        ends_.assign(lines_.size() - 1, first);  // the lines before this one all ended with `first`
      }
    }
    if (!ends_.empty()) ends_.push_back(k);
    start = at + len;
  }
  if (!ends_.empty()) ends_.push_back(first);  // the last line: unused
  base_ = first;
  if (!ends_.empty()) base_ = lineEnding();
}

const TextBuffer::EndingCounts& TextBuffer::stats() const {
  if (statsValid_) return stats_;
  stats_ = {};
  if (ends_.empty()) {
    const int n = lineCount() - 1;
    switch (base_) {
      case LineEnding::CRLF: stats_.crlf = n; break;
      case LineEnding::CR: stats_.cr = n; break;
      case LineEnding::NEL: stats_.nel = n; break;
      case LineEnding::LS: stats_.ls = n; break;
      case LineEnding::PS: stats_.ps = n; break;
      default: stats_.lf = n; break;
    }
  } else {
    size_t c[6] = {0, 0, 0, 0, 0, 0};
    for (size_t i = 0; i + 1 < ends_.size(); ++i) ++c[static_cast<size_t>(ends_[i])];
    stats_.lf = static_cast<int>(c[0]);
    stats_.crlf = static_cast<int>(c[1]);
    stats_.cr = static_cast<int>(c[2]);
    stats_.nel = static_cast<int>(c[3]);
    stats_.ls = static_cast<int>(c[4]);
    stats_.ps = static_cast<int>(c[5]);
  }
  statsValid_ = true;
  return stats_;
}

TextBuffer::EndingCounts TextBuffer::endingCounts() const { return stats(); }

LineEnding TextBuffer::lineEnding() const {
  const EndingCounts& c = stats();
  if (c.total() == 0) return base_;
  // The most frequent; ties: LF, then CRLF, CR, NEL, LS, PS.
  LineEnding best = LineEnding::LF;
  int most = c.lf;
  const std::pair<LineEnding, int> rest[] = {{LineEnding::CRLF, c.crlf}, {LineEnding::CR, c.cr}, {LineEnding::NEL, c.nel},
                                             {LineEnding::LS, c.ls},     {LineEnding::PS, c.ps}};
  for (const auto& [e, n] : rest)
    if (n > most) {
      best = e;
      most = n;
    }
  return best;
}

void TextBuffer::setLineEnding(LineEnding e) {
  base_ = e;
  baseSet_ = true;
  ends_.clear();
  statsValid_ = false;
  sepValid_ = false;
}

void TextBuffer::compact() {
  if (ends_.empty() || mixedLineEndings()) return;
  base_ = lineEnding();
  ends_.clear();
  ends_.shrink_to_fit();
}

bool TextBuffer::mixedLineEndings() const {
  if (ends_.empty()) return false;
  const EndingCounts& c = stats();
  return (c.crlf != 0) + (c.lf != 0) + (c.cr != 0) + (c.nel != 0) + (c.ls != 0) + (c.ps != 0) > 1;
}

LineEnding TextBuffer::endingAfter(int i) const {
  const int n = lineCount();
  i = std::clamp(i, 0, n - 1);
  if (i + 1 < n) return endOf(static_cast<size_t>(i));
  return i > 0 ? endOf(static_cast<size_t>(i) - 1) : lineEnding();
}

LineEnding TextBuffer::endingForNewLine(int line, LineEnding fallback) const {
  if (lineCount() == 1) return baseSet_ ? base_ : fallback;
  return endingAfter(line);
}

void TextBuffer::materializeEnds() {
  if (ends_.empty()) ends_.assign(lines_.size(), base_);
}

std::string TextBuffer::text() const {
  std::string out;
  out.reserve(byteSize());
  for (size_t i = 0; i < lines_.size(); ++i) {
    if (i) out += '\n';
    out += lines_[i];
  }
  return out;
}

std::string TextBuffer::textForDisk() const {
  if (ends_.empty() && base_ == LineEnding::LF) return text();
  std::string out;
  out.reserve(byteSize() + lines_.size());
  for (size_t i = 0; i < lines_.size(); ++i) {
    out += lines_[i];
    if (i + 1 < lines_.size()) out += terminatorText(endOf(i));
  }
  return out;
}

std::string TextBuffer::textForLsp() const {
  if (ends_.empty() && (base_ == LineEnding::LF || base_ == LineEnding::CR)) return text();
  std::string out;
  out.reserve(byteSize() + lines_.size());
  for (size_t i = 0; i < lines_.size(); ++i) {
    out += lines_[i];
    if (i + 1 < lines_.size()) {
      const LineEnding e = endOf(i);
      out += e == LineEnding::CRLF || isUnicodeSeparator(e) ? terminatorText(e) : std::string_view("\n");
    }
  }
  return out;
}

std::string TextBuffer::textForIndex() const {
  if (!hasSeparators()) return text();
  std::string out;
  out.reserve(byteSize() + lines_.size() * 2);
  for (size_t i = 0; i < lines_.size(); ++i) {
    out += lines_[i];
    if (i + 1 < lines_.size()) {
      const LineEnding e = endOf(i);
      out += isUnicodeSeparator(e) ? terminatorText(e) : std::string_view("\n");
    }
  }
  return out;
}

bool TextBuffer::hasSeparators() const { return stats().separators() > 0; }

const std::vector<int>& TextBuffer::separatorLines() const {
  if (sepValid_) return sepLines_;
  sepLines_.clear();
  if (hasSeparators())
    for (size_t i = 0; i + 1 < lines_.size(); ++i)
      if (isUnicodeSeparator(endOf(i))) sepLines_.push_back(static_cast<int>(i) + 1);
  sepValid_ = true;
  return sepLines_;
}


std::string TextBuffer::lspText(std::string_view text) {
  std::string out(text);
  for (size_t i = 0; i < out.size(); ++i)
    if (out[i] == '\r') {
      if (i + 1 < out.size() && out[i + 1] == '\n') ++i;
      else out[i] = '\n';
    }
  return out;
}

std::string TextBuffer::withLineEnding(std::string_view text, LineEnding e) {
  const std::string_view sep = terminatorText(e);
  std::string out;
  out.reserve(text.size() + 16);
  size_t start = 0, len = 0;
  BreakFinder bf(text);
  while (true) {
    const size_t at = bf.next(start, len);
    if (at == std::string_view::npos) {
      out.append(text.substr(start));
      return out;
    }
    out.append(text.substr(start, at - start));
    out.append(sep);
    start = at + len;
  }
}

size_t TextBuffer::byteSize() const {
  size_t n = lines_.empty() ? 0 : lines_.size() - 1;
  for (auto& l : lines_) n += l.size();
  return n;
}

Position TextBuffer::clamp(Position p) const {
  if (p.line < 0) return {0, 0};
  if (p.line >= lineCount()) return endPosition();
  int len = static_cast<int>(lines_[static_cast<size_t>(p.line)].size());
  p.column = std::clamp(p.column, 0, len);
  return p;
}

Position TextBuffer::endPosition() const {
  int last = lineCount() - 1;
  return {last, static_cast<int>(lines_[static_cast<size_t>(last)].size())};
}

size_t TextBuffer::offsetOf(Position p) const {
  p = clamp(p);
  size_t off = 0;
  for (int i = 0; i < p.line; ++i) off += lines_[static_cast<size_t>(i)].size() + 1;
  return off + static_cast<size_t>(p.column);
}

Position TextBuffer::positionAt(size_t offset) const {
  for (int i = 0; i < lineCount(); ++i) {
    size_t len = lines_[static_cast<size_t>(i)].size();
    if (offset <= len) return {i, static_cast<int>(offset)};
    offset -= len + 1;
  }
  return endPosition();
}

std::string TextBuffer::textInRange(Range r) const {
  r.start = clamp(r.start);
  r.end = clamp(r.end);
  if (r.end < r.start) std::swap(r.start, r.end);
  if (r.start.line == r.end.line) {
    return lines_[static_cast<size_t>(r.start.line)].substr(static_cast<size_t>(r.start.column),
                                                            static_cast<size_t>(r.end.column - r.start.column));
  }
  std::string out = lines_[static_cast<size_t>(r.start.line)].substr(static_cast<size_t>(r.start.column));
  for (int i = r.start.line + 1; i < r.end.line; ++i) {
    out += '\n';
    out += lines_[static_cast<size_t>(i)];
  }
  out += '\n';
  out += lines_[static_cast<size_t>(r.end.line)].substr(0, static_cast<size_t>(r.end.column));
  return out;
}

std::string TextBuffer::textInRangeRaw(Range r) const {
  r.start = clamp(r.start);
  r.end = clamp(r.end);
  if (r.end < r.start) std::swap(r.start, r.end);
  if (r.start.line == r.end.line) return textInRange(r);
  std::string out = lines_[static_cast<size_t>(r.start.line)].substr(static_cast<size_t>(r.start.column));
  for (int i = r.start.line; i < r.end.line; ++i) {
    out += terminatorText(endOf(static_cast<size_t>(i)));
    if (i + 1 < r.end.line) out += lines_[static_cast<size_t>(i) + 1];
  }
  out += lines_[static_cast<size_t>(r.end.line)].substr(0, static_cast<size_t>(r.end.column));
  return out;
}

namespace {
// Splits `text` into lines at "\r\n", "\n" and lone '\r'; appends to `out` (and the kind of each break to `kinds`).
void splitInto(std::string_view text, std::vector<std::string>& out, std::vector<LineEnding>& kinds) {
  size_t start = 0, len = 0;
  BreakFinder bf(text);
  while (true) {
    const size_t at = bf.next(start, len);
    if (at == std::string_view::npos) {
      out.emplace_back(text.substr(start));
      return;
    }
    out.emplace_back(text.substr(start, at - start));
    kinds.push_back(kindOf(text, at, len));
    start = at + len;
  }
}

size_t extra0(const std::vector<TextBuffer::Edit>& edits) {
  size_t extra = 0;
  for (const TextBuffer::Edit& e : edits) extra += countBreaks(e.text);
  return extra;
}

bool allEqual(const std::vector<LineEnding>& v, LineEnding e) {
  for (LineEnding k : v)
    if (k != e) return false;
  return true;
}

// Where the end of `text` lands when it is inserted at `start`.
Position insertedEnd(Position start, std::string_view text) {
  size_t lastEnd = 0, len = 0;
  int breaks = 0;
  BreakFinder bf(text);
  for (size_t at = bf.next(0, len); at != std::string_view::npos; at = bf.next(lastEnd, len)) {
    ++breaks;
    lastEnd = at + len;
  }
  if (!breaks) return {start.line, start.column + static_cast<int>(text.size())};
  return {start.line + breaks, static_cast<int>(text.size() - lastEnd)};
}
}  // namespace

TextBuffer::EditResult TextBuffer::replace(Range r, std::string_view text) {
  r.start = clamp(r.start);
  r.end = clamp(r.end);
  if (r.end < r.start) std::swap(r.start, r.end);

  EditResult res;
  res.removed = r;
  const size_t sl = static_cast<size_t>(r.start.line), el = static_cast<size_t>(r.end.line);
  if (sl == el && !hasBreak(text)) {  // keystroke fast path: edit the one line in place
    lines_[sl].replace(static_cast<size_t>(r.start.column), static_cast<size_t>(r.end.column - r.start.column), text);
    res.inserted = {r.start, insertedEnd(r.start, text)};
    return res;
  }

  std::vector<std::string> pieces;
  std::vector<LineEnding> kinds;
  splitInto(text, pieces, kinds);
  const size_t m = pieces.size(), o = el - sl + 1;
  if (ends_.empty() && !kinds.empty()) {
    if (lines_.size() == 1 && !baseSet_) base_ = kinds[0];  // no break yet: the first one decides
    if (!allEqual(kinds, base_)) ends_.assign(lines_.size(), base_);
  }
  res.inserted = {r.start,
                  {r.start.line + static_cast<int>(m) - 1,
                   static_cast<int>((m == 1 ? static_cast<size_t>(r.start.column) : 0) + pieces.back().size())}};
  res.lineDelta = static_cast<int>(m) - static_cast<int>(o);

  // New first line = head of the first old line + first piece; new last line = last piece + tail of the last old line.
  std::string tail = lines_[el].substr(static_cast<size_t>(r.end.column));
  lines_[sl].resize(static_cast<size_t>(r.start.column));
  if (m == 1) {
    lines_[sl] += pieces[0];
    lines_[sl] += tail;
  } else {
    lines_[sl] += pieces[0];
    pieces.back() += tail;
    const size_t reuse = std::min(m, o);
    for (size_t j = 1; j < reuse; ++j) lines_[sl + j] = std::move(pieces[j]);
    if (m > o)  // a single shift of the lines below
      lines_.insert(lines_.begin() + static_cast<std::ptrdiff_t>(el + 1),
                    std::move_iterator(pieces.begin() + static_cast<std::ptrdiff_t>(o)),
                    std::move_iterator(pieces.end()));
  }
  if (m < o)
    lines_.erase(lines_.begin() + static_cast<std::ptrdiff_t>(sl + m),
                 lines_.begin() + static_cast<std::ptrdiff_t>(el + 1));
  sepValid_ = false;
  if (!ends_.empty()) {  // the new lines get the breaks of the inserted text; the last keeps the old last line's
    statsValid_ = false;
    kinds.push_back(ends_[el]);
    const auto at = static_cast<std::ptrdiff_t>(sl);
    const size_t reuse = std::min(m, o);
    std::copy(kinds.begin(), kinds.begin() + static_cast<std::ptrdiff_t>(reuse), ends_.begin() + at);
    if (m > o)
      ends_.insert(ends_.begin() + at + static_cast<std::ptrdiff_t>(o), kinds.begin() + static_cast<std::ptrdiff_t>(o),
                   kinds.end());
    if (m < o)
      ends_.erase(ends_.begin() + at + static_cast<std::ptrdiff_t>(m),
                  ends_.begin() + at + static_cast<std::ptrdiff_t>(o));
  } else {
    statsValid_ = false;
  }
  return res;
}

std::vector<TextBuffer::EditResult> TextBuffer::replaceMany(const std::vector<Edit>& edits) {
  const size_t n = edits.size();
  std::vector<EditResult> results(n);
  if (n == 0) return results;
  if (n == 1) {
    results[0] = replace(edits[0].range, edits[0].text);
    return results;
  }
  std::vector<Range> rs(n);
  bool sameLine = true;  // every edit stays within one line and inserts no line break
  bool ordered = true;
  for (size_t k = 0; k < n; ++k) {
    Range r{clamp(edits[k].range.start), clamp(edits[k].range.end)};
    if (r.end < r.start) std::swap(r.start, r.end);
    rs[k] = r;
    if (k > 0 && r.start < rs[k - 1].end) ordered = false;
    if (r.start.line != r.end.line || hasBreak(edits[k].text)) sameLine = false;
  }
  if (!ordered) {  // contract violated: still do something well-defined (bottom-to-top, one at a time)
    for (size_t k = n; k-- > 0;) results[k] = replace(edits[k].range, edits[k].text);
    return results;
  }
  for (size_t k = 0; k < n; ++k) {
    results[k].removed = rs[k];
    results[k].inserted = {rs[k].start, insertedEnd(rs[k].start, edits[k].text)};
    results[k].lineDelta = results[k].inserted.end.line - rs[k].start.line - (rs[k].end.line - rs[k].start.line);
  }
  if (sameLine) {  // later edits first, so earlier columns on a shared line stay valid
    for (size_t k = n; k-- > 0;)
      lines_[static_cast<size_t>(rs[k].start.line)].replace(static_cast<size_t>(rs[k].start.column),
                                                            static_cast<size_t>(rs[k].end.column - rs[k].start.column),
                                                            edits[k].text);
    return results;
  }

  if (ends_.empty()) {
    for (const Edit& e : edits)
      if (!allBreaksAre(e.text, base_)) {
        ends_.assign(lines_.size(), base_);
        break;
      }
  }
  statsValid_ = false;
  sepValid_ = false;
  const bool track = !ends_.empty();
  std::vector<LineEnding> oe;
  const size_t extra = extra0(edits);
  if (track) oe.reserve(lines_.size() + extra);

  // General case: build the new line vector once, moving untouched lines. `cur` is the line under construction;
  // (curLine, curCol) is the first old byte not yet consumed.
  std::vector<std::string> out;
  out.reserve(lines_.size() + extra);
  std::string cur;
  size_t curLine = 0, curCol = 0;
  for (size_t k = 0; k < n; ++k) {
    const Range& r = rs[k];
    const size_t sl = static_cast<size_t>(r.start.line), sc = static_cast<size_t>(r.start.column);
    if (sl > curLine) {
      cur.append(lines_[curLine], curCol, std::string::npos);
      out.push_back(std::move(cur));
      cur.clear();
      out.insert(out.end(), std::move_iterator(lines_.begin() + static_cast<std::ptrdiff_t>(curLine + 1)),
                 std::move_iterator(lines_.begin() + static_cast<std::ptrdiff_t>(sl)));
      if (track)
        oe.insert(oe.end(), ends_.begin() + static_cast<std::ptrdiff_t>(curLine),
                  ends_.begin() + static_cast<std::ptrdiff_t>(sl));
      curLine = sl;
      curCol = 0;
    }
    cur.append(lines_[curLine], curCol, sc - curCol);
    std::string_view text = edits[k].text;
    size_t start = 0, blen = 0;
    BreakFinder bf(text);
    while (true) {
      const size_t at = bf.next(start, blen);
      if (at == std::string_view::npos) {
        cur.append(text.substr(start));
        break;
      }
      cur.append(text.substr(start, at - start));
      out.push_back(std::move(cur));
      cur.clear();
      if (track) oe.push_back(kindOf(text, at, blen));
      start = at + blen;
    }
    curLine = static_cast<size_t>(r.end.line);
    curCol = static_cast<size_t>(r.end.column);
  }
  cur.append(lines_[curLine], curCol, std::string::npos);
  out.push_back(std::move(cur));
  out.insert(out.end(), std::move_iterator(lines_.begin() + static_cast<std::ptrdiff_t>(curLine + 1)),
             std::move_iterator(lines_.end()));
  if (track) {
    oe.insert(oe.end(), ends_.begin() + static_cast<std::ptrdiff_t>(curLine), ends_.end());
    ends_ = std::move(oe);
  }
  lines_ = std::move(out);
  return results;
}

int TextBuffer::utf8ToUtf16Column(std::string_view line, int byteCol) {
  int units = 0;
  size_t i = 0;
  size_t limit = std::min(line.size(), static_cast<size_t>(std::max(byteCol, 0)));
  while (i < limit) {
    unsigned char c = static_cast<unsigned char>(line[i]);
    size_t len = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
    units += (len == 4) ? 2 : 1;
    i += len;
  }
  return units;
}

int TextBuffer::utf16ToUtf8Column(std::string_view line, int utf16Col) {
  int units = 0;
  size_t i = 0;
  while (i < line.size() && units < utf16Col) {
    unsigned char c = static_cast<unsigned char>(line[i]);
    size_t len = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
    units += (len == 4) ? 2 : 1;
    i += len;
  }
  return static_cast<int>(std::min(i, line.size()));
}

TextBuffer::Word TextBuffer::wordAt(Position p) const {
  p = clamp(p);
  const std::string& l = lines_[static_cast<size_t>(p.line)];
  int s = p.column, e = p.column;
  auto ok = [&](int i) {
    return i >= 0 && i < static_cast<int>(l.size()) &&
           isIdentChar(static_cast<unsigned char>(l[static_cast<size_t>(i)]));
  };
  if (!ok(s) && ok(s - 1)) {
    --s;
    e = s;
  }  // cursor just after a word
  if (!ok(s)) return {"", {p, p}};
  while (ok(s - 1)) --s;
  while (ok(e)) ++e;
  return {l.substr(static_cast<size_t>(s), static_cast<size_t>(e - s)), {{p.line, s}, {p.line, e}}};
}

}  // namespace tf
