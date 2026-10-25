#include "ui/platform/Clipboard.h"

#include <SDL3/SDL.h>

namespace tf::ui {

std::string clipboardGet() {
  char* t = SDL_GetClipboardText();
  std::string s = t ? t : "";
  SDL_free(t);
  return s;
}

bool clipboardSet(const std::string& utf8) { return SDL_SetClipboardText(utf8.c_str()); }

}  // namespace tf::ui
