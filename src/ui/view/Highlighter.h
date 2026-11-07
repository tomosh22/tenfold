#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/Document.h"
#include "semantic/SemanticEngine.h"
#include "ui/gfx/TextLayout.h"
#include "ui/widgets/Theme.h"

namespace tf::ui {

// Lexical token classes of a line (whitespace is not a span).
enum class Lex : uint8_t {
  Keyword,
  Identifier,
  Number,
  String,
  Char,
  Comment,
  Preprocessor,
  Operator,
  Punct,
  ControlKeyword
};
struct LexSpan {
  uint32_t begin, end;
  Lex kind;
};
// What a line hands to the next one: inside a block comment, a raw string (with its delimiter), or a continued
// directive / line comment.
struct LexState {
  enum Mode : uint8_t { None, Block, Raw, Directive, LineComment };
  Mode mode = None;
  uint8_t dlen = 0;
  char delim[16] = {};
  bool operator==(const LexState& o) const;
};
// A resumable point of the scanner inside a line: a token boundary where the state is "no open construct"
// (mode None), or (mid != 0) a position inside a string / char literal with that quote character, so a literal of
// megabytes can be resumed too. lineStart says whether only whitespace / comments came before (a '#' there starts a
// directive).
struct LexResume {
  uint32_t pos;
  bool lineStart;
  char mid = 0;
};
// Scans one line (exposed for tests); returns the state at its end.
LexState lexLine(std::string_view line, LexState in, std::vector<LexSpan>& out);

// Colouring of a document for TextView (B2b, constraint 1.4: approximate first, authoritative later). Lexical spans
// per line come from a lazily filled cache and are available immediately; clangd semantic tokens (A9) override ONLY
// identifier colours, so a late arrival never changes keyword / comment / string colours (no flicker).
//
// Lexical cache: entry i holds the spans of line i lexed with the end state of line i-1. The first `consistent_`
// lines are known to chain correctly. An edit invalidates the edited lines only; re-lexing walks forward and keeps
// every later entry whose start state equals the state it was lexed with (a block comment closed again leaves the
// rest of the file untouched). First paint never lexes the whole file: past `kSyncBudget` re-lexed lines prepare()
// lexes just the visible range, starting `kLookback` lines early in the "no open construct" state (provisional), and
// step() finishes the exact pass in the background (a few thousand lines per tick).
//
// Very long lines (W11 perf; a minified file, a 1 MB expression): a line of kLongLine bytes or more keeps no span list.
// Its entry holds resume points every kResumeStride bytes (LexResume, the scanner's state at a token boundary) and
// the end state, which is what the next line needs. colorSpans(line, from, to) lexes just the window from the resume
// point before `from` and caches it. An edit keeps the resume points before it and shifts the ones after it (the
// "tail"); re-lexing starts at the last kept point and ends as soon as the scanner reaches a tail point in the same
// state (everything after is then unchanged, and so is the end state), so typing in a 1 MB line re-lexes a few KB,
// not the line. If the edit changes what follows (an opened string), the scan runs to the end of the line once.
class Highlighter {
 public:
  static constexpr int kSyncBudget = 1500, kLookback = 200;
  static constexpr size_t kLongLine = 2048, kResumeStride = 2048;

  void attach(const TextBuffer* b);  // new document: drops the cache and the semantic tokens
  bool attached(const TextBuffer* b) const { return buf_ == b; }
  void onChange(const Document::Change& c);
  void setTheme(const Theme& t);
  // Replaces the semantic tokens (sorted by line, column); `types` / `modifiers` are the server legend.
  void setSemanticTokens(std::vector<TokenSpan> spans, std::vector<std::string> types,
                         std::vector<std::string> modifiers);

  // Makes lines [first, last) available to colorSpans (exact when affordable, else provisional).
  void prepare(int first, int last);
  // The same for several ascending runs of lines (a folded view shows lines with hidden ones in between): hidden
  // lines are not lexed provisionally, only a lookback before each run.
  void prepareRuns(const std::vector<std::pair<int, int>>& runs);
  // Spans of one prepared line, sorted, for TextRenderer::drawSpans with palette(). For a long line only the tokens
  // that overlap bytes [from, to) are produced (those and their neighbours up to a resume point; a wider range is
  // fine, an empty result outside it is not): pass the window you draw.
  const std::vector<gfx::ColorSpan>& colorSpans(int line, size_t from = 0, size_t to = ~size_t{0});
  // Punctuation tokens (single characters of "(){}[];,.") of a prepared line that start in [from, to), appended to
  // `out`; false when the line is not prepared (or not in the exact prefix when `exact`). Works on long lines too.
  bool punctuation(int line, size_t from, size_t to, std::vector<LexSpan>& out, bool exact);
  bool isLong(int line) const;  // the line is lexed by windows
  // Bytes of long lines scanned so far (relexing after edits and window lexing), and the resume points of a line: for
  // tests of the incremental behaviour.
  uint64_t longLineBytesLexed() const { return lexedBytes_; }
  size_t resumePoints(int line, size_t* inLiterals = nullptr) const;
  const std::vector<gfx::Color>& palette() const { return pal_; }
  uint32_t defaultColor() const { return textIdx_; }
  // Lexical spans of a short line that is part of the exact prefix (null otherwise, and for long lines).
  const std::vector<LexSpan>* exactSpans(int line) const {
    return line >= 0 && line < consistent_ && !e_[static_cast<size_t>(line)].longLine ? &e_[static_cast<size_t>(line)].spans
                                                                                       : nullptr;
  }
  // Lexical spans of a prepared short line, exact or provisional (null when the line was not prepared or is long).
  const std::vector<LexSpan>* lineSpans(int line) const {
    const Entry* e = entryOf(line);
    return e && !e->longLine ? &e->spans : nullptr;
  }
  bool ensureExact(int line);  // extends the exact prefix to `line` within a budget; false if it did not get there
  int exactLines() const { return consistent_; }
  // Background work: true when visible colours may have changed. pending() says more is wanted (tick again).
  bool step(int budgetLines);
  bool pending() const { return pending_; }
  size_t semanticCount() const { return sem_.size(); }

 private:
  struct Entry {
    LexState begin, end;
    bool valid = false;
    std::vector<LexSpan> spans;  // a short line: all its spans; a long line: the cached window [winB, winE)
    // ---- long lines only (the 100k entries of a big file stay small) ----
    bool longLine = false;
    struct Long {
      std::vector<LexResume> ck;    // resume points, ascending; empty when the whole line is one leading construct
      std::vector<LexResume> tail;  // after an edit: the old resume points behind it, shifted (candidates to re-join)
      LexState tailEnd;             // the end state that goes with `tail`
      bool hasTail = false;
      uint32_t winB = 0, winE = 0;
      bool winValid = false;
    };
    std::unique_ptr<Long> lg;
  };
  bool advance(int target, int& budget);
  void relex(Entry& e, int line, const LexState& begin);
  void relexLong(Entry& e, int line, const LexState& begin);
  void windowLex(Entry& e, int line, size_t from, size_t to);
  const Entry* entryOf(int line) const;
  Entry* entryOf(int line) { return const_cast<Entry*>(static_cast<const Highlighter*>(this)->entryOf(line)); }
  void reset();
  uint32_t idx(gfx::Color c);
  uint32_t semanticColor(const TokenSpan& s);

  const TextBuffer* buf_ = nullptr;
  std::vector<Entry> e_;
  int consistent_ = 0;
  // Provisional lexing of the wanted runs when the exact prefix is too far behind.
  struct Seg {
    int base = 0;
    std::vector<Entry> e;
  };
  std::vector<Seg> provs_;
  int wantLast_ = 0;
  bool provValid_ = false, pending_ = false;

  uint64_t lexedBytes_ = 0;
  std::vector<TokenSpan> sem_;  // len == 0: dropped by an edit
  std::vector<std::string> types_, modifiers_;
  std::unordered_map<uint64_t, uint32_t> semIdx_;  // (modifiers << 16 | type) -> palette index, kNone = no override

  Theme theme_;
  std::vector<gfx::Color> pal_;
  uint32_t textIdx_ = 0, kindIdx_[10] = {};
  std::vector<gfx::ColorSpan> out_;
};

}  // namespace tf::ui
