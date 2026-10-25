#include "ui/platform/InputSdl.h"

namespace tf::ui {

uint8_t modsFromSdl(uint16_t m) {
  uint8_t r = ModNone;
  if (m & SDL_KMOD_SHIFT) r |= ModShift;
  if (m & SDL_KMOD_CTRL) r |= ModCtrl;
  if (m & SDL_KMOD_ALT) r |= ModAlt;
  if (m & SDL_KMOD_GUI) r |= ModSuper;
  return r;
}

Key keyFromSdl(uint32_t k) {
  if (k >= 'a' && k <= 'z') return static_cast<Key>(k - 'a' + 'A');
  if (k >= ' ' && k <= '~') return static_cast<Key>(k);
  if (k >= SDLK_F1 && k <= SDLK_F12) return static_cast<Key>(static_cast<uint32_t>(Key::F1) + (k - SDLK_F1));
  if (k >= SDLK_F13 && k <= SDLK_F24) return static_cast<Key>(static_cast<uint32_t>(Key::F13) + (k - SDLK_F13));
  if (k >= SDLK_KP_1 && k <= SDLK_KP_9) return static_cast<Key>('1' + (k - SDLK_KP_1));
  switch (k) {
    case SDLK_KP_0: return static_cast<Key>('0');
    case SDLK_KP_ENTER: return Key::KpEnter;
    case SDLK_KP_PLUS: return static_cast<Key>('+');
    case SDLK_KP_MINUS: return static_cast<Key>('-');
    case SDLK_KP_MULTIPLY: return static_cast<Key>('*');
    case SDLK_KP_DIVIDE: return static_cast<Key>('/');
    case SDLK_KP_PERIOD: return static_cast<Key>('.');
    case SDLK_BACKSPACE: return Key::Backspace;
    case SDLK_TAB: return Key::Tab;
    case SDLK_RETURN: return Key::Enter;
    case SDLK_ESCAPE: return Key::Escape;
    case SDLK_INSERT: return Key::Insert;
    case SDLK_DELETE: return Key::Delete;
    case SDLK_HOME: return Key::Home;
    case SDLK_END: return Key::End;
    case SDLK_PAGEUP: return Key::PageUp;
    case SDLK_PAGEDOWN: return Key::PageDown;
    case SDLK_LEFT: return Key::Left;
    case SDLK_RIGHT: return Key::Right;
    case SDLK_UP: return Key::Up;
    case SDLK_DOWN: return Key::Down;
    case SDLK_CAPSLOCK: return Key::CapsLock;
    case SDLK_APPLICATION: return Key::Menu;
    case SDLK_PRINTSCREEN: return Key::PrintScreen;
    case SDLK_PAUSE: return Key::Pause;
    default: return Key::Unknown;
  }
}

bool translateSdlEvent(const SDL_Event& e, float s, uint8_t fallbackMods, InputEvent& o) {
  o = InputEvent{};
  o.mods = fallbackMods;
  switch (e.type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
      o.type = e.type == SDL_EVENT_KEY_DOWN ? InputType::KeyDown : InputType::KeyUp;
      o.key = keyFromSdl(e.key.key);
      o.scancode = static_cast<uint32_t>(e.key.scancode);
      o.mods = modsFromSdl(e.key.mod);
      o.repeat = e.key.repeat;
      return true;
    case SDL_EVENT_TEXT_INPUT:
      o.type = InputType::Text;
      o.text = e.text.text ? e.text.text : "";
      return true;
    case SDL_EVENT_TEXT_EDITING:
      o.type = InputType::TextEditing;
      o.text = e.edit.text ? e.edit.text : "";
      o.editStart = e.edit.start;
      o.editLength = e.edit.length;
      return true;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
      if (e.button.button < 1 || e.button.button > 5) return false;
      o.type = e.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? InputType::MouseDown : InputType::MouseUp;
      o.button = static_cast<MouseButton>(e.button.button - 1);  // SDL: 1 left, 2 middle, 3 right, 4 X1, 5 X2
      o.clicks = e.button.clicks;
      o.x = e.button.x * s;
      o.y = e.button.y * s;
      return true;
    case SDL_EVENT_MOUSE_MOTION:
      o.type = InputType::MouseMove;
      o.x = e.motion.x * s;
      o.y = e.motion.y * s;
      o.dx = e.motion.xrel * s;
      o.dy = e.motion.yrel * s;
      o.buttons = static_cast<uint8_t>(e.motion.state & 0x1f);  // SDL button masks share our bit layout
      return true;
    case SDL_EVENT_MOUSE_WHEEL:
      o.type = InputType::Wheel;
      o.dx = e.wheel.x;
      o.dy = e.wheel.y;
      o.x = e.wheel.mouse_x * s;
      o.y = e.wheel.mouse_y * s;
      return true;
    case SDL_EVENT_WINDOW_FOCUS_GAINED: o.type = InputType::FocusGained; return true;
    case SDL_EVENT_WINDOW_FOCUS_LOST: o.type = InputType::FocusLost; return true;
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
      o.type = InputType::Resize;
      o.width = e.window.data1;
      o.height = e.window.data2;
      return true;
    case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
      o.type = InputType::ScaleChanged;
      o.scale = s;
      return true;
    case SDL_EVENT_WINDOW_EXPOSED: o.type = InputType::Expose; return true;
    case SDL_EVENT_DROP_FILE:
      o.type = InputType::DropFile;
      o.text = e.drop.data ? e.drop.data : "";
      return true;
    case SDL_EVENT_QUIT:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED: o.type = InputType::Quit; return true;
    case SDL_EVENT_USER: o.type = InputType::Wake; return true;
    default: return false;
  }
}

}  // namespace tf::ui
