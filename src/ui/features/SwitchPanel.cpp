#include "ui/features/SwitchPanel.h"

#include "ui/shell/Workbench.h"

namespace tf::ui {

bool SwitchPanel::step(bool forward) {
  if (popup_) {
    const int n = static_cast<int>(popup_->rows().size());
    if (n > 0) popup_->setSelected((popup_->selected() + (forward ? 1 : n - 1)) % n);
    return true;
  }
  Panel* p = bench_.activePanel();
  if (!p) return false;
  const TabStrip& st = p->tabs();
  ids_ = st.mruOrder();
  if (ids_.size() < 2) {
    ids_.clear();
    return false;
  }
  panel_ = p;
  std::vector<ListRow> rows;
  for (int id : ids_) {
    const int i = st.indexOf(id);
    ListRow r;
    r.key = std::to_string(id);
    r.primary = i >= 0 ? st.tabs()[static_cast<size_t>(i)].title : std::string();
    const std::filesystem::path path = bench_.tabPath(id);
    if (!path.empty()) r.secondary = relativePath(env_.editor.workspace().root(), path);
    rows.push_back(std::move(r));
  }
  auto pop = std::make_unique<ListPopup>(false);
  pop->setTitle("Active Files");
  pop->setMaxVisibleRows(static_cast<int>(std::min<size_t>(rows.size(), 14)));
  // The first row is the file already shown: Ctrl+Tab goes to the one before it, Ctrl+Shift+Tab to the oldest.
  pop->setRows(std::move(rows), forward ? 1 : static_cast<int>(ids_.size()) - 1);
  pop->onAccept([this, life = std::weak_ptr<int>(life_)](int row) {
    if (!life.expired()) activate(row);
  });
  pop->onClosed([this, life = std::weak_ptr<int>(life_)] {
    if (!life.expired()) popup_ = nullptr;
  });
  popup_ = ListPopup::open(env_.root, std::move(pop));
  return true;
}

void SwitchPanel::activate(int row) {
  if (row < 0 || row >= static_cast<int>(ids_.size())) return;
  const int id = ids_[static_cast<size_t>(row)];
  // The tab may be gone (closed while the panel was up) or moved to another panel.
  Panel* p = bench_.panelOf(id);
  if (!p) return;
  p->setActive(id);
  bench_.setActivePanel(p);
}

bool SwitchPanel::commit() {
  if (!popup_) return false;
  const int row = popup_->selected();
  popup_->close();
  activate(row);
  return true;
}

void SwitchPanel::close() {
  if (popup_) popup_->close();
}

}  // namespace tf::ui
