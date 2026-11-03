#pragma once
// Internal to src/term: the platform half of Pty (Pty_posix.cpp / Pty_win32.cpp). Not for other code.
#include <mutex>

#include "term/Pty.h"

namespace tf {

struct Pty::Backend {
  Backend() = default;
  ~Backend();
  Backend(const Backend&) = delete;
  Backend& operator=(const Backend&) = delete;

  bool start(const PtyOptions& options, std::string* error);
  // Waits up to timeoutMs for output. >0 bytes read; 0 nothing yet; -1 no more output will come (end of stream).
  long read(char* buf, size_t n, int timeoutMs);
  bool write(const char* data, size_t n);
  bool resize(int rows, int cols);
  // True once the program has exited (reaped); *code receives the exit code. Thread safe, cheap.
  bool pollExit(int* code);
  // POSIX: signal the process group (SIGHUP, or SIGKILL when force). Windows: terminate the job object.
  void killTree(bool force);
  // Called after the tree is dead and before the reader is joined (Windows: ClosePseudoConsole while the reader
  // still drains the pipe).
  void shutdownIo();

  static bool available();

  std::mutex exitMu;
  bool exitedFlag = false;
  int exitCodeValue = -1;
#ifdef _WIN32
  void* process = nullptr;  // HANDLE
  void* job = nullptr;      // HANDLE
  void* pseudo = nullptr;   // HPCON
  void* inWrite = nullptr;  // HANDLE: our end of the input pipe
  void* outRead = nullptr;  // HANDLE: our end of the output pipe
#else
  int pid = -1;
  int master = -1;
#endif
};

}  // namespace tf
