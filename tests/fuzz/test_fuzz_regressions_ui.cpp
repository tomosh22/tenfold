// The UI part of the fuzz regression replay: the layout.json and explorer inputs (their harnesses need the UI library).
#include "../test_main.h"
#include "replay_common.h"

TEST(fuzz_ui_regression_inputs_replay) {
  const tf::fuzz::ReplayResult r = tf::fuzz::replayRegressions(TENFOLD_SOURCE_DIR, true);
  for (const std::string& f : r.failures) tftest::fail(__FILE__, __LINE__, f);
  std::printf("  replayed %zu UI fuzz regression inputs\n", r.ran);
}

TEST(fuzz_ui_harnesses_accept_trivial_inputs) {
  for (const char* name : {"layout", "explorer"}) {
    const tf::fuzz::Target* t = tf::fuzz::findTarget(name);
    REQUIRE(t != nullptr);
    try {
      t->run(reinterpret_cast<const uint8_t*>(""), 0);
      for (const std::string& s : t->seedText) t->run(reinterpret_cast<const uint8_t*>(s.data()), s.size());
    } catch (const std::exception& e) {
      tftest::fail(__FILE__, __LINE__, std::string("harness ") + name + " rejects a plain input: " + e.what());
    }
  }
}
