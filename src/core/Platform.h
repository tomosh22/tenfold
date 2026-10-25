#pragma once

#include <cstdio>
#include <string>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace tf {

// Id of the running process (LSP initialize, unique temp-file names).
inline int currentProcessId() {
#ifdef _WIN32
  return _getpid();
#else
  return static_cast<int>(::getpid());
#endif
}

// popen() through the platform shell (sh -c / cmd /c). Read the output, then closeShellPipe().
inline FILE* openShellPipe(const std::string& cmd, const char* mode) {
#ifdef _WIN32
  // cmd /c strips one pair of outer quotes, so wrap the whole command to keep quoted arguments intact.
  return _popen(("\"" + cmd + "\"").c_str(), mode);
#else
  return ::popen(cmd.c_str(), mode);
#endif
}

inline int closeShellPipe(FILE* p) {
#ifdef _WIN32
  return _pclose(p);
#else
  return ::pclose(p);
#endif
}

}  // namespace tf
