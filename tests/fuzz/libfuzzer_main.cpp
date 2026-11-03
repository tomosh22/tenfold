// libFuzzer entry for one harness: build with -DTENFOLD_FUZZ_TARGET="<name>" (CMake option TENFOLD_FUZZ_LIBFUZZER does
// this for every harness) and link with -fsanitize=fuzzer. Seeds: run the binary on tests/fuzz/regressions/<name>/.
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "FuzzTarget.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  static const tf::fuzz::Target* target = [] {
    const tf::fuzz::Target* t = tf::fuzz::findTarget(TENFOLD_FUZZ_TARGET);
    if (!t) {
      std::fprintf(stderr, "no fuzz target '%s'\n", TENFOLD_FUZZ_TARGET);
      std::abort();
    }
    return t;
  }();
  target->run(data, size);
  return 0;
}
