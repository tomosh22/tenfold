#include "ui/features/ExternalChanges.h"

#include <algorithm>
#include <cctype>

#include "ui/shell/Workbench.h"

namespace tf::ui {

namespace fs = std::filesystem;

namespace {
constexpr int kPollMs = 1000;
}

ExternalChanges::Policy ExternalChanges::resolvePolicy(bool detect, bool reloadUnlessUnsaved,
                                                       const std::string& legacy) {
  std::string t;
  for (char c : legacy)
    if (!std::isspace(static_cast<unsigned char>(c)))
      t += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  Policy p{detect, reloadUnlessUnsaved, false};
  if (t == "auto") p = {true, true, false};
  else if (t == "prompt") p = {true, false, false};
  else if (t == "never") p = {false, false, true};
  return p;
}

ExternalChanges::ExternalChanges(PickerEnv& env, Workbench* bench) : env_(env), bench_(bench) {
  env_.editor.onExternalChange([this](Document& d) { onChanged(d); });
}

ExternalChanges::~ExternalChanges() {
  env_.editor.onExternalChange(nullptr);
  life_.reset();
  if (popup_) popup_->close();
}

std::string ExternalChanges::name(const fs::path& file) const {
  return relativePath(env_.editor.workspace().root(), file);
}

void ExternalChanges::onChanged(Document& d) {
  const fs::path file = d.path();
  if (!policy_.detect) {
    if (policy_.announce)
      env_.say(name(file) + " changed on disk (not reloaded: editor.reloadExternalChanges is never)");
    return;
  }
  if (policy_.reloadUnlessUnsaved && !d.dirty()) {
    reload(file);
    return;
  }
  if (file == asking_ && popup_) return;  // already asking about this file; Reload reads the newest content anyway
  if (std::find(queue_.begin(), queue_.end(), file) == queue_.end()) queue_.push_back(file);
}

void ExternalChanges::reload(const fs::path& file) {
  Document* d = env_.editor.document(file);
  if (!d) return;
  std::string err;
  if (env_.editor.reloadFromDisk(*d, &err)) env_.say("Reloaded " + name(file) + " (changed on disk)");
  else env_.say("Cannot reload " + name(file) + ": " + err);
}

void ExternalChanges::openPrompt(const fs::path& file) {
  const Document* d = env_.editor.document(file);
  if (!d) return;
  const bool dirty = d->dirty();
  asking_ = file;
  forced_ = false;
  auto p = std::make_unique<ListPopup>(false);
  p->setTitle("The file " + name(file) + " has been modified outside of the source editor. Do you want to reload it?");
  p->setWidthCells(110);
  std::vector<ListRow> rows(4);
  const char* keys[] = {"yes", "yesall", "no", "noall"};
  const char* labels[] = {"Yes", "Yes to All", "No", "No to All"};
  for (size_t i = 0; i < 4; ++i) {
    rows[i].key = keys[i];
    rows[i].primary = labels[i];
  }
  if (dirty) rows[0].secondary = "the unsaved changes are lost";
  rows[1].secondary = "reload the other changed files without unsaved changes too";
  rows[3].secondary = "keep every other changed file as it is";
  p->setMaxVisibleRows(4);
  p->setRows(std::move(rows), dirty ? 2 : 0);  // never default to throwing edits away
  p->onAccept([this, file, life = std::weak_ptr<int>(life_)](int row) {
    if (life.expired()) return;
    if (row == 0 || row == 1) reload(file);
    else env_.say("Keeping your version of " + name(file));
    if (row == 1) {  // the files waiting behind this one: clean ones reload, the ones with unsaved changes still ask
      for (auto it = queue_.begin(); it != queue_.end();) {
        const Document* d = env_.editor.document(*it);
        if (d && !d->dirty()) {
          reload(*it);
          it = queue_.erase(it);
        } else {
          ++it;
        }
      }
    } else if (row == 3) {
      queue_.clear();
    }
  });
  p->onClosed([this, file, life = std::weak_ptr<int>(life_)] {
    if (life.expired()) return;
    popup_ = nullptr;
    asking_.clear();
    // Closed by another popup opening (close()): the question stays open. The accept handler (which runs after this
    // one when the user chose) cannot be told apart here, so close() records `forced_` first.
    if (forced_) queue_.push_front(file);
  });
  popup_ = ListPopup::open(env_.root, std::move(p));
}

void ExternalChanges::close() {
  if (!popup_) return;
  forced_ = true;
  popup_->close();
}

void ExternalChanges::pollDeleted() {
  std::set<std::string> now;
  for (Document* d : env_.editor.documents()) {
    if (d->isUntitled()) continue;  // no file yet
    std::error_code ec;
    const bool gone = !fs::exists(d->path(), ec) && !ec;
    const std::string key = d->path().generic_string();
    if (gone) {
      now.insert(key);
      if (!missing_.count(key))
        env_.say(name(d->path()) + " was deleted on disk; the buffer is kept (save to recreate the file)");
    }
    if (bench_) bench_->setFileMissing(d->path(), gone);
  }
  missing_ = std::move(now);
}

int ExternalChanges::tick(int64_t nowMs) {
  if (!popup_ && !queue_.empty() && !env_.root.modal()) {
    const fs::path next = queue_.front();
    queue_.pop_front();
    openPrompt(next);
  }
  if (nowMs >= nextPoll_) {
    pollDeleted();
    nextPoll_ = nowMs + kPollMs;
  }
  return static_cast<int>(std::max<int64_t>(1, nextPoll_ - nowMs));
}

}  // namespace tf::ui
