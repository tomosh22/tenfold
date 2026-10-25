#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/Document.h"
#include "semantic/SemanticEngine.h"
#include "ui/gfx/TextLayout.h"
#include "ui/widgets/Theme.h"

namespace tf::ui {

// Lexical token classes of a line (whitespace is not a span).
enum class Lex : uint8_t { Keyword, Identifier, Number, String, Char, Comment, Preprocessor, Operator, Punct };
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
class Highlighter {
 public:
  static constexpr int kSyncBudget = 1500, kLookback = 200;

  void attach(const TextBuffer* b);  // new document: drops the cache and the semantic tokens
  bool attached(const TextBuffer* b) const { return buf_ == b; }
  void onChange(const Document::Change& c);
  void setTheme(const Theme& t);
  // Replaces the semantic tokens (sorted by line, column); `types` / `modifiers` are the server legend.
  void setSemanticTokens(std::vector<TokenSpan> spans, std::vector<std::string> types,
                         std::vector<std::string> modifiers);

  // Makes lines [first, last) available to colorSpans (exact when affordable, else provisional).
  void prepare(int first, int last);
  // Spans of one prepared line, sorted, for TextRenderer::drawSpans with palette().
  const std::vector<gfx::ColorSpan>& colorSpans(int line);
  const std::vector<gfx::Color>& palette() const { return pal_; }
  uint32_t defaultColor() const { return textIdx_; }
  // Lexical spans of a line that is part of the exact prefix (null otherwise).
  const std::vector<LexSpan>* exactSpans(int line) const {
    return line >= 0 && line < consistent_ ? &e_[static_cast<size_t>(line)].spans : nullptr;
  }
  // Lexical spans of a prepared line, exact or provisional (null when the line was not prepared).
  const std::vector<LexSpan>* lineSpans(int line) const { return spansOf(line); }
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
    std::vector<LexSpan> spans;
  };
  bool advance(int target, int& budget);
  void relex(Entry& e, int line, const LexState& begin);
  const std::vector<LexSpan>* spansOf(int line) const;
  void reset();
  uint32_t idx(gfx::Color c);
  uint32_t semanticColor(const TokenSpan& s);

  const TextBuffer* buf_ = nullptr;
  std::vector<Entry> e_;
  int consistent_ = 0;
  // Provisional lexing of [provBase_, provBase_ + prov_.size()) when the exact prefix is too far behind.
  std::vector<Entry> prov_;
  int provBase_ = 0, wantLast_ = 0;
  bool provValid_ = false, pending_ = false;

  std::vector<TokenSpan> sem_;  // len == 0: dropped by an edit
  std::vector<std::string> types_, modifiers_;
  std::unordered_map<uint64_t, uint32_t> semIdx_;  // (modifiers << 16 | type) -> palette index, kNone = no override

  Theme theme_;
  std::vector<gfx::Color> pal_;
  uint32_t textIdx_ = 0, kindIdx_[9] = {};
  std::vector<gfx::ColorSpan> out_;
};

}  // namespace tf::ui
