#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "debug/Breakpoints.h"

namespace tf {

// The debugger backend interface (C2a). The Visual Studio EnvDTE backend (C2b) implements it; tests use FakeDebugger.
//
// Threading: a DebugSession owns the backend and calls every method on ONE worker thread of its own, one call at a
// time (never the main thread, never concurrently), so a backend may keep thread-affine state (a COM apartment).
// The single exception is cancel(), which may be called from any thread while another call is blocked.
// Errors: nothing throws across this boundary; a failed call returns {false, text}.
// Lines are 0-based like every Position in the engine (Visual Studio counts from 1: convert in the backend).
// File paths are absolute.

enum class DebugMode {
  Disconnected,  // not connected to a debugger / debugger instance gone
  Design,        // connected, nothing is being debugged
  Running,       // the debuggee runs
  Break,         // the debuggee is stopped (breakpoint, step, break all)
};

inline const char* toString(DebugMode m) {
  switch (m) {
    case DebugMode::Disconnected: return "Disconnected";
    case DebugMode::Design: return "Design";
    case DebugMode::Running: return "Running";
    case DebugMode::Break: return "Break";
  }
  return "?";
}

struct DebugResult {
  bool ok = true;
  std::string error;  // set when !ok
  static DebugResult success() { return {}; }
  static DebugResult failure(std::string e) { return {false, std::move(e)}; }
  explicit operator bool() const { return ok; }
};

// What connect() is told about the workspace.
struct DebugWorkspaceInfo {
  std::filesystem::path root;
  std::filesystem::path solution;  // the .sln of the workspace; empty when it has none
};

struct DebugLocation {
  std::filesystem::path file;
  int line = 0;  // 0-based
  bool operator==(const DebugLocation& o) const { return file == o.file && line == o.line; }
  bool operator!=(const DebugLocation& o) const { return !(*this == o); }
};

// The state of the backend at one instant.
struct DebugSnapshot {
  DebugMode mode = DebugMode::Disconnected;
  std::optional<DebugLocation> currentStatement;  // meaningful in Break mode
  std::vector<Breakpoint> breakpoints;            // every line breakpoint the backend has, as it has them
  std::string lastError;                          // the backend's latest asynchronous problem; empty when none
};

class Debugger {
 public:
  virtual ~Debugger() = default;

  // Short name for the status bar ("Visual Studio 2022", "fake"). Called once on the main thread when the backend is
  // installed, before any worker call: it must be a cheap constant (no COM, no I/O).
  virtual std::string name() const = 0;

  // Attaches to (or launches) the debugger instance for the workspace. May take seconds. After a successful connect
  // poll() reports Design / Running / Break; after disconnect() or a lost instance, Disconnected.
  virtual DebugResult connect(const DebugWorkspaceInfo& workspace) = 0;
  virtual DebugResult disconnect() = 0;

  virtual DebugResult start() = 0;                  // start debugging (F5): also continues from Break
  virtual DebugResult startWithoutDebugging() = 0;  // Ctrl+F5
  virtual DebugResult stop() = 0;
  virtual DebugResult restart() = 0;
  virtual DebugResult breakAll() = 0;
  virtual DebugResult stepOver() = 0;
  virtual DebugResult stepInto() = 0;
  virtual DebugResult stepOut() = 0;
  virtual DebugResult runToCursor(const std::filesystem::path& file, int line) = 0;

  // Creates the breakpoint, or updates enabled / condition of the one at (file, line): idempotent.
  virtual DebugResult setBreakpoint(const Breakpoint& bp) = 0;
  // Removing a breakpoint that does not exist is a success.
  virtual DebugResult removeBreakpoint(const std::filesystem::path& file, int line) = 0;

  // Current state. On failure `out` is untouched and the session reports the error (it keeps the last mode).
  virtual DebugResult poll(DebugSnapshot& out) = 0;

  // Optional extra: shows the file in the debugger's own editor with the caret at (line, column), both 0-based (the
  // Visual Studio backend: "Open in Visual Studio at the caret"). Same threading as every other call. Default: not
  // supported.
  virtual DebugResult openInEditor(const std::filesystem::path& /*file*/, int /*line*/, int /*column*/) {
    return DebugResult::failure("not supported by this debugger backend");
  }

  // Optional, THE ONLY call allowed from another thread, concurrently with any other call: asks the call that is
  // currently blocked to return soon (a failure result is fine). Called when the session shuts down; the backend
  // must tolerate it with nothing blocked.
  virtual void cancel() {}
};

}  // namespace tf
