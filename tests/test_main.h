#pragma once
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace tftest {
struct Case {
  const char* name;
  std::function<void()> fn;
};
std::vector<Case>& cases();
int& failures();
struct Registrar {
  Registrar(const char* n, std::function<void()> f) { cases().push_back({n, std::move(f)}); }
};
void fail(const char* file, int line, const std::string& msg);

// Performance budgets (keystroke-to-paint, per-call cost, benchmark ratios) are wall-clock numbers that depend on the
// machine, so tests always print them but only fail on them when TENFOLD_ENFORCE_PERF=1 (see docs/BENCHMARKS.md).
// Functional timeouts and liveness bounds stay plain CHECKs.
inline bool enforcePerf() {
  static const bool on = [] {
    const char* e = std::getenv("TENFOLD_ENFORCE_PERF");
    return e && std::strcmp(e, "1") == 0;
  }();
  return on;
}
}  // namespace tftest

#define TEST(name)                                         \
  static void test_##name();                               \
  static tftest::Registrar reg_##name(#name, test_##name); \
  static void test_##name()

#define CHECK(...)                                                                       \
  do {                                                                                   \
    if (!(__VA_ARGS__)) tftest::fail(__FILE__, __LINE__, "CHECK failed: " #__VA_ARGS__); \
  } while (0)
#define CHECK_EQ(a, b)                                                           \
  do {                                                                           \
    auto _a = (a);                                                               \
    auto _b = (b);                                                               \
    if (!(_a == _b)) {                                                           \
      std::ostringstream _s;                                                     \
      _s << "CHECK_EQ failed: " #a " == " #b "  (" << _a << " vs " << _b << ")"; \
      tftest::fail(__FILE__, __LINE__, _s.str());                                \
    }                                                                            \
  } while (0)
#define REQUIRE(...)                                                     \
  do {                                                                   \
    if (!(__VA_ARGS__)) {                                                \
      tftest::fail(__FILE__, __LINE__, "REQUIRE failed: " #__VA_ARGS__); \
      return;                                                            \
    }                                                                    \
  } while (0)

// A performance-budget assertion: CHECK when TENFOLD_ENFORCE_PERF=1, otherwise only report that the budget was missed.
#define PERF_CHECK(cond, what)                                                                                     \
  do {                                                                                                             \
    if (tftest::enforcePerf()) {                                                                                   \
      if (!(cond)) tftest::fail(__FILE__, __LINE__, std::string("PERF budget failed: ") + (what) + ": " #cond);    \
    } else if (!(cond)) {                                                                                          \
      std::printf("PERF: %s over budget (not enforced; set TENFOLD_ENFORCE_PERF=1)\n", std::string(what).c_str()); \
    }                                                                                                              \
  } while (0)
