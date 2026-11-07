#include "ui/features/InlayHints.h"

#include <algorithm>

#include "ui/widgets/TextView.h"

namespace tf::ui {

InlayHints::Backend InlayHints::editorBackend(Editor& ed) {
  Backend b;
  Editor* e = &ed;
  b.fetch = [e](Range r, Done<LspInlayHint> d) { e->inlayHints(r, std::move(d)); };
  return b;
}

InlayHints::Mode InlayHints::parseMode(const std::string& s) {
  if (s == "off") return Mode::Off;
  if (s == "on") return Mode::On;
  return Mode::WhilePressed;
}

InlayHints::InlayHints(PickerEnv& env, ViewsFn views)
    : env_(env), views_(std::move(views)), backend_(editorBackend(env.editor)) {}
InlayHints::~InlayHints() = default;

void InlayHints::setMode(Mode m) {
  if (m == mode_) return;
  mode_ = m;
  dirty_ = true;
  applyShown();
}

void InlayHints::setHeld(bool held) {
  if (held == held_) return;
  held_ = held;
  applyShown();
}

void InlayHints::applyShown() {
  const bool on = shown();
  if (on == appliedShown_) return;
  appliedShown_ = on;
  ++seq_;  // answers still in flight belong to the old state
  doc_ = nullptr;  // the next tick asks again
  if (views_) views_([on](TextView& v) { v.setInlayHintsShown(on); });
}

int InlayHints::tick(int64_t nowMs) {
  if (shown() != appliedShown_) applyShown();
  if (!appliedShown_ || !backend_.fetch) return -1;
  Document* d = env_.editor.currentDocument();
  if (!d) return -1;
  int first = 0, last = d->buffer().lineCount() - 1;
  if (views_)
    views_([&](TextView& v) {
      if (v.document() == d && v.isCurrent() && v.visible()) {
        const auto [a, b] = v.visibleLines();
        first = a;
        last = b;
      }
    });
  const bool covered = d == doc_ && first >= firstAsked_ && last <= lastAsked_;
  if (d == doc_ && d->version() == version_ && covered && !dirty_) return -1;
  if (d != doc_ || d->version() != version_ || !covered || first != seenFirst_ || last != seenLast_) {
    if (first != seenFirst_ || last != seenLast_ || d != doc_ || d->version() != version_) changedAt_ = nowMs;
    seenFirst_ = first;
    seenLast_ = last;
  }
  if (nowMs - changedAt_ < kDebounceMs) return static_cast<int>(kDebounceMs - (nowMs - changedAt_));
  if (allowed_ && !allowed_()) {
    doc_ = d;  // do not ask again until something changes
    version_ = d->version();
    firstAsked_ = first;
    lastAsked_ = last;
    dirty_ = false;
    return -1;
  }
  const int n = d->buffer().lineCount();
  const int a = std::max(0, first - kMarginLines), z = std::min(n - 1, last + kMarginLines);
  doc_ = d;
  version_ = d->version();
  firstAsked_ = a;
  lastAsked_ = z;
  dirty_ = false;
  const uint64_t seq = ++seq_;
  const int version = version_;
  Range r{{a, 0}, {z, static_cast<int>(d->buffer().line(z).size())}};
  backend_.fetch(r, [this, seq, d, version, life = std::weak_ptr<int>(alive_)](const std::vector<LspInlayHint>& hs,
                                                                               const std::string&) {
    if (life.expired() || seq != seq_ || !appliedShown_) return;
    if (env_.editor.currentDocument() != d || d->version() != version) return;
    views_([&](TextView& v) {
      if (v.document() == d) v.setInlayHints(hs);
    });
  });
  return -1;
}

}  // namespace tf::ui
