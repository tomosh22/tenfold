#include "ui/features/NavResultPicker.h"

#include <algorithm>

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
  const bool choice = std::any_of(locs.begin(), locs.end(), [](const SymbolLocation& l) { return !l.label.empty(); });
  if (!popup_) {
    env_.lines.clearCache();
    auto p = std::make_unique<ListPopup>(false);
    p->setMaxVisibleRows(10);
    if (choice && anchor_) p->setAnchor(*anchor_);
    p->onAccept([this, raw = p.get()](int row) {
      if (row < 0 || row >= static_cast<int>(rowLoc_.size()) || rowLoc_[static_cast<size_t>(row)] < 0) return;
      const SymbolLocation l = locs_[static_cast<size_t>(rowLoc_[static_cast<size_t>(row)])];
      openResult(env_, raw->acceptedByMouse(), [&] {
        std::string e;
        if (!env_.editor.gotoLocation(l.loc, &e, l.name)) env_.say("Cannot open " + l.loc.file.string() + ": " + e);
      });
    });
    p->onClosed([this] { popup_ = nullptr; });
    popup_ = ListPopup::open(env_.root, std::move(p));
  }
  locs_ = locs;
  const std::filesystem::path root = env_.editor.workspace().root();
  std::vector<ListRow> rows;
  rowLoc_.clear();
  int preferred = -1;
  // Find All References (VS_BEHAVIOUR N5): the engine ordered the answer definitions first, then by file, and gave every
  // place a role. Group headings (Definition, then one per file) and the columns Line:Col, Code, Read / Write.
  const bool grouped = std::any_of(locs.begin(), locs.end(), [](const SymbolLocation& l) { return !l.role.empty(); });
  std::string group;
  size_t files = 0;
  for (size_t i = 0; i < locs_.size(); ++i) {
    const SymbolLocation& l = locs_[i];
    ListRow r;
    r.key = locationKey(l);
    const std::string where = relativePath(root, l.loc.file) + ":" + std::to_string(l.loc.pos.line + 1);
    const std::string code = trimmedLine(env_.lines.line(l.loc.file, l.loc.pos.line));
    if (grouped) {
      const bool def = l.role == "Definition";
      const std::string g = def ? "Definition" : relativePath(root, l.loc.file);
      if (g != group) {
        group = g;
        files += def ? 0 : 1;
        size_t n = 0;
        for (size_t k = i; k < locs_.size() && (locs_[k].role == "Definition") == def &&
                           (def || relativePath(root, locs_[k].loc.file) == g);
             ++k)
          ++n;
        ListRow h;
        h.header = true;
        h.key = "group:" + g;
        h.primary = g;
        h.detail = std::to_string(n);
        rows.push_back(std::move(h));
        rowLoc_.push_back(-1);
      }
      r.primary = std::to_string(l.loc.pos.line + 1) + ":" + std::to_string(l.loc.pos.column + 1);
      r.secondary = code;
      r.badge = l.role;
      if (def) r.detail = where;
    } else if (choice) {
      r.primary = l.label.empty() ? where : l.label;
      r.secondary = code;
      r.detail = where;
    } else {
      r.primary = where;
      r.secondary = code;
    }
    r.dim = c == Confidence::Approximate;
    if (l.preferred && preferred < 0) preferred = static_cast<int>(rows.size());
    rows.push_back(std::move(r));
    rowLoc_.push_back(static_cast<int>(i));
  }
  if (choice) popup_->setMaxVisibleRows(static_cast<int>(std::min<size_t>(rows.size(), 10)));
  popup_->setTitle(choice ? title + ": choose the target"
                   : grouped
                       ? title + " (" + std::to_string(locs_.size()) + " in " + std::to_string(std::max<size_t>(files, 1)) +
                             (files > 1 ? " files)" : " file)")
                       : title + " (" + std::to_string(locs_.size()) + ")");
  popup_->setStatus(confidenceBadge(c, err));
  popup_->setRows(std::move(rows), std::max(0, preferred));
  const int at = keep.empty() ? -1 : popup_->indexOfKey(keep);
  if (at >= 0) popup_->setSelected(at);
}

}  // namespace tf::ui
