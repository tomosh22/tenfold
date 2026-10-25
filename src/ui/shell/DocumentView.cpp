#include "ui/shell/DocumentView.h"

#include "ui/widgets/TextView.h"

namespace tf::ui {

std::unique_ptr<Widget> makeDocumentView(Editor& editor, Document& doc) {
  return std::make_unique<TextView>(editor, &doc);  // pinned: a tab always shows its own document
}

}  // namespace tf::ui
