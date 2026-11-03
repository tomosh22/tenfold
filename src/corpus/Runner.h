#pragma once
// Corpus runner (W9a): opens a corpus project in the real editor model (Editor + SemanticEngine + clangd), evaluates
// its tenfold-expect.txt on the fast tier and on the final (clangd) answer, and measures. The format and the rules are
// specified in docs/TESTING.md; the pure parts (parser, comparison) are in corpus/Corpus.h.
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "corpus/Corpus.h"

namespace tf::corpus {

struct RunOptions {
  std::filesystem::path dir;  // the corpus project (contains tenfold-expect.txt)
  bool useClangd = true;      // false: fast tier only; expectations that need clangd are SKIPped
  std::string clangdPath;     // empty: the engine's default lookup (bundled, then PATH)
  int indexWaitSec = 120;     // how long to wait for clangd's background index (once, and after config)
  int timeoutSec = 180;       // time limit for the whole project (indexing waits and queries)
  bool verbose = false;       // engine log lines to stderr
  bool keepScratch = false;   // keep the temporary copy (printed in the report)
  std::function<void(const std::string&)> out;  // one report line at a time; default: stdout
};

// The outcome of one expectation line (or a directive that failed).
struct ItemResult {
  int line = 0;
  std::string text;
  Status status = Status::Pass;
  Kind kind = Kind::Def;
  bool isCheck = true;
  bool fast = false;
  std::string expected, actual, note;
  double ms = -1;  // latency of the answer the verdict is based on
};

struct Disagreement {
  int line = 0;
  std::string query, fast, final;
};

struct Report {
  std::string name;
  std::string dir;
  std::string scratch;
  std::string fatal;  // parse / setup problem; nothing was evaluated
  std::vector<ItemResult> results;
  int pass = 0, fail = 0, xfail = 0, xpass = 0, skip = 0;
  int partial = 0;            // failed on an incomplete clangd index (see indexComplete); not in `fail`
  bool indexComplete = true;  // clangd's background index finished within --index-wait (always true without clangd)
  int indexPercent = -1;      // its last reported progress when it did not

  // Fast tier vs final answer at every def / decl query (same file and line = agree).
  size_t agreeTotal = 0, agreeSame = 0;
  std::vector<Disagreement> disagreements;
  // Fast checks run while the target file's TU was not parsed by clangd yet / already was. The fast tier is
  // isolated by holding clangd's answers back (SemanticEngine::Options::fastOnlyQueries), so both are valid.
  int fastBeforeTu = 0, fastAfterTu = 0;

  // Latency samples in milliseconds per "<kind>.fast" / "<kind>.final".
  std::map<std::string, std::vector<double>> samples;

  bool clangd = false;
  std::string clangdVersion, compdbSource;
  double fastIndexMs = -1;    // the fast index's own wall time
  double startMs = -1;        // Editor::start (scan, compile database, clangd launch)
  double firstTuMs = -1;      // first document opened -> clangd finished parsing it
  double clangdIndexMs = -1;  // first wait for the clangd background index: how long until it was idle
  double totalMs = 0;
  double peakRssMiB = -1;  // this process; clangd is a child process and not included

  int exitCode() const { return !fatal.empty() ? 2 : (fail > 0 || xpass > 0) ? 1 : 0; }
  std::string toJson() const;
  std::string summary() const;  // one human line
};

Report runCorpus(const RunOptions& opt);

// Corpus projects below `root`: its immediate sub-directories that contain tenfold-expect.txt, sorted.
std::vector<std::filesystem::path> listCorpora(const std::filesystem::path& root);

}  // namespace tf::corpus
