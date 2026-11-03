// Shared by the regression tests of tenfold-tests and tenfold-ui-tests: runs every file of
// tests/fuzz/regressions/<target>/ through the harness named by the directory.
#pragma once
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "FuzzTarget.h"

namespace tf::fuzz {

// Targets whose harness needs the UI library; the engine test binary leaves their inputs to the UI test binary.
inline const std::set<std::string>& uiTargets() {
  static const std::set<std::string> s = {"layout", "explorer"};
  return s;
}

struct ReplayResult {
  size_t ran = 0;
  std::vector<std::string> failures;
};

inline ReplayResult replayRegressions(const std::string& sourceDir, bool uiTargetsOnly) {
  namespace fs = std::filesystem;
  ReplayResult res;
  const fs::path root = fs::path(sourceDir) / "tests" / "fuzz" / "regressions";
  std::error_code ec;
  std::vector<fs::path> files;
  for (auto it = fs::recursive_directory_iterator(root, ec); !ec && it != fs::recursive_directory_iterator();
       it.increment(ec))
    if (it->is_regular_file(ec) && it->path().extension() != ".md") files.push_back(it->path());
  std::sort(files.begin(), files.end());
  for (const fs::path& f : files) {
    const std::string target = f.parent_path().filename().string();
    if (uiTargets().count(target) != (uiTargetsOnly ? 1u : 0u)) continue;
    const Target* t = findTarget(target);
    if (!t) {
      res.failures.push_back("regression input without a harness: " + f.string());
      continue;
    }
    std::ifstream in(f, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string data = ss.str();
    try {
      t->run(reinterpret_cast<const uint8_t*>(data.data()), data.size());
    } catch (const std::exception& e) { res.failures.push_back(f.string() + ": " + e.what()); }
    ++res.ran;
  }
  return res;
}

}  // namespace tf::fuzz
