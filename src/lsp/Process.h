#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace tf {

// Minimal child-process wrapper with piped stdin/stdout. stderr is redirected
// to a log file (or discarded). POSIX implementation; the Windows version
// (CreateProcess + anonymous pipes) slots in behind the same interface.
class Process {
 public:
  Process() = default;
  ~Process();
  Process(const Process&) = delete;
  Process& operator=(const Process&) = delete;

  using Env = std::vector<std::pair<std::string, std::string>>;
  bool start(const std::string& exe, const std::vector<std::string>& args,
             const std::filesystem::path& cwd, const std::filesystem::path& stderrLog, std::string* error,
             const Env& extraEnv = {});

  // Blocking write of the whole buffer. Returns false on broken pipe.
  bool write(const char* data, size_t n);
  // Blocking read; returns bytes read, 0 on EOF, -1 on error.
  long read(char* buf, size_t n);

  bool running() const;
  void terminate();                 // SIGTERM
  void kill();                      // SIGKILL
  int wait(int timeoutMs = -1);     // exit code, -1 if still running after timeout
  void closeStdin();

 private:
  int pid_ = -1;
  int stdinFd_ = -1;
  int stdoutFd_ = -1;
  int exitCode_ = -1;
  bool exited_ = false;
};

}  // namespace tf
