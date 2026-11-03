#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "term/Pty.h"
#include "term/VtScreen.h"

namespace tf::ui {

// One terminal: a Pty, its VtScreen and the plumbing between them (W8f, docs/ARCHITECTURE.md "Terminal panel").
//
// Threads. The Pty reader thread only appends the output to a byte queue (and asks the main thread to look at it); the
// VtScreen is touched on the main thread alone, so it needs no lock. The main thread feeds the queue in `drain()` with
// a time budget per call, so a 50 MB burst costs one slice per frame instead of freezing the UI; the reader blocks
// once kHighWater bytes are queued (backpressure, like a real terminal's output being throttled by a slow emulator).
// Input goes through a write queue served by a writer thread: Pty::write can block (a program that does not read, a
// large paste), the UI never does. The destructor kills the process tree (Pty::close) and joins both threads.
class TerminalSession {
 public:
  struct Hooks {
    std::function<void(std::function<void()>)> post;  // runs the function on the main thread (Editor::post)
    std::function<void()> wake;                       // optional; thread safe; makes an idle UI loop turn
  };
  TerminalSession(Hooks hooks, int rows, int cols, size_t scrollbackLines);
  ~TerminalSession();
  TerminalSession(const TerminalSession&) = delete;
  TerminalSession& operator=(const TerminalSession&) = delete;

  // Starts the program (options.rows/cols are replaced by the screen's size). False with *error on failure.
  bool start(PtyOptions options, std::string* error);

  // ---- main thread only ----
  VtScreen& screen() { return screen_; }
  const VtScreen& screen() const { return screen_; }
  // Feeds queued output for at most `budgetUs` microseconds (always at least one chunk). True when something was fed.
  // Normally called by the posted task; public so a test (or a caller without a pump) can drive it.
  bool drain(int budgetUs = kDrainBudgetUs);
  bool outputPending() const;  // output still queued (a drain ran out of budget)
  uint64_t bytesFed() const { return bytesFed_; }
  // Queues input bytes for the program (never blocks). Ignored once the program is gone.
  void write(std::string bytes);
  // Resizes the screen and tells the program (SIGWINCH / ResizePseudoConsole). No reflow.
  void resize(int rows, int cols);
  bool running() const { return pty_.running(); }
  bool exited() const { return exitReported_; }  // the program is gone and its output was fed (message included)
  int exitCode() const { return exitCode_; }
  // Called on the main thread after output was fed / when the program exited (after the message was written to the
  // screen). Set before start().
  std::function<void()> onOutput;
  std::function<void(int exitCode)> onExit;

  static constexpr int kDrainBudgetUs = 4000;
  static constexpr size_t kHighWater = 8u << 20;  // bytes queued before the reader thread waits

 private:
  struct Shared;
  void schedule();
  void startWriter();
  void stopWriter();

  VtScreen screen_;
  Pty pty_;
  Hooks hooks_;
  std::shared_ptr<Shared> shared_;
  uint64_t bytesFed_ = 0;
  bool exitReported_ = false;
  int exitCode_ = -1;

  // writer
  std::thread writer_;
  std::mutex wmu_;
  std::condition_variable wcv_;
  std::deque<std::string> wq_;
  std::atomic<bool> wstop_{false};
  bool writing_ = false;
};

}  // namespace tf::ui
