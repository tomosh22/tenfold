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
    if (end > start && text[end - 1] == '\r') { --end; ++crlf; } else { ++lf; }
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
    return lines_[static_cast<size_t>(r.start.line)].substr(
        static_cast<size_t>(r.start.column), static_cast<size_t>(r.end.column - r.start.column));
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

TextBuffer::EditResult TextBuffer::replace(Range r, std::string_view text) {
  r.start = clamp(r.start);
  r.end = clamp(r.end);
  if (r.end < r.start) std::swap(r.start, r.end);

  const int before = lineCount();
  std::string head = lines_[static_cast<size_t>(r.start.line)].substr(0, static_cast<size_t>(r.start.column));
  std::string tail = lines_[static_cast<size_t>(r.end.line)].substr(static_cast<size_t>(r.end.column));

  // Split the inserted text into lines.
  std::vector<std::string> newLines;
  size_t start = 0;
  while (true) {
    size_t nl = text.find('\n', start);
    if (nl == std::string_view::npos) {
      newLines.emplace_back(text.substr(start));
      break;
    }
    size_t end = nl;
    if (end > start && text[end - 1] == '\r') --end;
    newLines.emplace_back(text.substr(start, end - start));
    start = nl + 1;
  }

  Position insEnd{r.start.line + static_cast<int>(newLines.size()) - 1,
                  static_cast<int>((newLines.size() == 1 ? head.size() : 0) + newLines.back().size())};
  newLines.front() = head + newLines.front();
  newLines.back() += tail;

  auto first = lines_.begin() + r.start.line;
  auto last = lines_.begin() + r.end.line + 1;
  lines_.erase(first, last);
  lines_.insert(lines_.begin() + r.start.line, std::move_iterator(newLines.begin()),
                std::move_iterator(newLines.end()));

  EditResult res;
  res.removed = r;
  res.inserted = {r.start, insEnd};
  res.lineDelta = lineCount() - before;
  return res;
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
  auto ok = [&](int i) { return i >= 0 && i < static_cast<int>(l.size()) && isIdentChar(static_cast<unsigned char>(l[static_cast<size_t>(i)])); };
  if (!ok(s) && ok(s - 1)) { --s; e = s; }  // cursor just after a word
  if (!ok(s)) return {"", {p, p}};
  while (ok(s - 1)) --s;
  while (ok(e)) ++e;
  return {l.substr(static_cast<size_t>(s), static_cast<size_t>(e - s)), {{p.line, s}, {p.line, e}}};
}

}  // namespace tf
