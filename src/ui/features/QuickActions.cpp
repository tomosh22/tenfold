#include "ui/features/QuickActions.h"

#include <algorithm>
#include <functional>

namespace tf::ui {

QuickActions::Backend QuickActions::editorBackend(Editor& ed) {
  Backend b;
  Editor* e = &ed;
  b.fetch = [e](Range r, Done<CodeAction> d) { e->codeActions(r, std::move(d)); };
  b.apply = [e](const CodeAction& a, std::function<void(bool, const std::string&)> d) {
    e->applyCodeAction(a, std::move(d));
  };
  return b;
}

QuickActions::QuickActions(PickerEnv& env) : env_(env), backend_(editorBackend(env.editor)) {}
QuickActions::~QuickActions() { close(); }

Range QuickActions::rangeFor(Editor& ed) {
  for (const Selection& s : ed.selections())
    if (!s.empty()) return s.range();
  const Document* d = ed.currentDocument();
  const Position p = ed.selections()[0].head;
  if (!d) return {p, p};
  const std::string& line = d->buffer().line(p.line);
  int a = 0, z = static_cast<int>(line.size());
  while (a < z && (line[static_cast<size_t>(a)] == ' ' || line[static_cast<size_t>(a)] == '\t')) ++a;
  while (z > a && (line[static_cast<size_t>(z - 1)] == ' ' || line[static_cast<size_t>(z - 1)] == '\t' ||
                   line[static_cast<size_t>(z - 1)] == '\r'))
    --z;
  return {{p.line, a}, {p.line, z}};
}

void QuickActions::close() {
  if (popup_) popup_->close();
}

bool QuickActions::open() {
  if (!env_.editor.currentDocument()) return false;
  if (!backend_.fetch || (allowed_ && !allowed_())) {
    env_.say("No quick actions: they need clangd and a file that belongs to a translation unit");
    return false;
  }
  close();
  const uint64_t seq = ++seq_;
  const Document* doc = env_.editor.currentDocument();
  const int version = doc->version();
  backend_.fetch(rangeFor(env_.editor), [this, seq, doc, version, life = std::weak_ptr<int>(alive_)](
                                            const std::vector<CodeAction>& as, const std::string& err) {
    if (life.expired() || seq != seq_) return;  // a newer request, or the list was closed
    if (env_.editor.currentDocument() != doc || doc->version() != version) return;  // the text moved on
    deliver(seq, as, err);
  });
  return true;
}

void QuickActions::deliver(uint64_t, const std::vector<CodeAction>& as, const std::string& err) {
  if (as.empty()) {
    env_.say(err.empty() ? "No quick actions available here" : "No quick actions: " + err);
    return;
  }
  shown_ = as;
  auto p = std::make_unique<ListPopup>(false);
  p->setMaxVisibleRows(10);
  p->setTitle("Quick Actions and Refactorings");
  p->setWidthCells(60);
  if (anchor_) p->setAnchor(*anchor_);
  std::vector<ListRow> rows;
  int preferred = 0;
  for (size_t i = 0; i < shown_.size(); ++i) {
    const CodeAction& a = shown_[i];
    ListRow r;
    r.key = std::to_string(i);
    r.icon = a.kind.rfind("quickfix", 0) == 0 ? "!" : "*";
    r.iconToken = a.kind.rfind("quickfix", 0) == 0 ? "keyword" : "function";
    r.primary = a.title;
    r.badge = a.kind.rfind("quickfix", 0) == 0 ? "fix" : a.kind.rfind("refactor", 0) == 0 ? "refactor" : a.kind;
    if (a.preferred && preferred == 0) preferred = static_cast<int>(i);
    rows.push_back(std::move(r));
  }
  p->setRows(std::move(rows), preferred);
  p->onAccept([this](int row) {
    if (row < 0 || static_cast<size_t>(row) >= shown_.size() || !backend_.apply) return;
    const CodeAction a = shown_[static_cast<size_t>(row)];
    backend_.apply(a, [this, title = a.title, life = std::weak_ptr<int>(alive_)](bool ok, const std::string& e) {
      if (life.expired()) return;
      env_.say(ok ? "Applied: " + title : "Quick action failed: " + e);
    });
  });
  p->onClosed([this] { popup_ = nullptr; });
  popup_ = ListPopup::open(env_.root, std::move(p));
}

// ---- light bulb ----

void QuickActions::setBulbEnabled(bool on) {
  bulbOn_ = on;
  if (!on) clearBulb();
}

void QuickActions::clearBulb() {
  ++bulbSeq_;
  needAsk_ = false;
  keyDoc_ = nullptr;
  if (bulbLine_ != -1 || bulbDoc_) {
    bulbLine_ = -1;
    bulbDoc_ = nullptr;
    if (onBulbChanged) onBulbChanged();
  }
}

int QuickActions::tick(int64_t nowMs) {
  const Document* d = env_.editor.currentDocument();
  if (!bulbOn_ || !d || !backend_.fetch) {
    if (bulbLine_ != -1) clearBulb();
    return -1;
  }
  const int line = env_.editor.selections()[0].head.line;
  // clangd publishes its diagnostics after the text changed: a change of the ones on this line asks again.
  uint64_t dsig = 1469598103934665603ull;
  for (const Diagnostic& g : env_.editor.diagnostics(d->path()))
    if (g.range.start.line <= line && g.range.end.line >= line)
      dsig = (dsig ^ std::hash<std::string>()(g.message)) * 1099511628211ull;
  if (d != keyDoc_ || line != keyLine_ || d->version() != keyVersion_ || dsig != keyDiagSig_) {
    const bool sameSpot = d == keyDoc_ && line == keyLine_;
    keyDiagSig_ = dsig;
    keyDoc_ = d;
    keyLine_ = line;
    keyVersion_ = d->version();
    keyAt_ = nowMs;
    needAsk_ = true;
    ++bulbSeq_;  // an answer for the previous spot is stale
    if (!sameSpot && bulbLine_ != -1) {
      bulbLine_ = -1;
      bulbDoc_ = nullptr;
      if (onBulbChanged) onBulbChanged();
    }
  }
  if (!needAsk_) return -1;
  if (nowMs - keyAt_ < kBulbDebounceMs) return static_cast<int>(kBulbDebounceMs - (nowMs - keyAt_));
  needAsk_ = false;
  if (allowed_ && !allowed_()) {
    if (bulbLine_ != -1) clearBulb();
    return -1;
  }
  const uint64_t seq = ++bulbSeq_;
  const int version = d->version();
  const Range r = rangeFor(env_.editor);
  backend_.fetch(r, [this, seq, d, line, version, life = std::weak_ptr<int>(alive_)](const std::vector<CodeAction>& as,
                                                                                     const std::string&) {
    if (life.expired() || seq != bulbSeq_) return;
    if (env_.editor.currentDocument() != d || d->version() != version) return;
    const bool fix = std::any_of(as.begin(), as.end(), [](const CodeAction& a) { return a.kind.rfind("quickfix", 0) == 0; });
    const int nl = fix ? line : -1;
    bool error = false;
    if (fix)
      for (const Diagnostic& g : env_.editor.diagnostics(d->path()))
        if (g.severity <= 1 && g.range.start.line <= line && g.range.end.line >= line) error = true;
    if (nl == bulbLine_ && (fix ? d : nullptr) == bulbDoc_ && error == bulbError_) return;
    bulbError_ = error;
    bulbLine_ = nl;
    bulbDoc_ = fix ? d : nullptr;
    if (onBulbChanged) onBulbChanged();
  });
  return -1;
}

}  // namespace tf::ui
