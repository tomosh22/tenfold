// SDL -> InputEvent mapping, fed with synthetic SDL_Event structs (no window needed).
#include <cstring>

#include "ui/platform/InputSdl.h"
#include "ui_test.h"

using namespace tf::ui;

namespace {
InputEvent tr(const SDL_Event& e, float scale = 1.0f, uint8_t mods = 0) {
  InputEvent o;
  bool ok = translateSdlEvent(e, scale, mods, o);
  if (!ok) o.type = InputType::None;
  return o;
}
SDL_Event key(SDL_EventType t, SDL_Keycode k, SDL_Keymod m = SDL_KMOD_NONE, bool repeat = false) {
  SDL_Event e;
  std::memset(&e, 0, sizeof e);
  e.type = t;
  e.key.key = k;
  e.key.mod = m;
  e.key.repeat = repeat;
  e.key.scancode = SDL_SCANCODE_A;
  return e;
}
SDL_Event button(SDL_EventType t, Uint8 b, Uint8 clicks, float x, float y) {
  SDL_Event e;
  std::memset(&e, 0, sizeof e);
  e.type = t;
  e.button.button = b;
  e.button.clicks = clicks;
  e.button.x = x;
  e.button.y = y;
  return e;
}
}  // namespace

TEST(input_keys_and_modifiers) {
  InputEvent e = tr(key(SDL_EVENT_KEY_DOWN, SDLK_S, SDL_KMOD_LCTRL | SDL_KMOD_RSHIFT));
  CHECK(e.type == InputType::KeyDown);
  CHECK(e.key == static_cast<Key>('S'));
  CHECK_EQ(int(e.mods), int(ModCtrl | ModShift));
  CHECK_EQ(e.scancode, uint32_t(SDL_SCANCODE_A));
  e = tr(key(SDL_EVENT_KEY_DOWN, SDLK_RIGHT, SDL_KMOD_LALT | SDL_KMOD_RGUI, true));
  CHECK(e.key == Key::Right);
  CHECK(e.repeat);
  CHECK_EQ(int(e.mods), int(ModAlt | ModSuper));
  CHECK(tr(key(SDL_EVENT_KEY_UP, SDLK_F5)).type == InputType::KeyUp);
  CHECK(tr(key(SDL_EVENT_KEY_UP, SDLK_F5)).key == Key::F5);
  CHECK(tr(key(SDL_EVENT_KEY_UP, SDLK_F24)).key == Key::F24);
  CHECK(tr(key(SDL_EVENT_KEY_DOWN, SDLK_RETURN)).key == Key::Enter);
  CHECK(tr(key(SDL_EVENT_KEY_DOWN, SDLK_KP_ENTER)).key == Key::KpEnter);
  CHECK(tr(key(SDL_EVENT_KEY_DOWN, SDLK_ESCAPE)).key == Key::Escape);
  CHECK(tr(key(SDL_EVENT_KEY_DOWN, SDLK_PAGEDOWN)).key == Key::PageDown);
  CHECK(tr(key(SDL_EVENT_KEY_DOWN, SDLK_SPACE)).key == Key::Space);
  CHECK(tr(key(SDL_EVENT_KEY_DOWN, SDLK_1)).key == static_cast<Key>('1'));
  CHECK(tr(key(SDL_EVENT_KEY_DOWN, SDLK_KP_5)).key == static_cast<Key>('5'));
  CHECK(tr(key(SDL_EVENT_KEY_DOWN, SDLK_UNKNOWN)).key == Key::Unknown);
}

TEST(input_text_and_ime) {
  SDL_Event e;
  std::memset(&e, 0, sizeof e);
  e.type = SDL_EVENT_TEXT_INPUT;
  e.text.text = "h\xC3\xA9";  // "hé" in UTF-8
  InputEvent o = tr(e);
  CHECK(o.type == InputType::Text);
  CHECK_EQ(o.text, std::string("h\xC3\xA9"));
  std::memset(&e, 0, sizeof e);
  e.type = SDL_EVENT_TEXT_EDITING;
  e.edit.text = "\xE6\x97\xA5";
  e.edit.start = 0;
  e.edit.length = 1;
  o = tr(e);
  CHECK(o.type == InputType::TextEditing);
  CHECK_EQ(o.text, std::string("\xE6\x97\xA5"));
  CHECK_EQ(o.editLength, 1);
}

TEST(input_mouse_buttons_incl_thumb) {
  const struct {
    Uint8 sdl;
    MouseButton want;
  } map[] = {{SDL_BUTTON_LEFT, MouseButton::Left},
             {SDL_BUTTON_MIDDLE, MouseButton::Middle},
             {SDL_BUTTON_RIGHT, MouseButton::Right},
             {SDL_BUTTON_X1, MouseButton::X1},
             {SDL_BUTTON_X2, MouseButton::X2}};
  for (auto& m : map) {
    InputEvent o = tr(button(SDL_EVENT_MOUSE_BUTTON_DOWN, m.sdl, 2, 10, 20), 2.0f, ModCtrl);
    CHECK(o.type == InputType::MouseDown);
    CHECK(o.button == m.want);
    CHECK_EQ(o.clicks, 2);
    CHECK_EQ(o.x, 20.0f);  // points -> pixels at 2x
    CHECK_EQ(o.y, 40.0f);
    CHECK_EQ(int(o.mods), int(ModCtrl));  // fallback modifiers for mouse events
    CHECK(tr(button(SDL_EVENT_MOUSE_BUTTON_UP, m.sdl, 1, 0, 0)).type == InputType::MouseUp);
  }
  CHECK(tr(button(SDL_EVENT_MOUSE_BUTTON_DOWN, 9, 1, 0, 0)).type == InputType::None);
}

TEST(input_motion_wheel) {
  SDL_Event e;
  std::memset(&e, 0, sizeof e);
  e.type = SDL_EVENT_MOUSE_MOTION;
  e.motion.x = 5;
  e.motion.y = 6;
  e.motion.xrel = 1;
  e.motion.yrel = -2;
  e.motion.state = SDL_BUTTON_LMASK | SDL_BUTTON_X1MASK;
  InputEvent o = tr(e, 1.5f);
  CHECK(o.type == InputType::MouseMove);
  CHECK_EQ(o.x, 7.5f);
  CHECK_EQ(o.dy, -3.0f);
  CHECK_EQ(int(o.buttons), int((1 << int(MouseButton::Left)) | (1 << int(MouseButton::X1))));
  std::memset(&e, 0, sizeof e);
  e.type = SDL_EVENT_MOUSE_WHEEL;
  e.wheel.x = 1;
  e.wheel.y = -3;
  e.wheel.mouse_x = 10;
  e.wheel.mouse_y = 10;
  o = tr(e);
  CHECK(o.type == InputType::Wheel);
  CHECK_EQ(o.dy, -3.0f);
  CHECK_EQ(o.dx, 1.0f);
}

TEST(input_window_and_misc) {
  SDL_Event e;
  std::memset(&e, 0, sizeof e);
  e.type = SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED;
  e.window.data1 = 800;
  e.window.data2 = 600;
  InputEvent o = tr(e);
  CHECK(o.type == InputType::Resize);
  CHECK_EQ(o.width, 800);
  CHECK_EQ(o.height, 600);
  e.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
  CHECK(tr(e).type == InputType::FocusGained);
  e.type = SDL_EVENT_WINDOW_FOCUS_LOST;
  CHECK(tr(e).type == InputType::FocusLost);
  e.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
  CHECK(tr(e).type == InputType::Quit);
  e.type = SDL_EVENT_QUIT;
  CHECK(tr(e).type == InputType::Quit);
  e.type = SDL_EVENT_USER;
  CHECK(tr(e).type == InputType::Wake);
  std::memset(&e, 0, sizeof e);
  e.type = SDL_EVENT_DROP_FILE;
  e.drop.data = "/tmp/a b.txt";
  o = tr(e);
  CHECK(o.type == InputType::DropFile);
  CHECK_EQ(o.text, std::string("/tmp/a b.txt"));
  e.type = SDL_EVENT_KEYMAP_CHANGED;
  CHECK(tr(e).type == InputType::None);
}
