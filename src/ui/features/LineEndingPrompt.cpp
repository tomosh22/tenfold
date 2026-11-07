#include "ui/features/LineEndingPrompt.h"

#include <algorithm>

namespace tf::ui {

namespace fs = std::filesystem;

namespace {
constexpr LineEnding kOrder[] = {LineEnding::CRLF, LineEnding::CR, LineEnding::LF};  // Advanced Save Options: no LS/PS
}

// The drop-down of the consistency dialog, in VS's order. LS and PS are listed but disabled until TextBuffer has them
// as line breaks: branch task/LE-UNICODE adds LineEnding::LS and LineEnding::PS; when it is merged replace the
// std::nullopt of the two rows by those values (one line each) and the rows become choosable.
const LineEndingPrompt::Entry LineEndingPrompt::kEntries[5] = {
    {"Windows (CR LF)", LineEnding::CRLF},
    {"Macintosh (CR)", LineEnding::CR},
    {"Unix (LF)", LineEnding::LF},
    {"Unicode Line Separator (LS)", std::nullopt},       // task/LE-UNICODE: LineEnding::LS
    {"Unicode Paragraph Separator (PS)", std::nullopt},  // task/LE-UNICODE: LineEnding::PS
};

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
  if (dialog_) dialog_->close();
}

void LineEndingPrompt::askNormalize(const fs::path& file) {
  if (std::find(asking_.begin(), asking_.end(), file) != asking_.end() && dialog_) return;
  if (std::find(queue_.begin(), queue_.end(), file) == queue_.end()) queue_.push_back(file);
}

int LineEndingPrompt::tick(int64_t) {
  if (!popup_ && !dialog_ && !queue_.empty() && !env_.root.modal()) {
    std::vector<fs::path> files(queue_.begin(), queue_.end());  // everything queued so far: one dialog, like VS
    queue_.clear();
    openNormalize(files);
  }
  return queue_.empty() ? -1 : 200;  // a modal is up: look again soon
}

std::vector<DropDownItem> LineEndingPrompt::dropDownItems() {
  std::vector<DropDownItem> items;
  for (const Entry& e : kEntries) items.push_back({e.name, e.ending.has_value()});
  return items;
}

void LineEndingPrompt::openNormalize(const std::vector<fs::path>& candidates) {
  std::vector<fs::path> files;
  std::vector<LineEndingDialog::File> shown;
  TextBuffer::EndingCounts sum{};
  Document* only = nullptr;
  for (const fs::path& f : candidates) {
    Document* d = env_.editor.document(f);
    if (!d || !d->buffer().mixedLineEndings()) continue;
    const TextBuffer::EndingCounts c = d->buffer().endingCounts();
    sum.crlf += c.crlf;
    sum.lf += c.lf;
    sum.cr += c.cr;
    only = d;
    files.push_back(f);
    shown.push_back(
        {relativePath(env_.editor.workspace().root(), f),
         std::to_string(c.crlf) + " CR LF, " + std::to_string(c.lf) + " LF, " + std::to_string(c.cr) + " CR"});
  }
  if (files.empty()) return;
  asking_ = files;
  forced_ = false;
  // Pre-selected: the most frequent ending (VS's own choice is unknown). One file: its dominant ending; several: the
  // dominant one over all of them (ties LF, then CRLF).
  LineEnding pre = only->buffer().lineEnding();
  if (files.size() > 1)
    pre = sum.lf >= sum.crlf && sum.lf >= sum.cr ? LineEnding::LF
          : sum.crlf >= sum.cr                   ? LineEnding::CRLF
                                                 : LineEnding::CR;
  int selected = 0;
  for (size_t i = 0; i < std::size(kEntries); ++i)
    if (kEntries[i].ending && *kEntries[i].ending == pre) selected = static_cast<int>(i);
  auto d = std::make_unique<LineEndingDialog>();
  d->setMessage(
      "The line endings in the following files are not consistent. Do you want to normalize the line endings?");
  d->setFiles(std::move(shown));
  d->dropDown()->setItems(dropDownItems(), selected);
  d->onAnswer([this, files, life = std::weak_ptr<int>(life_)](bool yes, int entry, bool never) {
    if (life.expired()) return;
    if (never) disableCheck();
    if (!yes || entry < 0 || entry >= static_cast<int>(std::size(kEntries)) || !kEntries[entry].ending) return;
    size_t done = 0;
    for (const fs::path& f : files)
      if (Document* doc = env_.editor.document(f)) {
        env_.editor.setLineEnding(*doc, *kEntries[entry].ending);
        ++done;
      }
    if (done == 1) env_.say(std::string("Line endings normalized to ") + kEntries[entry].name);
    else if (done > 1)
      env_.say("Line endings of " + std::to_string(done) + " files normalized to " + kEntries[entry].name);
  });
  d->onClosed([this, files, life = std::weak_ptr<int>(life_)] {
    if (life.expired()) return;
    dialog_ = nullptr;
    asking_.clear();
    if (forced_)  // another popup took the screen: ask again later
      queue_.insert(queue_.begin(), files.begin(), files.end());
  });
  dialog_ = LineEndingDialog::open(env_.root, std::move(d));
}

void LineEndingPrompt::disableCheck() {
  env_.editor.editing().checkLineEndings = false;
  if (persistCheckOff) persistCheckOff();
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
  forced_ = true;
  if (dialog_) dialog_->close();
  forced_ = false;
  if (popup_) popup_->close();
}

}  // namespace tf::ui
