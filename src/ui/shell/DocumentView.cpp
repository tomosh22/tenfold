#include "ui/shell/DocumentView.h"

#include "ui/widgets/LinesView.h"

namespace tf::ui {

std::unique_ptr<Widget> makeDocumentView(Editor&, Document& doc) {
  return std::make_unique<LinesView>(std::make_shared<std::vector<std::string>>(doc.buffer().lines()));
}

}  // namespace tf::ui
