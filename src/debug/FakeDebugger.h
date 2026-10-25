#pragma once
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "debug/Debugger.h"

namespace tf {

// Test support (compiled into the tests, not into the library): a scriptable Debugger. All of its state lives in a
// FakeDebugger::State that tests share with the backend object, so a test can drive and inspect it from the main
// thread while the DebugSession's worker calls the Debugger methods, and can still release a call that is blocked
// after the session abandoned its worker.
//
// Default behaviour: connect -> Design; disconnect -> Disconnected; start / startWithoutDebugging / restart ->
// Running; stop -> Design; breakAll / stepOver / stepInto / stepOut -> Break at the statement set with
// State::setBreakStatement (else the previous one, else line 0 of "unknown"); runToCursor -> Break at the given
// location; setBreakpoint / removeBreakpoint edit the backend's list. Tests change anything directly through the
// State (as if the user acted in the debugger) and inject failures and blocking calls.
class FakeDebugger : public Debugger {
 public:
  struct Call {
    std::string name;
    std::string detail;  // arguments, e.g. "file:line" or "file:line:enabled:condition"
    std::thread::id thread;
  };

  struct State {
    // --- scripting (any thread) ---
    void setMode(DebugMode m, std::optional<DebugLocation> statement = std::nullopt);
    void setBreakStatement(DebugLocation l);
    void setLastError(std::string e);
    void injectBreakpoint(const Breakpoint& bp);  // appears / changes on the backend side
    void injectRemoveBreakpoint(const std::filesystem::path& file, int line);
    // The next `times` calls of `method` (a Debugger method name, "poll" included) fail with `error`; times < 0 =
    // always.
    void failNext(const std::string& method, std::string error, int times = 1);
    // Calls of `method` block until release(), cancel() (unless ignoreCancel) or `timeoutMs`; they then return
    // failure("cancelled" / "timed out") or, after release(), proceed normally.
    void blockOn(const std::string& method, int timeoutMs = 10000);
    void release();  // unblocks the blocked calls and stops blocking
    void ignoreCancel(bool b);

    // --- inspection (any thread) ---
    std::vector<Call> calls() const;
    std::vector<std::string> callNames() const;  // without poll
    size_t count(const std::string& method) const;
    std::vector<Call> callsOf(const std::string& method) const;
    std::vector<Breakpoint> breakpoints() const;
    DebugMode mode() const;
    int blockedNow() const;  // calls currently blocked
    bool cancelCalled() const;
    DebugWorkspaceInfo workspace() const;  // what the last connect() received
    bool destroyed() const;                // the FakeDebugger object was destroyed
    // Blocks the calling thread until pred() or timeout; wakes on every state change.
    bool waitUntil(const std::function<bool(const State&)>& pred, int timeoutMs) const;

    // --- used by FakeDebugger ---
    DebugResult enter(const std::string& method, const std::string& detail);  // records, blocks, fails per script

    mutable std::mutex mu;
    mutable std::condition_variable cv;
    std::vector<Call> log;
    DebugMode mode_ = DebugMode::Disconnected;
    std::optional<DebugLocation> statement_;
    DebugLocation breakAt_{"unknown", 0};
    std::vector<Breakpoint> bps_;
    std::string lastError_;
    std::map<std::string, std::pair<std::string, int>> failures_;
    std::map<std::string, int> blocking_;  // method -> timeout
    bool released_ = false, cancelled_ = false, ignoreCancel_ = false, destroyed_ = false;
    int blocked_ = 0;
    DebugWorkspaceInfo workspace_;
  };

  FakeDebugger() : st_(std::make_shared<State>()) {}
  explicit FakeDebugger(std::shared_ptr<State> st) : st_(std::move(st)) {}
  ~FakeDebugger() override;
  const std::shared_ptr<State>& state() const { return st_; }

  std::string name() const override { return "fake"; }
  DebugResult connect(const DebugWorkspaceInfo& workspace) override;
  DebugResult disconnect() override;
  DebugResult start() override;
  DebugResult startWithoutDebugging() override;
  DebugResult stop() override;
  DebugResult restart() override;
  DebugResult breakAll() override;
  DebugResult stepOver() override;
  DebugResult stepInto() override;
  DebugResult stepOut() override;
  DebugResult runToCursor(const std::filesystem::path& file, int line) override;
  DebugResult setBreakpoint(const Breakpoint& bp) override;
  DebugResult removeBreakpoint(const std::filesystem::path& file, int line) override;
  DebugResult poll(DebugSnapshot& out) override;
  void cancel() override;

 private:
  DebugResult go(const char* method, DebugMode to, bool breakAtStatement = false);
  std::shared_ptr<State> st_;
};

}  // namespace tf
