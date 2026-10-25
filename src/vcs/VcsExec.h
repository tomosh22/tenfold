#pragma once
#include <filesystem>
#include <string>
#include <vector>

#include "lsp/Process.h"

namespace tf {

// Runs a command line tool to completion and captures its output. No shell is involved, so arguments with spaces,
// quotes or non-ASCII characters are passed through untouched. Blocking: for worker threads only.
struct ExecOptions {
  std::string exe;
  std::vector<std::string> args;
  std::filesystem::path cwd;
  Process::Env env;
  int timeoutMs = 60000;  // the child is killed after this long (<= 0: no limit)
};

struct ExecResult {
  bool started = false;  // false: the process could not be created (see err)
  bool timedOut = false;
  int exitCode = -1;
  std::string out;
  std::string err;  // stderr, or the failure to start
  bool ok() const { return started && !timedOut && exitCode == 0; }
};

ExecResult runCommand(const ExecOptions& opt);

// Command line tools speak UTF-8; fs::path's narrow conversions use the ANSI code page on Windows.
std::filesystem::path pathFromUtf8(const std::string& s);
std::string pathToUtf8(const std::filesystem::path& p);  // generic ('/') separators

}  // namespace tf
