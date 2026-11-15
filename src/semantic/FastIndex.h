#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace tf {

enum class SymbolKind : uint8_t {
  Namespace,
  Class,
  Struct,
  Union,
  Enum,
  Enumerator,
  Function,
  Method,
  Constructor,
  Variable,
  Field,
  Typedef,
  Macro,
  Concept,
  Unknown
};
const char* symbolKindName(SymbolKind k);

struct Symbol {
  std::string name;   // unqualified, e.g. "length_sq"
  std::string scope;  // qualified enclosing scope, e.g. "geo::Vec2" ("" at global scope)
  int line = 0;       // 0-based
  int col = 0;        // 0-based byte column of the name token
  SymbolKind kind = SymbolKind::Unknown;
  bool definition = true;
  bool isTemplate = false;
  // Number of declared parameters of a function / method / constructor (default arguments count, `(void)` is 0),
  // -1 when not a function or unknown. Used to pair a prototype with its definition; capped at kMaxArity. For a macro:
  // -1 object-like, the parameter count for a function-like macro.
  int8_t arity = -1;
  // Function / method / constructor: the number of parameters without a default argument (-1 unknown); a call with
  // n arguments can reach it when minArity <= n <= arity, or n >= minArity when `variadic` (a trailing `...`).
  int8_t minArity = -1;
  bool variadic = false;
  // Internal linkage that is visible in the symbol's own file only: `static` at namespace scope (functions and
  // variables). Entities in an anonymous namespace carry "(anonymous)" in their scope instead.
  bool fileLocal = false;
  // A forward declaration (`class Widget;`, `struct Node;`) of a class / struct / union: kept for go to declaration
  // only. Never an answer of search() or lookup() (forwardDeclarations() lists them); definition is false.
  bool forward = false;
  std::string qualified() const { return scope.empty() ? name : scope + "::" + name; }
};

// One arm of a preprocessor conditional group (`#if`, `#ifdef`, `#ifndef`, `#elif`, `#else`). Symbols record the arm
// they sit in (FileSymbols::symArm); an arm is active when its parent arm is, every earlier arm of its group (`prev`)
// is not, and its own condition holds. Ids are 1-based indexes into FileSymbols::arms, 0 = outside any conditional.
// An include guard (`#ifndef X` directly followed by `#define X`) is not an arm: its content is unconditional.
struct CondArm {
  enum Kind : uint8_t {
    If,      // `#if expr`
    Ifdef,   // `#ifdef NAME` (expr = NAME)
    Ifndef,  // `#ifndef NAME`
    Elif,    // `#elif expr` (`#elifdef X` / `#elifndef X` are stored as `defined(X)` / `!defined(X)`)
    Else,
    Never  // the `#else` / `#elif` of an include guard: the guard is taken, so these are not
  };
  Kind kind = If;
  uint32_t parent = 0;  // enclosing arm
  uint32_t prev = 0;    // previous arm of the same group (Elif / Else / Never)
  int line = 0;         // 0-based line of the directive
  std::string expr;     // condition text, comments removed
};

// A `#define` / `#undef` of the file, in source order (the conditions evaluator replays them).
struct MacroDef {
  std::string name;
  int line = 0;
  uint32_t arm = 0;   // conditional arm the directive sits in
  int32_t body = -1;  // index into FileSymbols::bodies: the replacement list of an object-like macro whose body is
                      // a plain integer expression; -1 = no body worth keeping (value unknown)
  bool undef = false;
  bool functionLike = false;
};

// ---- FAST-DECL: names declared through macros --------------------------------------------------------------------
// A macro the file defines (`#define NAME(params) body`; object-like ones only when their body holds an identifier),
// kept (bounded) so that the macro calls that declare things can be expanded (MacroExpand.h). Source order within the
// file; `line` is the 0-based line of the directive.
struct MacroTemplate {
  std::string name;
  std::vector<std::string> params;  // without a trailing `...` (variadic) and without `__VA_ARGS__`
  bool variadic = false;
  bool objectLike = false;  // `#define NAME body` (kept only when the body names something: an identifier)
  std::string body;         // the replacement list: its tokens (comments removed) joined by single spaces
  // Where each token of `body` is spelled in the file: (line - `line`) << 16 | column, line delta and column clipped
  // to 255 / 65535 (0xFFFFFFFF: unknown). One entry per token, or empty (all unknown).
  std::vector<uint32_t> pos;
  int line = 0;
};
// One argument of a macro call, as written (source text, comments included) and where its first token is.
struct MacroArg {
  std::string text;
  int line = 0;
  int col = 0;
};
// A call that looks like a macro invoked in place of a declaration: `NAME(args)` at namespace or class scope with no
// type before it (`DECLARE_CLASS(Gadget)`, `X(Red, 1, "red")`). `line` / `col` are the position of NAME.
struct MacroCall {
  std::string name;
  int line = 0;
  int col = 0;
  std::string scope;  // qualified enclosing scope, as Symbol::scope
  SymbolKind scopeKind =
      SymbolKind::Namespace;  // innermost enclosing scope: Namespace (also global), Class, Struct, Union
  uint32_t arm = 0;           // conditional arm the call sits in (as Symbol arms)
  std::vector<MacroArg> args;
  // A call that names the declarator after a type (`int CAT(var_, one) = 1;`): the text before the name (from the start
  // of the declaration) and after the closing parenthesis (up to the `;`), so the expansion is read in its statement.
  bool withContext = false;
  MacroArg pre, post;
};

// An `#include` that follows a `#define` directly (`#define X(n) ...` / `#include "list.def"`): the included file's
// macro calls are read with the macros defined before it (an X-macro list). `enumName` is set when the include sits in
// the enumerator list of that enum.
struct MacroInclude {
  std::string spelling;
  bool angled = false;
  int line = 0;
  int col = 0;
  std::string scope;  // qualified scope the include sits in (for an enumerator list: the enum's enclosing scope)
  SymbolKind scopeKind = SymbolKind::Namespace;
  uint32_t arm = 0;
  std::string enumName;
  bool enumScoped = false;
};

struct FileSymbols {
  std::filesystem::path file;
  std::vector<Symbol> symbols;
  std::vector<std::string> includes;  // paths as written in #include directives
  std::vector<uint8_t> angled;        // parallel to `includes`: 1 = `<x>`, 0 = `"x"` (empty: all quoted)
  // Preprocessor conditionals (W12-FAST-PP). Empty for a file without any.
  std::vector<CondArm> arms;
  std::vector<std::pair<uint32_t, uint32_t>> symArm;  // (symbol index, arm id) for the symbols inside an arm, sorted
  std::vector<MacroDef> macros;                       // #define / #undef directives
  std::vector<std::string> bodies;
  // FAST-DECL (store format 4): function-like macros defined here, and the declaration-position macro calls.
  std::vector<MacroTemplate> templates;
  std::vector<MacroCall> calls;
  std::vector<MacroInclude> xincludes;
  // Not persisted. Set on the entry that holds the symbols a file's macro calls declare (FastIndex::refreshExpansions):
  // `file` is the calling file, `origin` the calling file's own entry (the one the arm ids of `symArm` refer to), and
  // the symbols are positioned at the macro argument / call that produced them.
  std::shared_ptr<const FileSymbols> origin;
  // The arm id of symbol `i` (0 = unconditional).
  uint32_t armOf(size_t i) const {
    if (symArm.empty()) return 0;
    auto it = std::lower_bound(symArm.begin(), symArm.end(), std::make_pair(static_cast<uint32_t>(i), uint32_t{0}));
    return it != symArm.end() && it->first == i ? it->second : 0;
  }
  // On-disk stat of the file when it was lexed (0 = unknown, e.g. lexed from an unsaved buffer). The persisted
  // store (FastIndexStore) trusts an entry only when both still match. mtime is last_write_time in clock ticks.
  int64_t mtime = 0;
  uint64_t size = 0;
};

// Heuristic declaration extractor: lexer-based, no preprocessing, no type
// resolution. Fast (hundreds of MB/s/core) and deliberately over-inclusive:
// it indexes every #if branch and never needs compile flags. Its results are
// APPROXIMATE; clangd is the source of truth (see SemanticEngine).
// `isC`: the file is C (.c), where class, delete, new, template, this, namespace ... are plain identifiers.
std::vector<Symbol> extractSymbols(std::string_view source, std::vector<std::string>* includes = nullptr,
                                   bool isC = false);
// Everything extractSymbols does, plus the preprocessor conditionals: fills out.symbols, out.includes, out.arms,
// out.symArm, out.macros and out.bodies (not file / mtime / size).
void extractFile(std::string_view source, FileSymbols& out, bool isC = false);
bool isCSourceFile(const std::filesystem::path& file);
// Canonical spelling of an operator function name, given the source text that follows the `operator` keyword:
// "+" -> "operator+", "()" -> "operator()", " new[]" -> "operator new[]", " const char*()" -> "operator const char*"
// (a space only between two word-like tokens). The extractor names operator functions and conversion operators this
// way; the approximate-answer path builds the same name for a caret on a written `operator` so the two agree.
// Empty when `text` does not start like an operator-function-id.
std::string operatorFunctionName(std::string_view textAfterKeyword);
constexpr int kMaxArity = 62;

class FastIndex {
 public:
  struct Match {
    std::shared_ptr<const FileSymbols> file;
    size_t index = 0;
    int score = 0;
    const Symbol& sym() const { return file->symbols[index]; }
  };

  struct Stats {
    size_t files = 0;
    size_t symbols = 0;
    size_t bytes = 0;
    double seconds = 0;
    size_t reused = 0;   // indexFilesCached: entries taken from the cache (stat matched)
    size_t relexed = 0;  // files actually read and lexed
    size_t dropped = 0;  // cached entries whose file is no longer in the list / on disk
  };

  // Index files from disk using `threads` workers. Blocking; call from a
  // background thread for a responsive UI. `progress(done, total)` is optional.
  Stats indexFiles(const std::vector<std::filesystem::path>& files, int threads = 0,
                   std::function<void(size_t, size_t)> progress = {});
  // Warm-start variant: `cached` holds entries from a previous run (FastIndexStore::load). A file whose
  // mtime and size still match its cached entry is reused without being read; new and changed files are lexed;
  // cached entries not in `files` (or no longer readable) are dropped. The result is inserted with a single
  // bulk import (one lock acquisition). Stats.reused/relexed/dropped report what happened.
  Stats indexFilesCached(const std::vector<std::filesystem::path>& files,
                         std::vector<std::shared_ptr<const FileSymbols>> cached, int threads = 0,
                         std::function<void(size_t, size_t)> progress = {});
  // Bulk insert (replaces entries with the same path). Takes the lock once.
  void importFiles(std::vector<std::shared_ptr<const FileSymbols>> files);
  // Snapshot of every indexed file (cheap: shared_ptr copies), for persisting.
  std::vector<std::shared_ptr<const FileSymbols>> exportFiles() const;
  // Incremented by every mutation; lets a saver tell whether the index changed since it last persisted.
  uint64_t generation() const { return generation_.load(std::memory_order_acquire); }

  // FAST-DECL: expands the declaration-position macro calls of every indexed file with the macros the index knows
  // (MacroExpand.h) and indexes the symbols they declare, as extra entries of the calling file (FileSymbols::origin is
  // set; exportFiles() / fileCount() do not see them). Called by every mutation; cheap when nothing it depends on
  // changed (a call is expanded again only when its file changed, a macro definition changed or the include graph
  // changed). Thread-safe.
  void refreshExpansions();
  // Switches the expansion layer off (and removes it) or on again; on by default. For tests that compare the index with
  // an oracle over the extracted files, and for measuring.
  void setExpandMacros(bool on);
  // Number of symbols that come from macro expansion (tests, statistics).
  size_t expandedSymbolCount() const;

  void updateFile(const std::filesystem::path& file, std::string_view text);
  // Bytes of text updateFile() has lexed so far (tests: how much re-lexing an edit costs on the calling thread).
  uint64_t updatedBytes() const { return updatedBytes_.load(std::memory_order_relaxed); }
  void removeFile(const std::filesystem::path& file);
  void clear();

  // Fuzzy search, best matches first (score desc, then name asc, then index insertion order). A query containing
  // "::" is matched against qualified names. Thread-safe (shared lock); large indexes are scanned by several
  // threads and the result is bounded top-`limit` selection, so cost does not grow with the number of matches.
  // `cancelled` (optional) is polled between scan blocks; once it returns true the scan stops early and an
  // EMPTY vector is returned (the caller is going to discard the answer anyway).
  std::vector<Match> search(std::string_view query, size_t limit = 200,
                            const std::function<bool()>& cancelled = {}) const;
  // Files that are in the index only so that name resolution sees their declarations (headers a solution workspace's
  // files include but no project lists): search() does not return their symbols; lookup(), reachableFrom() and every
  // other query still see them. `keys` are pathKey()s and replace the previous set; a file added later with such a key
  // is hidden too.
  void setSearchHidden(std::unordered_set<std::string> keys);
  // Like setSearchHidden for files that stay hidden whatever setSearchHidden later says (the lists an X-macro file
  // includes, read for their macro calls only). Call before the files are added.
  void addSearchHidden(const std::unordered_set<std::string>& keys);
  // Worker threads used by search(): 0 (default) = hardware concurrency capped at 8, 1 = scan on the calling thread.
  void setSearchThreads(int n) { searchThreads_ = n; }
  // Optional warm-up: builds the scan table plain (non-qualified) queries use. search() does this itself on demand
  // (and again after a mutation added or removed a name); calling it right after indexing just moves that cost
  // off the first query. Thread-safe.
  void prepareSearch() const;
  // Exact (case-sensitive) name lookup, definitions first.
  std::vector<Match> lookup(std::string_view name) const;
  // The forward declarations (`class Widget;`) of a class-like name; lookup() and search() never return them.
  std::vector<Match> forwardDeclarations(std::string_view name) const;

  // What the caller wants out of a name lookup (see shapeAnswer).
  enum class Want { Any, Definition, Declaration };
  struct Shape {
    Want want = Want::Any;
    // The word is written right after `struct` / `class` / `union` / `enum`: a tag is meant, not a typedef of the
    // same name.
    bool afterTagKeyword = false;
    // The caret is on the name of a constructor declaration / definition: only constructors of that scope are meant.
    bool constructorOnly = false;
    std::string constructorScope;
    int constructorArity = -1;  // with constructorOnly: only the constructors with this many parameters (-1 = any)
  };
  // Narrows a by-name lookup the way a declaration-aware tool would, using only what the lexer index knows:
  //  * a class name is the class, not its constructors (unless `constructorOnly`);
  //  * a typedef and a struct/class/union/enum tag of the same name and scope: the typedef for a plain use, the tag
  //    after the tag keyword;
  //  * a function / method / constructor / variable that has both prototypes and definitions: Want::Definition keeps
  //    the definitions that are paired with a prototype of the same name, scope and arity, drops those prototypes;
  //    Want::Declaration drops the definitions that have such a prototype (the definition alone when there is none).
  // The order of the remaining matches is kept. Name, scope and arity only: the index has no overload resolution.
  static std::vector<Match> shapeAnswer(std::vector<Match> matches, const Shape& shape);
  std::shared_ptr<const FileSymbols> fileSymbols(const std::filesystem::path& file) const;

  // The files whose declarations `file` can see through #include, transitively, `file` itself first-class: their
  // path keys (FastIndex::pathKey). An include is resolved the way the compiler's quoted lookup would: next to the
  // includer, else by path suffix against the indexed files (no include directories are known to the lexer index).
  // Null when the walk reaches more than `maxFiles` files (the answer is then "no restriction"). Cached per file
  // until a file is added, removed or changes its #include list.
  // `quoteDirs` / `angleDirs` (optional) are the include directories of the file's compile command, in the order the
  // compiler searches them: a spelling is looked up next to the includer (quoted only), in `quoteDirs` (quoted only),
  // then in `angleDirs`; a spelling none of them resolves falls back to the path-suffix rule.
  using Reach = std::unordered_set<std::string>;
  std::shared_ptr<const Reach> reachableFrom(const std::filesystem::path& file,
                                             const std::vector<std::filesystem::path>& quoteDirs = {},
                                             const std::vector<std::filesystem::path>& angleDirs = {},
                                             size_t maxFiles = 6000) const;
  // The indexed file with this path key (FastIndex::pathKey), else null.
  std::shared_ptr<const FileSymbols> fileSymbolsByKey(const std::string& key) const;
  // The files that may #include `target`: those with an #include whose last path component is target's file name (a
  // superset; the caller verifies the spelling). Sorted by path key. The reverse include table is built on first use
  // and kept until the include graph changes.
  std::vector<std::shared_ptr<const FileSymbols>> includersOf(const std::filesystem::path& target) const;
  static std::string pathKey(const std::filesystem::path& p);
  // `<vector>`, `<stdio.h>`, `<sys/types.h>`: the standard library and OS interface headers a system include
  // directory holds (as opposed to a project or third-party header).
  static bool looksLikeSystemHeader(const std::string& spelling);

  size_t fileCount() const;
  size_t symbolCount() const;
  // Distinct #include paths seen anywhere in the index (for include-dir inference).
  std::vector<std::string> allIncludes() const;

  // Scoring is exposed for tests / UI highlighting. 0 means no match.
  static int fuzzyScore(std::string_view query, std::string_view candidate);

 private:
  struct Ref {
    std::shared_ptr<const FileSymbols> file;
    uint32_t index;
  };
  using NameMap = std::unordered_map<std::string, std::vector<Ref>>;
  struct ScanTable;
  void addLocked(std::shared_ptr<const FileSymbols> fs);
  // `synthetic`: an expansion entry (see synthKey): not counted as a file, not in the reverse include table, hidden
  // like its file, does not change generation().
  void insertLocked(const std::string& key, std::shared_ptr<const FileSymbols> fs, bool synthetic);
  void removeLocked(const std::string& key);
  // The indexed file an X-macro include names (next to the includer, else the only indexed file whose path ends with
  // the spelling), null when there is none or it is ambiguous. Caller holds mu_.
  std::shared_ptr<const FileSymbols> resolveListLocked(const FileSymbols& includer, const MacroInclude& x) const;
  // Builds / refreshes the table the plain (non-qualified) search scans if a mutation made it stale. Caller holds
  // mu_ (shared or unique). The returned table is valid for as long as mu_ stays held.
  std::shared_ptr<const ScanTable> prepare() const;
  void ensureFileMasks() const;  // caller holds mu_ (shared or unique)

  std::atomic<uint64_t> updatedBytes_{0};
  mutable std::shared_mutex mu_;
  std::unordered_map<std::string, std::shared_ptr<const FileSymbols>> files_;  // key: generic path string
  NameMap byName_;                                                             // key: exact name
  NameMap fwdByName_;  // forward declarations (Symbol::forward), by name: not in byName_
  size_t symbolCount_ = 0;
  std::unordered_map<std::string, std::vector<const FileSymbols*>> byBase_;  // lower-cased file name -> files
  std::atomic<uint64_t> reachGen_{1};  // bumped when the #include graph may have changed
  struct ReachEntry {
    uint64_t gen = 0;
    std::shared_ptr<const Reach> set;
  };
  mutable uint64_t incByGen_ = 0;                                                                   // reachMu_
  mutable std::unordered_map<std::string, std::vector<std::shared_ptr<const FileSymbols>>> incBy_;  // reachMu_
  mutable std::mutex reachMu_;
  mutable std::unordered_map<std::string, ReachEntry> reach_;
  std::atomic<uint64_t> generation_{0};
  std::atomic<int> searchThreads_{0};
  // FAST-DECL. mxEpoch_ changes when a function-like macro definition is added, removed or changed (mu_); callers_
  // holds the keys of the files that have macro calls (mu_); synthCount_ the entries of files_ that are expansions
  // (mu_); mxDone_ is what the last refresh was computed for (mxMu_).
  std::atomic<bool> expandMacros_{true};
  uint64_t mxEpoch_ = 0;
  std::unordered_set<std::string> callers_;
  std::unordered_set<std::string> mxListKeys_;  // keys of the files some X-macro include named (mu_)
  size_t xincFiles_ = 0;                        // files with X-macro includes (mu_)
  std::unordered_set<std::string> auxHidden_;   // keys of files added by addSearchHidden (mu_)
  std::unordered_map<std::string, std::vector<std::string>>
      synthOf_;  // calling file's key -> its expansion entries' keys (mu_)
  size_t synthCount_ = 0;
  size_t synthSymbols_ = 0;
  struct TplRef {
    std::shared_ptr<const FileSymbols> file;
    uint32_t index = 0;
  };
  std::mutex mxMu_;  // serialises refreshExpansions; guards the three below
  uint64_t mxDoneEpoch_ = ~uint64_t{0};
  uint64_t mxDoneReach_ = ~uint64_t{0};
  uint64_t mxTplsEpoch_ = ~uint64_t{0};
  std::unordered_map<std::string, std::weak_ptr<const FileSymbols>> mxSeen_;  // caller key -> the entry last expanded
  std::unordered_map<std::string, std::vector<TplRef>> mxTpls_;  // macro name -> its templates, as of mxTplsEpoch_
  // Per indexed file: its add order (search tie-break for qualified queries: refs of one name are in add order)
  // and, once a qualified query has needed them, the per-symbol character masks of "scope::name" that let that
  // scan skip most symbols without touching their strings. Guarded by mu_; `masks` is filled lazily under prepMu_.
  struct FileInfo {
    uint32_t seq = 0;
    mutable std::vector<uint32_t> masks;
  };
  std::unordered_map<const FileSymbols*, FileInfo> fileInfo_;
  uint32_t nextSeq_ = 0;
  std::unordered_set<std::string> hiddenKeys_;     // guarded by mu_
  std::unordered_set<const FileSymbols*> hidden_;  // the entries of files_ whose key is in hiddenKeys_ (mu_)
  // Search acceleration (see prepare()). namesGen_ changes when a name is added to / erased from byName_ (guarded
  // by mu_); table_ is guarded by prepMu_.
  size_t namesGen_ = 1;
  mutable std::mutex prepMu_;
  mutable std::shared_ptr<const ScanTable> table_;
};

}  // namespace tf
