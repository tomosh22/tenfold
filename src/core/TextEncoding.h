#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
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

// What the encoding picker offers (File.ReopenWithEncoding / File.SaveWithEncoding, the status bar's segment). An
// Encoding plus, for UTF-8, whether the file carries its signature (the BOM EF BB BF). A document's choice is derived:
// Utf8Signature when its encoding is Utf8 and it begins with the signature (Document::encodingChoice).
enum class EncodingChoice { Utf8, Utf8Signature, Utf16LE, Utf16BE, Windows1252 };
inline constexpr EncodingChoice kEncodingChoices[] = {EncodingChoice::Utf8, EncodingChoice::Utf8Signature,
                                                      EncodingChoice::Utf16LE, EncodingChoice::Utf16BE,
                                                      EncodingChoice::Windows1252};
Encoding encodingOf(EncodingChoice c);
// Labels like Visual Studio's encoding widget: "UTF-8", "UTF-8 with signature", "UTF-16 LE", "UTF-16 BE",
// "Windows-1252".
const char* encodingLabel(EncodingChoice c);
// The editor.defaultEncoding spellings: "utf-8", "utf-8-bom", "utf-16le", "utf-16be", "windows-1252" (case-insensitive,
// a few aliases such as "utf8" and "cp1252"; UTF-16 is always written with its BOM). Nullopt for anything else.
std::optional<EncodingChoice> parseEncodingChoice(std::string_view setting);
const char* encodingChoiceSetting(EncodingChoice c);

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
// Detection settings (VS "Auto-detect UTF-8 encoding without signature", editor.autoDetectUtf8; the fallback code
// page).
struct DetectOptions {
  // On (Visual Studio's default): BOM-less valid UTF-8 is UTF-8. Off: only ASCII and files with a UTF-8 BOM are UTF-8,
  // any other BOM-less non-ASCII file is read in the fallback code page.
  bool autoDetectUtf8 = true;
  // The system ANSI code page the fallback stands for (0: systemAnsiCodePage()). tenfold's codec has Windows-1252
  // only, so any other page (932 on Japanese Windows) is read as 1252 (see fallbackEncoding).
  unsigned ansiCodePage = 0;
};

// The system ANSI code page: GetACP() on Windows, 1252 elsewhere. setAnsiCodePageForTesting(n) overrides it for the
// process (0 restores the system's), so tests can inject e.g. 932.
unsigned systemAnsiCodePage();
void setAnsiCodePageForTesting(unsigned codePage);
// The encoding tenfold uses for BOM-less non-UTF-8 bytes when the system ANSI page is `codePage` (0: the system's):
// always Windows-1252, the only single-byte page the codec has. `*exact`, when given, is false when the page is not
// 1252, i.e. when bytes above 0x7F decode differently than the system would. Callers log that once.
Encoding fallbackEncoding(unsigned codePage = 0, bool* exact = nullptr);

DecodedText decodeFileBytes(std::string_view bytes);
DecodedText decodeFileBytes(std::string_view bytes, const DetectOptions& opts);

// Reads `bytes` as encoding `e` because the user said so (File.ReopenWithEncoding), without detection. UTF-8 and
// Windows-1252 always succeed (UTF-8 keeps stray bytes as they are, so saving writes them back unchanged); UTF-16
// needs well-formed code units (a BOM of the same byte order is dropped, none is required) and otherwise returns
// false with a sentence in `*error`.
bool decodeForced(std::string_view bytes, Encoding e, std::string& out, std::string* error = nullptr);
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
// `columnBase` is added to every reported column (the line was given without its first bytes).
void collectUnencodable(std::string_view line, int lineNo, UnencodableReport& rep, int columnBase = 0);

// "U+6F22" (at least four hex digits).
std::string codePointLabel(uint32_t cp);
// "U+6F22", or "invalid byte 0xFF" for a byte that is not UTF-8.
std::string unencodableLabel(const UnencodableChar& c);
// "U+6F22 at line 3, column 5" (1-based, like the status bar).
std::string describeUnencodableChar(const UnencodableChar& c);
// "Windows-1252 cannot hold 2 characters (first: U+6F22 at line 3, column 5)".
std::string describeUnencodable(const UnencodableReport& rep);

}  // namespace tf
