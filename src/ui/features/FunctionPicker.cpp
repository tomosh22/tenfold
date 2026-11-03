#include "ui/features/FunctionPicker.h"

#include <algorithm>

#include "ui/features/ParserCommands.h"

namespace tf::ui {

namespace {
constexpr int kBefore = 5, kLines = 12;

std::string qualified(const SymbolLocation& l) { return l.container.empty() ? l.name : l.container + "::" + l.name; }
}  // namespace

bool FunctionPicker::open() {
  close();
  Document* d = env_.editor.currentDocument();
  if (!d) {
    env_.say("No file open");
    return false;
  }
  env_.lines.clearCache();
  auto p = std::make_unique<ListPopup>(true);
  p->input()->setPlaceholder("Find function in " + d->path().filename().string());
  p->setEmptyText("Searching...");
  p->setPreviewEnabled(env_.preview);
  p->onQuery([this](const std::string&) { refilter(); });
  p->onAccept([this](int row) {
    if (row < 0 || row >= static_cast<int>(shown_.size())) return;
    const SymbolLocation l = shown_[static_cast<size_t>(row)];
    std::string err;
    if (!env_.editor.gotoLocation(l.loc, &err, l.name)) env_.say("Cannot open " + l.loc.file.string() + ": " + err);
  });
  p->onSelection([this](int) { updatePreview(); });
  p->onClosed([this] {
    popup_ = nullptr;
    ++seq_;  // answers still in flight are dropped
  });
  all_.clear();
  shown_.clear();
  answered_ = false;
  error_.clear();
  popup_ = ListPopup::open(env_.root, std::move(p));
  popup_->setStatus("searching\xE2\x80\xA6");
  const uint64_t seq = ++seq_;
  auto cb = [this, seq, alive = std::weak_ptr<int>(alive_)](Confidence c, const std::vector<SymbolLocation>& locs,
                                                            const std::string& err) {
    if (!alive.expired()) onResult(seq, c, locs, err);
  };
  if (symbols_) symbols_(cb);
  else env_.editor.documentSymbols(cb);
  return true;
}

void FunctionPicker::close() {
  if (popup_) popup_->close();
}

void FunctionPicker::onResult(uint64_t seq, Confidence c, const std::vector<SymbolLocation>& locs,
                              const std::string& err) {
  if (!popup_ || seq != seq_) return;
  // An error that arrives after an approximate list (clangd absent) keeps the list and only changes the badge.
  const Document* d = env_.editor.currentDocument();
  if (c == Confidence::Approximate || err.empty() || !answered_) {
    all_.clear();
    if (d)
      for (const SymbolLocation& l : locs)
        if (isFunctionDefinition(l, d->buffer())) all_.push_back(l);
    std::stable_sort(all_.begin(), all_.end(),
                     [](const SymbolLocation& a, const SymbolLocation& b) { return a.loc.pos < b.loc.pos; });
    answered_ = true;
  }
  confidence_ = c;
  error_ = err;
  refilter();
}

void FunctionPicker::refilter() {
  if (!popup_) return;
  const std::string q = popup_->input()->text();
  const std::string keep = popup_->selectedRow() ? popup_->selectedRow()->key : std::string();
  struct Hit {
    size_t idx;
    int score;
  };
  std::vector<Hit> hits;
  for (size_t i = 0; i < all_.size(); ++i) {
    if (q.empty()) {
      hits.push_back({i, 0});
      continue;
    }
    const std::vector<int> m = fuzzyMatchPositions(q, qualified(all_[i]));
    if (m.empty()) continue;
    hits.push_back({i, m.back() - m.front() + 2 * m.front()});
  }
  if (!q.empty())
    std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.score < b.score; });
  shown_.clear();
  std::vector<ListRow> rows;
  for (const Hit& h : hits) {
    const SymbolLocation& l = all_[h.idx];
    shown_.push_back(l);
    ListRow r;
    r.key = locationKey(l);
    kindIcon(l.kind, r.icon, r.iconToken);
    r.primary = l.name;
    r.secondary = l.container;
    r.detail = ":" + std::to_string(l.loc.pos.line + 1);
    r.match = fuzzyMatchPositions(q, l.name);
    r.dim = confidence_ == Confidence::Approximate;
    rows.push_back(std::move(r));
  }
  popup_->setStatus(answered_ ? confidenceBadge(confidence_, error_) : "searching\xE2\x80\xA6");
  popup_->setEmptyText(!answered_ ? "Searching..."
                       : !error_.empty() && confidence_ != Confidence::Approximate && all_.empty()
                           ? error_
                           : "No functions defined in this file");
  popup_->setRows(std::move(rows), 0);
  if (!keep.empty()) {
    const int at = popup_->indexOfKey(keep);
    if (at >= 0) popup_->setSelected(at);
  }
}

void FunctionPicker::updatePreview() {
  if (!popup_ || !env_.preview) return;
  const int sel = popup_->selected();
  ListPreview pv;
  if (sel >= 0 && sel < static_cast<int>(shown_.size())) {
    const SymbolLocation& l = shown_[static_cast<size_t>(sel)];
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
