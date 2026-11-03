// tenfold-corpus-tests (W9a): runs every tests/corpus/<name>/ project against the real engine and clangd and fails
// when an expectation fails (or an xfail starts passing). Skipped, with a message, without clangd.
//   TENFOLD_CORPUS_REPORT_DIR=<dir>  write <dir>/<name>.json per project (CI uploads them)
//   TENFOLD_CORPUS_ONLY=<name>       run just that project
//   TENFOLD_CORPUS_TIMEOUT=<s>       per-project time limit (default 300)
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "corpus/Runner.h"
#include "test_main.h"
#include "workspace/Workspace.h"

using namespace tf;
namespace fs = std::filesystem;

TEST(corpus_projects) {
  if (findExecutable("clangd").empty()) {
    std::cerr << "    SKIPPED: clangd not found on PATH (the corpus needs the real language server)\n";
    return;
  }
  const fs::path root = fs::path(TENFOLD_SOURCE_DIR) / "tests" / "corpus";
  const char* only = std::getenv("TENFOLD_CORPUS_ONLY");
  const char* reports = std::getenv("TENFOLD_CORPUS_REPORT_DIR");
  const char* limit = std::getenv("TENFOLD_CORPUS_TIMEOUT");
  auto dirs = corpus::listCorpora(root);
  CHECK(!dirs.empty());
  for (auto& dir : dirs) {
    if (only && dir.filename().string() != only) continue;
#ifdef _WIN32
    // A project that needs symlinks (git creates them only where core.symlinks is on) is marked and skipped here.
    if (fs::exists(dir / "tenfold-corpus-linux-only")) {
      std::cerr << "    SKIPPED " << dir.filename().string() << " (Linux only)\n";
      continue;
    }
#endif
    corpus::RunOptions ro;
    ro.dir = dir;
    ro.timeoutSec = limit ? std::max(1, std::atoi(limit)) : 300;
    ro.out = [](const std::string& l) { std::cerr << "    " << l << "\n"; };
    corpus::Report rep = corpus::runCorpus(ro);
    if (reports) {
      std::error_code ec;
      fs::create_directories(reports, ec);
      std::ofstream(fs::path(reports) / (dir.filename().string() + ".json"), std::ios::binary) << rep.toJson() << "\n";
    }
    if (rep.exitCode() != 0)
      tftest::fail(__FILE__, __LINE__,
                   "corpus " + dir.filename().string() + ": " + (rep.fatal.empty() ? rep.summary() : rep.fatal));
  }
}
