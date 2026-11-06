#include "ui/features/SaveAsPrompt.h"

namespace tf::ui {

namespace fs = std::filesystem;

namespace {
std::string trimmed(const std::string& s) {
  const size_t b = s.find_first_not_of(" \t");
  if (b == std::string::npos) return {};
  return s.substr(b, s.find_last_not_of(" \t") - b + 1);
}
}  // namespace

fs::path SaveAsPrompt::resolve(const std::string& text) const {
  const fs::path p(trimmed(text));
  if (p.empty()) return {};
  return p.is_absolute() ? p : env_.editor.workspace().root() / p;
}

std::string SaveAsPrompt::problem(const fs::path& target) const {
  if (target.empty()) return "type a file name";
  std::error_code ec;
  if (env_.editor.document(target) || fs::exists(target, ec)) return "already exists";
  if (!fs::is_directory(target.parent_path(), ec)) return "the folder does not exist";
  return {};
}

void SaveAsPrompt::open(const fs::path& untitled, const std::string& suggestion) {
  close();
  doc_ = untitled;
  auto p = std::make_unique<ListPopup>(true);
  p->input()->setPlaceholder("Save as (path in the workspace, or absolute)");
  p->setMaxVisibleRows(1);
  p->setEmptyText("Type a file name");
  p->onQuery([this](const std::string& q) { update(q); });
  p->onAccept([this](int) {
    const fs::path from = doc_;
    const std::string typed = text_;
    Document* d = env_.editor.document(from);
    const fs::path target = resolve(typed);
    std::string err = problem(target);
    UnencodableReport rep;
    if (d && err.empty() && env_.editor.saveAs(*d, target, &err, &rep)) {
      if (onSaved) onSaved(from, target);
      return;
    }
    if (d && !rep.empty()) {  // not written: the characters are listed, the document stays untitled
      env_.refused({{target, std::move(rep)}});
      return;
    }
    env_.say("Cannot save " + (target.empty() ? from.filename().string() : target.filename().string()) + ": " + err);
    open(from, typed);  // try again with what was typed
  });
  p->onClosed([this] { popup_ = nullptr; });
  popup_ = ListPopup::open(env_.root, std::move(p));
  popup_->input()->setText(suggestion, true);
  update(suggestion);
}

void SaveAsPrompt::close() {
  if (popup_) popup_->close();
}

void SaveAsPrompt::update(const std::string& text) {
  text_ = text;
  if (!popup_) return;
  const fs::path target = resolve(text);
  if (target.empty()) {
    popup_->setRows({}, -1);
    return;
  }
  const std::string why = problem(target);
  ListRow r;
  r.key = "saveas";
  r.primary = "Save as " + relativePath(env_.editor.workspace().root(), target);
  r.secondary = why;
  r.dim = !why.empty();
  popup_->setRows({r}, 0);
}

}  // namespace tf::ui
