#pragma once
#include <functional>
#include <string>

namespace tf::ui {
// UTF-8 text clipboard. The default backend is the system clipboard and requires a live Window (video subsystem
// initialised); tests install a fake with setClipboardBackend.
struct ClipboardBackend {
  std::function<std::string()> get;
  std::function<bool(const std::string&)> set;
};
// Replaces the backend; an empty ClipboardBackend restores the system clipboard.
void setClipboardBackend(ClipboardBackend b);
std::string clipboardGet();
bool clipboardSet(const std::string& utf8);
}  // namespace tf::ui
