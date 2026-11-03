#pragma once
#include <string>

namespace tf {

// Key encoding for a terminal: editor key events -> the bytes a program in the terminal expects (xterm conventions).
// Pure functions, no state; the modes come from VtScreen (applicationCursorKeys(), bracketedPaste()).

enum class TermKey {
  Text,  // a typed character / string: TermKeyEvent::text (UTF-8)
  Enter,
  Backspace,
  Tab,
  Escape,
  Up,
  Down,
  Left,
  Right,
  Home,
  End,
  PageUp,
  PageDown,
  Insert,
  Delete,
  F1,
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
};

struct TermKeyEvent {
  TermKey key = TermKey::Text;
  std::string text;  // for TermKey::Text: the character(s) as UTF-8 (the base character for Ctrl/Alt combinations)
  bool ctrl = false, alt = false, shift = false;
};

struct TermKeyModes {
  bool applicationCursorKeys = false;  // VtScreen::applicationCursorKeys(): arrows / Home / End use SS3 (ESC O x)
};

// Returns the bytes to write to the Pty; empty when the event has no encoding (e.g. Ctrl+unmapped character).
//  * Text: as is; Ctrl+letter -> 0x01..0x1A, Ctrl+@/Space -> 0, Ctrl+[ \ ] ^ _ -> 0x1B..0x1F, Ctrl+? -> 0x7F; Alt
//    prefixes ESC.
//  * Enter CR, Tab HT, Shift+Tab ESC [ Z, Backspace DEL (Ctrl+Backspace BS), Escape ESC; Alt prefixes ESC.
//  * Arrows / Home / End: ESC [ x (ESC O x in application cursor mode); with modifiers ESC [ 1 ; m x, where
//    m = 1 + shift + 2 alt + 4 ctrl. Insert, Delete, PageUp, PageDown: ESC [ n ~ (ESC [ n ; m ~).
//  * F1-F4: ESC O P..S (ESC [ 1 ; m P..S with modifiers); F5-F12: ESC [ 15/17/18/19/20/21/23/24 ~.
std::string encodeTermKey(const TermKeyEvent& ev, const TermKeyModes& modes = {});

// Pasted text: wrapped in ESC [ 200 ~ ... ESC [ 201 ~ when bracketed paste is on (any embedded ESC [ 201 ~ is
// removed so a paste cannot end the bracket early); otherwise the text with line breaks normalised to CR.
std::string encodeTermPaste(const std::string& text, bool bracketed);

}  // namespace tf
