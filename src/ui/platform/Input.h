#pragma once
#include <cstdint>
#include <string>

// Platform-neutral input events. Nothing in this header (or anything outside
// src/ui/platform) depends on SDL. All coordinates are in physical pixels with
// a top-left origin, matching what gfx::Renderer draws in.
namespace tf::ui {

// Printable keys use their upper-case ASCII value (letters, digits, punctuation, Space);
// everything else is >= 256.
enum class Key : uint32_t {
  Unknown = 0,
  Space = ' ',
  Backspace = 256,
  Tab,
  Enter,
  KpEnter,
  Escape,
  Insert,
  Delete,
  Home,
  End,
  PageUp,
  PageDown,
  Left,
  Right,
  Up,
  Down,
  CapsLock,
  Menu,
  PrintScreen,
  Pause,
  F1 = 300,
  F2,
  F3,
  F4,
  F5,
  F6,
  F7,
  F8,
  F9,
  F10,
  F11,
  F12,
  F13,
  F14,
  F15,
  F16,
  F17,
  F18,
  F19,
  F20,
  F21,
  F22,
  F23,
  F24,
};

enum Mod : uint8_t { ModNone = 0, ModShift = 1, ModCtrl = 2, ModAlt = 4, ModSuper = 8 };

// Bit (1 << button) of InputEvent::buttons. X1/X2 are the thumb buttons (Back/Forward).
enum class MouseButton : uint8_t { Left = 0, Middle = 1, Right = 2, X1 = 3, X2 = 4 };

enum class InputType : uint8_t {
  None,
  KeyDown,
  KeyUp,
  Text,
  TextEditing,
  MouseDown,
  MouseUp,
  MouseMove,
  Wheel,
  FocusGained,
  FocusLost,
  Resize,
  ScaleChanged,
  Expose,
  DropFile,
  Quit,
  Wake,
};

struct InputEvent {
  InputType type = InputType::None;
  uint8_t mods = ModNone;                  // Mod bit set (left/right merged)
  Key key = Key::Unknown;                  // KeyDown/KeyUp
  uint32_t scancode = 0;                   // physical key (layout independent), KeyDown/KeyUp
  bool repeat = false;                     // KeyDown auto-repeat
  std::string text;                        // Text / TextEditing (UTF-8 IME text) / DropFile (path)
  int editStart = 0, editLength = 0;       // TextEditing: selection inside the composition
  float x = 0, y = 0;                      // mouse position (pixels)
  float dx = 0, dy = 0;                    // MouseMove: relative motion (pixels); Wheel: lines, +y = up
  MouseButton button = MouseButton::Left;  // MouseDown/MouseUp
  int clicks = 0;                          // MouseDown/MouseUp: 1 single, 2 double, ...
  uint8_t buttons = 0;                     // MouseMove: buttons held
  int width = 0, height = 0;               // Resize: new size in pixels
  float scale = 1;                         // ScaleChanged: new pixels-per-point
};

}  // namespace tf::ui
