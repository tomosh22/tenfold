#pragma once
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "debug/Breakpoints.h"
#include "debug/Debugger.h"

namespace tf {

class Editor;

// The editor's side of debugging (C2a): owns a Debugger backend, runs every call into it on its own worker thread
// (a command queue plus a periodic poll) and delivers every result through Editor::post(), so all state below is
// main-thread state that the front end reads after pump().
//
// Everything here is called on the main thread (the one that pumps the Editor). Commands are asynchronous: they
// return as soon as they are queued (false when they cannot even be queued) and run in order on the worker; the
// optional `done` callback runs on the pump thread with the backend's result. Failures are also kept in lastError().
//
// Breakpoint sync is two-way and Breakpoints (owned by the Editor) is the store:
//  * tenfold -> backend: every change of Breakpoints (toggle, condition, edits that move lines, renames) is diffed
//    against what was last told to the backend and pushed as removeBreakpoint / setBreakpoint, removals first.
//  * backend -> tenfold: when a poll snapshot reports breakpoints that differ from what the backend was last told,
//    Breakpoints is updated to match (the backend wins for state it reports). A snapshot is only trusted when the
//    backend has executed every breakpoint command that was queued, so a poll that overtook a push never undoes it,
//    and applying a snapshot never pushes back (no echo).
// On connect the workspace's breakpoints are pushed and the ones only the backend has are adopted.
class DebugSession {
 public:
  using Done = std::function<void(const DebugResult&)>;
  using Factory = std::function<std::unique_ptr<Debugger>()>;

  explicit DebugSession(Editor& editor);
  ~DebugSession();  // shutdown(): never blocks longer than the shutdown timeout
  DebugSession(const DebugSession&) = delete;
  DebugSession& operator=(const DebugSession&) = delete;

  // --- backend ---
  // How connect() creates the backend (Editor::setDebuggerFactory). Without a factory or a backend the session
  // reports "no debugger backend on this platform".
  void setFactory(Factory f) { factory_ = std::move(f); }
  // Installs `d` directly (tests); replaces and shuts down a running one. Null removes it.
  void setBackend(std::unique_ptr<Debugger> d);
  bool hasBackend() const { return backend_ || impl_ || factory_; }

  // --- commands (queued onto the worker; false and lastError() when impossible) ---
  // Everything except connect / disconnect connects first when the session is not connected yet.
  bool connect(Done done = {});
  bool disconnect(Done done = {});
  bool start(Done done = {});
  bool startWithoutDebugging(Done done = {});
  bool stop(Done done = {});
  bool restart(Done done = {});
  bool breakAll(Done done = {});
  bool stepOver(Done done = {});
  bool stepInto(Done done = {});
  bool stepOut(Done done = {});
  bool runToCursor(const std::filesystem::path& file, int line, Done done = {});  // 0-based line

  // --- state (main thread; moves only inside pump()) ---
  DebugMode mode() const { return mode_; }
  bool connected() const { return connected_; }
  // The statement the debuggee is stopped at; nullopt outside Break mode.
  const std::optional<DebugLocation>& currentStatement() const { return statement_; }
  const std::string& lastError() const { return error_; }
  void clearError();
  // One line for the status bar: "Debugger: <backend>: <mode>[ at file:line][ - error]", or the "no backend" text.
  std::string status() const;
  // Bumped by every change of anything above (mode, statement, error, connection, breakpoints adopted from the
  // backend); front ends repaint the status bar / gutter when it moves.
  uint64_t generation() const { return gen_; }

  // --- settings-like ---
  // In Break mode with a current statement the editor jumps there through Editor::gotoLocation (so Back returns).
  bool followCurrentStatement = true;
  // The .sln given to connect(); by default the workspace's first solution file.
  void setSolutionPath(std::filesystem::path p) { solution_ = std::move(p); }
  // Poll intervals: fast while Running / Break, slow otherwise (and while Design). Constants unless tests change them.
  static constexpr int kDefaultFastPollMs = 100;
  static constexpr int kDefaultSlowPollMs = 1000;
  static constexpr int kDefaultShutdownTimeoutMs = 3000;
  void setPollIntervals(int fastMs, int slowMs);
  // How long shutdown() waits for a blocked backend call (after Debugger::cancel()) before abandoning the worker
  // thread. An abandoned worker finishes on its own and touches nothing of the editor.
  void setShutdownTimeout(int ms) { shutdownMs_ = ms; }

  // Stops the worker (disconnecting a connected backend on that thread) and delivers nothing afterwards.
  void shutdown();

 private:
  struct Impl;
  struct Cmd;
  enum class Kind { Normal, Connect, Disconnect, Breakpoint };

  bool ensureBackend();
  void startWorker();
  void stopWorker();
  bool submit(const char* what, std::function<DebugResult(Debugger&)> fn, Done done, Kind kind = Kind::Normal);
  bool command(const char* what, std::function<DebugResult(Debugger&)> fn, Done done);
  void setError(std::string e);
  void touch() { ++gen_; }
  void commandFinished(const char* what, Kind kind, const DebugResult& r, uint64_t epoch, const Done& done);
  void applySnapshot(const DebugSnapshot& s, uint64_t epoch, uint64_t bpExecuted);
  void adoptBackendBreakpoints(const std::vector<Breakpoint>& backend);
  void pushBreakpoints();
  void onBreakpointsChanged() { pushBreakpoints(); }
  void setMode(DebugMode m);

  Editor& ed_;
  Factory factory_;
  std::unique_ptr<Debugger> backend_;  // installed, not yet handed to a worker
  std::string backendName_;
  std::shared_ptr<Impl> impl_;  // the running worker
  std::shared_ptr<int> token_;  // posted callbacks hold a weak_ptr; gone when the worker is stopped
  std::filesystem::path solution_;
  int fastMs_ = kDefaultFastPollMs, slowMs_ = kDefaultSlowPollMs, shutdownMs_ = kDefaultShutdownTimeoutMs;

  // Main-thread state.
  DebugMode mode_ = DebugMode::Disconnected;
  std::optional<DebugLocation> statement_;
  std::string error_, backendError_;
  bool connectRequested_ = false, connected_ = false;
  uint64_t epoch_ = 0;  // bumped by connect() / disconnect(); snapshots of an older epoch are stale
  uint64_t gen_ = 0;
  // Breakpoint sync.
  using Key = std::pair<std::string, int>;
  std::map<Key, Breakpoint> known_;  // what the backend was last told (and, after a trusted snapshot, has)
  uint64_t bpEnqueued_ = 0;
  bool applying_ = false;
};

}  // namespace tf
