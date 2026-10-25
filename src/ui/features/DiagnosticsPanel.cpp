#include "ui/features/DiagnosticsPanel.h"

#include <algorithm>
#include <cctype>
#include <functional>

namespace tf::ui {

namespace {
std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}
}  // namespace

uint64_t DiagnosticsPanel::signature() const {
  uint64_t h = 1469598103934665603ull;
  auto mix = [&](uint64_t v) { h = (h ^ v) * 1099511628211ull; };
  for (Document* d : env_.editor.documents())
    for (const Diagnostic& g : env_.editor.diagnostics(d->path())) {
      mix(std::hash<std::string>()(d->path().string()));
      mix(static_cast<uint64_t>(g.range.start.line) << 32 | static_cast<uint32_t>(g.range.start.column));
      mix(static_cast<uint64_t>(g.severity));
      mix(std::hash<std::string>()(g.message));
    }
  mix(popup_ ? std::hash<std::string>()(popup_->input()->text()) : 0);
  return h;
}

void DiagnosticsPanel::open() {
  close();
  auto p = std::make_unique<ListPopup>(true);
  p->input()->setPlaceholder("Filter diagnostics");
  p->setMaxVisibleRows(16);
  p->setEmptyText("No diagnostics");
  p->setStatus("open files only (clangd)");
  p->onQuery([this](const std::string&) { rebuild(); });
  p->onAccept([this](int row) {
    if (row < 0 || row >= static_cast<int>(shown_.size())) return;
    const Diagnostic& g = shown_[static_cast<size_t>(row)];
    std::string err;
    if (!env_.editor.gotoLocation({g.file, g.range.start}, &err))
      env_.say("Cannot open " + g.file.string() + ": " + err);
  });
  p->onClosed([this] { popup_ = nullptr; });
  popup_ = ListPopup::open(env_.root, std::move(p));
  rebuild();
}

void DiagnosticsPanel::close() {
  if (popup_) popup_->close();
}

void DiagnosticsPanel::refresh() {
  if (popup_ && signature() != sig_) rebuild();
}

void DiagnosticsPanel::rebuild() {
  const std::string filter = lower(popup_->input()->text());
  const std::filesystem::path root = env_.editor.workspace().root();
  std::vector<Diagnostic> all;
  for (Document* d : env_.editor.documents())
    for (Diagnostic g : env_.editor.diagnostics(d->path())) {
      g.file = d->path();
      if (!filter.empty() && lower(g.message).find(filter) == std::string::npos &&
          lower(relativePath(root, g.file)).find(filter) == std::string::npos)
        continue;
      all.push_back(std::move(g));
    }
  std::stable_sort(all.begin(), all.end(), [](const Diagnostic& a, const Diagnostic& b) {
    if (a.severity != b.severity) return a.severity < b.severity;
    if (a.file != b.file) return a.file < b.file;
    return a.range.start < b.range.start;
  });
  const std::string keep = popup_->selectedRow() ? popup_->selectedRow()->key : std::string();
  shown_ = all;
  std::vector<ListRow> rows;
  for (const Diagnostic& g : shown_) {
    ListRow r;
    r.key = g.file.generic_string() + ":" + std::to_string(g.range.start.line) + ":" +
            std::to_string(g.range.start.column) + g.message;
    r.icon = g.severity == 1 ? "E" : g.severity == 2 ? "W" : "I";
    r.primary = g.message.substr(0, g.message.find('\n'));
    r.detail = relativePath(root, g.file) + ":" + std::to_string(g.range.start.line + 1);
    r.badge = g.code;
    rows.push_back(std::move(r));
  }
  popup_->setRows(std::move(rows), 0);
  if (const int at = keep.empty() ? -1 : popup_->indexOfKey(keep); at >= 0) popup_->setSelected(at);
  sig_ = signature();
}

}  // namespace tf::ui
