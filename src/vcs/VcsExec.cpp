#include "vcs/VcsExec.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>

#include "core/Platform.h"

namespace tf {

namespace fs = std::filesystem;

ExecResult runCommand(const ExecOptions& opt) {
  ExecResult res;
  static std::atomic<unsigned> counter{0};
  std::error_code ec;
  const fs::path errFile = fs::temp_directory_path(ec) / ("tenfold_vcs_" + std::to_string(currentProcessId()) + "_" +
                                                          std::to_string(counter++) + ".err");
  Process p;
  std::string startError;
  if (!p.start(opt.exe, opt.args, opt.cwd, errFile, &startError, opt.env)) {
    res.err = startError.empty() ? "cannot start " + opt.exe : startError;
    return res;
  }
  res.started = true;
  p.closeStdin();

  // Watchdog: kills the child when it overstays, which also ends the blocking read below.
  std::mutex mu;
  std::condition_variable cv;
  bool done = false;
  std::atomic<bool> timedOut{false};
  std::thread dog;
  if (opt.timeoutMs > 0) {
    dog = std::thread([&] {
      std::unique_lock<std::mutex> lk(mu);
      if (!cv.wait_for(lk, std::chrono::milliseconds(opt.timeoutMs), [&] { return done; })) {
        timedOut = true;
        p.kill();
      }
    });
  }

  char buf[16384];
  while (true) {
    long n = p.read(buf, sizeof buf);
    if (n <= 0) break;
    res.out.append(buf, static_cast<size_t>(n));
  }
  res.exitCode = p.wait(opt.timeoutMs > 0 ? opt.timeoutMs : -1);
  {
    std::lock_guard<std::mutex> lk(mu);
    done = true;
  }
  cv.notify_all();
  if (dog.joinable()) dog.join();
  res.timedOut = timedOut;

  {
    std::ifstream in(errFile, std::ios::binary);
    if (in) {
      std::stringstream ss;
      ss << in.rdbuf();
      res.err = ss.str();
    }
  }
  fs::remove(errFile, ec);
  if (res.timedOut) res.err += (res.err.empty() ? "" : "\n") + opt.exe + " timed out";
  else if (res.exitCode == 127 && res.out.empty() && res.err.empty()) res.err = "cannot run " + opt.exe;
  return res;
}

std::filesystem::path pathFromUtf8(const std::string& s) {
  return fs::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
}

std::string pathToUtf8(const std::filesystem::path& p) {
  const std::u8string u = p.generic_u8string();
  return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}

}  // namespace tf
