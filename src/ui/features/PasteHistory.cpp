#include "ui/features/PasteHistory.h"

#include <algorithm>
#include <cctype>

#include "ui/settings/Settings.h"

namespace tf::ui {

namespace {
std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}
}  // namespace

void PasteHistory::applySettings(const Settings& s) {
  setLimit(static_cast<size_t>(std::clamp(s.getInt("editor.pasteHistorySize"), 0, 1000)));
}

void PasteHistory::setLimit(size_t n) {
  limit_ = n;
  while (entries_.size() > limit_) entries_.pop_back();
}

void PasteHistory::record(const std::string& text) {
  lastClipboard_ = text;
  if (text.empty() || limit_ == 0) return;
  auto it = std::find(entries_.begin(), entries_.end(), text);
  if (it != entries_.end()) entries_.erase(it);
  entries_.push_front(text);
  while (entries_.size() > limit_) entries_.pop_back();
}

void PasteHistory::syncFromClipboard() {
  if (!hooks_.clipboardGet) return;
  const std::string now = hooks_.clipboardGet();
  if (now != lastClipboard_) record(now);
}

std::string PasteHistory::preview(const std::string& text, size_t max) {
  const size_t b = text.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) return "(blank)";
  const size_t e = text.find_first_of("\r\n", b);
  const bool more = e != std::string::npos && text.find_first_not_of(" \t\r\n", e) != std::string::npos;
  const std::string line = trimmedLine(text.substr(b, e == std::string::npos ? std::string::npos : e - b), max);
  return more ? line + " ..." : line;
}

void PasteHistory::open() {
  close();
  auto p = std::make_unique<ListPopup>(true);
  p->input()->setPlaceholder("Paste history");
  p->setMaxVisibleRows(12);
  p->setEmptyText("Nothing copied yet");
  p->onQuery([this](const std::string&) { rebuild(); });
  p->onAccept([this](int row) {
    if (row < 0 || row >= static_cast<int>(shown_.size())) return;
    const std::string text = entries_[shown_[static_cast<size_t>(row)]];
    if (hooks_.paste && !hooks_.paste(text)) return;
    record(text);  // to the top
    if (hooks_.clipboardSet) hooks_.clipboardSet(text);
  });
  p->onClosed([this] { popup_ = nullptr; });
  popup_ = ListPopup::open(env_.root, std::move(p));
  rebuild();
}

void PasteHistory::close() {
  if (popup_) popup_->close();
}

void PasteHistory::rebuild() {
  const std::string filter = lower(popup_->input()->text());
  shown_.clear();
  std::vector<ListRow> rows;
  for (size_t i = 0; i < entries_.size(); ++i) {
    const std::string& t = entries_[i];
    if (!filter.empty() && lower(t).find(filter) == std::string::npos) continue;
    ListRow r;
    r.key = std::to_string(i);
    r.primary = preview(t);
    const size_t lines = static_cast<size_t>(std::count(t.begin(), t.end(), '\n')) + (t.back() == '\n' ? 0 : 1);
    if (lines > 1) r.detail = std::to_string(lines) + " lines";
    rows.push_back(std::move(r));
    shown_.push_back(i);
  }
  popup_->setRows(std::move(rows), 0);
}

}  // namespace tf::ui
