#include "term/Pty.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>

#include "term/PtyBackend.h"

namespace tf {

Pty::Pty() = default;
Pty::~Pty() { close(); }

std::string Pty::defaultShell(const std::string& configured) {
  if (!configured.empty()) return configured;
#ifdef _WIN32
  const char* cs = std::getenv("ComSpec");
  return (cs && *cs) ? cs : "cmd.exe";
#else
  const char* sh = std::getenv("SHELL");
  return (sh && *sh) ? sh : "/bin/sh";
#endif
}

std::vector<std::string> Pty::splitCommand(const std::string& commandLine) {
  std::vector<std::string> out;
  std::string cur;
  bool inQuote = false, have = false;
  for (char c : commandLine) {
    if (c == '"') {
      inQuote = !inQuote;
      have = true;
    } else if ((c == ' ' || c == '\t') && !inQuote) {
      if (have) out.push_back(std::move(cur));
      cur.clear();
      have = false;
    } else {
      cur += c;
      have = true;
    }
  }
  if (have) out.push_back(std::move(cur));
  return out;
}

bool Pty::available() { return Backend::available(); }

bool Pty::start(const PtyOptions& options, DataCallback onData, ExitCallback onExit, std::string* error) {
  close();
  PtyOptions o = options;
  if (o.program.empty()) {
    auto parts = splitCommand(defaultShell());
    if (parts.empty()) {
      if (error) *error = "no shell";
      return false;
    }
    o.program = parts[0];
    o.args.assign(parts.begin() + 1, parts.end());
  }
  o.rows = std::max(1, o.rows);
  o.cols = std::max(1, o.cols);
  auto be = std::make_unique<Backend>();
  if (!be->start(o, error)) return false;
  backend_ = std::move(be);
  onData_ = std::move(onData);
  onExit_ = std::move(onExit);
  closing_ = false;
  stopReader_ = false;
  exited_ = false;
  exitCode_ = -1;
  reader_ = std::thread([this] { readerLoop(); });
  return true;
}

void Pty::readerLoop() {
  Backend& be = *backend_;
  char buf[16384];
  auto emit = [&](long n) {
    if (n > 0 && !closing_ && onData_) onData_(buf, static_cast<size_t>(n));
  };
  bool eof = false;
  int code = -1;
  while (!stopReader_) {
    if (!eof) {
      long n = be.read(buf, sizeof buf, 25);
      if (n > 0) {
        emit(n);
        continue;
      }
      if (n < 0) eof = true;
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (be.pollExit(&code)) {
      // The program is gone: deliver what is still buffered (bounded: a surviving grandchild may keep the line open).
      if (!eof) {
        for (int i = 0; i < 200; ++i) {
          long n = be.read(buf, sizeof buf, 20);
          if (n <= 0) break;
          emit(n);
        }
      }
      exitCode_ = code;
      exited_ = true;
      if (!closing_ && onExit_) onExit_(code);
      // Keep draining until close(): the pipe must not fill up (Windows ClosePseudoConsole waits for it).
      while (!stopReader_) {
        long n = be.read(buf, sizeof buf, 25);
        if (n < 0) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        else emit(n);
      }
      return;
    }
  }
}

bool Pty::write(const char* data, size_t n) {
  if (!backend_ || closing_) return false;
  return backend_->write(data, n);
}

bool Pty::resize(int rows, int cols) {
  if (!backend_ || closing_) return false;
  return backend_->resize(std::max(1, rows), std::max(1, cols));
}

int Pty::wait(int timeoutMs) {
  auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs < 0 ? 0x7fffffff : timeoutMs);
  while (!exited_) {
    if (!backend_) return exitCode_;
    if (timeoutMs >= 0 && std::chrono::steady_clock::now() >= deadline) return -1;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return exitCode_;
}

void Pty::close() {
  if (!backend_) return;
  closing_ = true;
  Backend& be = *backend_;
  int code = -1;
  be.killTree(false);
  for (int i = 0; i < 60 && !be.pollExit(&code); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(5));
  be.killTree(true);  // also stragglers that outlived the leader
  be.shutdownIo();
  stopReader_ = true;
  if (reader_.joinable()) reader_.join();
  for (int i = 0; i < 400 && !be.pollExit(&code); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(5));
  if (!exited_) {
    exitCode_ = be.pollExit(&code) ? code : -1;
    exited_ = true;
  }
  backend_.reset();
  onData_ = nullptr;
  onExit_ = nullptr;
}

}  // namespace tf
