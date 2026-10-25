#include "vcs/VcsManager.h"

#include <algorithm>

#include "app/Editor.h"
#include "core/Document.h"
#include "core/PathUtil.h"
#include "vcs/GitProvider.h"

namespace tf {

namespace fs = std::filesystem;

std::vector<VcsManager::ProviderFactory> VcsManager::defaultProviders() {
  std::vector<ProviderFactory> v;
  v.push_back([] { return std::make_unique<GitProvider>(); });
  return v;
}

VcsManager::VcsManager(Editor& editor, Options opt) : editor_(editor), opt_(std::move(opt)) {
  if (opt_.providers.empty()) opt_.providers = defaultProviders();
}

VcsManager::~VcsManager() {
  shutdown();
  provider_.reset();
}

void VcsManager::shutdown() {
  stopped_ = true;
  {
    std::lock_guard<std::mutex> lk(mu_);
    stopWorker_ = true;
  }
  cv_.notify_all();
  if (thread_.joinable()) thread_.join();
}

// ---------------------------------------------------------------------------
// Worker
// ---------------------------------------------------------------------------

void VcsManager::ensureStarted() {
  if (started_ || stopped_ || !opt_.enabled) return;
  started_ = true;
  thread_ = std::thread([this] { workerLoop(); });
  fs::path root = editor_.workspace().root();
  push([this, root] { detectJob(root); });
}

void VcsManager::push(std::function<void()> job) {
  {
    std::lock_guard<std::mutex> lk(mu_);
    jobs_.push_back(std::move(job));
  }
  cv_.notify_all();
}

void VcsManager::schedule(const std::string& key, int delayMs, std::function<void()> fn) {
  {
    std::lock_guard<std::mutex> lk(mu_);
    timers_[key] = {std::chrono::steady_clock::now() + std::chrono::milliseconds(delayMs), std::move(fn)};
  }
  cv_.notify_all();
}

void VcsManager::workerLoop() {
  std::unique_lock<std::mutex> lk(mu_);
  while (!stopWorker_) {
    const auto now = std::chrono::steady_clock::now();
    for (auto it = timers_.begin(); it != timers_.end();) {
      if (it->second.due <= now) {
        jobs_.push_back(std::move(it->second.fn));
        it = timers_.erase(it);
      } else {
        ++it;
      }
    }
    if (!jobs_.empty()) {
      auto job = std::move(jobs_.front());
      jobs_.pop_front();
      lk.unlock();
      job();
      lk.lock();
      continue;
    }
    if (timers_.empty()) {
      cv_.wait(lk);
    } else {
      auto earliest = std::chrono::steady_clock::time_point::max();
      for (auto& [k, t] : timers_) earliest = std::min(earliest, t.due);
      cv_.wait_until(lk, earliest);
    }
  }
}

void VcsManager::post(std::function<void()> fn) {
  editor_.post([this, fn = std::move(fn)] {
    if (!stopped_) fn();
  });
}

void VcsManager::detectJob(fs::path root) {
  for (auto& make : opt_.providers) {
    std::unique_ptr<VcsProvider> p = make();
    if (p && p->detect(root)) {
      provider_ = std::move(p);
      break;
    }
  }
  if (!provider_) {
    if (opt_.log) opt_.log("vcs: no version control system found for " + root.string());
    return;
  }
  auto all = std::make_shared<std::vector<VcsFileInfo>>(provider_->status({}));
  const std::string name = provider_->name();
  const fs::path pr = provider_->root();
  if (opt_.log) opt_.log("vcs: " + name + " at " + pr.string());
  post([this, name, pr, all] {
    active_ = true;
    providerName_ = name;
    root_ = pr;
    rootKey_ = keyOf(pr);
    applyStatuses(*all, true);
    changed({});
  });
}

// ---------------------------------------------------------------------------
// Main side
// ---------------------------------------------------------------------------

std::string VcsManager::keyOf(const fs::path& p) const {
  const std::string raw = p.generic_string();
  auto it = keyCache_.find(raw);
  if (it != keyCache_.end()) return it->second;
  std::error_code ec;
  std::string key = pathKey(fs::weakly_canonical(p, ec));
  if (keyCache_.size() > 20000) keyCache_.clear();
  keyCache_.emplace(raw, key);
  return key;
}

VcsManager::DocState* VcsManager::find(const fs::path& p) const {
  auto it = docs_.find(keyOf(p));
  return it == docs_.end() ? nullptr : it->second.get();
}

void VcsManager::changed(const fs::path& file) {
  ++generation_;
  if (changed_) changed_(file);
}

void VcsManager::reportError(const fs::path& file, const std::string& msg) {
  lastError_ = msg;
  if (opt_.log) opt_.log("vcs: " + msg);
  if (errorHandler_) errorHandler_(file, msg);
}

void VcsManager::applyStatuses(std::vector<VcsFileInfo> infos, bool replaceAll) {
  if (replaceAll) statuses_.clear();
  for (auto& fi : infos) {
    const std::string key = keyOf(fi.path);
    if (fi.status == VcsStatus::Unmodified) statuses_.erase(key);
    else statuses_[key] = std::move(fi);
  }
}

const std::vector<DiffHunk>& VcsManager::hunks(const fs::path& file) const {
  static const std::vector<DiffHunk> none;
  DocState* st = find(file);
  return st ? st->hunks : none;
}

int VcsManager::hunksVersion(const fs::path& file) const {
  DocState* st = find(file);
  return st ? st->hunksVersion : 0;
}

bool VcsManager::hasBase(const fs::path& file) const {
  DocState* st = find(file);
  return st && st->base;
}

std::shared_ptr<const std::vector<std::string>> VcsManager::baseLines(const fs::path& file) const {
  DocState* st = find(file);
  return st ? st->base : nullptr;
}

std::vector<std::string> VcsManager::hunkBaseLines(const fs::path& file, size_t hunkIndex) const {
  DocState* st = find(file);
  if (!st || !st->base || hunkIndex >= st->hunks.size()) return {};
  return tf::hunkBaseLines(st->hunks[hunkIndex], *st->base);
}

VcsFileInfo VcsManager::fileInfo(const fs::path& file) const {
  VcsFileInfo fi;
  fi.path = file;
  if (!active_) return fi;  // Unknown
  const std::string key = keyOf(file);
  auto it = statuses_.find(key);
  if (it != statuses_.end()) {
    fi = it->second;
  } else {
    const bool inside = key.size() > rootKey_.size() && key.compare(0, rootKey_.size(), rootKey_) == 0 &&
                        (key[rootKey_.size()] == '/' || rootKey_.back() == '/');
    fi.status = inside ? VcsStatus::Unmodified : VcsStatus::Unknown;
  }
  if (fi.status == VcsStatus::Unmodified) {
    auto d = docs_.find(key);
    if (d != docs_.end() && !d->second->hunks.empty()) fi.status = VcsStatus::Modified;
  }
  return fi;
}

VcsStatus VcsManager::fileStatus(const fs::path& file) const { return fileInfo(file).status; }

VcsManager::Stats VcsManager::stats() const {
  Stats s = mainStats_;
  s.diffsComputed = diffsComputed_;
  s.beforeFirstEditCalls = beforeCalls_;
  return s;
}

void VcsManager::requestBase(DocState& st) {
  if (stopped_ || !opt_.enabled) return;
  ensureStarted();
  const uint64_t id = ++st.baseId;
  const fs::path path = st.path;
  const std::string key = keyOf(path);
  push([this, path, key, id] {
    if (!provider_) return;
    std::vector<VcsFileInfo> infos = provider_->status({path});
    VcsFileInfo info = infos.empty() ? VcsFileInfo{path, VcsStatus::Unknown, {}} : infos[0];
    std::string err;
    std::shared_ptr<std::vector<std::string>> lines;
    if (info.status != VcsStatus::Unknown) {
      std::optional<std::string> text = provider_->baseText(path, &err);
      if (!text && err.empty() && info.status == VcsStatus::Renamed && !info.renamedFrom.empty())
        text = provider_->baseText(info.renamedFrom, &err);  // staged rename: the old name holds the base
      if (text) lines = std::make_shared<std::vector<std::string>>(splitLines(*text));
      else if (err.empty() && info.status == VcsStatus::Added) lines = std::make_shared<std::vector<std::string>>();
    }
    post([this, key, id, info = std::move(info), lines = std::move(lines), err = std::move(err)] {
      ++mainStats_.baseLoads;
      applyStatuses({info}, false);
      auto it = docs_.find(key);
      if (it != docs_.end() && it->second->baseId == id) {
        DocState& st = *it->second;
        if (!err.empty()) reportError(st.path, err);
        st.base = lines;
        if (!st.base) {
          st.hunks.clear();
          st.hunksVersion = 0;
        }
      }
      changed(info.path);
      if (it != docs_.end() && it->second->baseId == id) startDiff(key);
    });
  });
}

void VcsManager::requestFileStatus(const fs::path& file) {
  if (stopped_ || !opt_.enabled) return;
  ensureStarted();
  push([this, file] {
    if (!provider_) return;
    auto infos = std::make_shared<std::vector<VcsFileInfo>>(provider_->status({file}));
    post([this, file, infos] {
      applyStatuses(*infos, false);
      changed(file);
    });
  });
}

void VcsManager::startDiff(const std::string& key) {
  auto it = docs_.find(key);
  if (it == docs_.end()) return;
  DocState& st = *it->second;
  if (!st.base) return;
  auto lines = std::make_shared<std::vector<std::string>>(st.doc->buffer().lines());
  const int version = st.doc->version();
  const uint64_t seq = st.seq;
  auto base = st.base;
  push([this, key, version, seq, base, lines] {
    if (opt_.diffHook) opt_.diffHook();
    auto hunks = std::make_shared<std::vector<DiffHunk>>(diffLines(*base, *lines, opt_.diff));
    ++diffsComputed_;
    post([this, key, version, seq, base, hunks] {
      auto it = docs_.find(key);
      if (it == docs_.end() || it->second->seq != seq || it->second->base != base) {
        ++mainStats_.staleDropped;
        return;
      }
      DocState& st = *it->second;
      st.hunks = std::move(*hunks);
      st.hunksVersion = version;
      changed(st.path);
    });
  });
}

void VcsManager::refreshStatus() {
  if (stopped_ || !opt_.enabled) return;
  ensureStarted();
  push([this] {
    if (!provider_) return;
    auto all = std::make_shared<std::vector<VcsFileInfo>>(provider_->status({}));
    const std::string err = provider_->lastError();
    post([this, all, err] {
      if (all->empty() && !err.empty()) {
        reportError({}, err);
        return;
      }
      applyStatuses(*all, true);
      changed({});
    });
  });
  for (auto& [k, st] : docs_) requestBase(*st);
}

void VcsManager::revertFile(const fs::path& file, Done done) {
  if (stopped_ || !opt_.enabled) {
    if (done) done(false, "version control is disabled");
    return;
  }
  ensureStarted();
  push([this, file, done] {
    if (!provider_) {
      post([done] {
        if (done) done(false, "no version control system found for this workspace");
      });
      return;
    }
    std::string err;
    const bool ok = provider_->revert(file, &err);
    post([this, file, done, ok, err] {
      bool good = ok;
      std::string msg = err;
      if (!ok) {
        reportError(file, err);
      } else if (Document* d = editor_.document(file)) {
        std::string re;
        if (!editor_.reloadFromDisk(*d, &re)) {  // calls documentReloaded -> base and status are re-read
          good = false;
          msg = re;
        }
      } else {
        requestFileStatus(file);
      }
      if (done) done(good, msg);
    });
  });
}

void VcsManager::checkOut(const fs::path& file, Done done) {
  if (stopped_ || !opt_.enabled) {
    if (done) done(false, "version control is disabled");
    return;
  }
  ensureStarted();
  push([this, file, done] {
    if (!provider_) {
      post([done] {
        if (done) done(false, "no version control system found for this workspace");
      });
      return;
    }
    std::string err;
    const bool ok = provider_->beforeFirstEdit(file, &err);
    ++beforeCalls_;
    auto infos = std::make_shared<std::vector<VcsFileInfo>>(provider_->status({file}));
    post([this, file, done, ok, err, infos] {
      if (ok) {
        if (DocState* st = find(file)) st->firstEditDone = true;
      } else {
        reportError(file, err);
      }
      applyStatuses(*infos, false);
      changed(file);
      if (done) done(ok, err);
    });
  });
}

bool VcsManager::revertHunk(const fs::path& file, size_t index, std::string* error) {
  auto fail = [&](const std::string& m) {
    if (error) *error = m;
    return false;
  };
  DocState* st = find(file);
  if (!st || !opt_.enabled) return fail("file is not open");
  if (!st->base) return fail("no committed version to revert to");
  Document& doc = *st->doc;
  const std::vector<std::string>& cur = doc.buffer().lines();
  std::vector<DiffHunk> hs = st->hunksVersion == doc.version() ? st->hunks : diffLines(*st->base, cur, opt_.diff);
  if (index >= hs.size()) return fail("no such change");
  HunkRevert r = revertHunkEdit(hs[index], *st->base, cur);
  if (editor_.currentDocument() == &doc) {
    editor_.replaceRange(r.range, r.text);
  } else {
    doc.beginGroup();
    doc.replace(r.range, r.text);
    doc.endGroup();
  }
  // Exact result right away (the listener also scheduled the debounced recompute, which then just agrees).
  st->hunks = diffLines(*st->base, doc.buffer().lines(), opt_.diff);
  st->hunksVersion = doc.version();
  changed(file);
  return true;
}

// ---------------------------------------------------------------------------
// Editor notifications
// ---------------------------------------------------------------------------

void VcsManager::documentOpened(Document& doc) {
  if (stopped_ || !opt_.enabled) return;
  auto st = std::make_unique<DocState>();
  st->doc = &doc;
  st->path = doc.path();
  DocState& ref = *st;
  docs_[keyOf(doc.path())] = std::move(st);
  requestBase(ref);
}

void VcsManager::documentChanged(Document& doc) {
  if (stopped_ || !opt_.enabled) return;
  DocState* st = find(doc.path());
  if (!st) return;
  ++st->seq;
  const std::string key = keyOf(doc.path());
  if (!st->firstEditDone && doc.dirty()) {
    st->firstEditDone = true;
    const fs::path path = st->path;
    ensureStarted();
    push([this, path] {
      if (!provider_) return;
      std::string err;
      const bool ok = provider_->beforeFirstEdit(path, &err);
      ++beforeCalls_;
      auto infos = std::make_shared<std::vector<VcsFileInfo>>(provider_->status({path}));
      post([this, path, ok, err, infos] {
        if (!ok) reportError(path, err);
        applyStatuses(*infos, false);
        changed(path);
      });
    });
  }
  schedule("d:" + key, opt_.debounceMs, [this, key] { post([this, key] { startDiff(key); }); });
}

void VcsManager::documentSaved(Document& doc) {
  if (stopped_ || !opt_.enabled) return;
  if (DocState* st = find(doc.path())) requestBase(*st);
}

void VcsManager::documentReloaded(Document& doc) {
  if (stopped_ || !opt_.enabled) return;
  DocState* st = find(doc.path());
  if (!st) return;
  ++st->seq;
  st->firstEditDone = false;  // clean again; the next edit is a first edit (Perforce: the file may be read-only again)
  requestBase(*st);
}

void VcsManager::documentClosed(Document& doc) {
  if (stopped_) return;
  docs_.erase(keyOf(doc.path()));
}

void VcsManager::fileEvent(const fs::path& file) {
  if (stopped_ || !opt_.enabled || !started_ || !active_) return;
  if (!file.empty() && find(file)) return;  // open documents are handled by save / reload
  schedule("status", opt_.statusDebounceMs, [this] { post([this] { refreshStatus(); }); });
}

}  // namespace tf
