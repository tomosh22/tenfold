#include "ui/features/BookmarkList.h"

#include <algorithm>
#include <cctype>

namespace tf::ui {

namespace {
std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}
}  // namespace

void BookmarkList::open() {
  close();
  auto p = std::make_unique<ListPopup>(true);
  p->input()->setPlaceholder("Filter bookmarks");
  p->setMaxVisibleRows(12);
  p->setEmptyText("No bookmarks (Ctrl+F2 toggles one)");
  p->onQuery([this](const std::string&) { rebuild(); });
  p->onAccept([this](int row) {
    if (row >= 0 && row < static_cast<int>(shown_.size()) &&
        !env_.editor.gotoBookmarkItem(shown_[static_cast<size_t>(row)]))
      env_.say("Cannot open " + shown_[static_cast<size_t>(row)].file.string());
  });
  p->onClosed([this] { popup_ = nullptr; });
  popup_ = ListPopup::open(env_.root, std::move(p));
  rebuild();
}

void BookmarkList::close() {
  if (popup_) popup_->close();
}

void BookmarkList::rebuild() {
  const std::string filter = lower(popup_->input()->text());
  const std::filesystem::path root = env_.editor.workspace().root();
  shown_.clear();
  std::vector<ListRow> rows;
  for (const Bookmarks::Item& b : env_.editor.bookmarks().all()) {
    const std::string text = trimmedLine(env_.lines.line(b.file, b.line), 120);
    const std::string rel = relativePath(root, b.file);
    if (!filter.empty() && lower(text).find(filter) == std::string::npos &&
        lower(rel).find(filter) == std::string::npos)
      continue;
    ListRow r;
    r.key = rel + ":" + std::to_string(b.line);
    r.primary = text.empty() ? "(blank line)" : text;
    r.detail = rel + ":" + std::to_string(b.line + 1);
    rows.push_back(std::move(r));
    shown_.push_back(b);
  }
  popup_->setRows(std::move(rows), 0);
}

}  // namespace tf::ui
