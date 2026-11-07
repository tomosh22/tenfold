#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/LineBreaks.h"
#include "core/Position.h"

namespace tf {

// The first three are what clang knows; NEL (U+0085), LS (U+2028) and PS (U+2029) are line breaks in Visual Studio's
// editor and here, but ordinary characters to clang / clangd (see "Text for clangd" in docs/ARCHITECTURE.md).
enum class LineEnding : uint8_t { LF, CRLF, CR, NEL, LS, PS };
constexpr bool isUnicodeSeparator(LineEnding e) { return e >= LineEnding::NEL; }

// Line-oriented UTF-8 text storage. Lines are stored without terminators.
// "a\nb\n" is stored as {"a","b",""}, so lineCount() is always >= 1.
// A line ends at "\r\n", "\n", a lone '\r', NEL (C2 85), LS (E2 80 A8) or PS (E2 80 A9) like in Visual Studio's editor
// (core/LineBreaks.h), so a stored line never contains a line break. clang and clangd only know the first three:
// textForLsp() and LspLineMap bridge the difference.
//
// Every line keeps its OWN terminator (Visual Studio does the same: docs/VS_BEHAVIOUR.md section 1): setText() records
// what each break was, replace() records the breaks of the text it inserts (text handed to replace() is interpreted
// literally: "\r\n" is a CRLF break, "\n" an LF break, a lone '\r' a CR break, U+0085 / U+2028 / U+2029 a NEL / LS / PS
// break), and textForDisk() writes every line
// with the terminator it has, so a mixed file stays byte for byte what it was. Joining two lines keeps the terminator
// of the later one. The last line has no terminator. Representation (compact): `base_` is the ending of every line
// while the document is uniform (the usual case: no per-line storage at all); the first break of a different kind
// switches to one byte per line (`ends_`, parallel to lines_). lineEnding() is the dominant ending (ties: LF, then
// CRLF) and mixedLineEndings() tells whether the terminators differ; both are cached and recomputed only after an edit
// in a document that has `ends_`.
class TextBuffer {
 public:
  TextBuffer() : lines_(1) {}
  explicit TextBuffer(std::string_view text) { setText(text); }

  void setText(std::string_view text);
  std::string text() const;         // joined with '\n'
  std::string textForDisk() const;  // every line followed by its own terminator (what a file holds)
  // What a language server is sent for this document: the text with its real line endings (a CRLF file stays CRLF, so
  // the buffer clangd parses is what is on disk and what its index holds: W9e finding 4), except that a lone CR is sent
  // as LF: clangd's LSP layer finds lines at '\n' only while clang's SourceManager also ends them at a lone '\r', so
  // CR text makes the two disagree. NEL / LS / PS are sent as the characters they are (clangd does not see them as line
  // breaks), so with them the language server's lines are not the editor's: positions go through LspLineMap. Without
  // them line numbers and columns are identical for every ending.
  std::string textForLsp() const;
  // The text for the fast index (the lexer): '\n' for the breaks clang knows, the separators as characters, so a `//`
  // comment continues past an LS like it does in clang. Equal to text() when the document has no NEL / LS / PS.
  std::string textForIndex() const;
  // `text` as a language server must see it (see textForLsp): lone '\r' breaks become '\n', "\r\n" stays.
  static std::string lspText(std::string_view text);
  // `text` with every line break ('\r\n', '\n', lone '\r', NEL, LS, PS) replaced by `e`.
  static std::string withLineEnding(std::string_view text, LineEnding e);
  // Number of line breaks in `text` ("\r\n" counts once).
  static int countBreaks(std::string_view text);
  static std::string_view terminatorText(LineEnding e) {
    switch (e) {
      case LineEnding::CRLF: return "\r\n";
      case LineEnding::CR: return "\r";
      case LineEnding::NEL: return "\xC2\x85";
      case LineEnding::LS: return "\xE2\x80\xA8";
      case LineEnding::PS: return "\xE2\x80\xA9";
      default: return "\n";
    }
  }

  int lineCount() const { return static_cast<int>(lines_.size()); }
  const std::string& line(int i) const { return lines_[static_cast<size_t>(i)]; }
  const std::vector<std::string>& lines() const { return lines_; }
  // The dominant terminator (ties: LF, then CRLF). With no terminator at all (one line) it is the ending set by
  // setLineEnding() (the default for new lines of an empty document).
  LineEnding lineEnding() const;
  // Makes every terminator `e` (no undo: Document::convertLineEndings is the undoable edit). On a one-line document it
  // only sets the ending the next line break will use.
  void setLineEnding(LineEnding e);
  bool mixedLineEndings() const;  // the terminators are not all the same
  struct EndingCounts {
    int crlf = 0, lf = 0, cr = 0, nel = 0, ls = 0, ps = 0;
    int total() const { return crlf + lf + cr + nel + ls + ps; }
    int separators() const { return nel + ls + ps; }
  };
  EndingCounts endingCounts() const;
  // Terminator that follows line `i` (the last line has none: then the ending a break after it would get, which is
  // the one of the line before it, else the dominant one).
  LineEnding endingAfter(int i) const;
  bool hasTerminator(int i) const { return i + 1 < lineCount(); }
  // Visual Studio's ReplicateNewLineCharacter: the ending a new line break typed on `line` gets: the terminator of
  // that line, else (last line) the one of the line before, else `fallback` (a document without any break).
  LineEnding endingForNewLine(int line, LineEnding fallback) const;
  // Drops the per-line storage when every terminator turned out to be the same (after edits that unified them).
  void compact();
  // The raw terminators of the lines (empty = every line uses lineEnding(); see the class comment).
  bool hasPerLineEndings() const { return !ends_.empty(); }

  // Does a NEL / LS / PS end any line? Then the language server's lines differ from the editor's (see textForLsp).
  bool hasSeparators() const;
  // Editor line numbers j (ascending) whose line starts right after a NEL / LS / PS: line j is a continuation of the
  // language server's line of line j-1 (see LspLineMap). Built on demand and cached until an edit changes the line
  // structure (keystrokes do not); like every lazily cached value here it must be called on the thread that owns the
  // buffer.
  const std::vector<int>& separatorLines() const;

  size_t byteSize() const;

  struct EditResult {
    Range removed;      // the range that was replaced (clamped)
    Range inserted;     // where the new text now lives
    int lineDelta = 0;  // lines after the edit minus lines before
  };

  // Replace `range` with `text`. Returns details for listeners (undo,
  // navigation history, LSP incremental sync).
  EditResult replace(Range range, std::string_view text);
  // Apply several edits at once. `edits` must be sorted by position and non-overlapping (edit k+1 starts at or
  // after the end of edit k), all in the coordinates of the buffer BEFORE the call. Cost is O(lines + inserted text)
  // instead of O(lines) per edit. Result k equals what replace() returns when the edits are applied one at a time
  // from the last to the first: `removed` and `inserted.start` are in the original coordinates, `inserted.end`
  // is where the text ends in the state with edits k+1.. applied but not 0..k-1.
  struct Edit {
    Range range;
    std::string_view text;
  };
  std::vector<EditResult> replaceMany(const std::vector<Edit>& edits);
  EditResult insert(Position at, std::string_view text) { return replace({at, at}, text); }
  EditResult erase(Range range) { return replace(range, {}); }

  std::string textInRange(Range range) const;     // line breaks as '\n'
  std::string textInRangeRaw(Range range) const;  // line breaks as stored (CRLF / LF / CR): what undo and copy keep

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
  void materializeEnds();
  LineEnding endOf(size_t i) const { return ends_.empty() ? base_ : ends_[i]; }

  std::vector<std::string> lines_;
  LineEnding base_ = LineEnding::LF;  // ending of every line while ends_ is empty
  bool baseSet_ = false;              // setLineEnding() chose base_ (matters while the document has no break)
  std::vector<LineEnding> ends_;      // empty, or one entry per line (the last one is unused)
  mutable bool statsValid_ = false;
  mutable EndingCounts stats_;
  mutable bool sepValid_ = false;
  mutable std::vector<int> sepLines_;
  const EndingCounts& stats() const;
};

}  // namespace tf
