#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>

#include "lsp/Process.h"

namespace tf {

Process::~Process() {
  if (running() || (tree_ && pid_ > 0)) kill();  // a tree: also reap stragglers that outlived the leader
  if (stdinFd_ >= 0) ::close(stdinFd_);
  if (stdoutFd_ >= 0) ::close(stdoutFd_);
  if (pid_ > 0 && !exited_) wait(100);
}

bool Process::start(const std::string& exe, const std::vector<std::string>& args, const std::filesystem::path& cwd,
                    const std::filesystem::path& stderrLog, std::string* error, const Env& extraEnv,
                    const ProcessOptions& options) {
  // A server that died must not kill us when we write to its stdin: EPIPE is handled by the caller. (Reset to the
  // default in the child below, so spawned programs keep normal pipeline behaviour.)
  static std::once_flag ignoreSigpipe;
  std::call_once(ignoreSigpipe, [] { ::signal(SIGPIPE, SIG_IGN); });
  int inPipe[2], outPipe[2];
  if (::pipe(inPipe) != 0 || ::pipe(outPipe) != 0) {
    if (error) *error = std::strerror(errno);
    return false;
  }
  int errFd = -1;
  if (options.mergeStderr) errFd = -2;  // dup'd from the stdout pipe in the child
  else if (!stderrLog.empty()) errFd = ::open(stderrLog.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (errFd == -1) errFd = ::open("/dev/null", O_WRONLY);

  std::vector<std::string> argvStorage;
  argvStorage.push_back(exe);
  for (auto& a : args) argvStorage.push_back(a);
  std::vector<char*> argv;
  for (auto& a : argvStorage) argv.push_back(const_cast<char*>(a.c_str()));
  argv.push_back(nullptr);

  pid_t pid = ::fork();
  if (pid < 0) {
    if (error) *error = std::strerror(errno);
    return false;
  }
  if (pid == 0) {
    ::signal(SIGPIPE, SIG_DFL);
    if (options.processTree) ::setpgid(0, 0);
    ::dup2(inPipe[0], STDIN_FILENO);
    ::dup2(outPipe[1], STDOUT_FILENO);
    ::dup2(errFd == -2 ? outPipe[1] : errFd, STDERR_FILENO);
    ::close(inPipe[0]);
    ::close(inPipe[1]);
    ::close(outPipe[0]);
    ::close(outPipe[1]);
    if (errFd > 2) ::close(errFd);
    if (!cwd.empty()) {
      if (::chdir(cwd.c_str()) != 0) { /* ignore */
      }
    }
    for (auto& [k, v] : extraEnv) ::setenv(k.c_str(), v.c_str(), 1);
    ::execvp(exe.c_str(), argv.data());
    _exit(127);
  }
  if (options.processTree) ::setpgid(pid, pid);  // also in the parent: no window without the group
  ::close(inPipe[0]);
  ::close(outPipe[1]);
  if (errFd > 2) ::close(errFd);
  tree_ = options.processTree;
  exitCode_ = -1;
  pid_ = pid;
  stdinFd_ = inPipe[1];
  stdoutFd_ = outPipe[0];
  exited_ = false;
  return true;
}

bool Process::write(const char* data, size_t n) {
  while (n > 0) {
    ssize_t w = ::write(stdinFd_, data, n);
    if (w < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    data += w;
    n -= static_cast<size_t>(w);
  }
  return true;
}

long Process::read(char* buf, size_t n) {
  while (true) {
    ssize_t r = ::read(stdoutFd_, buf, n);
    if (r < 0 && errno == EINTR) continue;
    return static_cast<long>(r);
  }
}

bool Process::running() const {
  if (pid_ <= 0 || exited_) return false;
  int status = 0;
  pid_t r = ::waitpid(pid_, &status, WNOHANG);
  if (r == pid_) {
    auto* self = const_cast<Process*>(this);
    self->exited_ = true;
    self->exitCode_ = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return false;
  }
  return r == 0;
}

void Process::terminate() {
  if (tree_ && pid_ > 0) ::kill(-pid_, SIGTERM);  // the group outlives its leader while any member runs
  else if (pid_ > 0 && !exited_) ::kill(pid_, SIGTERM);
}
void Process::kill() {
  if (tree_ && pid_ > 0) ::kill(-pid_, SIGKILL);
  else if (pid_ > 0 && !exited_) ::kill(pid_, SIGKILL);
}

void Process::closeStdin() {
  if (stdinFd_ >= 0) {
    ::close(stdinFd_);
    stdinFd_ = -1;
  }
}

int Process::wait(int timeoutMs) {
  if (pid_ <= 0) return -1;
  if (exited_) return exitCode_;
  auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs < 0 ? 0 : timeoutMs);
  while (true) {
    int status = 0;
    pid_t r = ::waitpid(pid_, &status, timeoutMs < 0 ? 0 : WNOHANG);
    if (r == pid_) {
      exited_ = true;
      exitCode_ = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
      return exitCode_;
    }
    if (r < 0) {
      exited_ = true;
      return -1;
    }
    if (std::chrono::steady_clock::now() >= deadline) return -1;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
}

}  // namespace tf
