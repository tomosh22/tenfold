#include "ui/platform/Clipboard.h"

#include <SDL3/SDL.h>

namespace tf::ui {

namespace {
ClipboardBackend& backend() {
  static ClipboardBackend b;
  return b;
}
}  // namespace

void setClipboardBackend(ClipboardBackend b) { backend() = std::move(b); }

std::string clipboardGet() {
  if (backend().get) return backend().get();
  char* t = SDL_GetClipboardText();
  std::string s = t ? t : "";
  SDL_free(t);
  return s;
}

bool clipboardSet(const std::string& utf8) {
  if (backend().set) return backend().set(utf8);
  return SDL_SetClipboardText(utf8.c_str());
}

}  // namespace tf::ui
