#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>

#include "lsp/Process.h"

extern char** environ;

namespace tf {

namespace {
// posix_spawn shares the parent's address space with the child until exec (CLONE_VM | CLONE_VFORK on Linux), where
// fork() copies the page tables of the whole process while holding the mmap lock, which stalls every other thread's
// page faults, and then makes every first write after the fork a copy-on-write fault. In a process with a few hundred
// MB of heap that cost several ms of the editor's main thread each time a worker started git (COMPL-PERF).
// Returns false (nothing started) when the request needs something posix_spawn cannot do or the spawn failed; the
// caller then falls back to fork/exec, which also keeps the "exit code 127" report for a program that cannot run.
bool spawnFast(const std::string& exe, const std::vector<char*>& argv, const std::filesystem::path& cwd,
               const Process::Env& extraEnv, bool tree, const int inPipe[2], const int outPipe[2], int stderrTarget,
               pid_t* pid) {
#if (defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 29))) || defined(__APPLE__)
  for (auto& kv : extraEnv)
    if (kv.first == "PATH") return false;  // execvp would search the child's PATH: keep the exact old semantics
  posix_spawn_file_actions_t fa;
  posix_spawnattr_t at;
  if (posix_spawn_file_actions_init(&fa) != 0) return false;
  if (posix_spawnattr_init(&at) != 0) {
    posix_spawn_file_actions_destroy(&fa);
    return false;
  }
  bool ok = posix_spawn_file_actions_adddup2(&fa, inPipe[0], STDIN_FILENO) == 0 &&
            posix_spawn_file_actions_adddup2(&fa, outPipe[1], STDOUT_FILENO) == 0 &&
            posix_spawn_file_actions_adddup2(&fa, stderrTarget, STDERR_FILENO) == 0;
  if (ok && !cwd.empty()) ok = posix_spawn_file_actions_addchdir_np(&fa, cwd.c_str()) == 0;
  sigset_t def;
  sigemptyset(&def);
  sigaddset(&def, SIGPIPE);  // we ignore SIGPIPE ourselves; the child gets the default back
  short flags = POSIX_SPAWN_SETSIGDEF;
  if (tree) flags = static_cast<short>(flags | POSIX_SPAWN_SETPGROUP);
  if (ok)
    ok = posix_spawnattr_setsigdefault(&at, &def) == 0 && posix_spawnattr_setpgroup(&at, 0) == 0 &&
         posix_spawnattr_setflags(&at, flags) == 0;
  std::vector<std::string> envStorage;
  std::vector<char*> envp;
  if (ok) {
    for (char** e = environ; e && *e; ++e) {
      const std::string s = *e;
      bool replaced = false;
      for (auto& kv : extraEnv)
        if (s.compare(0, kv.first.size() + 1, kv.first + "=") == 0) replaced = true;
      if (!replaced) envStorage.push_back(s);
    }
    for (auto& kv : extraEnv) envStorage.push_back(kv.first + "=" + kv.second);
    for (auto& s : envStorage) envp.push_back(const_cast<char*>(s.c_str()));
    envp.push_back(nullptr);
  }
  pid_t p = -1;
  if (ok) ok = ::posix_spawnp(&p, exe.c_str(), &fa, &at, argv.data(), envp.data()) == 0;
  posix_spawn_file_actions_destroy(&fa);
  posix_spawnattr_destroy(&at);
  if (!ok) return false;
  *pid = p;
  return true;
#else
  (void)exe, (void)argv, (void)cwd, (void)extraEnv, (void)tree, (void)inPipe, (void)outPipe, (void)stderrTarget;
  (void)pid;
  return false;
#endif
}
}  // namespace

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
  // Every descriptor is close-on-exec: another thread may fork a different child at the same moment, and a child that
  // inherited this child's stdout pipe would keep it open, so our reader would never see EOF after this child exits.
  // (dup2 onto 0/1/2 in the child clears the flag on those.)
  auto makePipe = [](int fds[2]) {
#ifdef __linux__
    return ::pipe2(fds, O_CLOEXEC) == 0;
#else
    if (::pipe(fds) != 0) return false;
    ::fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    ::fcntl(fds[1], F_SETFD, FD_CLOEXEC);
    return true;
#endif
  };
  int inPipe[2], outPipe[2];
  if (!makePipe(inPipe) || !makePipe(outPipe)) {
    if (error) *error = std::strerror(errno);
    return false;
  }
  int errFd = -1;
  if (options.mergeStderr) errFd = -2;  // dup'd from the stdout pipe in the child
  else if (!stderrLog.empty()) errFd = ::open(stderrLog.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
  if (errFd == -1) errFd = ::open("/dev/null", O_WRONLY | O_CLOEXEC);

  std::vector<std::string> argvStorage;
  argvStorage.push_back(exe);
  for (auto& a : args) argvStorage.push_back(a);
  std::vector<char*> argv;
  for (auto& a : argvStorage) argv.push_back(const_cast<char*>(a.c_str()));
  argv.push_back(nullptr);

  pid_t pid = -1;
  const int stderrTarget = errFd == -2 ? outPipe[1] : errFd;
  if (!spawnFast(exe, argv, cwd, extraEnv, options.processTree, inPipe, outPipe, stderrTarget, &pid)) pid = -1;
  if (pid < 0) pid = ::fork();
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
