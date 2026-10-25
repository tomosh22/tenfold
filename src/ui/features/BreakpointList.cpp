#include "ui/features/BreakpointList.h"

#include <algorithm>

namespace tf::ui {

namespace {
std::string keyOf(const Breakpoint& b) { return b.file.generic_string() + ":" + std::to_string(b.line); }

// Space and Delete act on the selected row; everything else is the list's own.
class BreakpointPopup : public ListPopup {
 public:
  BreakpointPopup(std::function<void()> toggle, std::function<void()> remove)
      : ListPopup(false), toggle_(std::move(toggle)), remove_(std::move(remove)) {}
  bool onEvent(const InputEvent& e) override {
    if (e.type == InputType::KeyDown && !(e.mods & (ModCtrl | ModAlt | ModShift))) {
      if (e.key == Key::Space) {
        toggle_();
        return true;
      }
      if (e.key == Key::Delete) {
        remove_();
        return true;
      }
    }
    return ListPopup::onEvent(e);
  }

 private:
  std::function<void()> toggle_, remove_;
};

void setConditionRow(ListPopup& p, const std::string& text) {
  ListRow r;
  r.key = "cond";
  r.primary = text.empty() ? "Remove the condition" : "Break when " + text;
  p.setRows({r}, 0);
}
}  // namespace

void BreakpointList::open() {
  close();
  auto p = std::make_unique<BreakpointPopup>([this] { toggleSelected(); }, [this] { removeSelected(); });
  p->setTitle("Breakpoints  (Enter: go to, Space: enable / disable, Delete: remove)");
  p->setMaxVisibleRows(12);
  p->setEmptyText("No breakpoints (F9 toggles one)");
  p->onAccept([this](int row) {
    if (row < 0 || row >= static_cast<int>(shown_.size())) return;
    const Breakpoint& b = shown_[static_cast<size_t>(row)];
    std::string err;
    if (!env_.editor.gotoLocation({b.file, {b.line, 0}}, &err, "breakpoint"))
      env_.say("Cannot open " + b.file.string() + (err.empty() ? "" : ": " + err));
  });
  p->onClosed([this] { popup_ = nullptr; });
  popup_ = ListPopup::open(env_.root, std::move(p));
  rebuild({});
}

void BreakpointList::close() {
  if (popup_) popup_->close();
  if (cond_) cond_->close();
}

void BreakpointList::refresh() {
  if (popup_ && env_.editor.breakpoints().generation() != gen_) {
    const ListRow* sel = popup_->selectedRow();
    rebuild(sel ? sel->key : std::string());
  }
}

void BreakpointList::rebuild(const std::string& keepKey) {
  if (!popup_) return;
  const std::filesystem::path root = env_.editor.workspace().root();
  const int prev = popup_->selected();
  gen_ = env_.editor.breakpoints().generation();
  shown_ = env_.editor.breakpoints().all();
  std::vector<ListRow> rows;
  int sel = -1;
  for (const Breakpoint& b : shown_) {
    ListRow r;
    r.key = keyOf(b);
    r.primary =
        std::string(b.enabled ? "[x] " : "[ ] ") + relativePath(root, b.file) + ":" + std::to_string(b.line + 1);
    r.secondary = b.condition.empty() ? trimmedLine(env_.lines.line(b.file, b.line), 100) : "when " + b.condition;
    r.dim = !b.enabled;
    if (r.key == keepKey) sel = static_cast<int>(rows.size());
    rows.push_back(std::move(r));
  }
  if (sel < 0)
    sel = std::clamp(prev, 0, std::max(0, static_cast<int>(rows.size()) - 1));  // the row that took its place
  popup_->setRows(std::move(rows), sel);
}

void BreakpointList::toggleSelected() {
  const int i = popup_ ? popup_->selected() : -1;
  if (i < 0 || i >= static_cast<int>(shown_.size())) return;
  const Breakpoint b = shown_[static_cast<size_t>(i)];
  env_.editor.breakpoints().setEnabled(b.file, b.line, !b.enabled);
  env_.editor.saveBreakpoints();
  rebuild(keyOf(b));
}

void BreakpointList::removeSelected() {
  const int i = popup_ ? popup_->selected() : -1;
  if (i < 0 || i >= static_cast<int>(shown_.size())) return;
  const Breakpoint b = shown_[static_cast<size_t>(i)];
  env_.editor.breakpoints().remove(b.file, b.line);
  env_.editor.saveBreakpoints();
  rebuild({});
}

void BreakpointList::openCondition() {
  close();
  const Document* d = env_.editor.currentDocument();
  if (!d) return;
  condFile_ = d->path();
  condLine_ = env_.editor.selections().empty() ? 0 : env_.editor.selections()[0].head.line;
  const auto bp = env_.editor.breakpoints().get(condFile_, condLine_);
  if (!bp) {
    env_.say("No breakpoint on line " + std::to_string(condLine_ + 1) + " (F9 sets one)");
    return;
  }
  auto p = std::make_unique<ListPopup>(true);
  p->input()->setPlaceholder("Breakpoint condition (empty: unconditional)");
  p->setMaxVisibleRows(1);
  p->setStatus("line " + std::to_string(condLine_ + 1));
  p->onQuery([this](const std::string& q) {
    condText_ = q;
    if (cond_) setConditionRow(*cond_, q);
  });
  p->onAccept([this](int) {
    env_.editor.breakpoints().setCondition(condFile_, condLine_, condText_);
    env_.editor.saveBreakpoints();
  });
  p->onClosed([this] { cond_ = nullptr; });
  cond_ = ListPopup::open(env_.root, std::move(p));
  condText_ = bp->condition;
  cond_->input()->setText(condText_, true);  // programmatic: onQuery is not called
  setConditionRow(*cond_, condText_);
}

}  // namespace tf::ui
