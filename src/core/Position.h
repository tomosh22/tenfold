#pragma once
#include <compare>
#include <filesystem>
#include <string>

namespace tf {

// A position inside a text buffer. `line` is 0-based. `column` is the 0-based
// UTF-8 byte offset within that line (what the buffer stores). Conversions to
// UTF-16 code units (what LSP servers use by default) live in TextBuffer.
struct Position {
  int line = 0;
  int column = 0;
  auto operator<=>(const Position&) const = default;
};

struct Range {
  Position start;
  Position end;
  bool empty() const { return start == end; }
  bool contains(Position p) const { return p >= start && p < end; }
  auto operator<=>(const Range&) const = default;
};

// A position inside a particular file on disk.
struct Location {
  std::filesystem::path file;
  Position pos;
  bool operator==(const Location& o) const { return file == o.file && pos == o.pos; }
};

inline std::string toString(Position p) { return std::to_string(p.line + 1) + ":" + std::to_string(p.column + 1); }
inline std::string toString(const Location& l) { return l.file.string() + ":" + toString(l.pos); }

}  // namespace tf
