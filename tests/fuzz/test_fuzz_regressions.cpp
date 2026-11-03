// Replays every input kept under tests/fuzz/regressions/<target>/ through its fuzz harness (see docs/TESTING.md,
// "Fuzzing"). Each file is a minimised input that once crashed, hung, threw or broke an invariant in a parser.
#include <random>

#include "../test_main.h"
#include "glob_reference.h"
#include "replay_common.h"
#include "search/TextSearch.h"

TEST(fuzz_regression_inputs_replay) {
  const tf::fuzz::ReplayResult r = tf::fuzz::replayRegressions(TENFOLD_SOURCE_DIR, false);
  for (const std::string& f : r.failures) tftest::fail(__FILE__, __LINE__, f);
  CHECK(r.ran > 0);
  std::printf("  replayed %zu fuzz regression inputs\n", r.ran);
}

// Every harness answers an empty input, a short garbage input and its own synthetic samples without complaint.
TEST(fuzz_harnesses_accept_trivial_inputs) {
  CHECK(tf::fuzz::registry().size() >= 15);
  for (const tf::fuzz::Target& t : tf::fuzz::registry()) {
    try {
      t.run(reinterpret_cast<const uint8_t*>(""), 0);
      const std::string junk = "\x01\xff{[(<\"'\\\n\n";
      t.run(reinterpret_cast<const uint8_t*>(junk.data()), junk.size());
      for (const std::string& s : t.seedText) t.run(reinterpret_cast<const uint8_t*>(s.data()), s.size());
    } catch (const std::exception& e) {
      tftest::fail(__FILE__, __LINE__, "harness " + t.name + " rejects a plain input: " + e.what());
    }
  }
}

// The memoised glob matcher against the plain recursive one (the matcher the memo replaced), on random small cases.
TEST(fuzz_glob_matcher_agrees_with_the_reference) {
  std::mt19937 rng(7);
  const char patternChars[] = "ab/**??", pathChars[] = "ab/ab";
  size_t matches = 0;
  for (int i = 0; i < 300000; ++i) {
    std::string pattern, path;
    for (size_t k = 0, n = rng() % 11; k < n; ++k) pattern += patternChars[rng() % 7];
    for (size_t k = 0, n = rng() % 14; k < n; ++k) path += pathChars[rng() % 5];
    const bool want = tf::fuzz::referenceGlob(pattern.data(), pattern.data() + pattern.size(), path.data(),
                                              path.data() + path.size());
    matches += want;
    if (tf::TextSearch::globMatch(pattern, path) != want) {
      tftest::fail(__FILE__, __LINE__, "glob '" + pattern + "' on '" + path + "' differs from the reference");
      return;
    }
  }
  CHECK(matches > 10000);  // the cases are not all misses
}
