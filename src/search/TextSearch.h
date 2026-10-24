#pragma once
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace tf {

// Fast parallel text search over a list of files. Standalone: it knows nothing
// about Editor or Workspace (the caller supplies the file list and, optionally,
// in-memory text for open documents), so tests and benchmarks can drive it
// directly. Callbacks run on worker threads, serialised by an internal mutex;
// Editor::findInWorkspace hops them to the main thread.
//
// Literal search never uses std::regex: it is a memchr first-byte scan plus a
// compare loop (ASCII case folding for case-insensitive). Regex uses PCRE2 when
// built with TENFOLD_WITH_PCRE2; otherwise a regex query reports an error.
//
// Case-insensitivity is ASCII only. "Whole word" means the match is not
// adjacent to [A-Za-z0-9_] or a non-ASCII byte (the check is skipped on a side
// where the query itself starts/ends with a non-word character). Files that
// contain a NUL in their first 8 KiB are treated as binary and skipped.
// Regex matches may span lines; they are reported on their first line with the
// length clipped to that line, and empty matches are ignored.
struct SearchOptions {
  std::string query;
  bool caseSensitive = true;
  bool wholeWord = false;
  bool regex = false;
  bool includeOtherFiles = false;  // Editor: also search Workspace::otherFiles()
  // Globs on workspace-relative generic paths ('/' separated). `*` matches
  // within one path segment, `**` across segments, `?` one non-'/' char. A
  // pattern without '/' matches any single path segment (so "*.cpp" matches
  // by file name and "third_party" excludes a whole directory); a pattern with
  // '/' is anchored at the root and also covers everything beneath a matching
  // directory. Empty include list = everything.
  std::vector<std::string> includeGlobs, excludeGlobs;
  size_t maxResults = 10000;
  size_t batchSize = 200;               // matches per onBatch call (last batch may be smaller)
  size_t maxFileBytes = 64u << 20;      // larger files are skipped
  unsigned threads = 0;                 // 0 = hardware concurrency
};

struct SearchMatch {
  std::filesystem::path file;
  int line = 0;        // 0-based
  int column = 0;      // 0-based UTF-8 byte offset within the line
  int length = 0;      // bytes, clipped to the end of the line
  std::string text;    // the line, without '\n' / '\r' (very long lines are windowed around the match)
  int textOffset = 0;  // byte offset of `text` within the line (0 unless the line was windowed)
};

struct SearchSummary {
  size_t matches = 0;        // delivered through onBatch
  size_t filesSearched = 0;
  size_t filesMatched = 0;
  bool truncated = false;    // more than maxResults matches existed
  bool cancelled = false;
  std::string error;         // e.g. bad regex, "regex support not built"
  double elapsedMs = 0;
};

struct SearchInput {
  std::filesystem::path root;                   // globs are matched against paths relative to this
  std::vector<std::filesystem::path> files;
  // Text to search instead of the disk version, keyed by path relative to
  // `root` (generic '/' form). Take the snapshot before starting.
  std::map<std::string, std::string> overrides;
};

using SearchBatchFn = std::function<void(std::vector<SearchMatch>&&)>;
using SearchDoneFn = std::function<void(const SearchSummary&)>;

// Shared handle to a running search. Copyable; all methods are thread-safe.
class SearchHandle {
 public:
  struct State;
  SearchHandle() = default;
  explicit SearchHandle(std::shared_ptr<State> s) : s_(std::move(s)) {}
  static SearchHandle create();  // an idle handle that can be passed to TextSearch::start
  bool valid() const { return s_ != nullptr; }
  void cancel();
  bool cancelled() const;
  bool finished() const;  // onDone has returned
  void wait() const;      // block until finished

 private:
  friend class TextSearch;
  std::shared_ptr<State> s_;
};

class TextSearch {
 public:
  static bool regexSupported();
  // Launch asynchronously; returns immediately. onDone is called exactly once,
  // after the last onBatch, even on error or cancellation.
  // Pass a handle from SearchHandle::create() when the callbacks need to see it (e.g. to check cancelled()).
  static SearchHandle start(SearchInput in, SearchOptions opt, SearchBatchFn onBatch, SearchDoneFn onDone,
                            SearchHandle handle = {});
  // Blocking convenience wrapper (collects everything).
  static SearchSummary run(SearchInput in, SearchOptions opt, std::vector<SearchMatch>* out);

  // Glob helpers (exposed for tests and for UIs that pre-validate filters).
  static bool globMatch(std::string_view pattern, std::string_view path);           // whole string, '/'-aware
  static bool pathMatchesGlob(std::string_view pattern, std::string_view relPath);  // with the segment/prefix rules
};

}  // namespace tf
