#include "core/TextBuffer.h"

#include <algorithm>
#include <cstring>

namespace tf {

namespace {
bool isIdentChar(unsigned char c) {
  return std::isalnum(c) || c == '_' || c >= 0x80;  // treat non-ASCII as identifier-ish
}
}  // namespace

void TextBuffer::setText(std::string_view text) {
  lines_.clear();
  int crlf = 0, lf = 0;
  size_t start = 0;
  while (true) {
    size_t nl = text.find('\n', start);
    if (nl == std::string_view::npos) {
      lines_.emplace_back(text.substr(start));
      break;
    }
    size_t end = nl;
    if (end > start && text[end - 1] == '\r') {
      --end;
      ++crlf;
    } else {
      ++lf;
    }
    lines_.emplace_back(text.substr(start, end - start));
    start = nl + 1;
  }
  lineEnding_ = (crlf > lf) ? LineEnding::CRLF : LineEnding::LF;
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
  if (lineEnding_ == LineEnding::LF) return text();
  std::string out;
  for (size_t i = 0; i < lines_.size(); ++i) {
    if (i) out += "\r\n";
    out += lines_[i];
  }
  return out;
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

namespace {
// Splits `text` into lines, dropping a '\r' that precedes a '\n'; appends to `out`.
void splitInto(std::string_view text, std::vector<std::string>& out) {
  size_t start = 0;
  while (true) {
    size_t nl = text.find('\n', start);
    if (nl == std::string_view::npos) {
      out.emplace_back(text.substr(start));
      return;
    }
    size_t end = nl;
    if (end > start && text[end - 1] == '\r') --end;
    out.emplace_back(text.substr(start, end - start));
    start = nl + 1;
  }
}

// Where the end of `text` lands when it is inserted at `start`.
Position insertedEnd(Position start, std::string_view text) {
  size_t last = text.rfind('\n');
  if (last == std::string_view::npos) return {start.line, start.column + static_cast<int>(text.size())};
  const int newlines = static_cast<int>(std::count(text.begin(), text.end(), '\n'));
  return {start.line + newlines, static_cast<int>(text.size() - last - 1)};
}
}  // namespace

TextBuffer::EditResult TextBuffer::replace(Range r, std::string_view text) {
  r.start = clamp(r.start);
  r.end = clamp(r.end);
  if (r.end < r.start) std::swap(r.start, r.end);

  EditResult res;
  res.removed = r;
  const size_t sl = static_cast<size_t>(r.start.line), el = static_cast<size_t>(r.end.line);
  if (sl == el && text.find('\n') == std::string_view::npos) {  // keystroke fast path: edit the one line in place
    lines_[sl].replace(static_cast<size_t>(r.start.column), static_cast<size_t>(r.end.column - r.start.column), text);
    res.inserted = {r.start, insertedEnd(r.start, text)};
    return res;
  }

  std::vector<std::string> pieces;
  splitInto(text, pieces);
  const size_t m = pieces.size(), o = el - sl + 1;
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
    if (r.start.line != r.end.line || edits[k].text.find('\n') != std::string_view::npos) sameLine = false;
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

  // General case: build the new line vector once, moving untouched lines. `cur` is the line under construction;
  // (curLine, curCol) is the first old byte not yet consumed.
  size_t extra = 0;
  for (const Edit& e : edits) extra += static_cast<size_t>(std::count(e.text.begin(), e.text.end(), '\n'));
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
      curLine = sl;
      curCol = 0;
    }
    cur.append(lines_[curLine], curCol, sc - curCol);
    std::string_view text = edits[k].text;
    size_t start = 0;
    while (true) {
      size_t nl = text.find('\n', start);
      if (nl == std::string_view::npos) {
        cur.append(text.substr(start));
        break;
      }
      size_t end = nl;
      if (end > start && text[end - 1] == '\r') --end;
      cur.append(text.substr(start, end - start));
      out.push_back(std::move(cur));
      cur.clear();
      start = nl + 1;
    }
    curLine = static_cast<size_t>(r.end.line);
    curCol = static_cast<size_t>(r.end.column);
  }
  cur.append(lines_[curLine], curCol, std::string::npos);
  out.push_back(std::move(cur));
  out.insert(out.end(), std::move_iterator(lines_.begin() + static_cast<std::ptrdiff_t>(curLine + 1)),
             std::move_iterator(lines_.end()));
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
