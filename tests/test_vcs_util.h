#pragma once
// Shared helpers of the C3 tests: temporary git repositories (tests skip with a message when git is missing).
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "core/PathUtil.h"
#include "vcs/VcsExec.h"

namespace tfvcs {

namespace fs = std::filesystem;

inline void setEnvVar(const char* k, const char* v) {
#ifdef _WIN32
  _putenv_s(k, v);
#else
  ::setenv(k, v, 1);
#endif
}

inline bool gitAvailable() {
  static const bool ok = [] {
    tf::ExecOptions o;
    o.exe = "git";
    o.args = {"--version"};
    o.timeoutMs = 10000;
    bool good = tf::runCommand(o).ok();
    if (good) {  // the user's own configuration (autocrlf, hooks templates, ...) must not leak into the tests
#ifdef _WIN32
      setEnvVar("GIT_CONFIG_GLOBAL", "NUL");
#else
      setEnvVar("GIT_CONFIG_GLOBAL", "/dev/null");
#endif
      setEnvVar("GIT_CONFIG_NOSYSTEM", "1");
    }
    return good;
  }();
  return ok;
}

#define SKIP_WITHOUT_GIT()                                            \
  do {                                                                \
    if (!tfvcs::gitAvailable()) {                                     \
      std::cerr << "    SKIP: git is not installed or not on PATH\n"; \
      return;                                                         \
    }                                                                 \
  } while (0)

inline fs::path tempDir(const std::string& tag) {
  static int counter = 0;
  std::error_code ec;
  fs::path d =
      fs::weakly_canonical(fs::temp_directory_path(ec), ec) /
      ("tenfold_vcs_" + tag + "_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_" +
       std::to_string(counter++));
  fs::create_directories(d, ec);
  return d;
}

inline void writeFile(const fs::path& p, const std::string& content) {
  std::error_code ec;
  fs::create_directories(p.parent_path(), ec);
  std::ofstream out(p, std::ios::binary | std::ios::trunc);
  out << content;
}

inline std::string readFile(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

// A git repository in a fresh temporary directory (removed on destruction).
struct TempRepo {
  fs::path dir;
  bool ok = false;

  explicit TempRepo(bool init = true) : dir(tempDir("git")) {
    if (init) {
      ok = git({"init", "-q"}).ok();
      git({"config", "user.name", "Tenfold Test"});
      git({"config", "user.email", "test@example.invalid"});
      git({"config", "commit.gpgsign", "false"});
      git({"config", "core.autocrlf", "false"});
    }
  }
  ~TempRepo() {
    std::error_code ec;
    fs::remove_all(dir, ec);
  }
  TempRepo(const TempRepo&) = delete;
  TempRepo& operator=(const TempRepo&) = delete;

  tf::ExecResult git(std::vector<std::string> args) const {
    tf::ExecOptions o;
    o.exe = "git";
    o.args = std::move(args);
    o.cwd = dir;
    o.env = {{"GIT_LITERAL_PATHSPECS", "1"}};
    o.timeoutMs = 30000;
    return tf::runCommand(o);
  }
  fs::path path(const std::string& rel) const { return dir / tf::pathFromUtf8(rel); }
  void write(const std::string& rel, const std::string& content) const { writeFile(path(rel), content); }
  std::string read(const std::string& rel) const { return readFile(path(rel)); }
  void commitAll(const std::string& msg = "commit") const {
    git({"add", "-A"});
    git({"commit", "-q", "-m", msg});
  }
};

}  // namespace tfvcs
