#pragma once
// Corpus format (W9a): anchors scanned from the sources of a corpus project, the expectations of its
// tenfold-expect.txt, and the comparison of tenfold's answers with them. Everything here is pure (no engine, no
// clangd), so the parser and the comparison rules are unit-tested without a language server. The runner that drives
// the editor is in corpus/Runner.h; the format itself is specified in docs/TESTING.md.
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace tf::corpus {

// Name of the expectation file at the root of a corpus project.
inline constexpr const char* kExpectFileName = "tenfold-expect.txt";

// `/*@name*/token`: the position of the first character of `token`. Lines and columns are 0-based, columns are byte
// offsets; `file` is relative to the corpus root with '/' separators. `len` is the byte length of the token (an
// identifier, `~` + identifier of a destructor, or one punctuation character). `skip` is the number of bytes from
// `col` to the first identifier character: 1 for a destructor name (`~Foo`: queries are asked at the `F`), else 0.
// A `~` is a destructor's only when `~Identifier(` follows a declaration context (start of the line, `::`, `{`, `;`,
// `}`, `->`, `.`, or one of virtual / inline / constexpr / explicit / friend / public: ...) or the anchor says so
// (`/*@name~*/~Foo`); otherwise it is the unary operator, a one-character token like any punctuation.
struct Anchor {
  std::string name;
  std::string file;
  int line = 0, col = 0, len = 1, skip = 0;
};
using AnchorMap = std::map<std::string, Anchor>;

// A position (or range, when endLine >= 0) in an answer, in the same coordinates as Anchor. `file` is relative to the
// corpus root when the file is inside it, else as given.
struct Loc {
  std::string file;
  int line = 0, col = 0;
  int endLine = -1, endCol = -1;
  bool preferred = false;  // go to definition: the entry the chooser selects by default (SymbolLocation::preferred)
};

// Scans `text` of `relFile` for anchors and adds them to `out`. Problems (duplicate name, anchor without a token on
// its line) are appended to `errors` as "file:line: message".
void scanAnchorsInText(const std::string& relFile, std::string_view text, AnchorMap& out,
                       std::vector<std::string>& errors);
// Scans every text file below `dir` (skipping .tenfold/, .git/, tenfold-expect.txt, compile_commands.json, binary
// files). False when `errors` got entries.
bool scanAnchors(const std::filesystem::path& dir, AnchorMap& out, std::vector<std::string>& errors);
// Just the names of the anchors written in `text` (used to learn the anchors an `edit` introduces).
std::vector<std::string> anchorNamesIn(std::string_view text);

// ---- expectations ----

enum class Kind { Def, Decl, Typedef, Impl, Refs, Hover, Sym, Rename };
enum class Rel {
  None,      // rename (no relation)
  Arrow,     // ->  def/decl/typedef: any of the alternatives; impl: the set; sym: the first result
  Eq,        // =   exactly this set
  Ge,        // >=  at least this set
  Contains,  // ~   hover contains every quoted text
  Has        // ∋ / has  sym: every group of targets has a member among the results
};

const char* toString(Kind k);
const char* toString(Rel r);

struct Item {
  enum class Type { Check, Config, Open, Close, Edit } type = Type::Check;
  int line = 0;      // 1-based line in the expectation file
  std::string text;  // the line as written (without the trailing newline), for reports

  // Type::Check
  Kind kind = Kind::Def;
  bool fast = false;   // judged on the fast tier (clangd held back)
  bool xfail = false;  // a known wrong answer: reported, not failed; XPASS when it starts passing
  std::string reason;  // the text after '#' of an xfail line
  std::string anchor;  // subject: the token whose definition / references / ... is asked
  Rel rel = Rel::None;
  bool none = false;                 // `-> none`, or an empty set; rename: the indented line `(none)` (nothing changes)
  bool refused = false;              // rename ... `!`: clangd must refuse the rename (and nothing changes)
  std::vector<std::string> targets;  // anchors: alternatives (def, decl, typedef, sym ->) or set members
  // def / decl / typedef with `=` or `>=` (a set): `default B` names the anchor the chooser must select by default.
  std::string preferred;
  // sym ∋: `A | B C` is two groups {A, B} and {C}; each group needs one member among the results. `targets` holds
  // every name of every group.
  std::vector<std::vector<std::string>> groups;
  std::vector<std::string> texts;    // hover: texts that must occur
  std::string query;                 // sym: the query; rename: the new name
  std::vector<std::string> renamed;  // rename: anchors whose token must now read `query`

  // Type::Config (arg = "Debug|x64"), Type::Open / Type::Close (arg = file), Type::Edit (arg = file, editLine/editCol
  // 1-based)
  std::string arg;
  int editLine = 0, editCol = 0;
  std::string editText;
};

// Parses the text of an expectation file. `fileLabel` prefixes error messages ("tenfold-expect.txt"). Anchor names
// must exist in `anchors` or be introduced by an earlier `edit` line. Returns false with every problem in `errors`
// ("tenfold-expect.txt:12: message"); `out` is only meaningful when it returns true.
bool parseExpectations(std::string_view text, const std::string& fileLabel, const AnchorMap& anchors,
                       std::vector<Item>& out, std::vector<std::string>& errors);

// ---- comparison ----

// Does an answer at `loc` land on `a`? Same file and line, and the answer starts at the token or inside it
// (col in [a.col, a.col + a.len)); or the answer is a range (endLine >= 0) that contains the anchor's first character.
bool locMatchesAnchor(const Loc& loc, const Anchor& a);
// "file:line:col" (1-based) plus " [name]" when it lands on an anchor, else " (near name)" for the closest anchor of
// the same file; nothing appended without anchors in that file.
std::string describeLoc(const Loc& loc, const AnchorMap& anchors);
std::string describeLocs(const std::vector<Loc>& locs, const AnchorMap& anchors, size_t limit = 8);
// Same file and line (the rule used to compare the fast and the final answer of a query).
bool sameSpot(const Loc& a, const Loc& b);
bool sameAnswer(const std::vector<Loc>& a, const std::vector<Loc>& b);

struct Verdict {
  bool ok = false;
  std::string expected, actual;
};
// def, decl, typedef, impl, refs, sym: `actual` is the answer in order (sym: ranked).
Verdict checkLocations(const Item& it, const std::vector<Loc>& actual, const AnchorMap& anchors);
Verdict checkHover(const Item& it, const std::string& hoverText);
// Rename: `after` holds the anchors re-scanned after the rename and `tokenAfter(name)` is the identifier at each
// anchor now; `before` the same before. Pure part of the rename check, see the runner.
Verdict checkRename(const Item& it, const std::map<std::string, std::string>& tokensBefore,
                    const std::map<std::string, std::string>& tokensAfter);
// A rename that must change nothing (`(none)`) or must be refused (`!`): `answered` is false when clangd did not
// answer in time, `err` its error (empty when it accepted the rename) and `edits` the number of edits applied.
// `(none)` passes when no anchor's token changed (clangd may answer with no edits or with an error); `!` also needs
// the error and no edits.
Verdict checkRenameNoChange(const Item& it, bool answered, const std::string& err, size_t edits,
                            const std::map<std::string, std::string>& tokensBefore,
                            const std::map<std::string, std::string>& tokensAfter);

// Partial: a verdict that failed while clangd's background index was incomplete (the answer may be wrong only
// because the index is partial); counted apart from Fail and not a failure of the run.
enum class Status { Pass, Fail, XFail, XPass, Skip, Partial };
const char* toString(Status s);
Status classify(bool xfail, bool ok);

// ---- statistics ----

struct LatencyStats {
  size_t n = 0;
  double median = 0, p95 = 0, max = 0;  // milliseconds
};
LatencyStats computeStats(std::vector<double> ms);  // p95 by nearest rank

// Decodes the escapes of a quoted string body (\" \\ \n \t); false on a bad escape.
bool unescapeQuoted(std::string_view body, std::string& out);

}  // namespace tf::corpus
