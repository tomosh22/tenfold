#include "ui/features/HoverTooltip.h"

#include <algorithm>

#include "ui/widgets/TextView.h"

namespace tf::ui {

namespace {
bool inside(const Range& r, Position p) { return !(p < r.start) && p < r.end; }

TextView* viewAt(RootWidget& root, float x, float y) {
  Widget* w = root.hitTest(x, y);
  while (w && !dynamic_cast<TextView*>(w)) w = w->parent();
  return static_cast<TextView*>(w);
}

// Markdown from clangd: drop code fences and heading marks, keep the text.
std::string plain(const std::string& md) {
  std::string out;
  size_t from = 0;
  while (from <= md.size()) {
    size_t nl = md.find('\n', from);
    if (nl == std::string::npos) nl = md.size();
    std::string l = md.substr(from, nl - from);
    from = nl + 1;
    if (l.rfind("```", 0) == 0) continue;
    size_t h = 0;
    while (h < l.size() && l[h] == '#') ++h;
    if (h) l.erase(0, l.find_first_not_of(' ', h) == std::string::npos ? l.size() : l.find_first_not_of(' ', h));
    out += l + "\n";
  }
  while (!out.empty() && out.back() == '\n') out.pop_back();
  return out;
}
}  // namespace

HoverTooltip::HoverTooltip(PickerEnv& env) : env_(env) {
  box_ = static_cast<PopupBox*>(env_.root.addChild(std::make_unique<PopupBox>()));
  box_->setMaxRows(16);
}

HoverTooltip::~HoverTooltip() {
  alive_.reset();
  if (box_ && box_->root()) box_->root()->removeChild(box_);
}

void HoverTooltip::dismiss() {
  ++seq_;
  pending_ = false;
  haveTarget_ = false;
  box_->hide();
}

void HoverTooltip::onMouse(const InputEvent& e) {
  if (e.type != InputType::MouseMove) return;
  if (box_->visible() && ui::contains(box_->box(), e.x, e.y)) return;  // moving onto the tooltip keeps it
  TextView* v = viewAt(env_.root, e.x, e.y);
  Document* d = v ? v->document() : nullptr;
  if (!d || !ui::contains(v->textArea(), e.x, e.y)) {
    dismiss();
    return;
  }
  const Position p = v->positionAt(e.x, e.y);
  Range target{p, p};
  bool found = false;
  for (const auto* ds : {&env_.editor.diagnostics(d->path()), &env_.editor.buildDiagnostics(d->path())})
    for (const Diagnostic& g : *ds)
      if (inside(g.range, p)) {
        target = found ? Range{std::min(target.start, g.range.start), std::max(target.end, g.range.end)} : g.range;
        found = true;
      }
  if (!found) {
    const TextBuffer::Word w = d->buffer().wordAt(p);
    if (w.text.empty() || !inside(w.range, p)) {
      dismiss();
      return;
    }
    target = w.range;
  }
  if (haveTarget_ && d->path() == file_ && target.start == target_.start && target.end == target_.end) return;
  dismiss();
  haveTarget_ = true;
  file_ = d->path();
  target_ = target;
  pos_ = found ? p : target.start;
  x_ = e.x;
  y_ = e.y;
  since_ = env_.clock ? env_.clock() : 0;
  pending_ = true;
}

int HoverTooltip::tick(int64_t now) {
  if (!pending_) return -1;
  if (now - since_ < kDwellMs) return static_cast<int>(kDwellMs - (now - since_));
  fire();
  return -1;
}

void HoverTooltip::fire() {
  pending_ = false;
  Document* d = env_.editor.document(file_);
  if (!d) return;
  diag_.clear();
  hoverText_.clear();
  using S = PopupSpan::Style;
  for (const Diagnostic& g : env_.editor.diagnostics(file_))
    if (inside(g.range, pos_)) {
      const S st = g.severity == 1 ? S::Error : g.severity == 2 ? S::Warn : S::Info;
      for (const std::string& l : PopupBox::wrap(g.message + (g.code.empty() ? "" : " [" + g.code + "]"), 80))
        diag_.push_back({{{l, st, false}}, "", false});
    }
  // The last build's message for this spot, labelled so it is not mistaken for clangd's (C1-UI).
  for (const Diagnostic& g : env_.editor.buildDiagnostics(file_))
    if (inside(g.range, pos_)) {
      const S st = g.severity == 1 ? S::Error : g.severity == 2 ? S::Warn : S::Info;
      bool first = true;
      for (const std::string& l : PopupBox::wrap(g.message + (g.code.empty() ? "" : " [" + g.code + "]"), 80)) {
        PopupLine pl;
        if (first) pl.spans.push_back({"build ", S::Dim, false});
        pl.spans.push_back({l, st, false});
        diag_.push_back(std::move(pl));
        first = false;
      }
    }
  if (!diag_.empty()) show();  // diagnostics first, the hover text is added when it arrives
  const uint64_t seq = ++seq_;
  auto cb = [this, seq, alive = std::weak_ptr<int>(alive_)](const std::string& text, const std::string& err) {
    if (!alive.expired()) onHover(seq, text, err);
  };
  if (hover_) hover_(*d, pos_, cb);
  else if (env_.editor.started()) env_.editor.hoverAt(*d, pos_, cb);
}

void HoverTooltip::onHover(uint64_t seq, const std::string& text, const std::string& err) {
  if (seq != seq_ || !err.empty() || text.empty()) return;  // dismissed, moved on, or nothing to say
  hoverText_ = plain(text);
  show();
}

void HoverTooltip::show() {
  using S = PopupSpan::Style;
  std::vector<PopupLine> lines = diag_;
  if (!hoverText_.empty()) {
    const bool approx = approx_ ? approx_() : !env_.editor.semantic().status().clangdInitialized;
    if (!lines.empty()) lines.push_back({});
    for (const std::string& l : PopupBox::wrap(hoverText_, 80))
      lines.push_back({{{l, approx ? S::Dim : S::Text, false}}, "", false});
    if (approx) lines.push_back({{{"approximate (clangd not running)", S::Dim, false}}, "", false});
  }
  if (lines.empty()) return;
  TextView* v = viewAt(env_.root, x_, y_);
  if (!v) return;
  box_->setContent(std::move(lines));
  box_->showAt(v->caretRect(target_.start));
}

}  // namespace tf::ui
