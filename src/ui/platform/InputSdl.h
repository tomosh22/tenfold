#pragma once
// Internal to src/ui/platform (and its unit tests): the only place SDL events become InputEvents.
#include <SDL3/SDL_events.h>

#include "ui/platform/Input.h"

namespace tf::ui {

uint8_t modsFromSdl(uint16_t sdlKeymod);
Key keyFromSdl(uint32_t sdlKeycode);
// pixelScale converts SDL's window-point coordinates to pixels; fallbackMods is used for mouse
// events (SDL does not attach modifiers to them). Returns false for events we do not translate.
bool translateSdlEvent(const SDL_Event& e, float pixelScale, uint8_t fallbackMods, InputEvent& out);

}  // namespace tf::ui
