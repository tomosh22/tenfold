// Fuzz harness registry. One harness per parser: a function that takes arbitrary bytes, must neither crash, hang nor
// throw, and may check invariants of the parser's result (FUZZ_REQUIRE). The same harness is driven by
//  * tenfold-fuzz (fuzz_main.cpp): the in-repo mutation fuzzer, works with any compiler and sanitizer runtime,
//  * libFuzzer (libfuzzer_main.cpp, option TENFOLD_FUZZ_LIBFUZZER) on machines that have the runtime,
//  * the regression replay tests (test_fuzz_regressions.cpp), which feed every file of
//  tests/fuzz/regressions/<target>/.
#pragma once
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace tf::fuzz {

// Thrown by FUZZ_REQUIRE. Uncaught it terminates the fuzzer like a crash (the input is saved first); the regression
// tests catch it and report a failure.
struct InvariantFailure : std::logic_error {
  using std::logic_error::logic_error;
};
[[noreturn]] void failInvariant(const char* file, int line, const std::string& what);

struct Target {
  std::string name;
  void (*run)(const uint8_t* data, size_t size) = nullptr;
  // Seed sources inside the repository: "dir:.ext1,.ext2" scans `dir` recursively for files with these extensions
  // ("dir:" = every file); "dir/file.ext" names one file. Large files are cut into pieces of at most maxLen bytes.
  std::vector<std::string> seedFiles;
  std::vector<std::string> seedText;  // small synthetic samples of the format
  std::vector<std::string> dict;      // tokens the mutator inserts (keywords, tags, escape sequences ...)
  size_t maxLen = 4096;               // preferred input length of the fuzzer (mutations may grow beyond it, 4x at most)
};

std::vector<Target>& registry();
const Target* findTarget(const std::string& name);
struct Registrar {
  explicit Registrar(Target t);
};

// A fresh per-process scratch directory (created on first use, removed at exit) for harnesses whose parser reads
// files. Files written there are overwritten on every run.
std::string scratchDir();
void writeScratch(const std::string& relPath, const uint8_t* data, size_t size);
inline void writeScratch(const std::string& relPath, const std::string& s) {
  writeScratch(relPath, reinterpret_cast<const uint8_t*>(s.data()), s.size());
}
// Splits `data` at every occurrence of the marker line "\n#--#\n" (at most `maxParts` parts; the rest stays in the
// last one). Multi-file harnesses use it to carry several files in one input.
std::vector<std::string> splitParts(const uint8_t* data, size_t size, size_t maxParts);

}  // namespace tf::fuzz

#define FUZZ_REQUIRE(...)                                                            \
  do {                                                                               \
    if (!(__VA_ARGS__)) ::tf::fuzz::failInvariant(__FILE__, __LINE__, #__VA_ARGS__); \
  } while (0)
