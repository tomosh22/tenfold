// tenfold-corpus-tests (W9a): runs every tests/corpus/<name>/ project against the real engine and clangd and fails
// when an expectation fails (or an xfail starts passing). Skipped, with a message, without clangd.
//   TENFOLD_CORPUS_REPORT_DIR=<dir>  write <dir>/<name>.json per project (CI uploads them)
//   TENFOLD_CORPUS_ONLY=<name>       run just that project
//   TENFOLD_CORPUS_TIMEOUT=<s>       per-project time limit (default 300)
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <utility>
#include <vector>

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

namespace {

// A throw-away corpus project: `files` (relative path -> text) plus the expectation text.
fs::path makeCorpus(const std::string& name, const std::vector<std::pair<std::string, std::string>>& files,
                    const std::string& expect) {
  std::error_code ec;
  const fs::path dir = fs::temp_directory_path(ec) / ("tenfold-runner-test-" + name);
  fs::remove_all(dir, ec);
  fs::create_directories(dir, ec);
  for (auto& [rel, text] : files) {
    fs::create_directories((dir / rel).parent_path(), ec);
    std::ofstream(dir / rel, std::ios::binary) << text;
  }
  std::ofstream(dir / corpus::kExpectFileName, std::ios::binary) << expect;
  return dir;
}

}  // namespace

// W9-FIX-D (e): an anchor in a document that is not synced to clangd (not a C/C++ source or header) fails at once
// with a clear note; the old wait for the document's TU ran until the project time limit.
TEST(corpus_runner_unsynced_document_fails_fast_and_close_needs_an_open_file) {
  if (findExecutable("clangd").empty()) {
    std::cerr << "    SKIPPED: clangd not found on PATH\n";
    return;
  }
  const fs::path dir = makeCorpus("unsynced",
                                  {{"a.cpp", "int /*@f*/f();\nint main() { return /*@use_f*/f(); }\n"},
                                   {"notes.txt", "see /*@in_text*/word here\n"}},
                                  "def use_f -> f\n"
                                  "def in_text -> use_f\n"
                                  "hover in_text ~ \"x\"\n"
                                  "rename in_text other\n  in_text\n"
                                  "close a.cpp\n"
                                  "def use_f -> f\n"
                                  "close notes.txt\n");
  corpus::RunOptions ro;
  ro.dir = dir;
  ro.timeoutSec = 600;  // a wait that runs into this limit would take minutes
  std::ostringstream log;
  ro.out = [&](const std::string& l) { log << l << "\n"; };
  const auto t0 = std::chrono::steady_clock::now();
  corpus::Report rep = corpus::runCorpus(ro);
  const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  std::error_code ec;
  fs::remove_all(dir, ec);
  const bool shapeOk = rep.results.size() == 5u;
  if (!(rep.fatal.empty() && sec < 60.0 && shapeOk)) std::cerr << log.str();
  CHECK(rep.fatal.empty());
  CHECK(sec < 60.0);
  REQUIRE(shapeOk);  // only the checks that ran are results; the two `close` directives passed silently
  CHECK(rep.results[0].status == corpus::Status::Pass);
  for (size_t i : {1u, 2u, 3u}) {
    CHECK(rep.results[i].status == corpus::Status::Fail);
    CHECK(rep.results[i].note.find("document not synced") != std::string::npos);
  }
  CHECK(rep.results[4].status == corpus::Status::Pass);  // after `close a.cpp` the file is read again from disk
}

// `close` of a document that is not open is the script's mistake, reported as a FAIL of that line.
TEST(corpus_runner_close_of_a_closed_file_fails) {
  if (findExecutable("clangd").empty()) {
    std::cerr << "    SKIPPED: clangd not found on PATH\n";
    return;
  }
  const fs::path dir = makeCorpus("close", {{"a.cpp", "int /*@f*/f();\nint main() { return /*@use_f*/f(); }\n"}},
                                  "close a.cpp\n"
                                  "open a.cpp\n"
                                  "close a.cpp\n"
                                  "close a.cpp\n");
  corpus::RunOptions ro;
  ro.dir = dir;
  ro.timeoutSec = 120;
  std::ostringstream log;
  ro.out = [&](const std::string& l) { log << l << "\n"; };
  corpus::Report rep = corpus::runCorpus(ro);
  std::error_code ec;
  fs::remove_all(dir, ec);
  if (rep.results.size() != 2u) std::cerr << log.str();
  REQUIRE(rep.results.size() == 2u);
  CHECK_EQ(rep.results[0].line, 1);
  CHECK_EQ(rep.results[1].line, 4);
  CHECK(rep.results[0].note.find("is not open") != std::string::npos);
}
