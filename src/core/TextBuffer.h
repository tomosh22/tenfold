#pragma once
#include <string>
#include <string_view>
#include <vector>

#include "core/Position.h"

namespace tf {

enum class LineEnding { LF, CRLF };

// Line-oriented UTF-8 text storage. Lines are stored without terminators.
// "a\nb\n" is stored as {"a","b",""}, so lineCount() is always >= 1.
class TextBuffer {
 public:
  TextBuffer() : lines_(1) {}
  explicit TextBuffer(std::string_view text) { setText(text); }

  void setText(std::string_view text);
  std::string text() const;         // joined with '\n'
  std::string textForDisk() const;  // joined with the detected line ending

  int lineCount() const { return static_cast<int>(lines_.size()); }
  const std::string& line(int i) const { return lines_[static_cast<size_t>(i)]; }
  const std::vector<std::string>& lines() const { return lines_; }
  LineEnding lineEnding() const { return lineEnding_; }
  void setLineEnding(LineEnding e) { lineEnding_ = e; }

  size_t byteSize() const;

  struct EditResult {
    Range removed;      // the range that was replaced (clamped)
    Range inserted;     // where the new text now lives
    int lineDelta = 0;  // lines after the edit minus lines before
  };

  // Replace `range` with `text`. Returns details for listeners (undo,
  // navigation history, LSP incremental sync).
  EditResult replace(Range range, std::string_view text);
  EditResult insert(Position at, std::string_view text) { return replace({at, at}, text); }
  EditResult erase(Range range) { return replace(range, {}); }

  std::string textInRange(Range range) const;

  Position clamp(Position p) const;
  Position endPosition() const;
  size_t offsetOf(Position p) const;  // byte offset in text()
  Position positionAt(size_t offset) const;

  // Column conversions between UTF-8 byte offsets and UTF-16 code units.
  static int utf8ToUtf16Column(std::string_view line, int byteCol);
  static int utf16ToUtf8Column(std::string_view line, int utf16Col);

  // Identifier under/adjacent to position (C/C++ identifier characters).
  struct Word {
    std::string text;
    Range range;
  };
  Word wordAt(Position p) const;

 private:
  std::vector<std::string> lines_;
  LineEnding lineEnding_ = LineEnding::LF;
};

}  // namespace tf
