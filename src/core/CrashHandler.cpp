#include "core/CrashHandler.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
//
#include <dbghelp.h>

#include <csignal>
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>

#include <ctime>
#if defined(__GLIBC__) || defined(__APPLE__)
#include <execinfo.h>
#define TF_HAVE_BACKTRACE 1
#endif
#ifdef __APPLE__
#include <sys/ucontext.h>
#endif
#endif

namespace fs = std::filesystem;

namespace tf {

fs::path defaultCrashDir() {
  auto env = [](const char* n) -> std::string {
    const char* v = std::getenv(n);
    return v ? v : "";
  };
#ifdef _WIN32
  std::string base = env("LOCALAPPDATA");
  if (base.empty()) base = env("APPDATA");
  if (base.empty()) return {};
  return fs::path(base) / "tenfold" / "crashes";
#elif defined(__APPLE__)
  std::string home = env("HOME");
  if (home.empty()) return {};
  return fs::path(home) / "Library" / "Application Support" / "tenfold" / "crashes";
#else
  std::string base = env("XDG_DATA_HOME");
  if (base.empty()) {
    std::string home = env("HOME");
    if (home.empty()) return {};
    base = home + "/.local/share";
  }
  return fs::path(base) / "tenfold" / "crashes";
#endif
}

namespace {

#ifdef _WIN32

wchar_t g_dir[MAX_PATH * 2];
using MiniDumpWriteDumpFn = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION,
                                          PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);
MiniDumpWriteDumpFn g_writeDump = nullptr;
volatile LONG g_inHandler = 0;

void writeDump(EXCEPTION_POINTERS* ep) {
  if (!g_writeDump || InterlockedExchange(&g_inHandler, 1)) return;
  wchar_t path[MAX_PATH * 2 + 64];
  wsprintfW(path, L"%s\\crash-%lu-%lu.dmp", g_dir, GetCurrentProcessId(), GetTickCount());
  HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (f == INVALID_HANDLE_VALUE) return;
  MINIDUMP_EXCEPTION_INFORMATION mei;
  mei.ThreadId = GetCurrentThreadId();
  mei.ExceptionPointers = ep;
  mei.ClientPointers = FALSE;
  g_writeDump(GetCurrentProcess(), GetCurrentProcessId(), f,
              static_cast<MINIDUMP_TYPE>(MiniDumpWithDataSegs | MiniDumpWithIndirectlyReferencedMemory),
              ep ? &mei : nullptr, nullptr, nullptr);
  CloseHandle(f);
}

LONG WINAPI unhandledFilter(EXCEPTION_POINTERS* ep) {
  writeDump(ep);
  return EXCEPTION_EXECUTE_HANDLER;
}

void abortHandler(int) {
  writeDump(nullptr);
  std::_Exit(3);
}

#else

char g_dir[4096];
alignas(16) char g_altStack[65536];
volatile sig_atomic_t g_inHandler = 0;

// Async-signal-safe helpers: no allocation, no stdio.
size_t putStr(char* out, size_t pos, const char* s) {
  while (*s) out[pos++] = *s++;
  return pos;
}
size_t putNum(char* out, size_t pos, unsigned long long v, unsigned base) {
  char tmp[32];
  int n = 0;
  do {
    tmp[n++] = "0123456789abcdef"[v % base];
    v /= base;
  } while (v && n < 31);
  while (n) out[pos++] = tmp[--n];
  return pos;
}
void writeStr(int fd, const char* s) {
  size_t n = std::strlen(s);
  while (n) {
    ssize_t w = ::write(fd, s, n);
    if (w <= 0) return;
    s += w;
    n -= static_cast<size_t>(w);
  }
}
void writeNum(int fd, unsigned long long v, unsigned base) {
  char buf[40];
  size_t n = putNum(buf, 0, v, base);
  buf[n] = 0;
  writeStr(fd, buf);
}

const char* signalName(int sig) {
  switch (sig) {
    case SIGSEGV: return "SIGSEGV";
    case SIGBUS: return "SIGBUS";
    case SIGFPE: return "SIGFPE";
    case SIGILL: return "SIGILL";
    case SIGABRT: return "SIGABRT";
    default: return "signal";
  }
}

#ifdef __APPLE__
// backtrace() called from a handler running on the alternate stack can come back with almost nothing on macOS, so
// the frames are also walked by hand from the interrupted context (frame-pointer chain; arm64 and x86_64 both keep
// {previous fp, return address} at fp). Only plain memory reads and write(2): async-signal-safe. Every step is
// validated (aligned, strictly rising, bounded distance) so a corrupt chain ends the walk instead of faulting.
void writeManualFrames(int fd, void* uctx) {
  auto* uc = static_cast<ucontext_t*>(uctx);
  if (!uc || !uc->uc_mcontext) return;
  unsigned long long pc, fp;
#if defined(__aarch64__)
  pc = reinterpret_cast<unsigned long long>(__darwin_arm_thread_state64_get_pc(uc->uc_mcontext->__ss));
  fp = reinterpret_cast<unsigned long long>(__darwin_arm_thread_state64_get_fp(uc->uc_mcontext->__ss));
#elif defined(__x86_64__)
  pc = uc->uc_mcontext->__ss.__rip;
  fp = uc->uc_mcontext->__ss.__rbp;
#else
  return;
#endif
  writeStr(fd, "frames (from the interrupted context):\n  #0 0x");
  writeNum(fd, pc, 16);
  writeStr(fd, "\n");
  for (int i = 1; i < 64; ++i) {
    if (fp == 0 || (fp & 7) != 0) break;
    const auto* rec = reinterpret_cast<const unsigned long long*>(fp);
    unsigned long long next = rec[0], ret = rec[1];
    if (ret == 0) break;
    writeStr(fd, "  #");
    writeNum(fd, static_cast<unsigned>(i), 10);
    writeStr(fd, " 0x");
    writeNum(fd, ret, 16);
    writeStr(fd, "\n");
    if (next <= fp || next - fp > (1ull << 24)) break;
    fp = next;
  }
}
#endif

void crashSignal(int sig, siginfo_t* info, void* uctx) {
  if (!g_inHandler) {
    g_inHandler = 1;
    char path[sizeof g_dir + 80];
    size_t n = putStr(path, 0, g_dir);
    n = putStr(path, n, "/crash-");
    n = putNum(path, n, static_cast<unsigned long long>(::getpid()), 10);
    n = putStr(path, n, "-");
    n = putNum(path, n, static_cast<unsigned long long>(::time(nullptr)), 10);
    n = putStr(path, n, ".txt");
    path[n] = 0;
    int fd = ::open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
      writeStr(fd, "tenfold crash report\nsignal: ");
      writeNum(fd, static_cast<unsigned>(sig), 10);
      writeStr(fd, " ");
      writeStr(fd, signalName(sig));
      writeStr(fd, "\npid: ");
      writeNum(fd, static_cast<unsigned long long>(::getpid()), 10);
      writeStr(fd, "\nfault address: 0x");
      writeNum(fd, info ? reinterpret_cast<unsigned long long>(info->si_addr) : 0, 16);
      writeStr(fd, "\nbacktrace:\n");
#ifdef __APPLE__
      writeManualFrames(fd, uctx);
      writeStr(fd, "backtrace() frames:\n");
#else
      (void)uctx;
#endif
#ifdef TF_HAVE_BACKTRACE
      void* frames[64];
      int cnt = ::backtrace(frames, 64);
      ::backtrace_symbols_fd(frames, cnt, fd);
#else
      writeStr(fd, "(not available on this platform)\n");
#endif
      ::close(fd);
    }
  }
  // Default action and re-raise: the exit status / core dump look as if we were never here.
  ::signal(sig, SIG_DFL);
  ::raise(sig);
}

#endif

}  // namespace

bool installCrashHandler(const fs::path& dirIn) {
  fs::path dir = dirIn.empty() ? defaultCrashDir() : dirIn;
  if (dir.empty()) return false;
  std::error_code ec;
  fs::create_directories(dir, ec);
#ifdef _WIN32
  std::wstring w = fs::absolute(dir, ec).native();
  if (w.size() >= sizeof(g_dir) / sizeof(g_dir[0])) return false;
  std::memcpy(g_dir, w.c_str(), (w.size() + 1) * sizeof(wchar_t));
  // Loaded now (not in the filter: loading a DLL inside a crashing process can deadlock on the loader lock).
  if (HMODULE m = LoadLibraryW(L"dbghelp.dll"))
    g_writeDump =
        reinterpret_cast<MiniDumpWriteDumpFn>(reinterpret_cast<void*>(GetProcAddress(m, "MiniDumpWriteDump")));
  if (!g_writeDump) return false;
  SetUnhandledExceptionFilter(unhandledFilter);
  std::signal(SIGABRT, abortHandler);
  return true;
#else
  std::string d = fs::absolute(dir, ec).string();
  if (d.size() >= sizeof(g_dir)) return false;
  std::memcpy(g_dir, d.c_str(), d.size() + 1);
#ifdef TF_HAVE_BACKTRACE
  void* warm[4];
  ::backtrace(warm, 4);  // first call loads libgcc (allocates); do it now, not in the handler
#endif
  stack_t ss;
  ss.ss_sp = g_altStack;
  ss.ss_size = sizeof g_altStack;
  ss.ss_flags = 0;
  ::sigaltstack(&ss, nullptr);
  struct sigaction sa;
  std::memset(&sa, 0, sizeof sa);
  sa.sa_sigaction = crashSignal;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_NODEFER;
  for (int sig : {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT}) ::sigaction(sig, &sa, nullptr);
  return true;
#endif
}

}  // namespace tf
