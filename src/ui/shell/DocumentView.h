#pragma once
#include <memory>

#include "app/Editor.h"
#include "ui/widgets/Widget.h"

namespace tf::ui {

// The content widget of one tab: a view onto `doc`. A pinned TextView (B2a). Several views may show the same Document
// at once.
std::unique_ptr<Widget> makeDocumentView(Editor& editor, Document& doc);

}  // namespace tf::ui
