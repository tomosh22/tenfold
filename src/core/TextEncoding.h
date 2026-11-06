#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace tf {

// How a file's bytes map to the UTF-8 text tenfold works with. Everything inside the editor (TextBuffer, the fast
// index, clangd, JSON) is UTF-8; files in another encoding are decoded when they are read and encoded again when they
// are written (docs/ARCHITECTURE.md, "Encodings"). Detection never fails: every byte sequence decodes.
enum class Encoding {
  Utf8,         // also the answer for plain ASCII and for files with a UTF-8 BOM (the BOM stays in the text)
  Utf16LE,      // only with a BOM (FF FE); the BOM is not part of the text and is written back
  Utf16BE,      // only with a BOM (FE FF)
  Windows1252,  // the fallback for anything else ("ANSI"/Latin-1); all 256 bytes map to one code point each
};

const char* encodingName(Encoding e);  // "UTF-8", "UTF-16 LE", "UTF-16 BE", "Windows-1252"

// Strict UTF-8 check: no overlong forms, no surrogates, nothing above U+10FFFF.
bool isValidUtf8(std::string_view s);

// The same text with every invalid byte replaced by U+FFFD. Returns `s` unchanged (no copy made beyond the return
// value) when it is valid. For text that must become JSON or be shown but never written back.
std::string sanitizeUtf8(std::string_view s);

struct DecodedText {
  std::string text;  // UTF-8
  Encoding encoding = Encoding::Utf8;
};

// Detection and decoding: valid UTF-8 (incl. ASCII, with or without BOM) -> Utf8; a UTF-16 BOM followed by well-formed
// UTF-16 -> that encoding; anything else -> Windows-1252. decodeText(encodeText(t)) is the identity for each encoding,
// and encodeText(decode(b)) == b for every byte string b (Windows-1252 maps the five undefined bytes 81 8D 8F 90 9D to
// the C1 controls U+0081 ...), apart from line endings, which TextBuffer owns.
DecodedText decodeFileBytes(std::string_view bytes);
std::string decodeAs(std::string_view bytes, Encoding e);  // `bytes` without any UTF-16 BOM handling difference: the
                                                           // UTF-16 BOM, if present, is dropped

// Encodes UTF-8 `text` as `e` (UTF-16 gets its BOM). Returns false when a character has no representation in `e`:
// a character above the Windows-1252 repertoire, or (UTF-16) a byte sequence that is not UTF-8 at all, e.g. a lone
// surrogate; UTF-8 is written as it is and never fails. `*firstBad`, when given, is the byte offset in `text` of the
// first such character and `out` is left in an unspecified state.
bool encodeText(std::string_view text, Encoding e, std::string& out, size_t* firstBad = nullptr);

// One character that the file's encoding cannot hold (Document::saveToDisk refuses to save such text). `line` and
// `column` are 0-based, `column` a UTF-8 byte offset in the line: a Position, as the buffer and gotoLocation use.
// `invalidByte` is >= 0 for a byte that is not valid UTF-8 (then `codePoint` is U+FFFD) and for the bytes of a lone
// surrogate (then `codePoint` is the surrogate and `invalidByte` its first byte); -1 for a real character.
struct UnencodableChar {
  uint32_t codePoint = 0;
  int line = 0;
  int column = 0;
  int invalidByte = -1;
  std::string text;  // the character as UTF-8 for showing it ("" for an invalid byte, U+FFFD is shown then)
};

// Every unencodable character of a text: the first `kMaxUnencodableListed` in document order, and the count of all.
inline constexpr size_t kMaxUnencodableListed = 200;
struct UnencodableReport {
  Encoding encoding = Encoding::Utf8;
  std::vector<UnencodableChar> chars;
  size_t total = 0;
  bool empty() const { return total == 0; }
};

// Appends the unencodable characters of one line (no terminator) to `rep` (rep.encoding is the target encoding;
// UTF-8 holds everything). Characters beyond the cap are counted in rep.total only.
void collectUnencodable(std::string_view line, int lineNo, UnencodableReport& rep);

// "U+6F22" (at least four hex digits).
std::string codePointLabel(uint32_t cp);
// "U+6F22", or "invalid byte 0xFF" for a byte that is not UTF-8.
std::string unencodableLabel(const UnencodableChar& c);
// "U+6F22 at line 3, column 5" (1-based, like the status bar).
std::string describeUnencodableChar(const UnencodableChar& c);
// "Windows-1252 cannot hold 2 characters (first: U+6F22 at line 3, column 5)".
std::string describeUnencodable(const UnencodableReport& rep);

}  // namespace tf
