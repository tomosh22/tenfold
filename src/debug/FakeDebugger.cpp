#include "debug/FakeDebugger.h"

#include <chrono>

#include "core/PathUtil.h"

namespace tf {

using Lock = std::unique_lock<std::mutex>;

// ---- State: scripting ----

void FakeDebugger::State::setMode(DebugMode m, std::optional<DebugLocation> statement) {
  {
    Lock lk(mu);
    mode_ = m;
    statement_ = std::move(statement);
  }
  cv.notify_all();
}

void FakeDebugger::State::setBreakStatement(DebugLocation l) {
  Lock lk(mu);
  breakAt_ = std::move(l);
}

void FakeDebugger::State::setLastError(std::string e) {
  {
    Lock lk(mu);
    lastError_ = std::move(e);
  }
  cv.notify_all();
}

void FakeDebugger::State::injectBreakpoint(const Breakpoint& bp) {
  {
    Lock lk(mu);
    for (Breakpoint& b : bps_)
      if (b.line == bp.line && breakpointFileKey(b.file) == breakpointFileKey(bp.file)) {
        b = bp;
        cv.notify_all();
        return;
      }
    bps_.push_back(bp);
  }
  cv.notify_all();
}

void FakeDebugger::State::injectRemoveBreakpoint(const std::filesystem::path& file, int line) {
  {
    Lock lk(mu);
    for (size_t i = 0; i < bps_.size(); ++i)
      if (bps_[i].line == line && breakpointFileKey(bps_[i].file) == breakpointFileKey(file)) {
        bps_.erase(bps_.begin() + static_cast<std::ptrdiff_t>(i));
        break;
      }
  }
  cv.notify_all();
}

void FakeDebugger::State::failNext(const std::string& method, std::string error, int times) {
  Lock lk(mu);
  failures_[method] = {std::move(error), times};
}

void FakeDebugger::State::blockOn(const std::string& method, int timeoutMs) {
  Lock lk(mu);
  blocking_[method] = timeoutMs;
  released_ = false;
}

void FakeDebugger::State::release() {
  {
    Lock lk(mu);
    released_ = true;
  }
  cv.notify_all();
}

void FakeDebugger::State::setNoInstance(bool b) {
  {
    Lock lk(mu);
    noInstance_ = b;
  }
  cv.notify_all();
}

std::vector<bool> FakeDebugger::State::connectLaunchFlags() const {
  Lock lk(mu);
  return connectFlags_;
}

void FakeDebugger::State::ignoreCancel(bool b) {
  Lock lk(mu);
  ignoreCancel_ = b;
}

// ---- State: inspection ----

std::vector<FakeDebugger::Call> FakeDebugger::State::calls() const {
  Lock lk(mu);
  return log;
}

std::vector<std::string> FakeDebugger::State::callNames() const {
  Lock lk(mu);
  std::vector<std::string> out;
  for (const Call& c : log)
    if (c.name != "poll") out.push_back(c.name);
  return out;
}

size_t FakeDebugger::State::count(const std::string& method) const {
  Lock lk(mu);
  size_t n = 0;
  for (const Call& c : log) n += c.name == method;
  return n;
}

std::vector<FakeDebugger::Call> FakeDebugger::State::callsOf(const std::string& method) const {
  Lock lk(mu);
  std::vector<Call> out;
  for (const Call& c : log)
    if (c.name == method) out.push_back(c);
  return out;
}

std::vector<Breakpoint> FakeDebugger::State::breakpoints() const {
  Lock lk(mu);
  return bps_;
}

DebugMode FakeDebugger::State::mode() const {
  Lock lk(mu);
  return mode_;
}

int FakeDebugger::State::blockedNow() const {
  Lock lk(mu);
  return blocked_;
}

bool FakeDebugger::State::cancelCalled() const {
  Lock lk(mu);
  return cancelled_;
}

bool FakeDebugger::State::destroyed() const {
  Lock lk(mu);
  return destroyed_;
}

DebugWorkspaceInfo FakeDebugger::State::workspace() const {
  Lock lk(mu);
  return workspace_;
}

bool FakeDebugger::State::waitUntil(const std::function<bool(const State&)>& pred, int timeoutMs) const {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
  while (true) {
    if (pred(*this)) return true;  // reads through the locking accessors
    if (std::chrono::steady_clock::now() >= deadline) return false;
    Lock lk(mu);
    cv.wait_for(lk, std::chrono::milliseconds(5));
  }
}

DebugResult FakeDebugger::State::enter(const std::string& method, const std::string& detail) {
  Lock lk(mu);
  log.push_back({method, detail, std::this_thread::get_id()});
  cv.notify_all();
  auto b = blocking_.find(method);
  if (b != blocking_.end() && !released_) {
    ++blocked_;
    cv.notify_all();
    const bool woke = cv.wait_for(lk, std::chrono::milliseconds(b->second),
                                  [&] { return released_ || (cancelled_ && !ignoreCancel_); });
    --blocked_;
    cv.notify_all();
    if (!woke) return DebugResult::failure("timed out");
    if (!released_) return DebugResult::failure("cancelled");
  }
  auto f = failures_.find(method);
  if (f != failures_.end() && f->second.second != 0) {
    std::string e = f->second.first;
    if (f->second.second > 0) --f->second.second;
    return DebugResult::failure(std::move(e));
  }
  return DebugResult::success();
}

// ---- FakeDebugger ----

namespace {
std::string describe(const Breakpoint& b) {
  return b.file.generic_string() + ":" + std::to_string(b.line) + ":" + (b.enabled ? "1" : "0") + ":" + b.condition;
}
}  // namespace

FakeDebugger::~FakeDebugger() {
  {
    Lock lk(st_->mu);
    st_->destroyed_ = true;
  }
  st_->cv.notify_all();
}

DebugResult FakeDebugger::go(const char* method, DebugMode to, bool breakAtStatement) {
  DebugResult r = st_->enter(method, "");
  if (!r.ok) return r;
  {
    Lock lk(st_->mu);
    st_->mode_ = to;
    if (breakAtStatement) st_->statement_ = st_->breakAt_;
    else if (to != DebugMode::Break) st_->statement_.reset();
  }
  st_->cv.notify_all();
  return r;
}

DebugResult FakeDebugger::connect(const DebugWorkspaceInfo& workspace) {
  DebugResult r = st_->enter("connect", workspace.solution.generic_string());
  if (!r.ok) return r;
  {
    Lock lk(st_->mu);
    st_->connectFlags_.push_back(workspace.launchIfMissing);
    if (st_->noInstance_) {
      if (!workspace.launchIfMissing) return DebugResult::noInstance("no debugger instance has the solution open");
      st_->noInstance_ = false;  // launched
    }
    st_->workspace_ = workspace;
    if (st_->mode_ == DebugMode::Disconnected) st_->mode_ = DebugMode::Design;
  }
  st_->cv.notify_all();
  return r;
}

DebugResult FakeDebugger::disconnect() { return go("disconnect", DebugMode::Disconnected); }
DebugResult FakeDebugger::start() { return go("start", DebugMode::Running); }
DebugResult FakeDebugger::startWithoutDebugging() { return go("startWithoutDebugging", DebugMode::Running); }
DebugResult FakeDebugger::stop() { return go("stop", DebugMode::Design); }
DebugResult FakeDebugger::restart() { return go("restart", DebugMode::Running); }
DebugResult FakeDebugger::breakAll() { return go("breakAll", DebugMode::Break, true); }
DebugResult FakeDebugger::stepOver() { return go("stepOver", DebugMode::Break, true); }
DebugResult FakeDebugger::stepInto() { return go("stepInto", DebugMode::Break, true); }
DebugResult FakeDebugger::stepOut() { return go("stepOut", DebugMode::Break, true); }

DebugResult FakeDebugger::runToCursor(const std::filesystem::path& file, int line) {
  DebugResult r = st_->enter("runToCursor", file.generic_string() + ":" + std::to_string(line));
  if (!r.ok) return r;
  {
    Lock lk(st_->mu);
    st_->mode_ = DebugMode::Break;
    st_->statement_ = DebugLocation{file, line};
  }
  st_->cv.notify_all();
  return r;
}

DebugResult FakeDebugger::openInEditor(const std::filesystem::path& file, int line, int column) {
  return st_->enter("openInEditor", file.generic_string() + ":" + std::to_string(line) + ":" + std::to_string(column));
}

DebugResult FakeDebugger::setBreakpoint(const Breakpoint& bp) {
  DebugResult r = st_->enter("setBreakpoint", describe(bp));
  if (!r.ok) return r;
  st_->injectBreakpoint(bp);
  return r;
}

DebugResult FakeDebugger::removeBreakpoint(const std::filesystem::path& file, int line) {
  DebugResult r = st_->enter("removeBreakpoint", file.generic_string() + ":" + std::to_string(line));
  if (!r.ok) return r;
  st_->injectRemoveBreakpoint(file, line);
  return r;
}

DebugResult FakeDebugger::poll(DebugSnapshot& out) {
  DebugResult r = st_->enter("poll", "");
  if (!r.ok) return r;
  Lock lk(st_->mu);
  out.mode = st_->mode_;
  out.currentStatement = st_->statement_;
  out.breakpoints = st_->bps_;
  out.lastError = st_->lastError_;
  return r;
}

void FakeDebugger::cancel() {
  {
    Lock lk(st_->mu);
    st_->cancelled_ = true;
  }
  st_->cv.notify_all();
}

}  // namespace tf
