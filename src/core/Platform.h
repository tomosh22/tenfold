#pragma once

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

}  // namespace tf
