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
// Only for short, rarely used probes (cmake configure, xcrun). On Windows _popen creates its child with
// bInheritHandles=TRUE and no handle list, so a Process::start running on another thread at the same moment can leak
// its child-side pipe ends into that shell for as long as it lives (Process::start itself is safe against other
// Process::start calls). On POSIX all pipes are close-on-exec. Use Process for anything long-lived or concurrent.
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
