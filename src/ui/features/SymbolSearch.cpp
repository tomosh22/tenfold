#include "ui/features/SymbolSearch.h"

#include <algorithm>

namespace tf::ui {

namespace {
constexpr size_t kMaxRows = 500;
constexpr int kBefore = 5, kLines = 12;
}  // namespace

SymbolSearch::SymbolSearch(PickerEnv& env) : env_(env) {}

SymbolSearch::~SymbolSearch() {
  alive_.reset();
  {
    std::lock_guard<std::mutex> lk(mu_);
    stop_ = true;
  }
  cv_.notify_all();
  if (worker_.joinable()) worker_.join();
}

void SymbolSearch::submit(const std::string& query, Editor::NavResult cb) {
  {
    std::lock_guard<std::mutex> lk(mu_);
    jobQuery_ = query;
    jobCb_ = std::move(cb);
    hasJob_ = true;
  }
  if (!worker_.joinable()) worker_ = std::thread([this] { workerLoop(); });
  cv_.notify_one();
}

void SymbolSearch::workerLoop() {
  for (;;) {
    std::string q;
    Editor::NavResult cb;
    {
      std::unique_lock<std::mutex> lk(mu_);
      cv_.wait(lk, [&] { return stop_ || hasJob_; });
      if (stop_) return;
      q = std::move(jobQuery_);
      cb = std::move(jobCb_);
      hasJob_ = false;
    }
    env_.editor.findSymbols(q, std::move(cb));  // posts its answer to the Editor's main-thread queue
  }
}

void SymbolSearch::open() {
  close();
  auto p = std::make_unique<ListPopup>(true);
  p->input()->setPlaceholder("Search symbols");
  p->setEmptyText("Type to search symbols");
  p->setPreviewEnabled(env_.preview);
  p->onQuery([this](const std::string&) { queryChanged(); });
  p->onAccept([this](int row) {
    if (row < 0 || row >= static_cast<int>(locs_.size())) return;
    const SymbolLocation l = locs_[static_cast<size_t>(row)];
    std::string err;
    if (!env_.editor.gotoLocation(l.loc, &err, l.name)) env_.say("Cannot open " + l.loc.file.string() + ": " + err);
  });
  p->onSelection([this](int) { updatePreview(); });
  p->onClosed([this] {
    popup_ = nullptr;
    ++seq_;  // answers still in flight are dropped
    pending_ = false;
  });
  locs_.clear();
  shownSeq_ = 0;
  pending_ = false;
  popup_ = ListPopup::open(env_.root, std::move(p));
}

void SymbolSearch::close() {
  if (popup_) popup_->close();
}

void SymbolSearch::queryChanged() {
  const int64_t now = env_.clock ? env_.clock() : 0;
  if (popup_->input()->text().empty()) {  // nothing to look for: clear at once and drop answers in flight
    ++seq_;
    pending_ = false;
    locs_.clear();
    popup_->setStatus("");
    popup_->setRows({});
    return;
  }
  if (now - lastFire_ >= debounceMs_) fire();  // leading edge: the first key after a pause is not delayed
  else pending_ = true;
}

int SymbolSearch::tick(int64_t now) {
  if (!popup_ || !pending_) return -1;
  const int64_t due = lastFire_ + debounceMs_;
  if (now >= due) {
    fire();
    return -1;
  }
  return static_cast<int>(std::max<int64_t>(1, due - now));
}

void SymbolSearch::fire() {
  pending_ = false;
  lastFire_ = env_.clock ? env_.clock() : 0;
  const uint64_t seq = ++seq_;
  ++issued_;
  const std::string q = popup_->input()->text();
  popup_->setStatus("searching\xE2\x80\xA6");
  if (!search_ && !env_.editor.started()) {
    popup_->setStatus("engine not started");
    return;
  }
  auto cb = [this, seq, alive = std::weak_ptr<int>(alive_)](Confidence c, const std::vector<SymbolLocation>& locs,
                                                            const std::string& err) {
    if (!alive.expired()) onResult(seq, c, locs, err);
  };
  if (search_) search_(q, cb);
  else submit(q, cb);
}

void SymbolSearch::onResult(uint64_t seq, Confidence c, const std::vector<SymbolLocation>& locs,
                            const std::string& err) {
  if (!popup_ || seq != seq_) return;  // closed, or the query moved on
  const std::string q = popup_->input()->text();
  // The same symbol stays selected when the authoritative list replaces the approximate one for the same query.
  const std::string keep = seq == shownSeq_ && popup_->selectedRow() ? popup_->selectedRow()->key : std::string();
  locs_.assign(locs.begin(), locs.begin() + static_cast<std::ptrdiff_t>(std::min(locs.size(), kMaxRows)));
  std::vector<ListRow> rows;
  const std::filesystem::path root = env_.editor.workspace().root();
  for (const SymbolLocation& l : locs_) {
    ListRow r;
    r.key = locationKey(l);
    kindIcon(l.kind, r.icon, r.iconToken);
    r.primary = l.name;
    r.secondary = l.container;
    r.detail = relativePath(root, l.loc.file) + ":" + std::to_string(l.loc.pos.line + 1);
    r.match = fuzzyMatchPositions(q, l.name);
    r.dim = c == Confidence::Approximate;  // constraint 1.4: never present approximate as final
    rows.push_back(std::move(r));
  }
  shownSeq_ = seq;
  popup_->setStatus(confidenceBadge(c, err));
  popup_->setEmptyText(err.empty() || c == Confidence::Approximate ? "No matching symbols" : err);
  popup_->setRows(std::move(rows), 0);
  if (!keep.empty()) {
    const int at = popup_->indexOfKey(keep);
    if (at >= 0) popup_->setSelected(at);
  }
}

void SymbolSearch::updatePreview() {
  if (!popup_ || !env_.preview) return;
  const int sel = popup_->selected();
  ListPreview pv;
  if (sel >= 0 && sel < static_cast<int>(locs_.size())) {
    const SymbolLocation& l = locs_[static_cast<size_t>(sel)];
    int first = 0;
    const int line = l.loc.pos.line;
    if (env_.lines.lines(l.loc.file, std::max(0, line - kBefore), kLines, pv.lines, first)) {
      pv.firstLine = first + 1;
      pv.highlight = line - first;
    }
    pv.title = relativePath(env_.editor.workspace().root(), l.loc.file) + ":" + std::to_string(line + 1);
  }
  popup_->setPreview(std::move(pv));
}

}  // namespace tf::ui
