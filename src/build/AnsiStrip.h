#pragma once
#include <string>
#include <string_view>

namespace tf {

// Removes terminal escape sequences from build output (gcc / clang -fdiagnostics-color, -fdiagnostics-urls):
// CSI (ESC [ params final-byte), OSC (ESC ] ... terminated by BEL or ESC \) and other two-character ESC
// sequences. An unterminated sequence at the end of the text is dropped. Plain text is returned unchanged.
std::string stripAnsiEscapes(std::string_view s);

}  // namespace tf
