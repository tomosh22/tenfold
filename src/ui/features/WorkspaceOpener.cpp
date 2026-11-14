#include "ui/features/WorkspaceOpener.h"

#include <algorithm>
#include <cctype>

#include "core/PathUtil.h"
#include "workspace/Workspace.h"

namespace tf::ui {

namespace fs = std::filesystem;

namespace {
std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}
fs::path canon(const fs::path& p) {
  std::error_code ec;
  fs::path c = fs::weakly_canonical(p, ec);
  return ec ? p.lexically_normal() : c;
}
}  // namespace

WorkspaceOpener::WorkspaceOpener(PickerEnv& env, UnsavedPrompt& unsaved) : env_(env), unsaved_(unsaved) {}

fs::path WorkspaceOpener::currentTarget() const {
  const Workspace& ws = env_.editor.workspace();
  fs::path sln = ws.chosenSolution();
  return sln.empty() ? ws.root() : sln;
}

void WorkspaceOpener::openProjectOrSolutionDialog() {
  if (!canShowDialog()) {
    env_.say("Open Project/Solution is not available here");
    return;
  }
  FileDialogRequest req;
  req.title = "Open Project/Solution";
  req.filters = {{"All Project Files", "*.sln;*.slnx;*.vcxproj"}, {"All Files", "*.*"}};
  req.startDir = env_.editor.workspace().root();
  hooks_.dialog(req, [this, life = std::weak_ptr<int>(life_)](std::vector<fs::path> picked) {
    if (life.expired() || picked.empty()) return;
    request(picked.front());
  });
}

void WorkspaceOpener::openFolderDialog() {
  if (!canShowDialog()) {
    env_.say("Open Folder is not available here");
    return;
  }
  FileDialogRequest req;
  req.folder = true;
  req.title = "Open Folder";
  req.startDir = env_.editor.workspace().root();
  hooks_.dialog(req, [this, life = std::weak_ptr<int>(life_)](std::vector<fs::path> picked) {
    if (life.expired() || picked.empty()) return;
    request(picked.front());
  });
}

bool WorkspaceOpener::request(const fs::path& path) {
  if (!canOpen()) return false;
  const WorkspaceTarget t = resolveWorkspaceTarget(path);
  const Workspace& ws = env_.editor.workspace();
  const fs::path haveSln = ws.chosenSolution();
  const bool same = canon(t.root) == canon(ws.root()) &&
                    (t.solution.empty() ? (t.file.empty() && haveSln.empty())
                                        : (!haveSln.empty() && canon(t.solution) == canon(haveSln)));
  if (same) {
    env_.say((t.solution.empty() ? pathUtf8(t.root) : pathUtf8(t.solution.filename())) + " is already open");
    return true;
  }
  std::error_code ec;
  if (!fs::exists(path, ec)) {
    env_.say("Cannot open " + pathUtf8(path) + ": it does not exist");
    return false;
  }
  std::vector<fs::path> dirty;
  if (!hooks_.confirmUnsaved || hooks_.confirmUnsaved())
    for (Document* d : env_.editor.documents())
      if (d->dirty()) dirty.push_back(d->path());
  closePicker();
  // Yes / No both go on (Yes saved the checked documents first); Cancel never calls back, so the solution stays open.
  unsaved_.ask(dirty, [open = hooks_.open, path](bool) { open(path); });
  return true;
}

void WorkspaceOpener::openRecentPicker() {
  closePicker();
  if (!canOpen()) {
    env_.say("Recent Projects and Solutions is not available here");
    return;
  }
  auto p = std::make_unique<ListPopup>(true);
  p->input()->setPlaceholder("Recent Projects and Solutions");
  p->setMaxVisibleRows(12);
  p->setWidthCells(100);
  p->setEmptyText("No recent projects or solutions");
  p->onQuery([this](const std::string& q) { fillRecent(q); });
  p->onAccept([this](int row) {
    if (row < 0 || row >= static_cast<int>(shown_.size())) return;
    chooseRecent(shown_[static_cast<size_t>(row)]);
  });
  p->onClosed([this] { popup_ = nullptr; });
  popup_ = ListPopup::open(env_.root, std::move(p));
  fillRecent({});
}

void WorkspaceOpener::closePicker() {
  if (popup_) popup_->close();
}

void WorkspaceOpener::fillRecent(const std::string& filter) {
  if (!popup_) return;
  const std::string f = lower(filter);
  const fs::path cur = canon(currentTarget());
  std::vector<ListRow> rows;
  shown_.clear();
  if (hooks_.recent) {
    for (const fs::path& e : hooks_.recent->entries()) {
      const std::string name = pathUtf8(e.filename().empty() ? e : e.filename());
      const std::string where = pathUtf8(e.parent_path());
      if (!f.empty() && lower(name).find(f) == std::string::npos && lower(where).find(f) == std::string::npos) continue;
      ListRow r;
      r.key = pathKeyUtf8(e);
      r.primary = name;
      r.detail = where;
      const bool missing = !RecentSolutions::exists(e);
      if (missing) {
        r.badge = "missing";
        r.dim = true;
      } else if (canon(e) == cur) {
        r.badge = "open";
      }
      std::error_code ec;
      r.icon = fs::is_directory(e, ec) ? "d" : "s";
      rows.push_back(std::move(r));
      shown_.push_back(e);
    }
  }
  popup_->setRows(std::move(rows), 0);
}

void WorkspaceOpener::chooseRecent(const fs::path& p) {
  if (!RecentSolutions::exists(p)) {
    // The entry points at something that is gone: say so and drop it from the list (nothing opens).
    if (hooks_.recent && hooks_.recent->drop(p)) hooks_.recent->save();
    env_.say(pathUtf8(p) + " no longer exists; removed from the recent list");
    return;
  }
  request(p);
}

}  // namespace tf::ui
