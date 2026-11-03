// ConPTY backend. CreatePseudoConsole and friends are resolved with GetProcAddress so the binary still starts (and the
// mingw toolchain still compiles) on systems without them; Pty::available() reports that.
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

#include "term/PtyBackend.h"

#ifndef PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
#define PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE 0x00020016
#endif

namespace tf {

namespace {

using HPCON_ = void*;
using CreatePseudoConsoleFn = HRESULT(WINAPI*)(COORD, HANDLE, HANDLE, DWORD, HPCON_*);
using ResizePseudoConsoleFn = HRESULT(WINAPI*)(HPCON_, COORD);
using ClosePseudoConsoleFn = void(WINAPI*)(HPCON_);

struct ConPtyApi {
  CreatePseudoConsoleFn create = nullptr;
  ResizePseudoConsoleFn resize = nullptr;
  ClosePseudoConsoleFn close = nullptr;
};

const ConPtyApi& api() {
  static const ConPtyApi a = [] {
    ConPtyApi r;
    HMODULE k = ::GetModuleHandleW(L"kernel32.dll");
    if (k) {
      r.create =
          reinterpret_cast<CreatePseudoConsoleFn>(reinterpret_cast<void*>(::GetProcAddress(k, "CreatePseudoConsole")));
      r.resize =
          reinterpret_cast<ResizePseudoConsoleFn>(reinterpret_cast<void*>(::GetProcAddress(k, "ResizePseudoConsole")));
      r.close =
          reinterpret_cast<ClosePseudoConsoleFn>(reinterpret_cast<void*>(::GetProcAddress(k, "ClosePseudoConsole")));
    }
    if (!r.create || !r.resize || !r.close) r = ConPtyApi{};
    return r;
  }();
  return a;
}

std::wstring utf8ToWide(const std::string& s) {
  if (s.empty()) return {};
  int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  if (n <= 0) return {};
  std::wstring w(static_cast<size_t>(n), L'\0');
  ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
  return w;
}

std::string lastError(const char* what) {
  return std::string(what) + " failed (error " + std::to_string(::GetLastError()) + ")";
}

// One argument quoted per the CommandLineToArgvW rules.
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
  cmd.append(backslashes * 2, L'\\');
  cmd += L'"';
}

std::wstring upper(std::wstring s) {
  for (auto& c : s) c = static_cast<wchar_t>(std::towupper(c));
  return s;
}

std::wstring envKey(const std::wstring& e) {
  size_t eq = e.find(L'=', e.empty() ? 0 : 1);
  return eq == std::wstring::npos ? e : e.substr(0, eq);
}

std::wstring buildEnvBlock(const std::vector<std::pair<std::string, std::string>>& extra) {
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

void closeH(void*& h) {
  if (h) {
    ::CloseHandle(h);
    h = nullptr;
  }
}

}  // namespace

bool Pty::Backend::available() { return api().create != nullptr; }

Pty::Backend::~Backend() {
  if (job) ::TerminateJobObject(job, 1);
  shutdownIo();
  closeH(inWrite);
  closeH(outRead);
  closeH(process);
  closeH(job);  // KILL_ON_JOB_CLOSE reaps anything left
}

bool Pty::Backend::start(const PtyOptions& o, std::string* error) {
  const ConPtyApi& a = api();
  if (!a.create) {
    if (error) *error = "ConPTY is not available on this system";
    return false;
  }
  // The pipe ends are not inheritable and CreateProcess runs with bInheritHandles = FALSE (the pseudo console attribute
  // provides the child's standard handles), so a Process::start on another thread cannot leak them either way.
  HANDLE inRead = nullptr, inW = nullptr, outR = nullptr, outWrite = nullptr;
  auto closeLocal = [&]() {
    for (HANDLE* h : {&inRead, &inW, &outR, &outWrite})
      if (*h) {
        ::CloseHandle(*h);
        *h = nullptr;
      }
  };
  if (!::CreatePipe(&inRead, &inW, nullptr, 0) || !::CreatePipe(&outR, &outWrite, nullptr, 0)) {
    if (error) *error = lastError("CreatePipe");
    closeLocal();
    return false;
  }
  COORD size;
  size.X = static_cast<SHORT>(o.cols);
  size.Y = static_cast<SHORT>(o.rows);
  HPCON_ hpc = nullptr;
  HRESULT hr = a.create(size, inRead, outWrite, 0, &hpc);
  if (FAILED(hr)) {
    if (error) *error = "CreatePseudoConsole failed (0x" + std::to_string(static_cast<unsigned long>(hr)) + ")";
    closeLocal();
    return false;
  }
  closeH(inRead);  // the pseudo console owns its duplicates
  closeH(outWrite);

  HANDLE jobH = ::CreateJobObjectW(nullptr, nullptr);
  if (jobH) {
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION jl{};
    jl.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    ::SetInformationJobObject(jobH, JobObjectExtendedLimitInformation, &jl, sizeof jl);
  }
  auto fail = [&](const char* what) {
    if (error) *error = lastError(what);
    a.close(hpc);
    closeLocal();
    if (jobH) ::CloseHandle(jobH);
    return false;
  };
  if (!jobH) return fail("CreateJobObject");

  SIZE_T attrSize = 0;
  ::InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
  std::vector<char> attrStorage(attrSize);
  STARTUPINFOEXW si{};
  si.StartupInfo.cb = sizeof si;
  si.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrStorage.data());
  // The child must get the pseudo console's standard handles, never ours. A parent whose own stdio is redirected (a CI
  // job, a pipe) otherwise passes those on through the process parameters and the child writes to the real console and
  // reads the real stdin, bypassing ConPTY. STARTF_USESTDHANDLES with invalid handles (what wezterm and node-pty do;
  // bInheritHandles stays FALSE) makes conhost attach the pseudo console's handles instead.
  si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
  si.StartupInfo.hStdInput = INVALID_HANDLE_VALUE;
  si.StartupInfo.hStdOutput = INVALID_HANDLE_VALUE;
  si.StartupInfo.hStdError = INVALID_HANDLE_VALUE;
  if (!::InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &attrSize))
    return fail("InitializeProcThreadAttributeList");
  if (!::UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, hpc, sizeof(HPCON_),
                                   nullptr, nullptr)) {
    DWORD e = ::GetLastError();
    ::DeleteProcThreadAttributeList(si.lpAttributeList);
    ::SetLastError(e);
    return fail("UpdateProcThreadAttribute");
  }

  std::wstring cmd;
  appendQuoted(cmd, utf8ToWide(o.program));
  for (auto& arg : o.args) {
    cmd += L' ';
    appendQuoted(cmd, utf8ToWide(arg));
  }
  cmd.push_back(L'\0');
  std::wstring env = buildEnvBlock(o.env);
  std::wstring cwd = o.cwd.empty() ? std::wstring() : o.cwd.wstring();

  PROCESS_INFORMATION pi{};
  BOOL ok = ::CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                             EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED, env.data(),
                             cwd.empty() ? nullptr : cwd.c_str(), &si.StartupInfo, &pi);
  DWORD createErr = ::GetLastError();
  ::DeleteProcThreadAttributeList(si.lpAttributeList);
  if (!ok) {
    ::SetLastError(createErr);
    return fail("CreateProcess");
  }
  ::AssignProcessToJobObject(jobH, pi.hProcess);  // tolerated to fail: the process is still terminated directly
  ::ResumeThread(pi.hThread);
  ::CloseHandle(pi.hThread);
  process = pi.hProcess;
  job = jobH;
  pseudo = hpc;
  inWrite = inW;
  outRead = outR;
  return true;
}

long Pty::Backend::read(char* buf, size_t n, int timeoutMs) {
  // Polling: after the program exits ConPTY does not necessarily close the output pipe until ClosePseudoConsole, so a
  // blocking ReadFile could wait forever. The wait on the process handle doubles as the sleep.
  DWORD avail = 0;
  if (!::PeekNamedPipe(outRead, nullptr, 0, nullptr, &avail, nullptr)) return -1;
  if (avail == 0) {
    DWORD w = ::WaitForSingleObject(process, static_cast<DWORD>(std::min(std::max(timeoutMs, 0), 10)));
    if (w == WAIT_OBJECT_0) {
      if (!::PeekNamedPipe(outRead, nullptr, 0, nullptr, &avail, nullptr)) return -1;
      if (avail == 0) {
        ::Sleep(static_cast<DWORD>(std::min(std::max(timeoutMs, 0), 10)));  // exited: do not spin
        return 0;
      }
    } else {
      return 0;
    }
  }
  DWORD got = 0;
  if (!::ReadFile(outRead, buf, static_cast<DWORD>(std::min<size_t>(n, avail)), &got, nullptr)) return -1;
  return static_cast<long>(got);
}

bool Pty::Backend::write(const char* data, size_t n) {
  while (n > 0) {
    DWORD w = 0;
    if (!::WriteFile(inWrite, data, static_cast<DWORD>(std::min<size_t>(n, 1 << 16)), &w, nullptr)) return false;
    data += w;
    n -= w;
  }
  return true;
}

bool Pty::Backend::resize(int rows, int cols) {
  const ConPtyApi& a = api();
  if (!a.resize || !pseudo) return false;
  COORD size;
  size.X = static_cast<SHORT>(cols);
  size.Y = static_cast<SHORT>(rows);
  return SUCCEEDED(a.resize(pseudo, size));
}

bool Pty::Backend::pollExit(int* code) {
  std::lock_guard<std::mutex> lk(exitMu);
  if (!exitedFlag && process) {
    DWORD c = 0;
    if (::WaitForSingleObject(process, 0) == WAIT_OBJECT_0 && ::GetExitCodeProcess(process, &c)) {
      exitedFlag = true;
      exitCodeValue = static_cast<int>(c);
    }
  }
  if (exitedFlag && code) *code = exitCodeValue;
  return exitedFlag;
}

void Pty::Backend::killTree(bool force) {
  if (!force) return;  // there is no gentle hang-up for a console: ClosePseudoConsole (shutdownIo) delivers it
  if (job) ::TerminateJobObject(job, 1);
  else if (process) ::TerminateProcess(process, 1);
}

void Pty::Backend::shutdownIo() {
  if (pseudo) {
    // The reader thread keeps draining the output pipe meanwhile (ClosePseudoConsole may wait for it to be empty).
    api().close(pseudo);
    pseudo = nullptr;
  }
}

}  // namespace tf
