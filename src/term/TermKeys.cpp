#include "term/TermKeys.h"

namespace tf {

namespace {

int modParam(const TermKeyEvent& ev) { return 1 + (ev.shift ? 1 : 0) + (ev.alt ? 2 : 0) + (ev.ctrl ? 4 : 0); }
bool anyMod(const TermKeyEvent& ev) { return ev.shift || ev.alt || ev.ctrl; }

std::string csiLetter(char letter, const TermKeyEvent& ev, bool app) {
  if (anyMod(ev)) return std::string("\x1b[1;") + std::to_string(modParam(ev)) + letter;
  return std::string(app ? "\x1bO" : "\x1b[") + letter;
}

std::string csiTilde(int n, const TermKeyEvent& ev) {
  std::string s = "\x1b[" + std::to_string(n);
  if (anyMod(ev)) s += ";" + std::to_string(modParam(ev));
  return s + "~";
}

}  // namespace

std::string encodeTermKey(const TermKeyEvent& ev, const TermKeyModes& modes) {
  const bool app = modes.applicationCursorKeys;
  switch (ev.key) {
    case TermKey::Text: {
      if (ev.text.empty()) return {};
      std::string out = ev.text;
      if (ev.ctrl && out.size() == 1) {
        unsigned char c = static_cast<unsigned char>(out[0]);
        if (c >= 'a' && c <= 'z') c = static_cast<unsigned char>(c - 'a' + 1);
        else if (c >= 'A' && c <= 'Z') c = static_cast<unsigned char>(c - 'A' + 1);
        else if (c == '@' || c == ' ') c = 0;
        else if (c >= '[' && c <= '_') c = static_cast<unsigned char>(c - '[' + 0x1B);
        else if (c == '?') c = 0x7F;
        else return {};
        out.assign(1, static_cast<char>(c));
      }
      return ev.alt ? "\x1b" + out : out;
    }
    case TermKey::Enter: return ev.alt ? "\x1b\r" : "\r";
    case TermKey::Tab:
      if (ev.shift) return "\x1b[Z";
      return ev.alt ? "\x1b\t" : "\t";
    case TermKey::Backspace: {
      std::string b(1, ev.ctrl ? '\b' : '\x7f');
      return ev.alt ? "\x1b" + b : b;
    }
    case TermKey::Escape: return ev.alt ? "\x1b\x1b" : "\x1b";
    case TermKey::Up: return csiLetter('A', ev, app);
    case TermKey::Down: return csiLetter('B', ev, app);
    case TermKey::Right: return csiLetter('C', ev, app);
    case TermKey::Left: return csiLetter('D', ev, app);
    case TermKey::Home: return csiLetter('H', ev, app);
    case TermKey::End: return csiLetter('F', ev, app);
    case TermKey::Insert: return csiTilde(2, ev);
    case TermKey::Delete: return csiTilde(3, ev);
    case TermKey::PageUp: return csiTilde(5, ev);
    case TermKey::PageDown: return csiTilde(6, ev);
    case TermKey::F1:
    case TermKey::F2:
    case TermKey::F3:
    case TermKey::F4: {
      char letter = static_cast<char>('P' + (static_cast<int>(ev.key) - static_cast<int>(TermKey::F1)));
      if (anyMod(ev)) return std::string("\x1b[1;") + std::to_string(modParam(ev)) + letter;
      return std::string("\x1bO") + letter;
    }
    default: {
      static const int codes[] = {15, 17, 18, 19, 20, 21, 23, 24};  // F5..F12
      int i = static_cast<int>(ev.key) - static_cast<int>(TermKey::F5);
      if (i < 0 || i >= 8) return {};
      return csiTilde(codes[i], ev);
    }
  }
}

std::string encodeTermPaste(const std::string& text, bool bracketed) {
  std::string body;
  body.reserve(text.size());
  for (size_t i = 0; i < text.size(); ++i) {
    char c = text[i];
    if (bracketed) {
      if (c == '\x1b' && text.compare(i, 6, "\x1b[201~") == 0) {
        i += 5;
        continue;
      }
      body += c;
    } else if (c == '\r') {
      body += '\r';
      if (i + 1 < text.size() && text[i + 1] == '\n') ++i;
    } else {
      body += (c == '\n') ? '\r' : c;
    }
  }
  return bracketed ? "\x1b[200~" + body + "\x1b[201~" : body;
}

}  // namespace tf
