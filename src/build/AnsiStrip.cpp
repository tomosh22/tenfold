#include "build/AnsiStrip.h"

namespace tf {

std::string stripAnsiEscapes(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] != '\x1b') {
      out += s[i];
      continue;
    }
    if (i + 1 >= s.size()) break;  // lone trailing ESC
    const char k = s[i + 1];
    if (k == '[') {  // CSI: parameter / intermediate bytes 0x20-0x3F, final byte 0x40-0x7E
      i += 2;
      while (i < s.size() && s[i] >= 0x20 && s[i] <= 0x3F) ++i;
      // s[i] is now the final byte (consumed by the loop increment) or the end of the text
    } else if (k == ']') {  // OSC: up to BEL or ST (ESC \)
      i += 2;
      while (i < s.size() && s[i] != '\a' && !(s[i] == '\x1b' && i + 1 < s.size() && s[i + 1] == '\\')) ++i;
      if (i < s.size() && s[i] == '\x1b') ++i;  // skip the backslash of ST
    } else {
      ++i;  // two-character sequence such as ESC ( or ESC =
    }
  }
  return out;
}

}  // namespace tf
