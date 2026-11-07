#pragma once
#include <memory>
#include <string>
#include <vector>

#include "core/Position.h"
#include "core/TextBuffer.h"

namespace tf {

// Editor (line, UTF-8 byte column) <-> language server (line, column) for a document with NEL / LS / PS line breaks.
//
// The editor ends a line at NEL, LS and PS like Visual Studio does; clang and clangd do not (they are ordinary
// characters there), so TextBuffer::textForLsp() sends them as characters and the server's lines are the editor's lines
// glued together at those separators. A "run" is such a group of consecutive editor lines; every other line is one
// line for both and only moves by the number of separators before it. The map is an immutable snapshot of the text
// that was sent (build it from the buffer when sending), so it can be used from any thread, and answers in O(log runs).
// A document without separators has no map at all (build() returns null): the editor's line is the server's line and
// only the column is converted (utf16 or not), as before.
class LspLineMap {
 public:
  // Null when no line of `b` ends in NEL / LS / PS.
  static std::shared_ptr<const LspLineMap> build(const TextBuffer& b);

  // `lines` are the editor lines (TextBuffer::lines()); only the line `p.line` is read. `utf16`: columns count UTF-16
  // code units (the LSP default), else bytes (offsetEncoding utf-8).
  Position toServer(Position p, const std::vector<std::string>& lines, bool utf16) const;
  // A server position past the editor's last line comes back with its line number shifted but its column unchanged.
  Position fromServer(Position q, const std::vector<std::string>& lines, bool utf16) const;

  // Number of server lines for a buffer of `editorLines` editor lines.
  int serverLineCount(int editorLines) const;
  size_t runCount() const { return runs_.size(); }

 private:
  struct Seg {
    int u8 = 0;     // offset of the segment's first byte in the server line (UTF-8 bytes)
    int u16 = 0;    // the same in UTF-16 code units
    int bytes = 0;  // content bytes of the editor line
    int units = 0;  // its length in UTF-16 code units
  };
  struct Run {
    int editorStart = 0;  // first editor line
    int count = 0;        // editor lines in the run (>= 2)
    int serverLine = 0;
    int firstSeg = 0;
    int sepBefore = 0;  // separators before the run's first line
  };
  std::vector<Run> runs_;
  std::vector<Seg> segs_;
};

}  // namespace tf
