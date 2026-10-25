#include "debug/VsDebugger_win32.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "debug/ComDispatch_win32.h"
#include "debug/VsDteUtil.h"

// Visual Studio debugger backend over EnvDTE (late-bound IDispatch). Everything runs on the DebugSession worker
// thread (a single-threaded apartment). Mapping of the Debugger interface onto the automation model:
//   start / continue      DTE.Debugger.Go(false)
//   startWithoutDebugging DTE.ExecuteCommand("Debug.StartWithoutDebugging")
//   stop                  DTE.Debugger.Stop(false)            (a success when nothing is being debugged)
//   restart               DTE.ExecuteCommand("Debug.Restart")
//   breakAll              DTE.Debugger.Break(false)
//   stepOver/Into/Out     DTE.Debugger.StepOver/StepInto/StepOut(false)
//   runToCursor           opens the file in VS at the line (ItemOperations.OpenFile + TextSelection.GotoLine), then
//                         DTE.ExecuteCommand("Debug.RunToCursor") (the command acts on the active editor's caret,
//                         which is why the file is opened first; Debugger.RunToCursor takes no position)
//   breakpoints           DTE.Debugger.Breakpoints.Add / Item / Delete, Breakpoint.Enabled
//   poll                  CurrentMode, CurrentStackFrame.FileName / LineNumber (fallback when the frame cannot be
//                         read at all: ActiveDocument.FullName + Selection.CurrentLine), the breakpoint list
namespace tf {

namespace {

namespace fs = std::filesystem;
using com::Bstr;
using com::Dispatch;
using com::Variant;

constexpr int kLaunchWaitMs = 90000;

std::string pathUtf8(const fs::path& p) { return com::narrow(p.wstring()); }
fs::path pathFromUtf8(const std::string& s) { return fs::path(com::widen(s)); }

// ---- IMessageFilter: retry calls Visual Studio rejects while it is busy (modal dialog, build, solution load) ----
// Shared between the backend and its message filter (the filter is called by COM on the same thread, but it is
// reference counted and outlives nothing that matters; cancel() writes `cancelled` from another thread).
struct RetryState {
  std::atomic<bool> cancelled{false};
  std::atomic<unsigned long> limitMs{15000};  // per call: how long one call is retried while VS rejects it as busy
  std::atomic<unsigned long> deadline{0};     // GetTickCount() value after which nothing is retried; 0 = none
};

class RetryFilter final : public IMessageFilter {
 public:
  explicit RetryFilter(RetryState* state) : st_(state) {}

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
    if (riid == IID_IUnknown || riid == IID_IMessageFilter) {
      *ppv = static_cast<IMessageFilter*>(this);
      AddRef();
      return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return static_cast<ULONG>(++refs_); }
  ULONG STDMETHODCALLTYPE Release() override {
    ULONG n = static_cast<ULONG>(--refs_);
    if (n == 0) delete this;
    return n;
  }
  DWORD STDMETHODCALLTYPE HandleInComingCall(DWORD, HTASK, DWORD, LPINTERFACEINFO) override {
    return SERVERCALL_ISHANDLED;
  }
  // Returns the delay in ms before the call is retried, or -1 to give up (the caller then sees
  // RPC_E_CALL_REJECTED / RPC_E_SERVERCALL_RETRYLATER).
  DWORD STDMETHODCALLTYPE RetryRejectedCall(HTASK, DWORD tickCount, DWORD rejectType) override {
    const unsigned long dl = st_->deadline.load();
    const bool past = dl != 0 && static_cast<long>(GetTickCount() - dl) >= 0;
    return static_cast<DWORD>(
        retryDelayMs(rejectType == SERVERCALL_RETRYLATER, tickCount, st_->limitMs.load(), past, st_->cancelled.load()));
  }
  DWORD STDMETHODCALLTYPE MessagePending(HTASK, DWORD, DWORD) override { return PENDINGMSG_WAITDEFPROCESS; }

 private:
  std::atomic<long> refs_{1};
  RetryState* st_;
};

// Bounds the retry window for the duration of one poll().
struct PollWindow {
  RetryState& st;
  unsigned long restore;
  PollWindow(RetryState& s, unsigned long pollMs, unsigned long commandMs) : st(s), restore(commandMs) {
    unsigned long d = GetTickCount() + pollMs;
    st.deadline.store(d == 0 ? 1 : d);
    st.limitMs.store(pollMs);
  }
  ~PollWindow() {
    st.deadline.store(0);
    st.limitMs.store(restore);
  }
};

// Runs a command line and returns its stdout; nullopt when it cannot run or exceeds the timeout.
bool runCapture(const std::wstring& cmdline, DWORD timeoutMs, std::string& out, const std::atomic<bool>& cancelled) {
  SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
  HANDLE rd = nullptr, wr = nullptr;
  if (!CreatePipe(&rd, &wr, &sa, 0)) return false;
  SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
  STARTUPINFOW si{};
  si.cb = sizeof si;
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdOutput = wr;
  si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
  si.hStdInput = nullptr;
  PROCESS_INFORMATION pi{};
  std::wstring cl = cmdline;
  BOOL ok = CreateProcessW(nullptr, cl.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
  CloseHandle(wr);
  if (!ok) {
    CloseHandle(rd);
    return false;
  }
  const DWORD t0 = GetTickCount();
  bool done = false;
  for (;;) {
    DWORD avail = 0;
    if (PeekNamedPipe(rd, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
      char buf[4096];
      DWORD n = 0;
      if (ReadFile(rd, buf, sizeof buf, &n, nullptr) && n > 0) out.append(buf, n);
      continue;
    }
    if (WaitForSingleObject(pi.hProcess, 20) == WAIT_OBJECT_0) {
      done = true;
      // drain
      DWORD n = 0;
      char buf[4096];
      while (PeekNamedPipe(rd, nullptr, 0, nullptr, &avail, nullptr) && avail > 0 &&
             ReadFile(rd, buf, sizeof buf, &n, nullptr) && n > 0)
        out.append(buf, n);
      break;
    }
    if (cancelled.load() || GetTickCount() - t0 > timeoutMs) break;
  }
  if (!done) TerminateProcess(pi.hProcess, 1);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  CloseHandle(rd);
  return done;
}

class VsDebugger final : public Debugger {
 public:
  explicit VsDebugger(const VsBusyPolicy& policy) : policy_(policy) {
    retry_.limitMs.store(static_cast<unsigned long>(policy.commandMs));
  }
  ~VsDebugger() override {
    dte_ = Dispatch();
    if (filterRegistered_) {
      IMessageFilter* prev = nullptr;
      CoRegisterMessageFilter(nullptr, &prev);
      if (prev) prev->Release();
    }
    if (comInit_) CoUninitialize();
  }

  std::string name() const override { return "Visual Studio"; }

  DebugResult connect(const DebugWorkspaceInfo& workspace) override {
    ws_ = workspace;
    haveWs_ = true;
    return connectImpl(workspace.launchIfMissing);
  }

  DebugResult disconnect() override {
    dte_ = Dispatch();
    note_.clear();
    return DebugResult::success();
  }

  DebugResult start() override {
    return withDebugger("Debugger.Go", [](Dispatch& dbg, std::string& d) {
      return dbg.call(L"Go", {Variant::fromBool(false)}, nullptr, &d);
    });
  }
  DebugResult startWithoutDebugging() override { return executeCommand("Debug.StartWithoutDebugging"); }
  DebugResult stop() override {
    return withDebugger("Debugger.Stop", [](Dispatch& dbg, std::string& d) {
      long mode = 1;
      if (SUCCEEDED(dbg.getLong(L"CurrentMode", mode)) && modeFromDte(mode) == DebugMode::Design) return S_OK;
      return dbg.call(L"Stop", {Variant::fromBool(false)}, nullptr, &d);
    });
  }
  DebugResult restart() override { return executeCommand("Debug.Restart"); }
  DebugResult breakAll() override {
    return withDebugger("Debugger.Break", [](Dispatch& dbg, std::string& d) {
      return dbg.call(L"Break", {Variant::fromBool(false)}, nullptr, &d);
    });
  }
  DebugResult stepOver() override { return step(L"StepOver"); }
  DebugResult stepInto() override { return step(L"StepInto"); }
  DebugResult stepOut() override { return step(L"StepOut"); }

  DebugResult runToCursor(const fs::path& file, int line) override {
    DebugResult r = openInEditor(file, line, 0);
    if (!r) return r;
    return executeCommand("Debug.RunToCursor");
  }

  DebugResult openInEditor(const fs::path& file, int line, int column) override {
    DebugResult c = ensureConnected();
    if (!c) return c;
    std::string d;
    Dispatch ops;
    HRESULT hr = dte_.getObject(L"ItemOperations", ops, &d);
    if (SUCCEEDED(hr)) {
      Variant win;
      hr = ops.call(L"OpenFile", {Variant::fromString(pathUtf8(file))}, &win, &d);
      if (SUCCEEDED(hr)) {
        Dispatch w = win.toDispatch();
        if (w) w.call(L"Activate", {}, nullptr);  // best effort: bring the editor tab forward
      }
    }
    if (FAILED(hr)) return finish(hr, "ItemOperations.OpenFile", d);
    Dispatch doc, sel;
    hr = dte_.getObject(L"ActiveDocument", doc, &d);
    if (SUCCEEDED(hr)) hr = doc.getObject(L"Selection", sel, &d);
    if (FAILED(hr)) return finish(hr, "ActiveDocument.Selection", d);
    if (column > 0)
      hr = sel.call(L"MoveToLineAndOffset",
                    {Variant::fromLong(toVsLine(line)), Variant::fromLong(column + 1), Variant::fromBool(false)},
                    nullptr, &d);
    else hr = sel.call(L"GotoLine", {Variant::fromLong(toVsLine(line)), Variant::fromBool(false)}, nullptr, &d);
    return finish(hr, "TextSelection.GotoLine", d);
  }

  DebugResult setBreakpoint(const Breakpoint& bp) override {
    return withDebugger("Breakpoints.Add", [&](Dispatch& dbg, std::string& d) -> HRESULT {
      const std::string file = pathUtf8(bp.file);
      const int line1 = toVsLine(bp.line);
      std::vector<BpInfo> all;
      HRESULT hr = listBreakpoints(dbg, all, d);
      if (FAILED(hr)) return hr;
      bool exists = false;
      for (BpInfo& b : all) {
        if (!sameSpot(b, file, line1)) continue;
        if (b.condition == bp.condition) {
          exists = true;
          if (b.enabled != bp.enabled) {
            hr = b.obj.putProperty(L"Enabled", Variant::fromBool(bp.enabled), &d);
            if (FAILED(hr)) return hr;
          }
        } else {  // Breakpoint.Condition is read-only: replace
          hr = b.obj.call(L"Delete", {}, nullptr, &d);
          if (FAILED(hr)) return hr;
        }
      }
      if (exists) return S_OK;
      Dispatch bps;
      hr = dbg.getObject(L"Breakpoints", bps, &d);
      if (FAILED(hr)) return hr;
      // Add(Function, File, Line, Column, Condition, ConditionType, ...): the function is omitted, a condition is
      // "break when true"; the language of the condition stays at its default.
      std::vector<Variant> args = {Variant::missing(), Variant::fromString(file), Variant::fromLong(line1)};
      if (!bp.condition.empty()) {
        args.push_back(Variant::fromLong(1));  // Column
        args.push_back(Variant::fromString(bp.condition));
        args.push_back(Variant::fromLong(1));  // dbgBreakpointConditionTypeWhenTrue
      }
      hr = bps.call(L"Add", args, nullptr, &d);
      if (FAILED(hr)) return hr;
      if (!bp.enabled) {
        std::vector<BpInfo> after;
        if (SUCCEEDED(listBreakpoints(dbg, after, d))) {
          for (BpInfo& b : after)
            if (sameSpot(b, file, line1)) b.obj.putProperty(L"Enabled", Variant::fromBool(false), &d);
        }
      }
      return S_OK;
    });
  }

  DebugResult removeBreakpoint(const fs::path& file, int line) override {
    return withDebugger("Breakpoint.Delete", [&](Dispatch& dbg, std::string& d) -> HRESULT {
      std::vector<BpInfo> all;
      HRESULT hr = listBreakpoints(dbg, all, d);
      if (FAILED(hr)) return hr;
      const std::string f = pathUtf8(file);
      const int line1 = toVsLine(line);
      for (BpInfo& b : all) {
        if (!sameSpot(b, f, line1)) continue;
        hr = b.obj.call(L"Delete", {}, nullptr, &d);
        if (FAILED(hr)) return hr;
      }
      return S_OK;  // nothing there is a success
    });
  }

  DebugResult poll(DebugSnapshot& out) override {
    DebugSnapshot s;
    s.lastError = note_;
    if (!dte_) {
      s.mode = DebugMode::Disconnected;
      out = std::move(s);
      return DebugResult::success();
    }
    // A poll must never hold up queued commands: a Visual Studio stuck in a modal dialog rejects every call, so
    // the whole poll gets a short retry window (the session keeps its last snapshot on the failure).
    PollWindow window(retry_, static_cast<unsigned long>(policy_.pollMs),
                      static_cast<unsigned long>(policy_.commandMs));
    std::string d;
    Dispatch dbg;
    HRESULT hr = dte_.getObject(L"Debugger", dbg, &d);
    long mode = 0;
    if (SUCCEEDED(hr)) hr = dbg.getLong(L"CurrentMode", mode, &d);
    if (FAILED(hr)) {
      if (isServerDeadHr(hr)) {
        dte_ = Dispatch();
        note_ = "Visual Studio went away";
        s.mode = DebugMode::Disconnected;
        s.lastError = note_;
        out = std::move(s);
        return DebugResult::success();
      }
      return pollFailure("Debugger.CurrentMode", hr, d);
    }
    s.mode = modeFromDte(mode);
    if (s.mode == DebugMode::Break) {
      HRESULT fatal = S_OK;
      s.currentStatement = currentStatement(dbg, fatal);
      if (FAILED(fatal)) {
        if (isServerDeadHr(fatal)) return finish(fatal, "Debugger.CurrentStackFrame", d);
        return pollFailure("Debugger.CurrentStackFrame", fatal, d);
      }
    }
    std::vector<BpInfo> all;
    d.clear();
    hr = listBreakpoints(dbg, all, d);
    if (FAILED(hr)) {
      if (isServerDeadHr(hr)) return finish(hr, "Debugger.Breakpoints", d);
      return pollFailure("Debugger.Breakpoints", hr, d);
    }
    for (const BpInfo& b : all) {
      Breakpoint bp;
      bp.file = pathFromUtf8(b.file);
      bp.line = fromVsLine(b.line);
      bp.enabled = b.enabled;
      bp.condition = b.condition;
      s.breakpoints.push_back(std::move(bp));
    }
    out = std::move(s);
    return DebugResult::success();
  }

  void cancel() override { retry_.cancelled.store(true); }

 private:
  struct BpInfo {
    Dispatch obj;
    std::string file;
    int line = 0;  // 1-based as VS reports it
    bool enabled = true;
    std::string condition;
  };

  static bool sameSpot(const BpInfo& b, const std::string& file, int line1) {
    return b.line == line1 && solutionPathsEqual(b.file, file);  // Windows path semantics
  }

  // A failed poll: "Visual Studio is busy" when the calls were rejected for the whole retry window.
  DebugResult pollFailure(const char* what, HRESULT hr, const std::string& detail) {
    if (isServerBusyHr(hr))
      return DebugResult::failure("Visual Studio is busy (" + std::string(what) + " rejected; is a dialog open?)");
    return DebugResult::failure(describeHr(what, hr, detail));
  }

  DebugResult finish(HRESULT hr, const char* what, const std::string& detail) {
    if (SUCCEEDED(hr)) return DebugResult::success();
    std::string msg = describeHr(what, hr, detail);
    if (isServerDeadHr(hr)) {
      dte_ = Dispatch();
      note_ = "Visual Studio went away";
      msg += "; Visual Studio is gone, the next command reconnects";
    } else if (isServerBusyHr(hr)) {
      msg += "; Visual Studio is busy (a dialog is open?)";
    }
    return DebugResult::failure(std::move(msg));
  }

  DebugResult withDebugger(const char* what, const std::function<HRESULT(Dispatch&, std::string&)>& f) {
    DebugResult c = ensureConnected();
    if (!c) return c;
    std::string d;
    Dispatch dbg;
    HRESULT hr = dte_.getObject(L"Debugger", dbg, &d);
    if (SUCCEEDED(hr)) hr = f(dbg, d);
    return finish(hr, what, d);
  }

  DebugResult step(const wchar_t* method) {
    std::string what = "Debugger." + com::narrow(method, wcslen(method));
    return withDebugger(what.c_str(), [method](Dispatch& dbg, std::string& d) {
      return dbg.call(method, {Variant::fromBool(false)}, nullptr, &d);
    });
  }

  DebugResult executeCommand(const char* command) {
    DebugResult c = ensureConnected();
    if (!c) return c;
    std::string d;
    HRESULT hr = dte_.call(L"ExecuteCommand", {Variant::fromString(command)}, nullptr, &d);
    return finish(hr, command, d);
  }

  HRESULT listBreakpoints(Dispatch& dbg, std::vector<BpInfo>& out, std::string& detail) {
    Dispatch bps;
    HRESULT hr = dbg.getObject(L"Breakpoints", bps, &detail);
    if (FAILED(hr)) return hr;
    long count = 0;
    hr = bps.getLong(L"Count", count, &detail);
    if (FAILED(hr)) return hr;
    for (long i = 1; i <= count; ++i) {
      Variant item;
      hr = bps.call(L"Item", {Variant::fromLong(i)}, &item);
      if (isServerBusyHr(hr) || isServerDeadHr(hr)) return hr;  // never report a partial list
      if (FAILED(hr)) continue;
      BpInfo b;
      b.obj = item.toDispatch();
      if (!b.obj) continue;
      long line = 0;
      hr = b.obj.getString(L"File", b.file);
      if (isServerBusyHr(hr) || isServerDeadHr(hr)) return hr;
      if (FAILED(hr) || b.file.empty()) continue;  // function breakpoint etc.
      hr = b.obj.getLong(L"FileLine", line);
      if (isServerBusyHr(hr) || isServerDeadHr(hr)) return hr;
      if (FAILED(hr)) continue;
      b.line = static_cast<int>(line);
      hr = b.obj.getBool(L"Enabled", b.enabled);
      if (isServerBusyHr(hr) || isServerDeadHr(hr)) return hr;
      hr = b.obj.getString(L"Condition", b.condition);
      if (isServerBusyHr(hr) || isServerDeadHr(hr)) return hr;
      out.push_back(std::move(b));
    }
    return S_OK;
  }

  // The statement the debuggee is stopped at. The current stack frame is authoritative; only when it cannot be read
  // at all (an exception, not an empty file name = no source) the active document and its caret stand in.
  std::optional<DebugLocation> currentStatement(Dispatch& dbg, HRESULT& fatal) {
    Dispatch frame;
    HRESULT fhr = dbg.getObject(L"CurrentStackFrame", frame);
    if (isServerBusyHr(fhr) || isServerDeadHr(fhr)) {  // not "cannot be read": the answer is unknown
      fatal = fhr;
      return std::nullopt;
    }
    if (SUCCEEDED(fhr)) {
      std::string file;
      long line = 0;
      HRESULT h1 = frame.getString(L"FileName", file);
      HRESULT h2 = SUCCEEDED(h1) ? frame.getLong(L"LineNumber", line) : h1;
      if (isServerBusyHr(h2) || isServerDeadHr(h2)) {
        fatal = h2;
        return std::nullopt;
      }
      if (SUCCEEDED(h2)) {
        if (file.empty() || line <= 0) return std::nullopt;
        return DebugLocation{pathFromUtf8(file), fromVsLine(static_cast<int>(line))};
      }
    }
    Dispatch doc, sel;
    std::string file;
    long line = 0;
    if (SUCCEEDED(dte_.getObject(L"ActiveDocument", doc)) && SUCCEEDED(doc.getString(L"FullName", file)) &&
        SUCCEEDED(doc.getObject(L"Selection", sel)) && SUCCEEDED(sel.getLong(L"CurrentLine", line)) && !file.empty() &&
        line > 0)
      return DebugLocation{pathFromUtf8(file), fromVsLine(static_cast<int>(line))};
    return std::nullopt;
  }

  DebugResult initCom() {
    if (!comInit_) {
      HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
      if (FAILED(hr)) {
        return DebugResult::failure(describeHr(
            "CoInitializeEx", hr,
            hr == RPC_E_CHANGED_MODE ? "the thread is already in a multithreaded apartment" : std::string()));
      }
      comInit_ = true;
    }
    if (!filterRegistered_) {
      auto* f = new RetryFilter(&retry_);
      IMessageFilter* prev = nullptr;
      if (SUCCEEDED(CoRegisterMessageFilter(f, &prev))) filterRegistered_ = true;
      if (prev) prev->Release();
      f->Release();  // the registration holds its own reference
    }
    return DebugResult::success();
  }

  DebugResult ensureConnected() {
    if (dte_) return DebugResult::success();
    if (!haveWs_) return DebugResult::failure("not connected to Visual Studio");
    return connectImpl(true);  // a command: launching Visual Studio is allowed
  }

  bool sleepMs(int ms) {  // false when cancelled
    for (int waited = 0; waited < ms; waited += 50) {
      if (retry_.cancelled.load()) return false;
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return !retry_.cancelled.load();
  }

  std::string solutionOf(Dispatch& dte) {
    Dispatch sol;
    std::string name;
    if (SUCCEEDED(dte.getObject(L"Solution", sol))) sol.getString(L"FullName", name);
    return name;
  }

  // The newest instance whose open solution is `sln`; empty Dispatch when there is none (or it is still loading).
  Dispatch findInstanceWithSolution(const std::string& sln, std::vector<com::DteInstance>& all) {
    for (com::DteInstance& i : all) {
      if (solutionPathsEqual(solutionOf(i.dte), sln)) {
        long mode = 0;
        Dispatch dbg;
        if (SUCCEEDED(i.dte.getObject(L"Debugger", dbg)) && SUCCEEDED(dbg.getLong(L"CurrentMode", mode))) return i.dte;
      }
    }
    return Dispatch();
  }

  std::wstring findDevenv(std::string& error) {
    if (const char* env = std::getenv("TENFOLD_DEVENV"); env && *env) return com::widen(env);
    const char* pf = std::getenv("ProgramFiles(x86)");
    if (!pf || !*pf) pf = std::getenv("ProgramFiles");
    std::wstring vswhere =
        com::widen(pf ? pf : "C:\\Program Files (x86)") + L"\\Microsoft Visual Studio\\Installer\\vswhere.exe";
    if (GetFileAttributesW(vswhere.c_str()) == INVALID_FILE_ATTRIBUTES) {
      error = "Visual Studio is not installed (vswhere.exe not found)";
      return {};
    }
    std::string out;
    if (!runCapture(L"\"" + vswhere + L"\" -latest -property productPath", 20000, out, retry_.cancelled)) {
      error = "vswhere.exe failed";
      return {};
    }
    std::string line = firstOutputLine(out);
    if (line.empty()) {
      error = "vswhere.exe found no Visual Studio installation";
      return {};
    }
    return com::widen(line);
  }

  DebugResult connectImpl(bool launchIfMissing) {
    DebugResult init = initCom();
    if (!init) return init;
    dte_ = Dispatch();
    note_.clear();
    const std::string sln = ws_.solution.empty() ? std::string() : pathUtf8(ws_.solution);
    std::vector<com::DteInstance> instances = com::enumerateDteInstances();
    if (!sln.empty()) {
      Dispatch hit = findInstanceWithSolution(sln, instances);
      if (hit) {
        dte_ = hit;
        return DebugResult::success();
      }
    } else {
      if (instances.empty()) {
        const char* msg = "no running Visual Studio, and the workspace has no solution to open";
        return launchIfMissing ? DebugResult::failure(msg) : DebugResult::noInstance(msg);
      }
      dte_ = instances.front().dte;
      note_ = "workspace has no solution: attached to the newest running Visual Studio " +
              instances.front().info.versionText + " (pid " + std::to_string(instances.front().info.pid) + ")";
      return DebugResult::success();
    }
    if (!launchIfMissing) return DebugResult::noInstance("no running Visual Studio has " + sln + " open");
    // Launch devenv.exe "<sln>" and wait for it to register in the ROT and load the solution.
    std::string err;
    std::wstring devenv = findDevenv(err);
    if (devenv.empty()) return DebugResult::failure(err);
    std::wstring cmd = L"\"" + devenv + L"\" \"" + ws_.solution.wstring() + L"\"";
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi))
      return DebugResult::failure("cannot start " + com::narrow(devenv) + " (error " + std::to_string(GetLastError()) +
                                  ")");
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kLaunchWaitMs);
    for (;;) {
      if (!sleepMs(500)) return DebugResult::failure("cancelled while waiting for Visual Studio to start");
      std::vector<com::DteInstance> now = com::enumerateDteInstances();
      Dispatch hit = findInstanceWithSolution(sln, now);
      if (hit) {
        dte_ = hit;
        return DebugResult::success();
      }
      if (std::chrono::steady_clock::now() > deadline)
        return DebugResult::failure("Visual Studio did not open " + sln + " within " +
                                    std::to_string(kLaunchWaitMs / 1000) + " s");
    }
  }

  DebugWorkspaceInfo ws_;
  bool haveWs_ = false;
  bool comInit_ = false;
  bool filterRegistered_ = false;
  Dispatch dte_;
  std::string note_;  // reported as DebugSnapshot::lastError until the next connect
  VsBusyPolicy policy_;
  RetryState retry_;
};

}  // namespace

std::unique_ptr<Debugger> createVisualStudioDebugger(const VsBusyPolicy& policy) {
  return std::make_unique<VsDebugger>(policy);
}

}  // namespace tf
