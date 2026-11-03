// tenfold-fuzz: a small in-process mutation fuzzer for the harnesses of tests/fuzz.
//
//   tenfold-fuzz --list
//   tenfold-fuzz <target> [--seconds N] [--runs N] [--seed S] [--corpus DIR] [--out DIR] [--max-len N]
//                         [--timeout-ms N] [--repo DIR] [--no-seeds]
//   tenfold-fuzz --replay <file|dir>...  [--target T]   run inputs once each (regression replay)
//   tenfold-fuzz --minimize FILE <target> [--out FILE]  shrink a crashing / hanging input (delta debugging)
//
// Why not libFuzzer: its runtime is not available everywhere (see TENFOLD_FUZZ_LIBFUZZER for machines that have it).
// What this driver provides instead:
//  * coverage feedback when the code under test is built with -fsanitize-coverage=trace-pc (TENFOLD_FUZZ_COVERAGE):
//    inputs that reach new edges join the corpus; without it every mutation of the seeds is just random;
//  * crash survival: the input is kept in <out>/<target>/current before every run, and a sanitizer death callback /
//    signal handler copies it to crash-<hash> (hang-<hash>, slow-<hash>) so that it outlives the abort;
//  * a per-input time limit (--timeout-ms, default 10000) and a "slow input" report (> 100x the median run time);
//  * a minimiser that re-runs candidates in a forked child and keeps those with the same failure signature.
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "FuzzTarget.h"
#include "fuzz_cov.h"

namespace fs = std::filesystem;
using namespace tf::fuzz;

// ------------------------------------------------------------------------------------------------------------------
// Sanitizer defaults and coverage hook
// ------------------------------------------------------------------------------------------------------------------

extern "C" {
const char* __asan_default_options() {
  return "abort_on_error=1:detect_leaks=1:malloc_context_size=30:max_allocation_size_mb=2048:handle_abort=0";
}
const char* __ubsan_default_options() { return "print_stacktrace=1:halt_on_error=1"; }
}

namespace {
constexpr size_t kCovSize = kFuzzCovSize;
uint8_t* const g_cov = g_fuzzCov;
bool g_haveCoverage = false;
}  // namespace

#if defined(__SANITIZE_ADDRESS__)
#define TF_FUZZ_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define TF_FUZZ_ASAN 1
#endif
#endif
#ifdef TF_FUZZ_ASAN
extern "C" void __sanitizer_set_death_callback(void (*)(void));
extern "C" void __sanitizer_print_stack_trace();
#endif

// ------------------------------------------------------------------------------------------------------------------
// Crash artefacts. Everything the handlers touch is preallocated; they use raw syscalls only.
// ------------------------------------------------------------------------------------------------------------------

namespace {

constexpr size_t kBufCap = 1u << 20;
uint8_t g_cur[kBufCap];
std::atomic<size_t> g_curSize{0};
char g_outDir[1024];
char g_target[128];
std::atomic<int> g_saveKind{0};  // 0 crash, 1 hang
std::atomic<bool> g_saved{false};

uint64_t fnv(const uint8_t* p, size_t n) {
  uint64_t h = 1469598103934665603ull;
  for (size_t i = 0; i < n; ++i) h = (h ^ p[i]) * 1099511628211ull;
  return h;
}

void saveArtefact(const char* kind) {
  const size_t n = g_curSize.load();
  const uint64_t h = fnv(g_cur, n);
  char path[1400];
  char hex[17];
  for (int i = 0; i < 16; ++i) hex[i] = "0123456789abcdef"[(h >> (60 - 4 * i)) & 15];
  hex[16] = 0;
  // <out>/<kind>-<target>-<hash>
  size_t k = 0;
  auto app = [&](const char* s) {
    while (*s && k + 1 < sizeof path) path[k++] = *s++;
  };
  app(g_outDir);
  app("/");
  app(kind);
  app("-");
  app(g_target);
  app("-");
  app(hex);
  path[k] = 0;
  int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd >= 0) {
    size_t off = 0;
    while (off < n) {
      ssize_t w = write(fd, g_cur + off, n - off);
      if (w <= 0) break;
      off += static_cast<size_t>(w);
    }
    close(fd);
  }
  static const char msg1[] = "\n==fuzz== input saved to ";
  if (write(2, msg1, sizeof msg1 - 1) < 0) {}
  if (write(2, path, k) < 0) {}
  if (write(2, "\n", 1) < 0) {}
}

void onDeath() {
  if (g_saved.exchange(true)) return;
  saveArtefact(g_saveKind.load() == 1 ? "hang" : "crash");
}

void onSignal(int sig) {
  onDeath();
  signal(sig, SIG_DFL);
  raise(sig);
}

std::atomic<long long> g_execStartMs{0};  // 0 = not running
std::atomic<int> g_timeoutMs{10000};
pthread_t g_mainThread;

long long nowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

void onHangSignal(int) {
#ifdef TF_FUZZ_ASAN
  __sanitizer_print_stack_trace();
#endif
  g_saveKind = 1;
  onDeath();
  const char m[] = "==fuzz== HANG: input exceeded the time limit\n";
  if (write(2, m, sizeof m - 1) < 0) {}
  _exit(78);
}

void watchdog() {
  for (;;) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    const long long st = g_execStartMs.load();
    if (st && nowMs() - st > g_timeoutMs.load()) {
      pthread_kill(g_mainThread, SIGUSR1);
      std::this_thread::sleep_for(std::chrono::seconds(5));
      g_saveKind = 1;
      onDeath();
      _exit(78);
    }
  }
}

void installHandlers() {
  g_mainThread = pthread_self();
#ifdef TF_FUZZ_ASAN
  __sanitizer_set_death_callback(onDeath);
#endif
  for (int s : {SIGABRT, SIGSEGV, SIGBUS, SIGFPE, SIGILL}) signal(s, onSignal);
  signal(SIGUSR1, onHangSignal);
}

// ------------------------------------------------------------------------------------------------------------------
// Random numbers, mutation
// ------------------------------------------------------------------------------------------------------------------

struct Rng {
  uint64_t s;
  explicit Rng(uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 0x1234567ull) {
    for (int i = 0; i < 4; ++i) next();
  }
  uint64_t next() {
    s ^= s >> 12;
    s ^= s << 25;
    s ^= s >> 27;
    return s * 2685821657736338717ull;
  }
  size_t below(size_t n) { return n ? static_cast<size_t>(next() % n) : 0; }
  bool chance(unsigned percent) { return below(100) < percent; }
};

const char* const kInteresting[] = {"0",
                                    "1",
                                    "-1",
                                    "255",
                                    "256",
                                    "32767",
                                    "65535",
                                    "65536",
                                    "2147483647",
                                    "2147483648",
                                    "-2147483648",
                                    "4294967295",
                                    "4294967296",
                                    "9223372036854775807",
                                    "18446744073709551615",
                                    "18446744073709551616",
                                    "99999999999999999999999",
                                    "1e999",
                                    "-0",
                                    "0.5e-400"};

struct Mutator {
  Rng& rng;
  const std::vector<std::string>& dict;
  const std::vector<std::string>& corpus;
  size_t softMax;

  std::string operator()(std::string s) {
    const size_t rounds = 1 + (rng.chance(50) ? rng.below(3) : 0) + (rng.chance(10) ? rng.below(10) : 0);
    for (size_t r = 0; r < rounds; ++r) one(s);
    const size_t hard = softMax * 4;
    if (s.size() > hard) s.resize(hard);
    return s;
  }

  void one(std::string& s) {
    switch (rng.below(16)) {
      case 0:  // byte replace
        if (!s.empty()) s[rng.below(s.size())] = static_cast<char>(rng.next());
        break;
      case 1:  // bit flip
        if (!s.empty()) s[rng.below(s.size())] ^= static_cast<char>(1u << rng.below(8));
        break;
      case 2: {  // insert random bytes
        std::string ins;
        for (size_t i = 0, n = 1 + rng.below(4); i < n; ++i) ins += static_cast<char>(rng.next());
        s.insert(rng.below(s.size() + 1), ins);
        break;
      }
      case 3: {  // delete a range
        if (s.empty()) break;
        const size_t at = rng.below(s.size());
        s.erase(at, 1 + rng.below(std::min<size_t>(rng.chance(20) ? 256 : 16, s.size() - at)));
        break;
      }
      case 4:
      case 5: {  // insert a dictionary token (twice as likely)
        if (dict.empty()) break;
        std::string tok = dict[rng.below(dict.size())];
        if (rng.chance(30)) tok += ' ';
        s.insert(rng.below(s.size() + 1), tok);
        break;
      }
      case 6: {  // replace a range with a dictionary token
        if (dict.empty() || s.empty()) break;
        const size_t at = rng.below(s.size());
        s.replace(at, 1 + rng.below(std::min<size_t>(8, s.size() - at)), dict[rng.below(dict.size())]);
        break;
      }
      case 7: {  // duplicate a chunk
        if (s.empty()) break;
        const size_t at = rng.below(s.size());
        const std::string chunk = s.substr(at, 1 + rng.below(std::min<size_t>(64, s.size() - at)));
        s.insert(rng.below(s.size() + 1), chunk);
        break;
      }
      case 8: {  // splice with another corpus entry
        if (corpus.empty()) break;
        const std::string& o = corpus[rng.below(corpus.size())];
        if (o.empty()) break;
        const size_t a = rng.below(o.size());
        const std::string piece = o.substr(a, 1 + rng.below(std::min<size_t>(256, o.size() - a)));
        if (rng.chance(40)) s.resize(rng.below(s.size() + 1));  // cut the tail, then append
        s.insert(rng.below(s.size() + 1), piece);
        break;
      }
      case 9: {  // repeat a short chunk many times (deep nesting, long runs)
        std::string chunk;
        if (!dict.empty() && rng.chance(60)) chunk = dict[rng.below(dict.size())];
        else if (!s.empty()) chunk = s.substr(rng.below(s.size()), 1 + rng.below(3));
        else chunk = "(";
        if (chunk.empty()) break;
        const size_t times = rng.chance(10) ? 1000 + rng.below(20000) : 2 + rng.below(300);
        std::string rep;
        for (size_t i = 0; i < times && rep.size() < softMax * 2; ++i) rep += chunk;
        s.insert(rng.below(s.size() + 1), rep);
        break;
      }
      case 10: {  // interesting number
        const char* n = kInteresting[rng.below(sizeof kInteresting / sizeof *kInteresting)];
        if (!s.empty() && rng.chance(50)) {
          // overwrite a digit run if there is one
          size_t at = rng.below(s.size());
          for (size_t i = 0; i < s.size(); ++i) {
            const size_t p = (at + i) % s.size();
            if (s[p] >= '0' && s[p] <= '9') {
              size_t e = p;
              while (e < s.size() && s[e] >= '0' && s[e] <= '9') ++e;
              s.replace(p, e - p, n);
              return;
            }
          }
        }
        s.insert(rng.below(s.size() + 1), n);
        break;
      }
      case 11: {  // swap two chunks
        if (s.size() < 4) break;
        const size_t a = rng.below(s.size() - 1);
        const size_t la = 1 + rng.below(std::min<size_t>(16, s.size() - a - 1));
        const size_t b = rng.below(s.size() - 1);
        const size_t lb = 1 + rng.below(std::min<size_t>(16, s.size() - b - 1));
        if (a + la <= b) {
          const std::string x = s.substr(a, la), y = s.substr(b, lb);
          s.replace(b, lb, x);
          s.replace(a, la, y);
        }
        break;
      }
      case 12:  // byte arithmetic
        if (!s.empty()) s[rng.below(s.size())] += static_cast<char>(static_cast<int>(rng.below(71)) - 35);
        break;
      case 13: {  // line operations
        if (s.empty()) break;
        std::vector<std::pair<size_t, size_t>> lines;  // [begin, end) incl. the terminator
        for (size_t b = 0; b < s.size();) {
          size_t e = s.find('\n', b);
          e = e == std::string::npos ? s.size() : e + 1;
          lines.emplace_back(b, e);
          b = e;
        }
        const auto [b, e] = lines[rng.below(lines.size())];
        const size_t op = rng.below(3);
        if (op == 0) s.erase(b, e - b);
        else if (op == 1) s.insert(b, s.substr(b, e - b));
        else {
          const auto [b2, e2] = lines[rng.below(lines.size())];
          if (b2 != b) { s.insert(b2, s.substr(b, e - b)); }
        }
        break;
      }
      case 14:  // truncate
        if (!s.empty()) s.resize(rng.below(s.size()));
        break;
      default: {  // letter case / quote flip
        if (s.empty()) break;
        const size_t at = rng.below(s.size());
        char& c = s[at];
        if (c == '"') c = '\'';
        else if (c == '\'') c = '"';
        else if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 32);
        else if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
        else c = static_cast<char>(rng.next());
        break;
      }
    }
  }
};

// ------------------------------------------------------------------------------------------------------------------
// Seeds
// ------------------------------------------------------------------------------------------------------------------

bool readFile(const fs::path& p, std::string& out) {
  std::ifstream in(p, std::ios::binary);
  if (!in) return false;
  std::stringstream ss;
  ss << in.rdbuf();
  out = ss.str();
  return true;
}

// Cuts `text` into pieces of at most `maxLen` bytes at line boundaries (a single over-long line is cut anywhere).
void addPieces(const std::string& text, size_t maxLen, std::vector<std::string>& out, size_t maxPieces = 12) {
  if (text.size() <= maxLen) {
    out.push_back(text);
    return;
  }
  std::vector<std::string> pieces;
  size_t pos = 0;
  while (pos < text.size()) {
    size_t end = std::min(text.size(), pos + maxLen);
    if (end < text.size()) {
      const size_t nl = text.rfind('\n', end);
      if (nl != std::string::npos && nl > pos) end = nl + 1;
    }
    pieces.push_back(text.substr(pos, end - pos));
    pos = end;
  }
  const size_t step = std::max<size_t>(1, pieces.size() / maxPieces);
  for (size_t i = 0; i < pieces.size(); i += step) out.push_back(std::move(pieces[i]));
}

std::vector<std::string> loadSeeds(const Target& t, const fs::path& repo, size_t maxLen) {
  std::vector<std::string> out;
  for (const std::string& spec : t.seedFiles) {
    const size_t colon = spec.find(':');
    std::error_code ec;
    if (colon == std::string::npos) {
      std::string text;
      if (readFile(repo / spec, text)) addPieces(text, maxLen, out);
      continue;
    }
    std::vector<std::string> exts;
    for (size_t b = colon + 1; b <= spec.size();) {
      size_t e = spec.find(',', b);
      if (e == std::string::npos) e = spec.size();
      if (e > b) exts.push_back(spec.substr(b, e - b));
      b = e + 1;
    }
    std::vector<fs::path> files;
    for (auto it = fs::recursive_directory_iterator(repo / spec.substr(0, colon), ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
      if (!it->is_regular_file(ec)) continue;
      const std::string n = it->path().filename().string();
      bool ok = exts.empty();
      for (const std::string& x : exts)
        if (n.size() >= x.size() && n.compare(n.size() - x.size(), x.size(), x) == 0) ok = true;
      if (ok && fs::file_size(it->path(), ec) < (4u << 20)) files.push_back(it->path());
    }
    std::sort(files.begin(), files.end());
    for (const fs::path& f : files) {
      std::string text;
      if (readFile(f, text)) addPieces(text, maxLen, out, 4);
    }
  }
  for (const std::string& s : t.seedText) out.push_back(s);
  return out;
}

// ------------------------------------------------------------------------------------------------------------------
// Running one input
// ------------------------------------------------------------------------------------------------------------------

int g_curFd = -1;

void publishCurrent(const std::string& in) {
  const size_t n = std::min(in.size(), kBufCap);
  std::memcpy(g_cur, in.data(), n);
  g_curSize = n;
  if (g_curFd >= 0) {
    if (ftruncate(g_curFd, static_cast<off_t>(n)) != 0) {}
    if (pwrite(g_curFd, in.data(), n, 0) < 0) {}
  }
}

double runOne(const Target& t, const std::string& in) {
  publishCurrent(in);
  std::memset(g_cov, 0, kCovSize);
  const auto t0 = std::chrono::steady_clock::now();
  g_execStartMs = nowMs();
  t.run(reinterpret_cast<const uint8_t*>(in.data()), in.size());
  g_execStartMs = 0;
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

size_t countEdges(const std::vector<uint8_t>& virgin) {
  size_t n = 0;
  for (uint8_t b : virgin) n += b;
  return n;
}

bool mergeCoverage(std::vector<uint8_t>& virgin) {
  bool fresh = false;
  for (size_t i = 0; i < kCovSize; ++i)
    if (g_cov[i] && !virgin[i]) {
      virgin[i] = 1;
      fresh = true;
    }
  return fresh;
}

void writeBytes(const fs::path& p, const std::string& s) {
  std::ofstream o(p, std::ios::binary | std::ios::trunc);
  o.write(s.data(), static_cast<std::streamsize>(s.size()));
}

// ------------------------------------------------------------------------------------------------------------------
// Commands
// ------------------------------------------------------------------------------------------------------------------

struct Options {
  std::string target;
  double seconds = 10;
  long long runs = -1;
  uint64_t seed = 1;
  fs::path corpusDir, outDir = "fuzz-out", repo;
  size_t maxLen = 0;
  int timeoutMs = 10000;
  double slowMs = 10;  // an input is only reported as slow above this absolute time
  bool noSeeds = false;
  std::vector<std::string> replay;
  std::string minimizeFile, minimizeOut;
  bool replayMode = false, list = false;
};

int fuzz(const Options& o, const Target& t) {
  const size_t maxLen = o.maxLen ? o.maxLen : t.maxLen;
  const fs::path dir = o.outDir / t.name;
  std::error_code ec;
  fs::create_directories(dir, ec);
  std::snprintf(g_outDir, sizeof g_outDir, "%s", dir.string().c_str());
  std::snprintf(g_target, sizeof g_target, "%s", t.name.c_str());
  g_curFd = open((dir / "current").c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  g_timeoutMs = o.timeoutMs;
  installHandlers();
  std::thread(watchdog).detach();

  std::vector<std::string> corpus;
  if (!o.noSeeds) corpus = loadSeeds(t, o.repo, maxLen);
  size_t fromRepo = corpus.size();
  if (!o.corpusDir.empty()) {
    std::vector<fs::path> files;
    for (auto& e : fs::directory_iterator(o.corpusDir, ec))
      if (e.is_regular_file(ec)) files.push_back(e.path());
    std::sort(files.begin(), files.end());
    for (auto& f : files) {
      std::string text;
      if (readFile(f, text)) corpus.push_back(std::move(text));
    }
    fs::create_directories(o.corpusDir, ec);
  }
  if (corpus.empty()) corpus.push_back("");

  // Run the seeds: they must pass, and with coverage only those that add edges stay.
  std::vector<uint8_t> virgin(kCovSize, 0);
  std::vector<std::string> kept;
  std::vector<double> times;
  std::vector<size_t> seedSizes;
  uint64_t execs = 0, slow = 0;
  const auto start = std::chrono::steady_clock::now();
  for (const std::string& s : corpus) {
    times.push_back(runOne(t, s));
    seedSizes.push_back(s.size());
    ++execs;
    if (mergeCoverage(virgin) || kept.empty()) kept.push_back(s);
  }
  g_haveCoverage = countEdges(virgin) > 0;
  if (!g_haveCoverage) kept = corpus;  // blind mode: keep every seed
  corpus = kept;
  std::fprintf(stderr,
               "[%s] %zu seeds (%zu from the repository and the harness, %zu from --corpus), kept %zu, coverage %s "
               "(%zu edges)\n",
               t.name.c_str(), static_cast<size_t>(execs), fromRepo, static_cast<size_t>(execs) - fromRepo, kept.size(),
               g_haveCoverage ? "on" : "off", countEdges(virgin));

  // Slow-input detection: the time per KiB of an input is compared with the median of inputs of a similar size, so a
  // long input is not "slow" just for being long, and the fixed cost of a run does not make short inputs look cheap.
  // A parser that is quadratic shows up as a median rate that grows from one size class to the next (printed at the
  // end of a run); one that blows up on a particular shape shows up as an outlier within its class.
  constexpr int kClasses = 5;  // < 256 B, < 1 KiB, < 4 KiB, < 16 KiB, larger
  auto sizeClass = [](size_t n) { return n < 256 ? 0 : n < 1024 ? 1 : n < 4096 ? 2 : n < 16384 ? 3 : 4; };
  std::vector<double> rates[kClasses];
  double classMedian[kClasses] = {};
  auto rateOf = [](double ms, size_t n) { return ms / std::max(0.0625, static_cast<double>(n) / 1024.0); };
  auto updateMedians = [&] {
    for (int c = 0; c < kClasses; ++c) {
      if (rates[c].size() < 30) continue;
      std::vector<double> v = rates[c];
      std::nth_element(v.begin(), v.begin() + static_cast<long>(v.size() / 2), v.end());
      classMedian[c] = v[v.size() / 2];
    }
  };
  for (size_t i = 0; i < times.size(); ++i) rates[sizeClass(seedSizes[i])].push_back(rateOf(times[i], seedSizes[i]));
  updateMedians();
  Rng rng(o.seed);
  std::vector<std::string> dict = t.dict;
  Mutator mut{rng, dict, corpus, maxLen};
  auto lastReport = start;
  uint64_t corpusAdds = 0;
  int slowSaved = 0;
  double worstMs = 0;
  std::string worstInput;
  const size_t corpusCap = 3000;
  while (true) {
    const auto now = std::chrono::steady_clock::now();
    const double el = std::chrono::duration<double>(now - start).count();
    if (o.runs >= 0 ? static_cast<long long>(execs) >= o.runs : el >= o.seconds) break;
    if (std::chrono::duration<double>(now - lastReport).count() >= 10) {
      lastReport = now;
      std::fprintf(stderr, "[%s] %.0fs execs=%llu (%.0f/s) corpus=%zu edges=%zu slow=%llu\n", t.name.c_str(), el,
                   static_cast<unsigned long long>(execs), static_cast<double>(execs) / std::max(el, 1e-9),
                   corpus.size(), countEdges(virgin), static_cast<unsigned long long>(slow));
    }
    // Prefer recently added entries a little: they carry fresh coverage.
    const size_t idx =
        rng.chance(30) && corpus.size() > 8 ? corpus.size() - 1 - rng.below(8) : rng.below(corpus.size());
    std::string in = mut(corpus[idx]);
    const double ms = runOne(t, in);
    ++execs;
    if (ms > worstMs) {
      worstMs = ms;
      worstInput = in;
    }
    const int cls = sizeClass(in.size());
    const double rate = rateOf(ms, in.size());
    if (times.size() < 4096) times.push_back(ms);
    else times[rng.below(times.size())] = ms;
    if (rates[cls].size() < 2048) rates[cls].push_back(rate);
    else rates[cls][rng.below(rates[cls].size())] = rate;
    if ((execs & 1023) == 0) updateMedians();
    // Confirm a slow input: a scheduling hiccup on a shared machine does not repeat, a pathological input does.
    const double limit = 100 * std::max(classMedian[cls], 0.005);
    bool confirmedSlow = false;
    if (ms > o.slowMs && classMedian[cls] > 0 && rate > limit) {
      double best = ms;
      for (int k = 0; k < 2 && best > o.slowMs; ++k) best = std::min(best, runOne(t, in));
      confirmedSlow = best > o.slowMs && rateOf(best, in.size()) > limit;
    }
    if (confirmedSlow) {
      ++slow;
      std::fprintf(stderr,
                   "[%s] SLOW input: %.1f ms for %zu bytes (median of its size class %.3f ms/KiB, this %.3f ms/KiB)\n",
                   t.name.c_str(), ms, in.size(), classMedian[cls], rate);
      if (slowSaved < 20) {
        char name[64];
        std::snprintf(name, sizeof name, "slow-%016llx",
                      static_cast<unsigned long long>(fnv(reinterpret_cast<const uint8_t*>(in.data()), in.size())));
        writeBytes(dir / name, in);
        ++slowSaved;
      }
    }
    if (g_haveCoverage ? mergeCoverage(virgin) : false) {
      ++corpusAdds;
      if (corpus.size() < corpusCap) corpus.push_back(in);
      else corpus[rng.below(corpus.size())] = in;
      if (!o.corpusDir.empty()) {
        char name[48];
        std::snprintf(name, sizeof name, "%016llx",
                      static_cast<unsigned long long>(fnv(reinterpret_cast<const uint8_t*>(in.data()), in.size())));
        writeBytes(o.corpusDir / name, in);
      }
    }
  }
  const double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  std::sort(times.begin(), times.end());
  std::printf(
      "RESULT target=%s seconds=%.0f execs=%llu execs_per_s=%.0f corpus=%zu new_cov_inputs=%llu edges=%zu "
      "slow=%llu median_ms=%.3f p99_ms=%.3f crashes=0 hangs=0\n",
      t.name.c_str(), el, static_cast<unsigned long long>(execs), static_cast<double>(execs) / std::max(el, 1e-9),
      corpus.size(), static_cast<unsigned long long>(corpusAdds), countEdges(virgin),
      static_cast<unsigned long long>(slow), times[times.size() / 2], times[times.size() * 99 / 100]);
  updateMedians();
  std::printf("median ms per KiB by input size (a rate that grows with the size means super-linear time):");
  static const char* const kClassName[kClasses] = {"<256B", "<1KiB", "<4KiB", "<16KiB", ">=16KiB"};
  for (int c = 0; c < kClasses; ++c)
    if (classMedian[c] > 0) std::printf(" %s=%.3f", kClassName[c], classMedian[c]);
  std::printf("\n");
  writeBytes(dir / "slowest", worstInput);
  std::printf("slowest input: %.1f ms, %zu bytes (saved to %s)\n", worstMs, worstInput.size(),
              (dir / "slowest").c_str());
  fs::remove(dir / "current", ec);
  return 0;
}

int replay(const Options& o) {
  std::vector<fs::path> files;
  for (const std::string& a : o.replay) {
    std::error_code ec;
    if (fs::is_directory(a, ec)) {
      for (auto& e : fs::recursive_directory_iterator(a, ec))
        if (e.is_regular_file(ec) && e.path().extension() != ".md") files.push_back(e.path());
    } else {
      files.push_back(a);
    }
  }
  std::sort(files.begin(), files.end());
  g_timeoutMs = o.timeoutMs;
  std::thread(watchdog).detach();
  installHandlers();
  std::snprintf(g_outDir, sizeof g_outDir, "%s", o.outDir.string().c_str());
  std::error_code ec;
  fs::create_directories(o.outDir, ec);
  int n = 0;
  for (const fs::path& f : files) {
    // regressions/<target>/<file>: the directory names the target unless --target is given
    std::string name = o.target.empty() ? f.parent_path().filename().string() : o.target;
    const Target* t = findTarget(name);
    if (!t) {
      std::fprintf(stderr, "skipping %s: no target '%s'\n", f.string().c_str(), name.c_str());
      continue;
    }
    std::snprintf(g_target, sizeof g_target, "%s", name.c_str());
    std::string text;
    if (!readFile(f, text)) continue;
    // --runs N repeats each input and prints the mean time: a quick way to measure one input (scaling checks).
    const int reps = o.runs > 0 ? static_cast<int>(o.runs) : 1;
    double total = 0;
    for (int r = 0; r < reps; ++r) total += runOne(*t, text);
    if (o.runs > 0)
      std::printf("%s: %zu bytes, %.3f ms per run (%d runs)\n", f.string().c_str(), text.size(), total / reps, reps);
    ++n;
  }
  std::printf("replayed %d inputs\n", n);
  return 0;
}

// ---- minimiser -----------------------------------------------------------------------------------------------

// Runs `in` in a forked child; returns a failure signature ("" = the input is fine).
std::string probe(const Target& t, const std::string& in, int timeoutMs) {
  char tmpl[] = "/tmp/tfmin-XXXXXX";
  int fd = mkstemp(tmpl);
  if (fd < 0) return "";
  pid_t pid = fork();
  if (pid == 0) {
    dup2(fd, 2);
    dup2(fd, 1);
    g_curFd = -1;
    g_curSize = 0;
    signal(SIGALRM, [](int) {
      const char m[] = "\n==min== TIMEOUT\n";
      if (write(2, m, sizeof m - 1) < 0) {}
      _exit(79);
    });
    alarm(static_cast<unsigned>((timeoutMs + 999) / 1000));
    try {
      t.run(reinterpret_cast<const uint8_t*>(in.data()), in.size());
    } catch (const std::exception& e) {
      std::fprintf(stderr, "==min== EXCEPTION %s\n", e.what());
      _exit(80);
    } catch (...) {
      std::fprintf(stderr, "==min== EXCEPTION unknown\n");
      _exit(80);
    }
    _exit(0);
  }
  int status = 0;
  waitpid(pid, &status, 0);
  std::string log;
  {
    std::ifstream l(tmpl, std::ios::binary);
    std::stringstream ss;
    ss << l.rdbuf();
    log = ss.str();
  }
  close(fd);
  unlink(tmpl);
  if (WIFEXITED(status) && WEXITSTATUS(status) == 0) return "";
  // Signature: the sanitizer's SUMMARY line (error kind, file:line, function), or the runtime-error text, or the
  // exception text; the stack addresses and the variable parts do not take part.
  for (const char* key :
       {"SUMMARY: ", "runtime error: ", "==min== EXCEPTION", "==min== TIMEOUT", "invariant violated"}) {
    const size_t p = log.find(key);
    if (p != std::string::npos) {
      size_t e = log.find('\n', p);
      std::string line = log.substr(p, e == std::string::npos ? std::string::npos : e - p);
      std::string sig;
      for (char c : line)
        if (!(c >= '0' && c <= '9') || line.compare(0, 8, "SUMMARY:") == 0)
          sig += c;  // drop numbers (lengths, addresses)
      return sig;
    }
  }
  return "exit-" + std::to_string(WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status));
}

int minimize(const Options& o, const Target& t) {
  std::string cur;
  if (!readFile(o.minimizeFile, cur)) {
    std::fprintf(stderr, "cannot read %s\n", o.minimizeFile.c_str());
    return 2;
  }
  const std::string sig = probe(t, cur, o.timeoutMs);
  if (sig.empty()) {
    std::fprintf(stderr, "the input does not fail\n");
    return 1;
  }
  std::fprintf(stderr, "signature: %s\nstart size: %zu\n", sig.c_str(), cur.size());
  const auto begin = std::chrono::steady_clock::now();
  auto expired = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count() > 240; };
  auto still = [&](const std::string& c) { return !expired() && probe(t, c, o.timeoutMs) == sig; };
  // ddmin over chunks, then single bytes, repeated to a fixpoint.
  bool progress = true;
  while (progress) {
    progress = false;
    for (size_t chunk = std::max<size_t>(1, cur.size() / 2); chunk >= 1; chunk /= 2) {
      for (size_t at = 0; at < cur.size();) {
        std::string cand = cur;
        cand.erase(at, chunk);
        if (still(cand)) {
          cur = std::move(cand);
          progress = true;
        } else {
          at += chunk;
        }
      }
      if (chunk == 1) break;
    }
    // simplify bytes: digits -> '0', letters -> 'a', other -> ' ' (only for small inputs: every try is a process fork)
    for (size_t i = 0; cur.size() <= 300 && i < cur.size(); ++i) {
      for (char r : {'0', 'a', ' '}) {
        if (cur[i] == r) continue;
        std::string cand = cur;
        cand[i] = r;
        if (still(cand)) {
          cur = std::move(cand);
          progress = true;
          break;
        }
      }
    }
  }
  const fs::path out = o.minimizeOut.empty() ? fs::path(o.minimizeFile + ".min") : fs::path(o.minimizeOut);
  writeBytes(out, cur);
  std::fprintf(stderr, "minimised to %zu bytes: %s\n", cur.size(), out.string().c_str());
  return 0;
}

void usage() {
  std::fprintf(stderr,
               "usage: tenfold-fuzz --list\n"
               "       tenfold-fuzz <target> [--seconds N] [--runs N] [--seed S] [--corpus DIR] [--out DIR]\n"
               "                    [--max-len N] [--timeout-ms N] [--slow-ms N] [--repo DIR] [--no-seeds]\n"
               "       tenfold-fuzz --replay <file|dir>... [--target T]\n"
               "       tenfold-fuzz --minimize FILE <target> [--out FILE] [--timeout-ms N]\n");
}

}  // namespace

int main(int argc, char** argv) {
  Options o;
#ifdef TENFOLD_SOURCE_DIR
  o.repo = TENFOLD_SOURCE_DIR;
#else
  o.repo = ".";
#endif
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    auto val = [&]() -> std::string {
      if (i + 1 >= argc) {
        usage();
        std::exit(2);
      }
      return argv[++i];
    };
    if (a == "--list") o.list = true;
    else if (a == "--seconds") o.seconds = std::atof(val().c_str());
    else if (a == "--runs") o.runs = std::atoll(val().c_str());
    else if (a == "--seed") o.seed = std::strtoull(val().c_str(), nullptr, 10);
    else if (a == "--corpus") o.corpusDir = val();
    else if (a == "--out") {
      std::string v = val();
      o.outDir = v;
      o.minimizeOut = v;  // for --minimize, --out names the output file
    } else if (a == "--max-len") o.maxLen = std::strtoull(val().c_str(), nullptr, 10);
    else if (a == "--timeout-ms") o.timeoutMs = std::atoi(val().c_str());
    else if (a == "--slow-ms") o.slowMs = std::atof(val().c_str());
    else if (a == "--repo") o.repo = val();
    else if (a == "--no-seeds") o.noSeeds = true;
    else if (a == "--target") o.target = val();
    else if (a == "--replay") o.replayMode = true;
    else if (a == "--minimize") o.minimizeFile = val();
    else if (!a.empty() && a[0] == '-') {
      usage();
      return 2;
    } else if (o.replayMode && o.minimizeFile.empty()) o.replay.push_back(a);
    else o.target = a;
  }
  if (o.list) {
    for (const Target& t : registry()) std::printf("%s\n", t.name.c_str());
    return 0;
  }
  if (o.replayMode) return replay(o);
  const Target* t = findTarget(o.target);
  if (!t) {
    usage();
    std::fprintf(stderr, "unknown target '%s'; --list shows them\n", o.target.c_str());
    return 2;
  }
  if (!o.minimizeFile.empty()) { return minimize(o, *t); }
  return fuzz(o, *t);
}
