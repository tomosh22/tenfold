#include "ui/features/GoToLine.h"

#include <cctype>
#include <cstdlib>

namespace tf::ui {

bool GoToLine::parse(const std::string& text, int& line, int& column) {
  size_t i = 0;
  while (i < text.size() && text[i] == ' ') ++i;
  size_t j = i;
  while (j < text.size() && std::isdigit(static_cast<unsigned char>(text[j]))) ++j;
  if (j == i || j - i > 9) return false;
  line = std::atoi(text.substr(i, j - i).c_str());
  column = 1;
  if (j < text.size() && text[j] == ':') {
    size_t k = j + 1;
    size_t e = k;
    while (e < text.size() && std::isdigit(static_cast<unsigned char>(text[e]))) ++e;
    if (e == k || e - k > 9) return false;
    column = std::atoi(text.substr(k, e - k).c_str());
    j = e;
  }
  while (j < text.size() && text[j] == ' ') ++j;
  return j == text.size() && line >= 1 && column >= 1;
}

void GoToLine::open() {
  close();
  auto p = std::make_unique<ListPopup>(true);
  p->input()->setPlaceholder("Go to line[:column]");
  p->setMaxVisibleRows(1);
  p->setEmptyText("Type a line number");
  p->onQuery([this](const std::string& q) { update(q); });
  p->onAccept([this](int) {
    std::string err;
    if (line_ > 0 && !env_.editor.gotoLine(line_, column_, &err)) env_.say("Cannot go to line: " + err);
  });
  p->onClosed([this] { popup_ = nullptr; });
  popup_ = ListPopup::open(env_.root, std::move(p));
  if (const Document* d = env_.editor.currentDocument())
    popup_->setStatus("of " + std::to_string(d->buffer().lineCount()) + " lines");
}

void GoToLine::close() {
  if (popup_) popup_->close();
}

void GoToLine::update(const std::string& text) {
  line_ = 0;
  if (!popup_) return;
  const Document* d = env_.editor.currentDocument();
  if (!d || !parse(text, line_, column_)) {
    line_ = 0;
    popup_->setRows({}, -1);
    return;
  }
  const int shown = line_ > d->buffer().lineCount() ? d->buffer().lineCount() : line_;
  ListRow r;
  r.key = "goto";
  r.primary = "Go to line " + std::to_string(shown) + (column_ > 1 ? ", column " + std::to_string(column_) : "");
  r.secondary = trimmedLine(d->buffer().line(shown - 1), 80);
  popup_->setRows({r}, 0);
}

}  // namespace tf::ui
