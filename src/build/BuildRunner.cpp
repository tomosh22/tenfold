#include "build/BuildRunner.h"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <thread>

#include "semantic/CompileCommands.h"

namespace tf {

namespace fs = std::filesystem;

namespace {

std::string quoteForDisplay(const std::string& a) {
  if (!a.empty() && a.find_first_of(" \t\"") == std::string::npos) return a;
  std::string q = "\"";
  for (char c : a) {
    if (c == '"') q += '\\';
    q += c;
  }
  return q + "\"";
}

BuildCommand direct(std::string exe, std::vector<std::string> args, fs::path cwd) {
  BuildCommand c;
  c.display = quoteForDisplay(exe);
  for (auto& a : args) c.display += " " + quoteForDisplay(a);
  c.exe = std::move(exe);
  c.args = std::move(args);
  c.cwd = std::move(cwd);
  return c;
}

fs::path firstExisting(const fs::path& dir, std::initializer_list<const char*> names) {
  std::error_code ec;
  for (auto n : names)
    if (fs::is_regular_file(dir / n, ec)) return dir / n;
  return {};
}

}  // namespace

bool detectBuildCommand(const Workspace& ws, const BuildOverrides& o, BuildCommand* out, std::string* error) {
  const fs::path root = ws.root();
  const auto& st = ws.settings();
  const std::string cfg = o.configuration.empty() ? st.vsConfiguration : o.configuration;
  const std::string plat = o.platform.empty() ? st.vsPlatform : o.platform;

  const std::string& custom = !o.command.empty() ? o.command : st.buildCommand;
  if (!custom.empty()) {
    BuildCommand c;
    c.shell = custom;
    c.display = custom;
    c.cwd = root;
    *out = std::move(c);
    return true;
  }
  // Visual Studio: the solution, else a lone project file in the root.
  fs::path msb;
  auto slns = ws.solutionFiles();
  if (!slns.empty()) msb = slns.front();
  if (msb.empty()) {
    std::error_code ec;
    for (auto& e : fs::directory_iterator(root, ec))
      if (e.is_regular_file(ec) && e.path().extension() == ".vcxproj") msb = e.path();
  }
  if (!msb.empty()) {
    *out = direct("msbuild", {msb.string(), "/m", "/p:Configuration=" + cfg, "/p:Platform=" + plat}, root);
    return true;
  }
  if (ws.hasCMake()) {
    fs::path dir = o.buildDir;
    if (dir.empty() && !st.buildDir.empty()) dir = st.buildDir;
    if (dir.empty()) {
      // The same build tree clangd's compile database comes from, when it is a real CMake build directory.
      std::error_code ec;
      if (auto cc = CompileDatabase::find(root); cc && fs::exists(cc->parent_path() / "CMakeCache.txt", ec))
        dir = cc->parent_path();
      else dir = ws.cacheDir() / "cmake-build";
    }
    if (dir.is_relative()) dir = root / dir;
    *out = direct("cmake", {"--build", dir.string(), "--config", cfg}, root);
    return true;
  }
  if (!firstExisting(root, {"Makefile", "makefile", "GNUmakefile"}).empty()) {
    unsigned cores = std::max(1u, std::thread::hardware_concurrency());
    *out = direct("make", {"-j" + std::to_string(cores)}, root);
    return true;
  }
  if (error) *error = "no build command: set \"buildCommand\" in .tenfold/workspace.json";
  return false;
}

// ---------------------------------------------------------------------------

namespace {
struct Callbacks {
  BuildRunner::Lines onLines;
  BuildRunner::Done onDone;
};
}  // namespace

struct BuildRunner::Job {
  Process proc;
  std::thread reader;
  // After cancel() a second thread keeps re-sending the kill until the output pipe reaches EOF. A single
  // kill(-pgid) can miss a child the shell was in the middle of forking (fork is slow on macOS: the build
  // "started" line is seen, the cancel lands during `sleep 120 &`'s fork, the new child escapes the signal and
  // keeps the pipe open for its whole life). killMu orders the final kill against the leader being reaped, so
  // a recycled process-group id is never signalled.
  std::thread killer;
  std::mutex killMu;
  bool eof = false;  // guarded by killMu
  std::atomic<bool> cancelled{false};
  std::atomic<bool> finished{false};
};

BuildRunner::BuildRunner(Post post) : post_(std::move(post)) {}

BuildRunner::~BuildRunner() {
  cancel();
  reap(true);
}

void BuildRunner::reap(bool all) {
  std::vector<std::shared_ptr<Job>> take;
  {
    std::lock_guard<std::mutex> lk(mu_);
    if (all && current_) {
      retired_.push_back(std::move(current_));
      current_.reset();
    }
    for (auto it = retired_.begin(); it != retired_.end();) {
      if (all || (*it)->finished) {
        take.push_back(std::move(*it));
        it = retired_.erase(it);
      } else {
        ++it;
      }
    }
  }
  for (auto& j : take) {
    if (j->reader.joinable()) j->reader.join();
    if (j->killer.joinable()) j->killer.join();
  }
}

void BuildRunner::cancel() {
  std::lock_guard<std::mutex> lk(mu_);
  if (!current_) return;
  current_->cancelled = true;
  current_->proc.kill();  // the whole tree: process group / job object
  if (!current_->killer.joinable()) {
    Job* j = current_.get();
    j->killer = std::thread([j] {
      while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        std::lock_guard<std::mutex> lk(j->killMu);
        if (j->eof) return;
        j->proc.kill();
      }
    });
  }
}

bool BuildRunner::running() const {
  std::lock_guard<std::mutex> lk(mu_);
  return current_ && !current_->finished;
}

bool BuildRunner::start(const BuildCommand& cmd, Lines onLines, Done onDone, std::string* error) {
  cancel();
  {
    std::lock_guard<std::mutex> lk(mu_);
    if (current_) retired_.push_back(std::move(current_));
    current_.reset();
  }
  reap(false);

  auto job = std::make_shared<Job>();
  ProcessOptions po;
  po.mergeStderr = true;
  po.processTree = true;
  std::string exe = cmd.exe;
  std::vector<std::string> args = cmd.args;
  if (!cmd.shell.empty()) {
#ifdef _WIN32
    exe = "cmd.exe";
    po.rawCommandLine = "cmd.exe /s /c \"" + cmd.shell + "\"";  // /s: strip exactly the outer quotes
#else
    exe = "/bin/sh";
    args = {"-c", cmd.shell};
#endif
  }
  std::string err;
  if (!job->proc.start(exe, args, cmd.cwd, {}, &err, {}, po)) {
    if (error) *error = "cannot start '" + cmd.display + "': " + err;
    return false;
  }
  job->proc.closeStdin();
  {
    std::lock_guard<std::mutex> lk(mu_);
    current_ = job;
  }

  auto cb = std::make_shared<Callbacks>(Callbacks{std::move(onLines), std::move(onDone)});
  Post post = post_;
  const auto t0 = std::chrono::steady_clock::now();
  Job* j = job.get();
  job->reader = std::thread([j, cb, post, t0] {
    std::string pending;
    char buf[16384];
    auto flush = [&](std::vector<std::string> lines) {
      if (lines.empty() || j->cancelled) return;  // a cancelled build delivers no further output
      post([cb, lines = std::move(lines)]() mutable {
        if (cb->onLines) cb->onLines(std::move(lines));
      });
    };
    while (true) {
      long n = j->proc.read(buf, sizeof buf);
      if (n <= 0) break;
      pending.append(buf, static_cast<size_t>(n));
      std::vector<std::string> lines;
      size_t start = 0, nl;
      while ((nl = pending.find('\n', start)) != std::string::npos) {
        size_t end = nl;
        if (end > start && pending[end - 1] == '\r') --end;
        lines.emplace_back(pending, start, end - start);
        start = nl + 1;
      }
      pending.erase(0, start);
      flush(std::move(lines));
    }
    if (!pending.empty()) {
      if (pending.back() == '\r') pending.pop_back();
      flush({std::move(pending)});
    }
    {
      std::lock_guard<std::mutex> lk(j->killMu);
      j->eof = true;  // before the leader is reaped (and its process-group id becomes reusable)
    }
    BuildResult r;
    r.exitCode = j->proc.wait(-1);
    r.cancelled = j->cancelled;
    r.elapsedMs = static_cast<long>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count());
    post([cb, r] {
      if (cb->onDone) cb->onDone(r);
    });
    j->finished = true;
  });
  return true;
}

}  // namespace tf
