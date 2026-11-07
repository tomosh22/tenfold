#pragma once
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "semantic/FastIndex.h"
#include "semantic/ScopeScan.h"

namespace tf {

// Scope-aware narrowing of the fast tier's by-name lookup (docs/ARCHITECTURE.md, "Scope-aware lookup in the fast
// tier"). Everything here uses the lexer index and the lexical scope maps (ScopeScan) only, and every rule is
// "narrow when the lexical evidence is clear, else leave the candidates alone": a rule that would leave nothing is
// not applied, so the narrowed answer is never emptier than the by-name one.

// Scope maps of files, cached until the file's index entry is replaced (an edit, a re-index).
class ScopeCache {
 public:
  // `a`/`b` identify the version of the text (an open document's version and identity, a file's mtime and size);
  // `build` runs without the lock when the cached entry is missing or has another stamp. Both 0 disables caching.
  std::shared_ptr<const ScopeMap> get(const std::string& key, uint64_t a, uint64_t b,
                                      const std::function<std::shared_ptr<const ScopeMap>()>& build);
  void clear();

 private:
  struct Entry {
    uint64_t a = 0, b = 0;
    std::shared_ptr<const ScopeMap> map;
    uint64_t tick = 0;
  };
  std::mutex mu_;
  std::unordered_map<std::string, Entry> map_;
  uint64_t tick_ = 0;
};

struct ScopeEnv {
  const FastIndex* index = nullptr;
  // The scope map of any indexed file (the engine reads it from the open document or from disk); may return null.
  std::function<std::shared_ptr<const ScopeMap>(const std::filesystem::path&)> mapOf;
  // The include directories of the compile command of `file` (quoted-only, then all), if it has one; may be null.
  std::function<void(const std::filesystem::path& file, std::vector<std::filesystem::path>& quote,
                     std::vector<std::filesystem::path>& angle)>
      includeDirsOf;
};

// What the caret says.
struct CaretInfo {
  std::filesystem::path file;
  std::shared_ptr<const ScopeMap> map;  // of `file`; null = unknown
  ScanPos at;                           // start of the word
  std::string_view text;                // the caret's line
  size_t wordStart = 0, wordEnd = 0;    // byte columns in `text`
  std::string word;
  std::string qualifier;      // "A::B" written to the left of the word
  bool globalQualifier = false;  // `::word`
  bool afterTagKeyword = false;  // the word is written right after struct / class / union / enum
  // The following lines (for an argument list that continues), `lineCount` lines in total; may be empty.
  std::function<const std::string&(int)> lineAt;
  int lineCount = 0;
};

enum class AccessKind { None, Dot, Arrow };

// How the word is reached: `x.word` / `p->word`, else None (a `::` qualifier is `qualifier`).
AccessKind accessKindOf(const CaretInfo& ci);

// A parameter / local variable of the function around the caret that this unqualified word names, if any.
std::optional<ScanVar> findLocal(const CaretInfo& ci);

// Number of arguments of the call whose '(' follows the word (`f(a, g(b), c)` = 3), -1 when no argument list follows
// or it cannot be counted reliably (template-looking angle brackets, an unterminated list).
int callArgCount(const CaretInfo& ci);

// How many arguments the construction of a class written at the caret has (`Name(a, b)`, `Name{a}`, `new Name(...)`,
// and in a function body `Name var{a};`; a plain `Name var;` names the class only, as clangd answers): -2 when the caret is not a construction, -1 when it is but the
// count is unknown.
int constructionArgCount(const CaretInfo& ci);

// Narrows `matches` (a by-name lookup of ci.word, qualifier already applied by the caller) with the rules of the
// design: visibility of file-local entities, member access by the receiver's lexical type, the caret's namespace /
// class chain with using-directives and base classes, the include graph, and the call's argument count.
// `isMemberAccess` and the class closure make a `T::f` qualifier also look into base classes.
std::vector<FastIndex::Match> narrowByScope(const ScopeEnv& env, const CaretInfo& ci, std::vector<FastIndex::Match> matches);

// The classes named by a qualifier written at the caret (`Derived` in `Derived::f`), and their bases, as qualified
// scope names. Empty when the qualifier is not a class known to the index.
std::vector<std::string> classScopesOfQualifier(const ScopeEnv& env, const CaretInfo& ci, const std::string& qualifier);

bool isSourceFileName(const std::filesystem::path& p);

}  // namespace tf
