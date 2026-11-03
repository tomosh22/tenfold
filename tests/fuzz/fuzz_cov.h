// The coverage map filled by __sanitizer_cov_trace_pc (fuzz_cov.cpp) when code is built with
// -fsanitize-coverage=trace-pc (TENFOLD_FUZZ_COVERAGE); tenfold-fuzz reads it after every run.
#pragma once
#include <cstddef>
#include <cstdint>

constexpr size_t kFuzzCovSize = size_t{1} << 16;
extern uint8_t g_fuzzCov[kFuzzCovSize];
