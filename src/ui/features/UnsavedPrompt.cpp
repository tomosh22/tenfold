#include "ui/features/UnsavedPrompt.h"

#include <algorithm>

namespace tf::ui {

bool UnsavedPrompt::saveAll(const std::vector<std::filesystem::path>& paths) {
  std::vector<SaveRefusal> refused;
  for (const auto& path : paths) {
    Document* d = env_.editor.document(path);
    if (!d || !d->dirty()) continue;
    std::string err;
    UnencodableReport rep;
    if (env_.editor.save(*d, &err, &rep)) continue;
    if (!rep.empty()) {  // a character the file's encoding cannot hold: the rest is still saved, the dialog lists these
      refused.push_back({path, std::move(rep)});
      continue;
    }
    env_.say("Cannot save " + relativePath(env_.editor.workspace().root(), path) + ": " +
             (err.empty() ? std::string("unknown error") : err));
    return false;  // stop at the first failure: the user fixes it (check out the file) and tries again
  }
  if (refused.empty()) return true;
  env_.refused(std::move(refused));  // nothing proceeds: the close / quit is cancelled for these documents
  return false;
}

// Several dirty documents (Close All, closing a panel, quitting): Visual Studio's one list dialog "Save changes to the
// following items?" (VS_BEHAVIOUR T6). Every item starts checked; Enter / Space / a click toggles the item under the
// selection, Yes saves the checked items (documents left unchecked are closed without saving), No closes without
// saving, Cancel (or Escape) aborts.
void UnsavedPrompt::askList(const std::vector<std::filesystem::path>& dirty, Proceed proceed) {
  struct State {
    std::vector<std::filesystem::path> files;
    std::vector<bool> checked;
  };
  auto st = std::make_shared<State>();
  st->files = dirty;
  st->checked.assign(dirty.size(), true);
  auto p = std::make_unique<ListPopup>(false);
  p->setTitle("Save changes to the following items?");
  p->setWidthCells(100);
  const int items = static_cast<int>(dirty.size());
  p->setMaxVisibleRows(std::min(items + 3, 14));
  auto build = [this, st] {
    std::vector<ListRow> rows;
    for (size_t i = 0; i < st->files.size(); ++i) {
      ListRow r;
      r.key = "item:" + std::to_string(i);
      r.primary =
          std::string(st->checked[i] ? "[x] " : "[ ] ") + relativePath(env_.editor.workspace().root(), st->files[i]);
      r.toggle = true;
      rows.push_back(std::move(r));
    }
    for (const char* label : {"Yes", "No", "Cancel"}) {
      ListRow r;
      r.key = label;
      r.primary = label;
      rows.push_back(std::move(r));
    }
    return rows;
  };
  p->setRows(build(), items);  // starts on Yes
  ListPopup* raw = p.get();
  p->onToggle([raw, st, build](int row) {
    if (row < 0 || row >= static_cast<int>(st->checked.size())) return;
    st->checked[static_cast<size_t>(row)] = !st->checked[static_cast<size_t>(row)];
    raw->setRows(build(), row);
  });
  p->onAccept([this, st, items, proceed = std::move(proceed), life = std::weak_ptr<int>(life_)](int row) {
    if (life.expired() || row < items || row > items + 1) return;  // Cancel
    if (row == items + 1) {                                        // No
      if (proceed) proceed(true);
      return;
    }
    std::vector<std::filesystem::path> chosen;
    bool all = true;
    for (size_t i = 0; i < st->files.size(); ++i) {
      if (st->checked[i]) chosen.push_back(st->files[i]);
      else all = false;
    }
    if (saveAll(chosen) && proceed) proceed(!all);
  });
  p->onClosed([this, life = std::weak_ptr<int>(life_)] {
    if (!life.expired()) popup_ = nullptr;
  });
  popup_ = ListPopup::open(env_.root, std::move(p));
}

void UnsavedPrompt::ask(const std::vector<std::filesystem::path>& paths, Proceed proceed) {
  close();
  std::vector<std::filesystem::path> dirty;
  for (const auto& path : paths) {
    const Document* d = env_.editor.document(path);
    if (d && d->dirty()) dirty.push_back(path);
  }
  if (dirty.empty()) {
    if (proceed) proceed(false);
    return;
  }
  if (dirty.size() > 1) {
    askList(dirty, std::move(proceed));
    return;
  }
  auto p = std::make_unique<ListPopup>(false);
  p->setTitle("Save changes to " + dirty[0].filename().string() + "?");
  ListRow save, discard, cancel;
  save.key = "save";
  save.primary = "Save";
  save.secondary = relativePath(env_.editor.workspace().root(), dirty[0]);
  discard.key = "discard";
  discard.primary = "Don't Save";
  discard.secondary = "the changes are lost";
  cancel.key = "cancel";
  cancel.primary = "Cancel";
  std::vector<ListRow> rows{save, discard, cancel};
  p->setMaxVisibleRows(static_cast<int>(rows.size()));
  p->setRows(std::move(rows), 0);
  p->onAccept([this, dirty, proceed = std::move(proceed), life = std::weak_ptr<int>(life_)](int row) {
    if (life.expired() || row < 0 || row > 1) return;  // Cancel
    if (row == 0) {
      if (saveAll(dirty) && proceed) proceed(false);
    } else if (proceed) {
      proceed(true);
    }
  });
  p->onClosed([this, life = std::weak_ptr<int>(life_)] {
    if (!life.expired()) popup_ = nullptr;
  });
  popup_ = ListPopup::open(env_.root, std::move(p));
}

void UnsavedPrompt::close() {
  if (popup_) popup_->close();
}

}  // namespace tf::ui
