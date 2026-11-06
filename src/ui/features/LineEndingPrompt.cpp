#include "ui/features/LineEndingPrompt.h"

#include <algorithm>

namespace tf::ui {

namespace fs = std::filesystem;

namespace {
constexpr LineEnding kOrder[] = {LineEnding::CRLF, LineEnding::CR, LineEnding::LF};  // VS's list order (no NEL/LS/PS)
}

const char* LineEndingPrompt::endingName(LineEnding e) {
  return e == LineEnding::CRLF ? "Windows (CR LF)" : e == LineEnding::CR ? "Macintosh (CR)" : "Unix (LF)";
}

LineEndingPrompt::LineEndingPrompt(PickerEnv& env) : env_(env) {
  env_.editor.onInconsistentLineEndings([this](Document& d) { askNormalize(d.path()); });
}

LineEndingPrompt::~LineEndingPrompt() {
  env_.editor.onInconsistentLineEndings(nullptr);
  life_.reset();
  if (popup_) popup_->close();
}

void LineEndingPrompt::askNormalize(const fs::path& file) {
  if (file == asking_ && popup_) return;
  if (std::find(queue_.begin(), queue_.end(), file) == queue_.end()) queue_.push_back(file);
}

int LineEndingPrompt::tick(int64_t) {
  if (!popup_ && !queue_.empty() && !env_.root.modal()) {
    const fs::path next = queue_.front();
    queue_.pop_front();
    openNormalize(next);
  }
  return queue_.empty() ? -1 : 200;  // a modal is up: look again soon
}

void LineEndingPrompt::openNormalize(const fs::path& file) {
  Document* d = env_.editor.document(file);
  if (!d || !d->buffer().mixedLineEndings()) return;
  asking_ = file;
  forced_ = false;
  const TextBuffer::EndingCounts c = d->buffer().endingCounts();
  auto p = std::make_unique<ListPopup>(false);
  p->setTitle("The line endings in the following files are not consistent. Do you want to normalize the line endings?");
  std::vector<ListRow> rows;
  ListRow head;
  head.key = "file";
  head.primary = "    " + relativePath(env_.editor.workspace().root(), file);
  head.secondary = std::to_string(c.crlf) + " CR LF, " + std::to_string(c.lf) + " LF, " + std::to_string(c.cr) + " CR";
  head.dim = true;
  rows.push_back(std::move(head));
  int selected = 1;
  for (size_t i = 0; i < 3; ++i) {
    ListRow r;
    r.key = std::string("yes:") + endingName(kOrder[i]);
    r.primary = std::string("Yes: normalize line endings to ") + endingName(kOrder[i]);
    if (kOrder[i] == d->buffer().lineEnding()) selected = static_cast<int>(rows.size());
    rows.push_back(std::move(r));
  }
  ListRow no;
  no.key = "no";
  no.primary = "No";
  no.secondary = "keep the line endings as they are";
  rows.push_back(std::move(no));
  p->setMaxVisibleRows(static_cast<int>(rows.size()));
  p->setRows(std::move(rows), selected);
  p->onAccept([this, file, life = std::weak_ptr<int>(life_)](int row) {
    if (life.expired() || row < 1 || row > 3) return;  // the file row, or No
    Document* doc = env_.editor.document(file);
    if (!doc) return;
    env_.editor.setLineEnding(*doc, kOrder[row - 1]);
    env_.say(std::string("Line endings normalized to ") + endingName(kOrder[row - 1]));
  });
  p->onClosed([this, file, life = std::weak_ptr<int>(life_)] {
    if (life.expired()) return;
    popup_ = nullptr;
    asking_.clear();
    if (forced_) queue_.push_front(file);  // another popup took the screen: ask again later
  });
  popup_ = ListPopup::open(env_.root, std::move(p));
}

void LineEndingPrompt::askSave(Document& doc) {
  close();
  const fs::path file = doc.path();
  auto p = std::make_unique<ListPopup>(false);
  p->setTitle("Advanced Save Options: Line endings");
  std::vector<ListRow> rows;
  ListRow cur;
  cur.key = "current";
  cur.primary = "Current Setting";
  cur.secondary = doc.buffer().mixedLineEndings() ? "mixed" : endingName(doc.buffer().lineEnding());
  rows.push_back(std::move(cur));
  for (LineEnding e : kOrder) {
    ListRow r;
    r.key = std::string("save:") + endingName(e);
    r.primary = endingName(e);
    rows.push_back(std::move(r));
  }
  p->setMaxVisibleRows(static_cast<int>(rows.size()));
  p->setRows(std::move(rows), 0);
  p->onAccept([this, file, life = std::weak_ptr<int>(life_)](int row) {
    if (life.expired() || row < 0 || row > 3) return;
    Document* d = env_.editor.document(file);
    if (!d) return;
    std::string err;
    const bool ok = row == 0 ? env_.editor.save(*d, &err) : env_.editor.saveWithLineEnding(*d, kOrder[row - 1], &err);
    if (!ok) env_.say("Cannot save " + file.filename().string() + (err.empty() ? std::string() : ": " + err));
  });
  p->onClosed([this, life = std::weak_ptr<int>(life_)] {
    if (!life.expired()) popup_ = nullptr;
  });
  popup_ = ListPopup::open(env_.root, std::move(p));
}

void LineEndingPrompt::close() {
  if (!popup_) return;
  forced_ = true;
  popup_->close();
}

}  // namespace tf::ui
