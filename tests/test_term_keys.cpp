// Terminal key encoding (pure functions).
#include <string>

#include "term/TermKeys.h"
#include "test_main.h"

using namespace tf;

namespace {
TermKeyEvent key(TermKey k, bool ctrl = false, bool alt = false, bool shift = false) {
  TermKeyEvent e;
  e.key = k;
  e.ctrl = ctrl;
  e.alt = alt;
  e.shift = shift;
  return e;
}
TermKeyEvent text(const char* t, bool ctrl = false, bool alt = false) {
  TermKeyEvent e;
  e.text = t;
  e.ctrl = ctrl;
  e.alt = alt;
  return e;
}
}  // namespace

TEST(termkeys_text_and_ctrl) {
  CHECK_EQ(encodeTermKey(text("a")), "a");
  CHECK_EQ(encodeTermKey(text("\xC3\xA9")), "\xC3\xA9");
  CHECK_EQ(encodeTermKey(text("a", true)), std::string("\x01"));
  CHECK_EQ(encodeTermKey(text("C", true)), std::string("\x03"));
  CHECK_EQ(encodeTermKey(text("z", true)), std::string("\x1a"));
  CHECK_EQ(encodeTermKey(text(" ", true)), std::string(1, '\0'));
  CHECK_EQ(encodeTermKey(text("[", true)), "\x1b");
  CHECK_EQ(encodeTermKey(text("\\", true)), "\x1c");
  CHECK_EQ(encodeTermKey(text("]", true)), "\x1d");
  CHECK_EQ(encodeTermKey(text("_", true)), "\x1f");
  CHECK_EQ(encodeTermKey(text("?", true)), "\x7f");
  CHECK_EQ(encodeTermKey(text("1", true)), "");  // no encoding
  CHECK_EQ(encodeTermKey(text("", false)), "");
  CHECK_EQ(encodeTermKey(text("b", false, true)),
           "\x1b"
           "b");
  CHECK_EQ(encodeTermKey(text("b", true, true)), "\x1b\x02");
}

TEST(termkeys_editing_keys) {
  CHECK_EQ(encodeTermKey(key(TermKey::Enter)), "\r");
  CHECK_EQ(encodeTermKey(key(TermKey::Tab)), "\t");
  CHECK_EQ(encodeTermKey(key(TermKey::Tab, false, false, true)), "\x1b[Z");
  CHECK_EQ(encodeTermKey(key(TermKey::Backspace)), "\x7f");
  CHECK_EQ(encodeTermKey(key(TermKey::Backspace, true)), "\b");
  CHECK_EQ(encodeTermKey(key(TermKey::Backspace, false, true)), "\x1b\x7f");
  CHECK_EQ(encodeTermKey(key(TermKey::Escape)), "\x1b");
  CHECK_EQ(encodeTermKey(key(TermKey::Enter, false, true)), "\x1b\r");
}

TEST(termkeys_navigation) {
  CHECK_EQ(encodeTermKey(key(TermKey::Up)), "\x1b[A");
  CHECK_EQ(encodeTermKey(key(TermKey::Down)), "\x1b[B");
  CHECK_EQ(encodeTermKey(key(TermKey::Right)), "\x1b[C");
  CHECK_EQ(encodeTermKey(key(TermKey::Left)), "\x1b[D");
  CHECK_EQ(encodeTermKey(key(TermKey::Home)), "\x1b[H");
  CHECK_EQ(encodeTermKey(key(TermKey::End)), "\x1b[F");
  TermKeyModes app;
  app.applicationCursorKeys = true;
  CHECK_EQ(encodeTermKey(key(TermKey::Up), app), "\x1bOA");
  CHECK_EQ(encodeTermKey(key(TermKey::Home), app), "\x1bOH");
  CHECK_EQ(encodeTermKey(key(TermKey::End), app), "\x1bOF");
  CHECK_EQ(encodeTermKey(key(TermKey::Up, true), app), "\x1b[1;5A");  // modifiers always use CSI
  CHECK_EQ(encodeTermKey(key(TermKey::Left, true)), "\x1b[1;5D");
  CHECK_EQ(encodeTermKey(key(TermKey::Right, false, true)), "\x1b[1;3C");
  CHECK_EQ(encodeTermKey(key(TermKey::Right, false, false, true)), "\x1b[1;2C");
  CHECK_EQ(encodeTermKey(key(TermKey::Right, true, true, true)), "\x1b[1;8C");
  CHECK_EQ(encodeTermKey(key(TermKey::Insert)), "\x1b[2~");
  CHECK_EQ(encodeTermKey(key(TermKey::Delete)), "\x1b[3~");
  CHECK_EQ(encodeTermKey(key(TermKey::PageUp)), "\x1b[5~");
  CHECK_EQ(encodeTermKey(key(TermKey::PageDown)), "\x1b[6~");
  CHECK_EQ(encodeTermKey(key(TermKey::Delete, true)), "\x1b[3;5~");
  CHECK_EQ(encodeTermKey(key(TermKey::PageUp, false, false, true)), "\x1b[5;2~");
}

TEST(termkeys_function_keys) {
  CHECK_EQ(encodeTermKey(key(TermKey::F1)), "\x1bOP");
  CHECK_EQ(encodeTermKey(key(TermKey::F2)), "\x1bOQ");
  CHECK_EQ(encodeTermKey(key(TermKey::F3)), "\x1bOR");
  CHECK_EQ(encodeTermKey(key(TermKey::F4)), "\x1bOS");
  CHECK_EQ(encodeTermKey(key(TermKey::F5)), "\x1b[15~");
  CHECK_EQ(encodeTermKey(key(TermKey::F6)), "\x1b[17~");
  CHECK_EQ(encodeTermKey(key(TermKey::F7)), "\x1b[18~");
  CHECK_EQ(encodeTermKey(key(TermKey::F8)), "\x1b[19~");
  CHECK_EQ(encodeTermKey(key(TermKey::F9)), "\x1b[20~");
  CHECK_EQ(encodeTermKey(key(TermKey::F10)), "\x1b[21~");
  CHECK_EQ(encodeTermKey(key(TermKey::F11)), "\x1b[23~");
  CHECK_EQ(encodeTermKey(key(TermKey::F12)), "\x1b[24~");
  CHECK_EQ(encodeTermKey(key(TermKey::F1, true)), "\x1b[1;5P");
  CHECK_EQ(encodeTermKey(key(TermKey::F5, false, false, true)), "\x1b[15;2~");
  CHECK_EQ(encodeTermKey(key(TermKey::F12, true, true)), "\x1b[24;7~");
}

TEST(termkeys_paste) {
  CHECK_EQ(encodeTermPaste("a\nb", true), "\x1b[200~a\nb\x1b[201~");
  CHECK_EQ(encodeTermPaste("a\nb\r\nc", false), "a\rb\rc");
  CHECK_EQ(encodeTermPaste("x\x1b[201~y;rm", true), "\x1b[200~xy;rm\x1b[201~");
  CHECK_EQ(encodeTermPaste("", true), "\x1b[200~\x1b[201~");
  CHECK_EQ(encodeTermPaste("", false), "");
}
