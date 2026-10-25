#include "ui/features/ExternalChanges.h"

#include <algorithm>
#include <cctype>

#include "ui/shell/Workbench.h"

namespace tf::ui {

namespace fs = std::filesystem;

namespace {
constexpr int kPollMs = 1000;
}

ExternalChanges::Mode ExternalChanges::parseMode(const std::string& text) {
  std::string t;
  for (char c : text)
    if (!std::isspace(static_cast<unsigned char>(c)))
      t += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (t == "prompt") return Mode::Prompt;
  if (t == "never") return Mode::Never;
  return Mode::Auto;
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
  if (mode_ == Mode::Never) {
    env_.say(name(file) + " changed on disk (not reloaded: editor.reloadExternalChanges is never)");
    return;
  }
  if (mode_ == Mode::Auto && !d.dirty()) {
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
  p->setTitle(name(file) + " changed on disk");
  ListRow reloadRow, keep;
  reloadRow.key = "reload";
  reloadRow.primary = dirty ? "Reload (lose my changes)" : "Reload";
  keep.key = "keep";
  keep.primary = "Keep my version";
  p->setMaxVisibleRows(2);
  p->setRows({reloadRow, keep}, dirty ? 1 : 0);  // never default to throwing edits away
  p->onAccept([this, file, life = std::weak_ptr<int>(life_)](int row) {
    if (life.expired()) return;
    if (row == 0) reload(file);
    else env_.say("Keeping your version of " + name(file));
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
