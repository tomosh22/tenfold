#include "ui/features/UnsavedPrompt.h"

namespace tf::ui {

namespace {
constexpr size_t kListed = 10;
}

bool UnsavedPrompt::saveAll(const std::vector<std::filesystem::path>& paths) {
  for (const auto& path : paths) {
    Document* d = env_.editor.document(path);
    if (!d || !d->dirty()) continue;
    std::string err;
    if (env_.editor.save(*d, &err)) continue;
    env_.say("Cannot save " + relativePath(env_.editor.workspace().root(), path) + ": " +
             (err.empty() ? std::string("unknown error") : err));
    return false;  // stop at the first failure: the user fixes it (check out the file) and tries again
  }
  return true;
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
  const bool many = dirty.size() > 1;
  auto p = std::make_unique<ListPopup>(false);
  p->setTitle(many ? "Save changes to " + std::to_string(dirty.size()) + " documents?"
                   : "Save changes to " + dirty[0].filename().string() + "?");
  ListRow save, discard, cancel;
  save.key = "save";
  save.primary = many ? "Save All" : "Save";
  discard.key = "discard";
  discard.primary = "Don't Save";
  discard.secondary = "the changes are lost";
  cancel.key = "cancel";
  cancel.primary = "Cancel";
  std::vector<ListRow> rows{save, discard, cancel};
  if (many) {
    for (size_t i = 0; i < dirty.size() && i < kListed; ++i) {
      ListRow r;
      r.key = "doc:" + std::to_string(i);
      r.primary = "    " + relativePath(env_.editor.workspace().root(), dirty[i]);
      r.dim = true;
      rows.push_back(std::move(r));
    }
    if (dirty.size() > kListed) {
      ListRow r;
      r.key = "more";
      r.primary = "    ... and " + std::to_string(dirty.size() - kListed) + " more";
      r.dim = true;
      rows.push_back(std::move(r));
    }
  } else {
    rows[0].secondary = relativePath(env_.editor.workspace().root(), dirty[0]);
  }
  p->setMaxVisibleRows(static_cast<int>(rows.size()));
  p->setRows(std::move(rows), 0);
  p->onAccept([this, dirty, proceed = std::move(proceed), life = std::weak_ptr<int>(life_)](int row) {
    if (life.expired() || row < 0 || row > 1) return;  // Cancel, or a click on the document list
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
