#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <mutex>

#include "term/PtyBackend.h"

extern char** environ;

namespace tf {

namespace {

void setCloexec(int fd) {
  int fl = ::fcntl(fd, F_GETFD);
  if (fl >= 0) ::fcntl(fd, F_SETFD, fl | FD_CLOEXEC);
}

bool setWinSize(int fd, int rows, int cols) {
  struct winsize ws {};
  ws.ws_row = static_cast<unsigned short>(rows);
  ws.ws_col = static_cast<unsigned short>(cols);
  return ::ioctl(fd, TIOCSWINSZ, &ws) == 0;
}

std::string slaveName(int master) {
#ifdef __linux__
  char buf[256];
  if (::ptsname_r(master, buf, sizeof buf) != 0) return {};
  return buf;
#else
  static std::mutex mu;  // ptsname returns a static buffer
  std::lock_guard<std::mutex> lk(mu);
  const char* n = ::ptsname(master);
  return n ? n : std::string();
#endif
}

}  // namespace

bool Pty::Backend::available() { return true; }

Pty::Backend::~Backend() {
  if (master >= 0) ::close(master);
  if (pid > 0) {
    int code;
    if (!pollExit(&code)) {
      ::kill(-pid, SIGKILL);
      ::kill(pid, SIGKILL);
      ::waitpid(pid, nullptr, 0);
    }
  }
}

bool Pty::Backend::start(const PtyOptions& o, std::string* error) {
  auto fail = [&](const char* what) {
    if (error) *error = std::string(what) + ": " + std::strerror(errno);
    return false;
  };
  // Every descriptor is close-on-exec (see Process_posix.cpp): a child forked concurrently on another thread must not
  // inherit this terminal's master, or closing it would not hang up the shell.
  int m = ::posix_openpt(O_RDWR | O_NOCTTY);
  if (m < 0) return fail("posix_openpt");
  setCloexec(m);
  if (::grantpt(m) != 0 || ::unlockpt(m) != 0) {
    int e = errno;
    ::close(m);
    errno = e;
    return fail("grantpt/unlockpt");
  }
  std::string sname = slaveName(m);
  int slave = sname.empty() ? -1 : ::open(sname.c_str(), O_RDWR | O_NOCTTY | O_CLOEXEC);
  if (slave < 0) {
    int e = errno;
    ::close(m);
    errno = e;
    return fail("open pty slave");
  }
  setWinSize(m, o.rows, o.cols);
#ifdef IUTF8
  struct termios tio {};
  if (::tcgetattr(slave, &tio) == 0) {
    tio.c_iflag |= IUTF8;
    ::tcsetattr(slave, TCSANOW, &tio);
  }
#endif
  int errPipe[2];
#ifdef __linux__
  if (::pipe2(errPipe, O_CLOEXEC) != 0) {
#else
  if (::pipe(errPipe) != 0) {
#endif
    int e = errno;
    ::close(m);
    ::close(slave);
    errno = e;
    return fail("pipe");
  }
#ifndef __linux__
  setCloexec(errPipe[0]);
  setCloexec(errPipe[1]);
#endif

  // Everything the child needs is prepared before fork(): only async-signal-safe calls run between fork and exec.
  std::vector<std::string> argvStorage{o.program};
  argvStorage.insert(argvStorage.end(), o.args.begin(), o.args.end());
  std::vector<char*> argv;
  for (auto& a : argvStorage) argv.push_back(const_cast<char*>(a.c_str()));
  argv.push_back(nullptr);

  std::vector<std::pair<std::string, std::string>> extra = o.env;
  auto has = [&](const char* k) {
    for (auto& e : extra)
      if (e.first == k) return true;
    return false;
  };
  if (!has("TERM")) extra.emplace_back("TERM", "xterm-256color");
  if (!has("COLORTERM")) extra.emplace_back("COLORTERM", "truecolor");
  std::vector<std::string> envStorage;
  for (char** e = environ; e && *e; ++e) {
    std::string s = *e;
    size_t eq = s.find('=');
    std::string key = s.substr(0, eq);
    if (!has(key.c_str())) envStorage.push_back(std::move(s));
  }
  for (auto& [k, v] : extra) envStorage.push_back(k + "=" + v);
  std::vector<char*> envp;
  for (auto& e : envStorage) envp.push_back(const_cast<char*>(e.c_str()));
  envp.push_back(nullptr);
  std::string cwd = o.cwd.string();

  pid_t child = ::fork();
  if (child < 0) {
    int e = errno;
    ::close(m);
    ::close(slave);
    ::close(errPipe[0]);
    ::close(errPipe[1]);
    errno = e;
    return fail("fork");
  }
  if (child == 0) {
    ::setsid();
    ::ioctl(slave, TIOCSCTTY, 0);
    ::dup2(slave, 0);
    ::dup2(slave, 1);
    ::dup2(slave, 2);
    ::signal(SIGPIPE, SIG_DFL);  // the parent ignores it; programs expect the default
    sigset_t none;
    sigemptyset(&none);
    ::sigprocmask(SIG_SETMASK, &none, nullptr);
    if (!cwd.empty() && ::chdir(cwd.c_str()) != 0) {
      int e = errno;
      ssize_t w = ::write(errPipe[1], &e, sizeof e);
      (void)w;
      _exit(127);
    }
    environ = envp.data();
    ::execvp(argv[0], argv.data());
    int e = errno;
    ssize_t w = ::write(errPipe[1], &e, sizeof e);
    (void)w;
    _exit(127);
  }
  ::close(slave);
  ::close(errPipe[1]);
  int childErr = 0;
  ssize_t r;
  do { r = ::read(errPipe[0], &childErr, sizeof childErr); } while (r < 0 && errno == EINTR);
  ::close(errPipe[0]);
  if (r == static_cast<ssize_t>(sizeof childErr)) {  // exec (or chdir) failed
    ::waitpid(child, nullptr, 0);
    ::close(m);
    if (error) *error = o.program + ": " + std::strerror(childErr);
    return false;
  }
  ::setpgid(child, child);  // already a group leader through setsid; harmless
  pid = child;
  master = m;
  return true;
}

long Pty::Backend::read(char* buf, size_t n, int timeoutMs) {
  struct pollfd p {};
  p.fd = master;
  p.events = POLLIN;
  int r = ::poll(&p, 1, timeoutMs);
  if (r < 0) return errno == EINTR ? 0 : -1;
  if (r == 0) return 0;
  if (p.revents & POLLIN) {
    ssize_t k = ::read(master, buf, n);
    if (k > 0) return k;
    if (k < 0 && (errno == EINTR || errno == EAGAIN)) return 0;
    return -1;  // EIO / 0: every slave descriptor is closed
  }
  return -1;  // POLLHUP / POLLERR without data
}

bool Pty::Backend::write(const char* data, size_t n) {
  while (n > 0) {
    ssize_t w = ::write(master, data, n);
    if (w < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    data += w;
    n -= static_cast<size_t>(w);
  }
  return true;
}

bool Pty::Backend::resize(int rows, int cols) { return setWinSize(master, rows, cols); }

bool Pty::Backend::pollExit(int* code) {
  std::lock_guard<std::mutex> lk(exitMu);
  if (!exitedFlag && pid > 0) {
    int status = 0;
    pid_t r = ::waitpid(pid, &status, WNOHANG);
    if (r == pid) {
      exitedFlag = true;
      exitCodeValue = WIFEXITED(status) ? WEXITSTATUS(status) : (WIFSIGNALED(status) ? 128 + WTERMSIG(status) : -1);
    } else if (r < 0 && errno != EINTR) {
      exitedFlag = true;  // already reaped elsewhere
    }
  }
  if (exitedFlag && code) *code = exitCodeValue;
  return exitedFlag;
}

void Pty::Backend::killTree(bool force) {
  if (pid <= 0) return;
  int sig = force ? SIGKILL : SIGHUP;
  ::kill(-pid, sig);  // the session leader's group (setsid made pid == pgid)
  if (!force) ::kill(-pid, SIGCONT);
}

void Pty::Backend::shutdownIo() {}

}  // namespace tf
