#include "debug/DebugSession.h"

#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

#include "app/Editor.h"
#include "core/PathUtil.h"

namespace tf {

namespace fs = std::filesystem;

namespace {

constexpr const char* kNoBackend = "no debugger backend on this platform";

DebugResult guarded(const std::function<DebugResult(Debugger&)>& fn, Debugger& d) {
  try {
    return fn(d);
  } catch (const std::exception& e) {
    return DebugResult::failure(std::string("backend threw: ") + e.what());
  } catch (...) { return DebugResult::failure("backend threw"); }
}

bool sameProps(const Breakpoint& a, const Breakpoint& b) {
  return a.enabled == b.enabled && a.condition == b.condition;
}

bool sameSnapshot(const DebugSnapshot& a, const DebugSnapshot& b) {
  if (a.mode != b.mode || a.currentStatement != b.currentStatement || a.lastError != b.lastError ||
      a.breakpoints.size() != b.breakpoints.size())
    return false;
  for (size_t i = 0; i < a.breakpoints.size(); ++i) {
    const Breakpoint &x = a.breakpoints[i], &y = b.breakpoints[i];
    if ((x.file != y.file && breakpointFileKey(x.file) != breakpointFileKey(y.file)) || x.line != y.line ||
        !sameProps(x, y))
      return false;
  }
  return true;
}

}  // namespace

struct DebugSession::Cmd {
  const char* what = "";
  std::function<DebugResult(Debugger&)> fn;
  Kind kind = Kind::Normal;
  uint64_t epoch = 0;
  Done done;
};

// Everything the worker thread touches. Shared with the thread, so an abandoned (blocked) worker can still finish
// safely after the DebugSession is gone; it then posts nothing (poster == null).
struct DebugSession::Impl {
  std::unique_ptr<Debugger> backend;  // destroyed on the worker thread
  std::mutex mu;
  std::condition_variable cv, doneCv;
  std::deque<Cmd> queue;
  bool stop = false;
  bool finished = false;
  std::atomic<uint64_t> bpExecuted{0};
  std::atomic<int> fastMs{kDefaultFastPollMs}, slowMs{kDefaultSlowPollMs};
  // Attach retries (attach-only connects the worker makes by itself while the backend is not connected): the
  // interval (0 = off), whether tenfold has breakpoints worth pushing, and what to connect with (under mu).
  std::atomic<int> attachMs{0};
  std::atomic<bool> haveBps{false};
  DebugWorkspaceInfo attachInfo;
  std::mutex postMu;
  std::function<void(std::function<void()>)> poster;
  std::thread thread;

  void post(std::function<void()> fn) {
    std::lock_guard<std::mutex> lk(postMu);
    if (poster) poster(std::move(fn));
  }
};

DebugSession::DebugSession(Editor& editor) : ed_(editor) {
  ed_.breakpoints().setChangeHandler([this] { onBreakpointsChanged(); });
}

DebugSession::~DebugSession() {
  shutdown();
  ed_.breakpoints().setChangeHandler({});
}

void DebugSession::setBackend(std::unique_ptr<Debugger> d) {
  stopWorker();
  connectRequested_ = connected_ = false;
  ++epoch_;
  known_.clear();
  backend_ = std::move(d);
  backendName_ = backend_ ? backend_->name() : std::string();
  setMode(DebugMode::Disconnected);
  touch();
}

void DebugSession::setPollIntervals(int fastMs, int slowMs) {
  fastMs_ = fastMs > 0 ? fastMs : 1;
  slowMs_ = slowMs > 0 ? slowMs : 1;
  if (impl_) {
    impl_->fastMs = fastMs_;
    impl_->slowMs = slowMs_;
    impl_->cv.notify_all();
  }
}

void DebugSession::onBreakpointsChanged() {
  if (impl_) {
    impl_->haveBps = !ed_.breakpoints().empty();
    impl_->cv.notify_all();  // the worker may now want to retry attaching
  }
  pushBreakpoints();
}

bool DebugSession::ensureBackend() {
  if (backend_ || impl_) return true;
  if (!factory_) return false;
  backend_ = factory_();
  if (!backend_) return false;
  backendName_ = backend_->name();
  return true;
}

void DebugSession::startWorker() {
  if (impl_) return;
  auto im = std::make_shared<Impl>();
  im->backend = std::move(backend_);
  im->fastMs = fastMs_;
  im->slowMs = slowMs_;
  token_ = std::make_shared<int>(0);
  im->poster = [this, tok = std::weak_ptr<int>(token_)](std::function<void()> fn) {
    // Called with the worker's postMu held; the Editor outlives the session, which clears the poster first.
    ed_.post([tok, fn = std::move(fn)] {
      if (!tok.expired()) fn();
    });
  };
  impl_ = im;
  im->thread = std::thread([im, this] {
    Debugger& be = *im->backend;
    bool connected = false;
    uint64_t epoch = 0;
    DebugMode lastMode = DebugMode::Disconnected;
    DebugSnapshot posted;
    bool havePosted = false;
    uint64_t postedExecuted = 0;
    std::string postedPollError;

    auto postSnapshot = [&](const DebugSnapshot& s) {
      const uint64_t executed = im->bpExecuted.load();
      if (havePosted && executed == postedExecuted && sameSnapshot(s, posted)) return;
      havePosted = true;
      posted = s;
      postedExecuted = executed;
      const uint64_t ep = epoch;
      im->post([this, s, ep, executed] { applySnapshot(s, ep, executed); });
    };
    auto pollOnce = [&] {
      DebugSnapshot s;
      DebugResult r;
      try {
        r = be.poll(s);
      } catch (const std::exception& e) {
        r = DebugResult::failure(std::string("backend threw: ") + e.what());
      } catch (...) { r = DebugResult::failure("backend threw"); }
      if (!r.ok) {
        if (r.error != postedPollError) {
          postedPollError = r.error;
          im->post([this, e = r.error] { setError("poll: " + e); });
        }
        return;
      }
      postedPollError.clear();
      lastMode = s.mode;
      postSnapshot(s);
    };

    using Clock = std::chrono::steady_clock;
    Clock::time_point lastAttach = Clock::now();
    // Nothing is attached (never was, or the instance went away) and attaching is wanted.
    auto wantsAttach = [&] {
      return im->attachMs.load() > 0 && im->haveBps.load() && (!connected || lastMode == DebugMode::Disconnected);
    };
    auto attachDue = [&] {
      return wantsAttach() && Clock::now() - lastAttach >= std::chrono::milliseconds(im->attachMs.load());
    };

    while (true) {
      Cmd cmd;
      bool haveCmd = false, polled = false, attach = false;
      {
        std::unique_lock<std::mutex> lk(im->mu);
        if (!im->stop && im->queue.empty()) {
          std::optional<std::chrono::milliseconds> wait;  // nullopt: until something is queued
          if (connected) {
            const bool fast = lastMode == DebugMode::Running || lastMode == DebugMode::Break;
            wait = std::chrono::milliseconds(fast ? im->fastMs.load() : im->slowMs.load());
          }
          if (wantsAttach()) {
            auto due = std::chrono::duration_cast<std::chrono::milliseconds>(
                lastAttach + std::chrono::milliseconds(im->attachMs.load()) - Clock::now());
            if (due < std::chrono::milliseconds(0)) due = std::chrono::milliseconds(0);
            if (!wait || due < *wait) wait = due;
          }
          const bool wasWanting = wantsAttach();
          auto ready = [&] { return im->stop || !im->queue.empty() || wantsAttach() != wasWanting; };
          bool timedOut = false;
          if (wait) timedOut = !im->cv.wait_for(lk, *wait, ready);
          else im->cv.wait(lk, ready);
          polled = timedOut && connected;
          attach = timedOut;
        }
        if (im->stop) break;
        if (!im->queue.empty()) {
          cmd = std::move(im->queue.front());
          im->queue.pop_front();
          haveCmd = true;
        }
      }
      if (haveCmd) {
        DebugResult r;
        if (!connected && cmd.kind != Kind::Connect && cmd.kind != Kind::Disconnect)
          r = DebugResult::failure("not connected");
        else r = guarded(cmd.fn, be);
        if (cmd.kind == Kind::Breakpoint) ++im->bpExecuted;
        if (cmd.kind == Kind::Connect) {
          epoch = cmd.epoch;
          lastAttach = Clock::now();
          connected = r.ok;
        } else if (cmd.kind == Kind::Disconnect) {
          epoch = cmd.epoch;
          connected = false;
          havePosted = false;
        }
        im->post([this, what = cmd.what, kind = cmd.kind, r, ep = cmd.epoch, done = std::move(cmd.done)] {
          commandFinished(what, kind, r, ep, done);
        });
        bool more;
        {
          std::lock_guard<std::mutex> lk(im->mu);
          more = !im->queue.empty();
        }
        if (connected && !more) pollOnce();
      } else if (attach && attachDue()) {
        lastAttach = Clock::now();
        DebugWorkspaceInfo info;
        {
          std::lock_guard<std::mutex> lk(im->mu);
          info = im->attachInfo;
        }
        DebugResult r = guarded([info](Debugger& d) { return d.connect(info); }, be);
        if (r.ok) {
          connected = true;
          havePosted = false;
          im->post([this, r, ep = epoch] { commandFinished("attach", Kind::Connect, r, ep, {}); });
          pollOnce();
        } else if (!r.notRunning) {
          // a real problem (not just "nothing to attach to"): report it, without touching the connection state
          im->post([this, r, ep = epoch] { commandFinished("attach", Kind::Normal, r, ep, {}); });
        }
        if (polled && connected) pollOnce();
      } else if (polled && connected) {
        pollOnce();
      }
    }
    // Shutting down: leave the debugger instance the way we found it, then drop the backend on this thread (a COM
    // apartment must be released where it was entered).
    if (connected) guarded([](Debugger& d) { return d.disconnect(); }, be);
    {
      std::lock_guard<std::mutex> lk(im->mu);  // stopWorker() reads im->backend under this lock
      im->backend.reset();
      im->finished = true;
    }
    im->doneCv.notify_all();
  });
}

void DebugSession::stopWorker() {
  if (!impl_) return;
  std::shared_ptr<Impl> im = std::move(impl_);
  token_.reset();
  {
    std::lock_guard<std::mutex> lk(im->postMu);
    im->poster = nullptr;
  }
  {
    std::lock_guard<std::mutex> lk(im->mu);
    im->stop = true;
    im->queue.clear();
  }
  im->cv.notify_all();
  // The worker may be blocked inside a backend call: ask the backend to give up, then wait a bounded time.
  // `backend` is only reset by the worker thread itself after it saw `stop`, which cannot happen before cancel()
  // returns when the worker is blocked; when it is not blocked it may reset concurrently, so take the pointer
  // under the lock.
  {
    std::unique_lock<std::mutex> lk(im->mu);
    if (!im->finished && im->backend) im->backend->cancel();
    im->doneCv.wait_for(lk, std::chrono::milliseconds(shutdownMs_), [&] { return im->finished; });
    if (im->finished) {
      lk.unlock();
      im->thread.join();
    } else {
      lk.unlock();
      im->thread.detach();  // abandoned: finishes by itself, owns everything it touches
    }
  }
}

void DebugSession::shutdown() {
  stopWorker();
  backend_.reset();
  connectRequested_ = connected_ = false;
  known_.clear();
}

bool DebugSession::submit(const char* what, std::function<DebugResult(Debugger&)> fn, Done done, Kind kind) {
  if (!impl_) return false;
  Cmd c;
  c.what = what;
  c.fn = std::move(fn);
  c.kind = kind;
  c.epoch = epoch_;
  c.done = std::move(done);
  if (kind == Kind::Breakpoint) ++bpEnqueued_;
  {
    std::lock_guard<std::mutex> lk(impl_->mu);
    impl_->queue.push_back(std::move(c));
  }
  impl_->cv.notify_all();
  return true;
}

bool DebugSession::connect(Done done) { return connectImpl(true, std::move(done)); }

bool DebugSession::attachIfRunning(Done done) { return connectImpl(false, std::move(done)); }

void DebugSession::setAttachRetryMs(int ms) {
  attachMs_ = ms > 0 ? ms : 0;
  if (impl_ && attachWanted_) {
    impl_->attachMs = attachMs_;
    impl_->cv.notify_all();
  }
}

bool DebugSession::connectImpl(bool launchIfMissing, Done done) {
  if (connectRequested_) {
    if (done) ed_.post([done] { done(DebugResult::success()); });
    return true;
  }
  if (!ensureBackend()) {
    if (launchIfMissing) setError(kNoBackend);  // an attach attempt on a platform without a backend stays quiet
    return false;
  }
  ed_.loadBreakpoints();
  startWorker();
  connectRequested_ = true;
  ++epoch_;
  DebugWorkspaceInfo info;
  info.root = ed_.workspace().root();
  if (!solution_.empty()) info.solution = solution_;
  else if (auto s = ed_.workspace().solutionFiles(); !s.empty()) info.solution = s.front();
  info.launchIfMissing = launchIfMissing;
  if (!launchIfMissing) attachWanted_ = true;
  {
    // The worker's own retries are always attach-only.
    DebugWorkspaceInfo a = info;
    a.launchIfMissing = false;
    std::lock_guard<std::mutex> lk(impl_->mu);
    impl_->attachInfo = std::move(a);
  }
  impl_->haveBps = !ed_.breakpoints().empty();
  impl_->attachMs = attachWanted_ ? attachMs_ : 0;
  return submit("connect", [info](Debugger& d) { return d.connect(info); }, std::move(done), Kind::Connect);
}

bool DebugSession::disconnect(Done done) {
  if (!impl_ || !connectRequested_) {
    if (done) ed_.post([done] { done(DebugResult::success()); });
    return true;
  }
  connectRequested_ = connected_ = false;
  attachWanted_ = false;  // the user disconnected: no more automatic attaching until attachIfRunning()
  impl_->attachMs = 0;
  known_.clear();
  ++epoch_;
  setMode(DebugMode::Disconnected);
  return submit("disconnect", [](Debugger& d) { return d.disconnect(); }, std::move(done), Kind::Disconnect);
}

bool DebugSession::command(const char* what, std::function<DebugResult(Debugger&)> fn, Done done) {
  if (!connectRequested_ && !connect()) return false;
  return submit(what, std::move(fn), std::move(done));
}

bool DebugSession::start(Done done) {
  return command("start", [](Debugger& d) { return d.start(); }, std::move(done));
}
bool DebugSession::startWithoutDebugging(Done done) {
  return command("startWithoutDebugging", [](Debugger& d) { return d.startWithoutDebugging(); }, std::move(done));
}
bool DebugSession::stop(Done done) {
  return command("stop", [](Debugger& d) { return d.stop(); }, std::move(done));
}
bool DebugSession::restart(Done done) {
  return command("restart", [](Debugger& d) { return d.restart(); }, std::move(done));
}
bool DebugSession::breakAll(Done done) {
  return command("breakAll", [](Debugger& d) { return d.breakAll(); }, std::move(done));
}
bool DebugSession::stepOver(Done done) {
  return command("stepOver", [](Debugger& d) { return d.stepOver(); }, std::move(done));
}
bool DebugSession::stepInto(Done done) {
  return command("stepInto", [](Debugger& d) { return d.stepInto(); }, std::move(done));
}
bool DebugSession::stepOut(Done done) {
  return command("stepOut", [](Debugger& d) { return d.stepOut(); }, std::move(done));
}
bool DebugSession::openInEditor(const fs::path& file, int line, int column, Done done) {
  return command(
      "openInEditor", [file, line, column](Debugger& d) { return d.openInEditor(file, line, column); },
      std::move(done));
}
bool DebugSession::runToCursor(const fs::path& file, int line, Done done) {
  return command("runToCursor", [file, line](Debugger& d) { return d.runToCursor(file, line); }, std::move(done));
}

void DebugSession::setError(std::string e) {
  if (e == error_) return;
  error_ = std::move(e);
  touch();
}

void DebugSession::clearError() { setError({}); }

void DebugSession::setMode(DebugMode m) {
  if (mode_ != m) {
    mode_ = m;
    touch();
  }
  if (mode_ != DebugMode::Break && statement_) {
    statement_.reset();
    touch();
  }
}

std::string DebugSession::status() const {
  if (!hasBackend()) return std::string("Debugger: ") + kNoBackend;
  std::string s = "Debugger";
  if (!backendName_.empty()) s += " (" + backendName_ + ")";
  s += std::string(": ") + toString(mode_);
  if (statement_) s += " at " + statement_->file.filename().string() + ":" + std::to_string(statement_->line + 1);
  if (!error_.empty()) s += " - " + error_;
  return s;
}

void DebugSession::commandFinished(const char* what, Kind kind, const DebugResult& r, uint64_t epoch,
                                   const Done& done) {
  const bool current = epoch == epoch_;
  if (!r.ok) {
    if (!(r.notRunning && kind == Kind::Connect)) setError(std::string(what) + ": " + r.error);  // else: quiet
  } else if (kind != Kind::Breakpoint && current) {
    clearError();
  }
  if (current && kind == Kind::Connect) {
    if (r.ok) {
      connected_ = connectRequested_ = true;  // (also after an attach the worker made by itself)
      touch();
      known_.clear();
      pushBreakpoints();  // everything we have; the backend's own are adopted from the first trusted snapshot
    } else {
      connectRequested_ = false;
    }
  }
  if (current && kind == Kind::Disconnect) setMode(DebugMode::Disconnected);
  if (done) done(r);
}

void DebugSession::applySnapshot(const DebugSnapshot& s, uint64_t epoch, uint64_t bpExecuted) {
  if (epoch != epoch_ || !connected_) return;  // stale: from before a (re)connect / disconnect
  const DebugMode prev = mode_;
  const std::optional<DebugLocation> prevStatement = statement_;
  mode_ = s.mode;
  if (mode_ != prev) touch();
  std::optional<DebugLocation> st = s.mode == DebugMode::Break ? s.currentStatement : std::nullopt;
  if (st != statement_) {
    statement_ = st;
    touch();
  }
  if (!s.lastError.empty() && s.lastError != backendError_) {
    backendError_ = s.lastError;
    setError(s.lastError);
  }
  if (bpExecuted == bpEnqueued_ && s.mode != DebugMode::Disconnected) adoptBackendBreakpoints(s.breakpoints);
  if (statement_ && (prev != DebugMode::Break || statement_ != prevStatement) && followCurrentStatement) {
    std::string err;
    if (!ed_.gotoLocation({statement_->file, {statement_->line, 0}}, &err, "debugger") && !err.empty())
      setError("cannot show current statement: " + err);
  }
}

void DebugSession::adoptBackendBreakpoints(const std::vector<Breakpoint>& backend) {
  std::map<Key, Breakpoint> snap;
  for (const Breakpoint& b : backend) snap[{ed_.breakpoints().fileKey(b.file), b.line}] = b;
  std::vector<std::pair<Key, Breakpoint>> upserts;
  std::vector<Key> removed;
  for (const auto& [k, b] : known_) {
    auto it = snap.find(k);
    if (it == snap.end()) removed.push_back(k);
    else if (!sameProps(it->second, b)) upserts.push_back(*it);
  }
  for (const auto& [k, b] : snap)
    if (!known_.count(k)) upserts.push_back({k, b});
  if (removed.empty() && upserts.empty()) return;
  applying_ = true;  // Breakpoints' change handler must not push these back
  for (const Key& k : removed) {
    const Breakpoint b = known_[k];
    ed_.breakpoints().remove(b.file, b.line);
    known_.erase(k);
  }
  for (auto& [k, b] : upserts) {
    ed_.breakpoints().add(b);
    known_[k] = b;
  }
  applying_ = false;
  ed_.saveBreakpoints();
  touch();
}

void DebugSession::pushBreakpoints() {
  if (!connected_ || applying_ || !impl_) return;
  std::map<Key, Breakpoint> cur;
  for (const Breakpoint& b : ed_.breakpoints().all()) cur[{ed_.breakpoints().fileKey(b.file), b.line}] = b;
  std::vector<Breakpoint> removals, sets;
  for (const auto& [k, b] : known_)
    if (!cur.count(k)) removals.push_back(b);
  for (const auto& [k, b] : cur) {
    auto it = known_.find(k);
    if (it == known_.end() || !sameProps(it->second, b)) sets.push_back(b);
  }
  known_ = std::move(cur);
  // Removals first: a breakpoint that moved from line 5 to 6 while another moved 6 -> 7 must not be removed after
  // it was set.
  for (const Breakpoint& b : removals)
    submit("removeBreakpoint", [b](Debugger& d) { return d.removeBreakpoint(b.file, b.line); }, {}, Kind::Breakpoint);
  for (const Breakpoint& b : sets)
    submit(
        "setBreakpoint", [b](Debugger& d) { return d.setBreakpoint(b); },
        [this, b](const DebugResult& r) {
          if (r.ok) return;
          // Not applied: forget we told the backend, so a later diff retries and a snapshot does not mistake the
          // missing breakpoint for a deletion made on the backend side.
          auto it = known_.find({ed_.breakpoints().fileKey(b.file), b.line});
          if (it != known_.end() && sameProps(it->second, b)) known_.erase(it);
        },
        Kind::Breakpoint);
}

}  // namespace tf
