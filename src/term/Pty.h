#pragma once
#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace tf {

// A shell (or any program) running in a pseudo-terminal: POSIX openpt/fork/setsid/TIOCSCTTY, Windows ConPTY
// (CreatePseudoConsole, loaded dynamically; STARTUPINFOEX + a kill-on-close job object). UI-agnostic: output is read on
// a reader thread and handed to a callback, which the owner typically posts to its main thread and feeds to a
// VtScreen. This header must not include <windows.h>. See docs/ARCHITECTURE.md ("Terminal engine").

struct PtyOptions {
  // Program to run (looked up in PATH) and its arguments. Empty program = Pty::defaultShell().
  std::string program;
  std::vector<std::string> args;
  std::filesystem::path cwd;  // working directory (the workspace root); empty = inherit
  int rows = 24, cols = 80;
  // Added to (overriding) the inherited environment. TERM=xterm-256color and COLORTERM=truecolor are set on POSIX
  // unless given here.
  std::vector<std::pair<std::string, std::string>> env;
};

class Pty {
 public:
  // Called on the reader thread with each chunk of output (raw bytes, UTF-8 and escape sequences). Must not call
  // close() or destroy the Pty.
  using DataCallback = std::function<void(const char* data, size_t n)>;
  // Called once on the reader thread when the program has exited by itself and its remaining output was delivered
  // (never after close()). The code is the exit status, or 128 + signal on POSIX.
  using ExitCallback = std::function<void(int exitCode)>;

  Pty();
  ~Pty();  // close()
  Pty(const Pty&) = delete;
  Pty& operator=(const Pty&) = delete;

  // Starts the program. Returns false with *error set on failure (unknown program, no ConPTY, ...). A Pty is started
  // once; close() then start() again is allowed.
  bool start(const PtyOptions& options, DataCallback onData, ExitCallback onExit, std::string* error);

  // Blocking write of input bytes (keystrokes, VtScreen replies). False when the program is gone.
  bool write(const char* data, size_t n);
  bool write(const std::string& s) { return write(s.data(), s.size()); }

  // Tells the terminal its new size (SIGWINCH / ResizePseudoConsole). The VtScreen must be resized to match.
  bool resize(int rows, int cols);

  // Kills the whole process tree (POSIX: the session's process group; Windows: the job object), closes the terminal and
  // joins the reader thread. No callback runs after it returns. Idempotent.
  void close();

  bool started() const { return backend_ != nullptr; }
  bool running() const { return backend_ && !exited_; }
  bool exited() const { return exited_; }
  int exitCode() const { return exitCode_; }  // -1 until the program has exited
  // Waits for the program to exit (it is the reader thread that notices); returns the exit code, -1 on timeout.
  int wait(int timeoutMs);

  // The shell to start: `configured` (the TerminalCommand setting; may carry arguments, see splitCommand) when not
  // empty, else %ComSpec% (cmd.exe) on Windows, $SHELL or /bin/sh on POSIX.
  static std::string defaultShell(const std::string& configured = {});
  // Splits a command line ("cmd /c agent", "\"C:\\Program Files\\x.exe\" -l") into program and arguments; double
  // quotes group, no other escapes.
  static std::vector<std::string> splitCommand(const std::string& commandLine);
  // False on a Windows without ConPTY (before 1809). Always true on POSIX.
  static bool available();

 private:
  struct Backend;  // platform part: Pty_posix.cpp / Pty_win32.cpp
  void readerLoop();

  std::unique_ptr<Backend> backend_;
  std::thread reader_;
  DataCallback onData_;
  ExitCallback onExit_;
  std::atomic<bool> closing_{false};     // callbacks suppressed
  std::atomic<bool> stopReader_{false};  // reader thread leaves its loop
  std::atomic<bool> exited_{false};
  std::atomic<int> exitCode_{-1};
};

}  // namespace tf
