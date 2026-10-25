#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "debug/Debugger.h"

namespace tf {

// Pure helpers of the Visual Studio (EnvDTE) debugger backend (C2b). No COM and no Windows headers: they compile and
// are unit-tested on every platform; VsDebugger_win32.cpp uses them.

// A Running Object Table entry of a Visual Studio instance: "!VisualStudio.DTE.17.0:12345".
struct DteMoniker {
  std::vector<int> version;  // {17, 0}
  std::string versionText;   // "17.0"
  uint32_t pid = 0;
  bool operator==(const DteMoniker& o) const { return version == o.version && pid == o.pid; }
};

// Parses a ROT display name; nullopt for anything that is not a `!VisualStudio.DTE.<ver>:<pid>` moniker (other
// "!VisualStudio.*" monikers, a missing pid, ...). Case-insensitive on the prefix.
std::optional<DteMoniker> parseDteMoniker(const std::string& displayName);

// True when `a` is a newer Visual Studio than `b` (component-wise numeric version compare, then the higher pid).
bool dteNewer(const DteMoniker& a, const DteMoniker& b);

// Sorts newest first.
void sortDteNewestFirst(std::vector<DteMoniker>& v);

// Solution.FullName comparison: Windows semantics whatever the host (separators, case, "." / ".." segments, repeated
// and trailing separators, the \\?\ long-path prefix).
std::string normalizeSolutionPath(const std::string& path);
bool solutionPathsEqual(const std::string& a, const std::string& b);

// Lines: the engine is 0-based, Visual Studio 1-based.
inline int toVsLine(int line0) { return line0 + 1; }
inline int fromVsLine(int line1) { return line1 > 0 ? line1 - 1 : 0; }

// EnvDTE.dbgDebugMode: dbgDesignMode = 1, dbgBreakMode = 2, dbgRunMode = 3. Anything else counts as Design.
DebugMode modeFromDte(long dbgDebugMode);

// HRESULT classes (values spelled out so the helpers need no Windows headers).
bool isServerBusyHr(long hr);  // RPC_E_CALL_REJECTED, RPC_E_SERVERCALL_RETRYLATER: retry later
bool isServerDeadHr(long hr);  // RPC_E_DISCONNECTED, RPC_S_SERVER_UNAVAILABLE, ...: the instance is gone

// How long calls are retried while Visual Studio rejects them as busy (RPC_E_SERVERCALL_RETRYLATER).
struct VsBusyPolicy {
  int commandMs = 15000;  // per call of a command / connect
  int pollMs = 1000;      // the whole of one poll(): a VS stuck in a modal dialog must not delay queued commands
};

// The message filter's decision for a rejected call: the delay in ms before the retry, or -1 to give up. `elapsedMs`
// is how long the call has been pending, `pastDeadline` is set when the poll window is over.
long retryDelayMs(bool retryLater, unsigned long elapsedMs, unsigned long limitMs, bool pastDeadline, bool cancelled);

// First non-empty line of vswhere's output, trimmed; empty when there is none.
std::string firstOutputLine(const std::string& output);

// "what failed (0x80010108): detail" for error messages.
std::string describeHr(const std::string& what, long hr, const std::string& detail = {});

}  // namespace tf
