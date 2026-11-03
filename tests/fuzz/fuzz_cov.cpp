// Edge-coverage hook for gcc / clang -fsanitize-coverage=trace-pc. Must be compiled WITHOUT the instrumentation flag.
// Every binary that links an instrumented tenfold_core needs this symbol, so it lives in its own small library.
#include "fuzz_cov.h"

uint8_t g_fuzzCov[kFuzzCovSize];

extern "C" void __sanitizer_cov_trace_pc() {
  static thread_local uintptr_t prev = 0;
  const uintptr_t pc = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
  g_fuzzCov[((pc >> 2) ^ (prev << 1)) & (kFuzzCovSize - 1)] = 1;
  prev = pc >> 2;
}
