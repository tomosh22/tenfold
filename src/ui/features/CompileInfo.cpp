#include "ui/features/CompileInfo.h"

#include <sstream>

namespace tf::ui {

void CompileInfo::bind(CommandRegistry& r, std::function<void()> beforeOpen) {
  r.bind("Help.ShowCompileCommand",
         [this, beforeOpen = std::move(beforeOpen)](const CommandContext&, std::string_view) {
           if (beforeOpen) beforeOpen();
           return open();
         });
}

bool CompileInfo::open() {
  close();
  const Document* d = env_.editor.currentDocument();
  if (!d) {
    env_.say("No file is open: open a file to see its compile command");
    return false;
  }
  text_ = env_.editor.compileCommandInfo(d->path()).text(76);
  auto p = std::make_unique<ListPopup>(false);
  p->setTitle("Compile command  (Escape closes)");
  p->setMaxVisibleRows(24);
  std::vector<ListRow> rows;
  std::istringstream in(text_);
  std::string line;
  int n = 0;
  while (std::getline(in, line)) {
    ListRow r;
    r.key = std::to_string(n++);
    r.primary = line.empty() ? " " : line;
    rows.push_back(std::move(r));
  }
  p->setRows(std::move(rows), 0);
  p->onClosed([this] { popup_ = nullptr; });
  popup_ = ListPopup::open(env_.root, std::move(p));
  return true;
}

void CompileInfo::close() {
  if (popup_) popup_->close();
}

}  // namespace tf::ui
