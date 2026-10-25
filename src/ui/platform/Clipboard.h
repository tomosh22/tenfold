#pragma once
#include <string>

namespace tf::ui {
// UTF-8 text clipboard. Requires a live Window (video subsystem initialised).
std::string clipboardGet();
bool clipboardSet(const std::string& utf8);
}  // namespace tf::ui
