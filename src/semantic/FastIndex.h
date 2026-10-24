#pragma once
#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace tf {

enum class SymbolKind {
  Namespace, Class, Struct, Union, Enum, Enumerator, Function, Method, Constructor,
  Variable, Field, Typedef, Macro, Concept, Unknown
};
const char* symbolKindName(SymbolKind k);

struct Symbol {
  std::string name;      // unqualified, e.g. "length_sq"
  std::string scope;     // qualified enclosing scope, e.g. "geo::Vec2" ("" at global scope)
  SymbolKind kind = SymbolKind::Unknown;
  int line = 0;          // 0-based
  int col = 0;           // 0-based byte column of the name token
  bool definition = true;
  bool isTemplate = false;
  std::string qualified() const { return scope.empty() ? name : scope + "::" + name; }
};

struct FileSymbols {
  std::filesystem::path file;
  std::vector<Symbol> symbols;
  std::vector<std::string> includes;  // paths as written in #include directives
};

// Heuristic declaration extractor: lexer-based, no preprocessing, no type
// resolution. Fast (hundreds of MB/s/core) and deliberately over-inclusive:
// it indexes every #if branch and never needs compile flags. Its results are
// APPROXIMATE; clangd is the source of truth (see SemanticEngine).
std::vector<Symbol> extractSymbols(std::string_view source, std::vector<std::string>* includes = nullptr);

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
  };

  // Index files from disk using `threads` workers. Blocking; call from a
  // background thread for a responsive UI. `progress(done, total)` is optional.
  Stats indexFiles(const std::vector<std::filesystem::path>& files, int threads = 0,
                   std::function<void(size_t, size_t)> progress = {});
  void updateFile(const std::filesystem::path& file, std::string_view text);
  void removeFile(const std::filesystem::path& file);
  void clear();

  // Fuzzy search, best matches first. A query containing "::" is matched
  // against qualified names.
  std::vector<Match> search(std::string_view query, size_t limit = 200) const;
  // Exact (case-sensitive) name lookup, definitions first.
  std::vector<Match> lookup(std::string_view name) const;
  std::shared_ptr<const FileSymbols> fileSymbols(const std::filesystem::path& file) const;

  size_t fileCount() const;
  size_t symbolCount() const;
  // Distinct #include paths seen anywhere in the index (for include-dir inference).
  std::vector<std::string> allIncludes() const;

  // Scoring is exposed for tests / UI highlighting. 0 means no match.
  static int fuzzyScore(std::string_view query, std::string_view candidate);

 private:
  struct Ref { std::shared_ptr<const FileSymbols> file; size_t index; };
  void addLocked(std::shared_ptr<const FileSymbols> fs);
  void removeLocked(const std::string& key);

  mutable std::shared_mutex mu_;
  std::unordered_map<std::string, std::shared_ptr<const FileSymbols>> files_;  // key: generic path string
  std::unordered_map<std::string, std::vector<Ref>> byName_;                    // key: exact name
  size_t symbolCount_ = 0;
};

}  // namespace tf
