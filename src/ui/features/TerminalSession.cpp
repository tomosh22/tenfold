#include "ui/features/TerminalSession.h"

#include <algorithm>
#include <chrono>

namespace tf::ui {

// State shared with the reader thread and the posted tasks. `owner` is only read and written on the main thread (the
// destructor clears it), so a task that outlives the session finds it null and does nothing.
struct TerminalSession::Shared {
  std::mutex mu;
  std::condition_variable cv;
  std::deque<std::string> chunks;
  size_t bytes = 0;
  bool posted = false;  // a task is queued (or running) that will look at `chunks` / `exited`
  bool closing = false;
  bool exited = false;
  int code = -1;
  TerminalSession* owner = nullptr;
};

TerminalSession::TerminalSession(Hooks hooks, int rows, int cols, size_t scrollbackLines)
    : screen_(rows, cols, scrollbackLines), hooks_(std::move(hooks)), shared_(std::make_shared<Shared>()) {
  shared_->owner = this;
  screen_.setReplyCallback([this](const std::string& s) { write(s); });
}

TerminalSession::~TerminalSession() {
  {
    std::lock_guard<std::mutex> lk(shared_->mu);
    shared_->closing = true;
    shared_->owner = nullptr;
  }
  shared_->cv.notify_all();  // a reader blocked on the high-water mark leaves its callback
  stopWriter();              // kills the tree first when the writer is stuck in a blocking write
  pty_.close();              // kills the process tree, joins the reader: no callback runs afterwards
}

bool TerminalSession::start(PtyOptions options, std::string* error) {
  options.rows = screen_.rows();
  options.cols = screen_.cols();
  auto sh = shared_;
  auto wake = hooks_.wake;
  const bool ok = pty_.start(
      options,
      [this, sh](const char* data, size_t n) {
        std::unique_lock<std::mutex> lk(sh->mu);
        sh->cv.wait(lk, [&] { return sh->bytes < kHighWater || sh->closing; });
        if (sh->closing) return;
        if (!sh->chunks.empty() && sh->chunks.back().size() < (64u << 10)) sh->chunks.back().append(data, n);
        else sh->chunks.emplace_back(data, n);
        sh->bytes += n;
        const bool post = !sh->posted;
        sh->posted = true;
        lk.unlock();
        if (post) schedule();
      },
      [this, sh](int code) {
        std::unique_lock<std::mutex> lk(sh->mu);
        if (sh->closing) return;
        sh->exited = true;
        sh->code = code;
        const bool post = !sh->posted;
        sh->posted = true;
        lk.unlock();
        if (post) schedule();
      },
      error);
  if (ok) startWriter();
  return ok;
}

// Any thread. The task holds the shared state, not the session: the session may be gone when it runs.
void TerminalSession::schedule() {
  auto sh = shared_;
  if (hooks_.post)
    hooks_.post([sh] {
      TerminalSession* s = nullptr;
      {
        std::lock_guard<std::mutex> lk(sh->mu);
        s = sh->owner;
      }
      if (s) s->drain();
    });
  if (hooks_.wake) hooks_.wake();
}

bool TerminalSession::outputPending() const {
  std::lock_guard<std::mutex> lk(shared_->mu);
  return !shared_->chunks.empty();
}

bool TerminalSession::drain(int budgetUs) {
  using clock = std::chrono::steady_clock;
  const auto t0 = clock::now();
  bool fed = false;
  for (;;) {
    std::string chunk;
    {
      std::lock_guard<std::mutex> lk(shared_->mu);
      if (shared_->chunks.empty()) break;
      chunk = std::move(shared_->chunks.front());
      shared_->chunks.pop_front();
      shared_->bytes -= chunk.size();
    }
    shared_->cv.notify_all();  // room for the reader
    screen_.feed(chunk);
    bytesFed_ += chunk.size();
    fed = true;
    if (std::chrono::duration_cast<std::chrono::microseconds>(clock::now() - t0).count() >= budgetUs) break;
  }
  bool again = false, report = false;
  int code = -1;
  {
    std::lock_guard<std::mutex> lk(shared_->mu);
    if (!shared_->chunks.empty()) again = true;  // out of budget: a later task continues; `posted` stays set
    else {
      shared_->posted = false;
      if (shared_->exited && !exitReported_) {
        report = true;
        code = shared_->code;
      }
    }
  }
  if (again) schedule();
  if (fed && onOutput) onOutput();
  if (report) {
    exitReported_ = true;
    exitCode_ = code;
    // The final screen stays; the message goes on a line of its own below the output.
    std::string msg;
    if (screen_.cursorCol() != 0) msg += "\r\n";
    msg += "\x1b[0m\x1b[2m[process exited with code " + std::to_string(code) + "]\x1b[0m\r\n";
    screen_.feed(msg);
    if (onOutput) onOutput();
    if (onExit) onExit(code);
  }
  return fed;
}

void TerminalSession::write(std::string bytes) {
  if (bytes.empty() || exitReported_) return;
  {
    std::lock_guard<std::mutex> lk(wmu_);
    if (wstop_) return;
    wq_.push_back(std::move(bytes));
  }
  wcv_.notify_one();
}

void TerminalSession::startWriter() {
  writer_ = std::thread([this] {
    std::unique_lock<std::mutex> lk(wmu_);
    for (;;) {
      wcv_.wait(lk, [&] { return wstop_ || !wq_.empty(); });
      if (wstop_) return;
      std::string s = std::move(wq_.front());
      wq_.pop_front();
      writing_ = true;
      lk.unlock();
      // May block; the main thread never waits for it. Chunks, so that a stop request is noticed between writes (one
      // huge write(2) into a pty that echoes it can run for seconds even after the program was killed).
      for (size_t off = 0; off < s.size() && !wstop_; off += 8192)
        if (!pty_.write(s.data() + off, std::min<size_t>(8192, s.size() - off))) break;
      lk.lock();
      writing_ = false;
    }
  });
}

void TerminalSession::stopWriter() {
  if (!writer_.joinable()) return;
  bool busy;
  {
    std::lock_guard<std::mutex> lk(wmu_);
    wstop_ = true;
    busy = writing_;
  }
  wcv_.notify_all();
  if (busy) pty_.kill();  // a write blocked on a program that does not read returns once the tree is dead
  writer_.join();
}

void TerminalSession::resize(int rows, int cols) {
  if (rows < 1 || cols < 1) return;
  if (rows == screen_.rows() && cols == screen_.cols()) return;
  screen_.resize(rows, cols);
  pty_.resize(rows, cols);
}

}  // namespace tf::ui
