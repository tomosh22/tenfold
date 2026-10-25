#pragma once
#include <atomic>
#include <filesystem>
#include <string>
#include <vector>

namespace tf {

// Minimal child-process wrapper with piped stdin/stdout. stderr is redirected
// to a log file (or discarded). Implemented in Process_posix.cpp (fork/exec)
// and Process_win32.cpp (CreateProcessW + anonymous pipes + job object);
// CMake picks one. This header must not include <windows.h>.
// Optional start() behaviour; the defaults keep the original semantics (stderr to a log file, child in our group).
struct ProcessOptions {
  // stderr goes into the stdout pipe (one interleaved stream); stderrLog is ignored.
  bool mergeStderr = false;
  // The child leads its own process group (POSIX) / terminate() and kill() take down the whole job object
  // (Windows), so they stop grandchildren too.
  bool processTree = false;
  // Windows only: use this string verbatim as the command line (exe/args are ignored). Ignored on POSIX.
  std::string rawCommandLine;
};

class Process {
 public:
  Process() = default;
  ~Process();
  Process(const Process&) = delete;
  Process& operator=(const Process&) = delete;

  using Env = std::vector<std::pair<std::string, std::string>>;
  bool start(const std::string& exe, const std::vector<std::string>& args, const std::filesystem::path& cwd,
             const std::filesystem::path& stderrLog, std::string* error, const Env& extraEnv = {},
             const ProcessOptions& options = {});

  // Blocking write of the whole buffer. Returns false on broken pipe.
  bool write(const char* data, size_t n);
  // Blocking read; returns bytes read, 0 on EOF, -1 on error.
  long read(char* buf, size_t n);

  bool running() const;
  // SIGTERM / TerminateProcess; with ProcessOptions::processTree the whole group / job. Safe to call from a
  // thread other than the one blocked in read()/wait().
  void terminate();
  void kill();                   // SIGKILL / TerminateProcess (same tree rule)
  int wait(int timeoutMs = -1);  // exit code, -1 if still running after timeout
  void closeStdin();

 private:
#ifdef _WIN32
  void* process_ = nullptr;  // HANDLE
  void* job_ = nullptr;      // HANDLE, kill-on-close job object
  void* stdinH_ = nullptr;   // HANDLE
  void* stdoutH_ = nullptr;  // HANDLE
#else
  int pid_ = -1;
  int stdinFd_ = -1;
  int stdoutFd_ = -1;
#endif
  bool tree_ = false;
  std::atomic<int> exitCode_{-1};
  std::atomic<bool> exited_{false};
};

}  // namespace tf
