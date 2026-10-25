#include "test_main.h"

#include <chrono>
#include <cstring>

namespace tftest {
std::vector<Case>& cases() {
  static std::vector<Case> c;
  return c;
}
int& failures() {
  static int f = 0;
  return f;
}
void fail(const char* file, int line, const std::string& msg) {
  std::cerr << "    " << file << ":" << line << ": " << msg << "\n";
  ++failures();
}
}  // namespace tftest

int main(int argc, char** argv) {
  const char* filter = argc > 1 ? argv[1] : nullptr;
  int run = 0, failedCases = 0;
  for (auto& c : tftest::cases()) {
    if (filter && std::strstr(c.name, filter) == nullptr) continue;
    int before = tftest::failures();
    auto t0 = std::chrono::steady_clock::now();
    std::cerr << "[ RUN  ] " << c.name << "\n";
    c.fn();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    bool ok = tftest::failures() == before;
    std::cerr << (ok ? "[  OK  ] " : "[ FAIL ] ") << c.name << " (" << ms << " ms)\n";
    ++run;
    if (!ok) ++failedCases;
  }
  std::cerr << run << " tests, " << failedCases << " failed\n";
  return failedCases == 0 ? 0 : 1;
}
