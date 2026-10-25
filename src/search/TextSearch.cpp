#include "search/TextSearch.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

#ifdef TENFOLD_HAVE_PCRE2
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#endif

namespace tf {

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Handle
// ---------------------------------------------------------------------------

struct SearchHandle::State {
  std::atomic<bool> cancel{false};
  std::mutex mu;
  std::condition_variable cv;
  bool done = false;
};

SearchHandle SearchHandle::create() { return SearchHandle(std::make_shared<State>()); }
void SearchHandle::cancel() {
  if (s_) s_->cancel = true;
}
bool SearchHandle::cancelled() const { return s_ && s_->cancel.load(); }
bool SearchHandle::finished() const {
  if (!s_) return true;
  std::lock_guard<std::mutex> lk(s_->mu);
  return s_->done;
}
void SearchHandle::wait() const {
  if (!s_) return;
  std::unique_lock<std::mutex> lk(s_->mu);
  s_->cv.wait(lk, [&] { return s_->done; });
}

// ---------------------------------------------------------------------------
// Globs
// ---------------------------------------------------------------------------

namespace {

bool globRec(const char* p, const char* pe, const char* s, const char* se) {
  while (p < pe) {
    if (*p == '*') {
      if (p + 1 < pe && p[1] == '*') {
        while (p < pe && *p == '*') ++p;
        if (p < pe && *p == '/' && globRec(p + 1, pe, s, se)) return true;  // "**/" may match zero directories
        for (const char* t = s;; ++t) {
          if (globRec(p, pe, t, se)) return true;
          if (t == se) break;
        }
        return false;
      }
      ++p;
      for (const char* t = s;; ++t) {
        if (globRec(p, pe, t, se)) return true;
        if (t == se || *t == '/') break;
      }
      return false;
    }
    if (s == se) return false;
    if (*p == '?') {
      if (*s == '/') return false;
    } else if (*p != *s) {
      return false;
    }
    ++p;
    ++s;
  }
  return s == se;
}

}  // namespace

bool TextSearch::globMatch(std::string_view pattern, std::string_view path) {
  return globRec(pattern.data(), pattern.data() + pattern.size(), path.data(), path.data() + path.size());
}

bool TextSearch::pathMatchesGlob(std::string_view pat, std::string_view rel) {
  while (pat.size() >= 2 && pat[0] == '.' && pat[1] == '/') pat.remove_prefix(2);
  while (!pat.empty() && pat.back() == '/') pat.remove_suffix(1);
  bool anchored = pat.find('/') != std::string_view::npos;
  while (!pat.empty() && pat.front() == '/') pat.remove_prefix(1);
  if (pat.empty()) return false;
  if (!anchored) {
    size_t i = 0;
    while (i <= rel.size()) {
      size_t j = rel.find('/', i);
      if (j == std::string_view::npos) j = rel.size();
      if (globMatch(pat, rel.substr(i, j - i))) return true;
      i = j + 1;
    }
    return false;
  }
  if (globMatch(pat, rel)) return true;
  for (size_t i = rel.find('/'); i != std::string_view::npos; i = rel.find('/', i + 1))
    if (globMatch(pat, rel.substr(0, i))) return true;  // pattern names a directory above the file
  return false;
}

// ---------------------------------------------------------------------------
// Matching engine
// ---------------------------------------------------------------------------

namespace {

constexpr size_t kWindow = 1u << 20;  // cancellation granularity inside one file
constexpr size_t kBinaryProbe = 8000;
constexpr int kMaxLineText = 2000;

inline bool isWordByte(unsigned char c) {
  return c >= 0x80 || c == '_' || (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

struct FoldTable {
  unsigned char t[256];
  FoldTable() {
    for (int i = 0; i < 256; ++i) t[i] = static_cast<unsigned char>(i >= 'A' && i <= 'Z' ? i + 32 : i);
  }
};
const FoldTable kFold;

#ifdef TENFOLD_HAVE_PCRE2
struct Regex {
  pcre2_code* code = nullptr;
  ~Regex() {
    if (code) pcre2_code_free(code);
  }
};
#endif

struct Run {
  const SearchInput& in;
  const SearchOptions& opt;
  const SearchBatchFn& onBatch;
  std::atomic<bool>& cancel;
  std::vector<size_t> order;  // indices into in.files that pass the globs

  // literal needle (already folded when !caseSensitive)
  std::string needle;
#ifdef TENFOLD_HAVE_PCRE2
  Regex re;
#endif

  std::atomic<size_t> next{0}, total{0}, searched{0}, matchedFiles{0};
  std::atomic<bool> stop{false}, truncated{false}, sawCancel{false};

  std::mutex outMu;
  std::vector<SearchMatch> pending;
  std::chrono::steady_clock::time_point lastFlush = std::chrono::steady_clock::now();

  Run(const SearchInput& i, const SearchOptions& o, const SearchBatchFn& b, std::atomic<bool>& c)
      : in(i), opt(o), onBatch(b), cancel(c) {}

  bool stopped() {
    if (cancel.load(std::memory_order_relaxed)) {
      sawCancel = true;
      return true;
    }
    return stop.load(std::memory_order_relaxed);
  }

  // Deliver full batches under the lock so batches never overlap and nothing
  // is delivered after cancellation was observed.
  void emit(std::vector<SearchMatch>& local, bool final) {
    std::lock_guard<std::mutex> lk(outMu);
    for (auto& m : local) pending.push_back(std::move(m));
    local.clear();
    size_t bs = std::max<size_t>(1, opt.batchSize);
    size_t pos = 0;
    // Partial batches go out after 50 ms so a slow search still streams.
    auto now = std::chrono::steady_clock::now();
    bool flushPartial = final || now - lastFlush > std::chrono::milliseconds(50);
    while (pending.size() - pos >= bs || (flushPartial && pending.size() > pos)) {
      lastFlush = now;
      size_t n = std::min(bs, pending.size() - pos);
      if (cancel.load()) {
        sawCancel = true;
        pending.clear();
        return;
      }
      std::vector<SearchMatch> batch(std::make_move_iterator(pending.begin() + static_cast<long>(pos)),
                                     std::make_move_iterator(pending.begin() + static_cast<long>(pos + n)));
      pos += n;
      if (onBatch) onBatch(std::move(batch));
    }
    pending.erase(pending.begin(), pending.begin() + static_cast<long>(pos));
  }
};

// Per-line bookkeeping while walking a buffer's matches in increasing order.
struct LineTracker {
  const char* end;
  const char* ls;
  const char* le;
  int line = 0;
  LineTracker(const char* d, const char* e) : end(e), ls(d) { le = findNl(d); }
  const char* findNl(const char* from) const {
    const void* r = from < end ? memchr(from, '\n', static_cast<size_t>(end - from)) : nullptr;
    return r ? static_cast<const char*>(r) : end;
  }
  void seek(const char* p) {
    while (le < end && p > le) {
      ls = le + 1;
      ++line;
      le = findNl(ls);
    }
  }
};

struct Worker {
  Run& run;
  const fs::path* file = nullptr;
  std::vector<SearchMatch> local;
  size_t found = 0;  // matches reported by this worker
  std::unique_ptr<char[]> buf;
  size_t bufCap = 0;
#ifdef TENFOLD_HAVE_PCRE2
  pcre2_match_data* md = nullptr;
  pcre2_match_context* mctx = nullptr;
  pcre2_jit_stack* jstack = nullptr;
#endif

  explicit Worker(Run& r) : run(r) {
#ifdef TENFOLD_HAVE_PCRE2
    if (run.re.code) {
      md = pcre2_match_data_create_from_pattern(run.re.code, nullptr);
      mctx = pcre2_match_context_create(nullptr);
      pcre2_set_match_limit(mctx, 20000000);
      jstack = pcre2_jit_stack_create(32 * 1024, 1024 * 1024, nullptr);
      if (jstack) pcre2_jit_stack_assign(mctx, nullptr, jstack);
    }
#endif
  }
  ~Worker() {
#ifdef TENFOLD_HAVE_PCRE2
    if (md) pcre2_match_data_free(md);
    if (mctx) pcre2_match_context_free(mctx);
    if (jstack) pcre2_jit_stack_free(jstack);
#endif
  }

  // Locate the first byte `c` in [p, lim), checking for cancellation every window.
  // Returns lim when there is none (or the search was cancelled).
  const char* findByte(const char* p, const char* lim, int c) {
    while (p < lim) {
      size_t n = std::min(static_cast<size_t>(lim - p), kWindow);
      if (const void* r = memchr(p, c, n)) return static_cast<const char*>(r);
      p += n;
      if (run.stopped()) return lim;
    }
    return lim;
  }

  // Record a match at byte offset `off` of the buffer. False = stop searching this file.
  bool report(const char* d, LineTracker& lt, size_t off, size_t len) {
    if (run.stopped()) return false;
    size_t idx = run.total.fetch_add(1, std::memory_order_relaxed);
    if (idx >= run.opt.maxResults) {
      run.truncated = true;
      run.stop = true;
      return false;
    }
    ++found;
    const char* p = d + off;
    lt.seek(p);
    const char* e = lt.le;
    if (e > lt.ls && e[-1] == '\r') --e;
    SearchMatch m;
    m.file = *file;
    m.line = lt.line;
    m.column = static_cast<int>(p - lt.ls);
    long avail = static_cast<long>(e - p);
    m.length = static_cast<int>(std::max<long>(0, std::min<long>(static_cast<long>(len), avail)));
    const char* ts = lt.ls;
    const char* te = e;
    if (te - ts > kMaxLineText) {  // window very long (minified) lines around the match
      ts = std::max(ts, p - 200);
      te = std::min(te, p + m.length + 1000);
      while (ts < p && (static_cast<unsigned char>(*ts) & 0xC0) == 0x80) ++ts;
      while (te < e && (static_cast<unsigned char>(*te) & 0xC0) == 0x80) ++te;
    }
    m.text.assign(ts, static_cast<size_t>(te - ts));
    m.textOffset = static_cast<int>(ts - lt.ls);
    local.push_back(std::move(m));
    if (local.size() >= std::max<size_t>(1, run.opt.batchSize)) run.emit(local, false);
    return true;
  }

  void literal(const char* d, size_t n) {
    const std::string& nd = run.needle;
    const size_t nl = nd.size();
    if (n < nl) return;
    const bool cs = run.opt.caseSensitive;
    const char* lim = d + (n - nl) + 1;  // exclusive bound for match starts
    const unsigned char c0 = static_cast<unsigned char>(nd[0]);
    // With folding, the first byte has up to two spellings; scan for each with memchr.
    int c1 = c0, c2 = c0;
    if (!cs && c0 >= 'a' && c0 <= 'z') c2 = c0 - 32;
    const bool wwL = run.opt.wholeWord && isWordByte(static_cast<unsigned char>(nd.front()));
    const bool wwR = run.opt.wholeWord && isWordByte(static_cast<unsigned char>(nd.back()));
    LineTracker lt(d, d + n);
    const char* p = d;
    const char* a = findByte(p, lim, c1);
    const char* b = c2 == c1 ? lim : findByte(p, lim, c2);
    while (true) {
      const char* q = std::min(a, b);
      if (q >= lim) return;
      bool ok;
      if (cs) {
        ok = memcmp(q + 1, nd.data() + 1, nl - 1) == 0;
      } else {
        ok = true;
        for (size_t i = 1; i < nl; ++i)
          if (kFold.t[static_cast<unsigned char>(q[i])] != static_cast<unsigned char>(nd[i])) {
            ok = false;
            break;
          }
      }
      if (ok && wwL && q > d && isWordByte(static_cast<unsigned char>(q[-1]))) ok = false;
      if (ok && wwR && q + nl < d + n && isWordByte(static_cast<unsigned char>(q[nl]))) ok = false;
      if (ok && !report(d, lt, static_cast<size_t>(q - d), nl)) return;
      p = q + (ok ? nl : 1);
      if (a < p) a = findByte(p, lim, c1);
      if (c2 != c1 && b < p) b = findByte(p, lim, c2);
      if (run.stopped()) return;
    }
  }

#ifdef TENFOLD_HAVE_PCRE2
  void regex(const char* d, size_t n) {
    LineTracker lt(d, d + n);
    size_t off = 0;
    while (off <= n) {
      int rc = pcre2_match(run.re.code, reinterpret_cast<PCRE2_SPTR>(d), n, off, 0, md, mctx);
      if (rc < 0) return;  // no match, or a match/JIT-stack limit: give up on this file
      PCRE2_SIZE* ov = pcre2_get_ovector_pointer(md);
      size_t s = ov[0], e = ov[1];
      if (e <= s) {  // empty match: ignore, step over one UTF-8 character
        off = std::max(off, s) + 1;
        while (off < n && (static_cast<unsigned char>(d[off]) & 0xC0) == 0x80) ++off;
        continue;
      }
      if (!report(d, lt, s, e - s) || run.stopped()) return;
      off = e;
    }
  }
#endif

  static FILE* openFile(const fs::path& p) {
#ifdef _WIN32
    return _wfopen(p.c_str(), L"rb");
#else
    return fopen(p.c_str(), "rb");
#endif
  }

  // Reads the whole file into the reusable buffer. False if unreadable / too big.
  bool read(const fs::path& p, size_t& n) {
    FILE* f = openFile(p);
    if (!f) return false;
    setvbuf(f, nullptr, _IONBF, 0);
    bool ok = false;
    if (fseek(f, 0, SEEK_END) == 0) {
      long sz = ftell(f);
      if (sz >= 0 && static_cast<size_t>(sz) <= run.opt.maxFileBytes && fseek(f, 0, SEEK_SET) == 0) {
        if (static_cast<size_t>(sz) + 1 > bufCap) {
          bufCap = std::max<size_t>(static_cast<size_t>(sz) + 1, bufCap * 2);
          buf.reset(new char[bufCap]);
        }
        n = fread(buf.get(), 1, static_cast<size_t>(sz), f);
        ok = n == static_cast<size_t>(sz);
      }
    }
    fclose(f);
    return ok;
  }

  void searchBuffer(const char* d, size_t n) {
    if (n == 0 || memchr(d, 0, std::min(n, kBinaryProbe))) return;
    size_t before = found;
#ifdef TENFOLD_HAVE_PCRE2
    if (run.re.code) regex(d, n);
    else
#endif
      literal(d, n);
    if (found != before) run.matchedFiles++;
  }

  void operator()() {
    const bool needRel = !run.in.overrides.empty();
    while (!run.stopped()) {
      size_t i = run.next.fetch_add(1, std::memory_order_relaxed);
      if (i >= run.order.size()) break;
      file = &run.in.files[run.order[i]];
      run.searched++;
      const std::string* over = nullptr;
      if (needRel) {
        auto it = run.in.overrides.find(file->lexically_relative(run.in.root).generic_string());
        if (it != run.in.overrides.end()) over = &it->second;
      }
      if (over) {
        searchBuffer(over->data(), over->size());
      } else {
        size_t n = 0;
        if (read(*file, n)) searchBuffer(buf.get(), n);
      }
      if (!local.empty()) run.emit(local, false);
    }
    run.emit(local, false);
  }
};

bool passesGlobs(const SearchOptions& o, const std::string& rel) {
  if (!o.includeGlobs.empty()) {
    bool any = false;
    for (auto& g : o.includeGlobs)
      if (TextSearch::pathMatchesGlob(g, rel)) {
        any = true;
        break;
      }
    if (!any) return false;
  }
  for (auto& g : o.excludeGlobs)
    if (TextSearch::pathMatchesGlob(g, rel)) return false;
  return true;
}

SearchSummary execute(const SearchInput& in, const SearchOptions& opt, const SearchBatchFn& onBatch,
                      std::atomic<bool>& cancel) {
  auto t0 = std::chrono::steady_clock::now();
  SearchSummary sum;
  auto finish = [&](SearchSummary s) {
    s.elapsedMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return s;
  };
  if (opt.query.empty()) {
    sum.error = "empty query";
    return finish(sum);
  }

  Run run(in, opt, onBatch, cancel);
  if (opt.regex) {
#ifdef TENFOLD_HAVE_PCRE2
    std::string pat = opt.wholeWord ? "\\b(?:" + opt.query + ")\\b" : opt.query;
    uint32_t flags = PCRE2_UTF | PCRE2_MULTILINE | (opt.caseSensitive ? 0u : PCRE2_CASELESS);
#ifdef PCRE2_MATCH_INVALID_UTF
    flags |= PCRE2_MATCH_INVALID_UTF;
#endif
    pcre2_compile_context* cc = pcre2_compile_context_create(nullptr);
    pcre2_set_newline(cc, PCRE2_NEWLINE_ANYCRLF);
    int errc = 0;
    PCRE2_SIZE erroff = 0;
    run.re.code = pcre2_compile(reinterpret_cast<PCRE2_SPTR>(pat.data()), pat.size(), flags, &errc, &erroff, cc);
    pcre2_compile_context_free(cc);
    if (!run.re.code) {
      PCRE2_UCHAR msg[256];
      pcre2_get_error_message(errc, msg, sizeof msg);
      sum.error = std::string("invalid regex: ") + reinterpret_cast<const char*>(msg);
      return finish(sum);
    }
    pcre2_jit_compile(run.re.code, PCRE2_JIT_COMPLETE);  // failure just means interpreted matching
#else
    sum.error = "regex support not built";
    return finish(sum);
#endif
  } else {
    if (opt.query.find('\n') != std::string::npos) {
      sum.error = "multi-line queries need regex";
      return finish(sum);
    }
    run.needle = opt.query;
    if (!opt.caseSensitive)
      for (auto& c : run.needle) c = static_cast<char>(kFold.t[static_cast<unsigned char>(c)]);
  }

  const bool globs = !opt.includeGlobs.empty() || !opt.excludeGlobs.empty();
  run.order.reserve(in.files.size());
  for (size_t i = 0; i < in.files.size(); ++i) {
    if (globs && !passesGlobs(opt, in.files[i].lexically_relative(in.root).generic_string())) continue;
    run.order.push_back(i);
  }

  unsigned nt = opt.threads ? opt.threads : std::max(1u, std::thread::hardware_concurrency());
  nt = static_cast<unsigned>(std::min<size_t>(nt, std::max<size_t>(1, run.order.size())));
  std::vector<std::thread> pool;
  for (unsigned t = 1; t < nt; ++t)
    pool.emplace_back([&run] {
      Worker w(run);
      w();
    });
  {
    Worker w(run);
    w();
  }
  for (auto& t : pool) t.join();
  {
    std::vector<SearchMatch> none;
    run.emit(none, true);
  }

  sum.matches = std::min(run.total.load(), opt.maxResults);
  sum.filesSearched = run.searched;
  sum.filesMatched = run.matchedFiles;
  sum.truncated = run.truncated;
  sum.cancelled = run.sawCancel;
  return finish(sum);
}

}  // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool TextSearch::regexSupported() {
#ifdef TENFOLD_HAVE_PCRE2
  return true;
#else
  return false;
#endif
}

SearchHandle TextSearch::start(SearchInput in, SearchOptions opt, SearchBatchFn onBatch, SearchDoneFn onDone,
                               SearchHandle handle) {
  if (!handle.valid()) handle = SearchHandle::create();
  auto st = handle.s_;
  std::thread([st, in = std::move(in), opt = std::move(opt), onBatch = std::move(onBatch),
               onDone = std::move(onDone)]() mutable {
    {
      SearchSummary s = execute(in, opt, onBatch, st->cancel);
      if (onDone) onDone(s);
      onDone = nullptr;
      onBatch = nullptr;
    }
    std::lock_guard<std::mutex> lk(st->mu);
    st->done = true;
    st->cv.notify_all();
  }).detach();
  return handle;
}

SearchSummary TextSearch::run(SearchInput in, SearchOptions opt, std::vector<SearchMatch>* out) {
  SearchSummary result;
  auto h = start(
      std::move(in), std::move(opt),
      [out](std::vector<SearchMatch>&& b) {
        if (out)
          for (auto& m : b) out->push_back(std::move(m));
      },
      [&result](const SearchSummary& s) { result = s; });
  h.wait();
  return result;
}

}  // namespace tf
