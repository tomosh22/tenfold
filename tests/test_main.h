#pragma once
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
