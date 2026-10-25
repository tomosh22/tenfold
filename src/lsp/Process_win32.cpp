#include "lsp/Process.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>

namespace tf {

namespace {

std::wstring utf8ToWide(const std::string& s) {
  if (s.empty()) return {};
  int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  if (n <= 0) return {};
  std::wstring w(static_cast<size_t>(n), L'\0');
  ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
  return w;
}

std::string lastErrorText(const char* what) {
  DWORD e = ::GetLastError();
  return std::string(what) + " failed (error " + std::to_string(e) + ")";
}

// Appends one argument quoted per the MSVCRT / CommandLineToArgvW rules.
void appendQuoted(std::wstring& cmd, const std::wstring& a) {
  if (!a.empty() && a.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
    cmd += a;
    return;
  }
  cmd += L'"';
  size_t backslashes = 0;
  for (wchar_t c : a) {
    if (c == L'\\') {
      ++backslashes;
    } else if (c == L'"') {
      cmd.append(backslashes * 2 + 1, L'\\');
      cmd += L'"';
      backslashes = 0;
    } else {
      cmd.append(backslashes, L'\\');
      cmd += c;
      backslashes = 0;
    }
  }
  cmd.append(backslashes * 2, L'\\');  // backslashes before the closing quote
  cmd += L'"';
}

std::wstring upper(std::wstring s) {
  for (auto& c : s) c = static_cast<wchar_t>(std::towupper(c));
  return s;
}

// Key of a "KEY=value" entry; entries like "=C:=C:\dir" keep the leading '='.
std::wstring envKey(const std::wstring& e) {
  size_t eq = e.find(L'=', e.empty() ? 0 : 1);
  return eq == std::wstring::npos ? e : e.substr(0, eq);
}

// Parent environment merged with extraEnv (case-insensitive override), sorted,
// double-NUL terminated.
std::wstring buildEnvBlock(const Process::Env& extra) {
  std::vector<std::wstring> entries;
  if (wchar_t* block = ::GetEnvironmentStringsW()) {
    for (const wchar_t* p = block; *p;) {
      std::wstring e(p);
      p += e.size() + 1;
      entries.push_back(std::move(e));
    }
    ::FreeEnvironmentStringsW(block);
  }
  for (auto& [k, v] : extra) {
    std::wstring key = utf8ToWide(k);
    std::wstring ukey = upper(key);
    std::erase_if(entries, [&](const std::wstring& e) { return upper(envKey(e)) == ukey; });
    entries.push_back(key + L'=' + utf8ToWide(v));
  }
  std::sort(entries.begin(), entries.end(),
            [](const std::wstring& a, const std::wstring& b) { return upper(envKey(a)) < upper(envKey(b)); });
  std::wstring out;
  for (auto& e : entries) {
    out += e;
    out += L'\0';
  }
  out += L'\0';
  if (entries.empty()) out += L'\0';
  return out;
}

void closeHandle(void*& h) {
  if (h) {
    ::CloseHandle(h);
    h = nullptr;
  }
}

}  // namespace

Process::~Process() {
  if (running()) kill();
  closeHandle(stdinH_);
  closeHandle(stdoutH_);
  if (process_ && !exited_) wait(100);
  closeHandle(process_);
  closeHandle(job_);  // KILL_ON_JOB_CLOSE reaps anything the child left behind
}

bool Process::start(const std::string& exe, const std::vector<std::string>& args, const std::filesystem::path& cwd,
                    const std::filesystem::path& stderrLog, std::string* error, const Env& extraEnv,
                    const ProcessOptions& options) {
  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof sa;
  sa.bInheritHandle = TRUE;

  HANDLE inRead = nullptr, inWrite = nullptr, outRead = nullptr, outWrite = nullptr, errH = nullptr;
  auto cleanup = [&]() {
    for (HANDLE* h : {&inRead, &inWrite, &outRead, &outWrite, &errH})
      if (*h) {
        ::CloseHandle(*h);
        *h = nullptr;
      }
  };
  auto fail = [&](const char* what) {
    if (error) *error = lastErrorText(what);
    cleanup();
    return false;
  };

  if (!::CreatePipe(&inRead, &inWrite, &sa, 0)) return fail("CreatePipe(stdin)");
  if (!::CreatePipe(&outRead, &outWrite, &sa, 0)) return fail("CreatePipe(stdout)");
  // Only the child's ends may be inherited.
  if (!::SetHandleInformation(inWrite, HANDLE_FLAG_INHERIT, 0) ||
      !::SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0))
    return fail("SetHandleInformation");

  if (!options.mergeStderr && !stderrLog.empty())
    errH = ::CreateFileW(stderrLog.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  // mergeStderr: the child's stderr is the write end of the stdout pipe, no extra handle.
  if (!options.mergeStderr && (!errH || errH == INVALID_HANDLE_VALUE))
    errH = ::CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (errH == INVALID_HANDLE_VALUE) {
    errH = nullptr;
    return fail("open stderr");
  }

  std::wstring cmd;
  if (!options.rawCommandLine.empty()) {
    cmd = utf8ToWide(options.rawCommandLine);
  } else {
    appendQuoted(cmd, utf8ToWide(exe));
    for (auto& a : args) {
      cmd += L' ';
      appendQuoted(cmd, utf8ToWide(a));
    }
  }
  cmd.push_back(L'\0');  // CreateProcessW may modify the buffer
  std::wstring env = buildEnvBlock(extraEnv);

  HANDLE job = ::CreateJobObjectW(nullptr, nullptr);
  if (!job) return fail("CreateJobObject");
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION jl{};
  jl.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if (!::SetInformationJobObject(job, JobObjectExtendedLimitInformation, &jl, sizeof jl)) {
    DWORD e = ::GetLastError();
    ::CloseHandle(job);
    ::SetLastError(e);
    return fail("SetInformationJobObject");
  }

  STARTUPINFOW si{};
  si.cb = sizeof si;
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = inRead;
  si.hStdOutput = outWrite;
  si.hStdError = options.mergeStderr ? outWrite : errH;
  PROCESS_INFORMATION pi{};
  std::wstring wcwd = cwd.empty() ? std::wstring() : cwd.wstring();
  // Suspended so the child is in the job before it can spawn anything.
  BOOL ok = ::CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED, env.data(),
                             wcwd.empty() ? nullptr : wcwd.c_str(), &si, &pi);
  if (!ok) {
    DWORD e = ::GetLastError();
    ::CloseHandle(job);
    ::SetLastError(e);
    return fail("CreateProcess");
  }
  // Failure to join a job (e.g. already in a non-nestable job) is not fatal.
  ::AssignProcessToJobObject(job, pi.hProcess);
  ::ResumeThread(pi.hThread);
  ::CloseHandle(pi.hThread);

  ::CloseHandle(inRead);
  ::CloseHandle(outWrite);
  if (errH) ::CloseHandle(errH);
  tree_ = options.processTree;
  process_ = pi.hProcess;
  job_ = job;
  stdinH_ = inWrite;
  stdoutH_ = outRead;
  exited_ = false;
  exitCode_ = -1;
  return true;
}

bool Process::write(const char* data, size_t n) {
  while (n > 0) {
    DWORD w = 0;
    if (!stdinH_ || !::WriteFile(stdinH_, data, static_cast<DWORD>(std::min<size_t>(n, 1u << 20)), &w, nullptr))
      return false;
    data += w;
    n -= w;
  }
  return true;
}

long Process::read(char* buf, size_t n) {
  if (!stdoutH_) return -1;
  DWORD r = 0;
  if (!::ReadFile(stdoutH_, buf, static_cast<DWORD>(std::min<size_t>(n, 1u << 20)), &r, nullptr)) {
    return ::GetLastError() == ERROR_BROKEN_PIPE ? 0 : -1;
  }
  return static_cast<long>(r);
}

bool Process::running() const {
  if (!process_ || exited_) return false;
  if (::WaitForSingleObject(process_, 0) == WAIT_TIMEOUT) return true;
  DWORD code = 0;
  auto* self = const_cast<Process*>(this);
  self->exitCode_ = ::GetExitCodeProcess(process_, &code) ? static_cast<int>(code) : -1;
  self->exited_ = true;
  return false;
}

void Process::terminate() { kill(); }
void Process::kill() {
  if (tree_ && job_) ::TerminateJobObject(job_, 1);  // everything the child spawned, even after it exited
  else if (process_ && !exited_) ::TerminateProcess(process_, 1);
}

void Process::closeStdin() { closeHandle(stdinH_); }

int Process::wait(int timeoutMs) {
  if (!process_) return -1;
  if (exited_) return exitCode_;
  DWORD r = ::WaitForSingleObject(process_, timeoutMs < 0 ? INFINITE : static_cast<DWORD>(timeoutMs));
  if (r != WAIT_OBJECT_0) return -1;
  DWORD code = 0;
  exitCode_ = ::GetExitCodeProcess(process_, &code) ? static_cast<int>(code) : -1;
  exited_ = true;
  return exitCode_;
}

}  // namespace tf
