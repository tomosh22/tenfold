#include "ui/features/NavResultPicker.h"

namespace tf::ui {

void NavResultPicker::close() {
  if (popup_) popup_->close();
}

void NavResultPicker::show(const std::string& title, Confidence c, const std::vector<SymbolLocation>& locs,
                           const std::string& err, size_t minResults) {
  if (locs.size() < minResults) {
    close();
    return;
  }
  std::string keep;  // same location stays selected when the authoritative list replaces the approximate one
  if (popup_ && popup_->selectedRow()) keep = popup_->selectedRow()->key;
  if (!popup_) {
    env_.lines.clearCache();
    auto p = std::make_unique<ListPopup>(false);
    p->setMaxVisibleRows(10);
    p->onAccept([this](int row) {
      if (row < 0 || row >= static_cast<int>(locs_.size())) return;
      const SymbolLocation l = locs_[static_cast<size_t>(row)];
      std::string e;
      if (!env_.editor.gotoLocation(l.loc, &e, l.name)) env_.say("Cannot open " + l.loc.file.string() + ": " + e);
    });
    p->onClosed([this] { popup_ = nullptr; });
    popup_ = ListPopup::open(env_.root, std::move(p));
  }
  locs_ = locs;
  const std::filesystem::path root = env_.editor.workspace().root();
  std::vector<ListRow> rows;
  for (const SymbolLocation& l : locs_) {
    ListRow r;
    r.key = locationKey(l);
    r.primary = relativePath(root, l.loc.file) + ":" + std::to_string(l.loc.pos.line + 1);
    r.secondary = trimmedLine(env_.lines.line(l.loc.file, l.loc.pos.line));
    r.dim = c == Confidence::Approximate;
    rows.push_back(std::move(r));
  }
  popup_->setTitle(title + " (" + std::to_string(locs_.size()) + ")");
  popup_->setStatus(confidenceBadge(c, err));
  popup_->setRows(std::move(rows), 0);
  const int at = keep.empty() ? -1 : popup_->indexOfKey(keep);
  if (at >= 0) popup_->setSelected(at);
}

}  // namespace tf::ui
