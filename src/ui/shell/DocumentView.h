#pragma once
#include <memory>

#include "app/Editor.h"
#include "ui/widgets/Widget.h"

namespace tf::ui {

// The content widget of one tab: a view onto `doc`. This is the single seam B2's TextView plugs into; today it is
// B3a's read-only LinesView over a snapshot of the buffer. Several views may show the same Document at once.
std::unique_ptr<Widget> makeDocumentView(Editor& editor, Document& doc);

}  // namespace tf::ui
