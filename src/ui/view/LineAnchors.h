#pragma once
#include <cstddef>
#include <vector>

#include "core/Document.h"
#include "ui/gfx/TextLayout.h"

namespace tf::ui {

// Column checkpoints for very long lines (W11 perf): every kStride bytes of a line of at least kMinLine bytes the
// cache remembers a cell boundary and its column (gfx::Anchor), so byte <-> column <-> x mappings, the first visible
// cell of a horizontally scrolled line and the caret's x scan a few hundred cells instead of the whole line. Without
// it a caret at the end of a 1 MB line of wide characters costs a 35 ms scan per frame.
//
// Anchors are built front to back, only as far as somebody asked (a query at byte N scans from the last anchor below
// N). An edit keeps the anchors that lie wholly before it (bytes up to the edit column minus a UTF-8 sequence), drops
// the rest and shifts the entries of the lines below; so typing at the end of a line keeps all of them, typing at the
// start drops them and the next query rebuilds only what it needs. Lines shorter than kMinLine are never stored and
// answer with the default Anchor (scan from byte 0, which is cheap for them).
class LineAnchors {
 public:
  static constexpr size_t kMinLine = 2048, kStride = 1024;

  // Rebinds; another buffer or tab width drops everything.
  void bind(const TextBuffer* buf, const gfx::TextLayout* tl);
  void reset() { entries_.clear(); }
  // Document change (Document::addListener), after the buffer changed.
  void onChange(const Document::Change& c);

  // The last anchor with byte <= `byte`, / col <= `col`. Extends the cache as far as needed.
  gfx::Anchor atByte(int line, size_t byte);
  gfx::Anchor atColumn(int line, int col);
  // Total columns of the line (scans to its end once; later answers come from the cache until an edit).
  int columnCount(int line);
  // Number of lines that have anchors (tests).
  size_t cachedLines() const { return entries_.size(); }
  // Anchors held for a line (tests).
  const std::vector<gfx::Anchor>* anchorsOf(int line) const;

 private:
  struct Entry {
    int line = 0;
    size_t size = 0;    // line length when the anchors were built (staleness check)
    bool done = false;  // the scan reached the end of the line; `cols` is its width
    int cols = 0;
    std::vector<gfx::Anchor> a{{}};  // ascending; a[0] is always {0, 0}
  };
  Entry* entry(int line);
  void extend(Entry& e, size_t limitByte);
  size_t lowerBound(int line) const;

  const TextBuffer* buf_ = nullptr;
  const gfx::TextLayout* tl_ = nullptr;
  int tabWidth_ = 0;
  std::vector<Entry> entries_;  // sorted by line
};

}  // namespace tf::ui
