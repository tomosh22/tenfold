#pragma once
#include <filesystem>

namespace tf {

// <user data dir>/tenfold/crashes: %LOCALAPPDATA% on Windows, ~/Library/Application Support on macOS,
// $XDG_DATA_HOME or ~/.local/share elsewhere. Empty when no home directory can be found.
std::filesystem::path defaultCrashDir();

// Installs the process crash handler (call once, early in main; the mains of tenfold-cli and tenfold-ui do, the
// tests do not).
//  * Windows: SetUnhandledExceptionFilter writes crash-<pid>-<tick>.dmp (a minidump through dbghelp.dll, loaded
//    dynamically) to `dir`; an abort() writes one too.
//  * POSIX: SIGSEGV/SIGBUS/SIGFPE/SIGILL/SIGABRT run on an alternate stack and write crash-<pid>-<time>.txt (signal,
//    fault address, backtrace_symbols_fd where available) using only async-signal-safe calls, then re-raise the
//    signal with the default action so the exit status and core dump are unchanged.
// `dir` empty means defaultCrashDir(). The directory is created now, not in the handler. Returns false if no
// directory is available (the handler is then not installed). Nothing is ever uploaded.
bool installCrashHandler(const std::filesystem::path& dir = {});

// D3: tells the handler where the diagnostic log (core/Log.h) is so the crash report can name it ("log: <path>";
// on Windows a crash-<pid>-<tick>.txt next to the minidump). Only the path is recorded: the handler never flushes
// the log, because that takes locks and allocates, which is not async-signal-safe, so up to one flush interval
// (500 ms) of records can be missing after a crash. An empty path clears it.
void setCrashLogPath(const std::filesystem::path& logFile);

}  // namespace tf
