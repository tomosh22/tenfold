#include "core/TextEncoding.h"

#include <atomic>
#include <cstdint>
#include <cstring>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace tf {

namespace {

constexpr uint16_t kCp1252[32] = {0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
                                  0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
                                  0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
                                  0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178};

void appendUtf8(std::string& out, uint32_t cp) {
  if (cp < 0x80) {
    out += static_cast<char>(cp);
  } else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

// Length of the valid UTF-8 sequence at s[i], or 0 when the bytes there are not valid.
size_t utf8SeqLen(std::string_view s, size_t i) {
  const auto b = [&](size_t k) { return static_cast<unsigned char>(s[k]); };
  const unsigned char c = b(i);
  const size_t n = s.size() - i;
  if (c < 0x80) return 1;
  if (c >= 0xC2 && c <= 0xDF) return n >= 2 && (b(i + 1) & 0xC0) == 0x80 ? 2 : 0;
  if (c >= 0xE0 && c <= 0xEF) {
    if (n < 3 || (b(i + 1) & 0xC0) != 0x80 || (b(i + 2) & 0xC0) != 0x80) return 0;
    if (c == 0xE0 && b(i + 1) < 0xA0) return 0;   // overlong
    if (c == 0xED && b(i + 1) >= 0xA0) return 0;  // surrogate
    return 3;
  }
  if (c >= 0xF0 && c <= 0xF4) {
    if (n < 4 || (b(i + 1) & 0xC0) != 0x80 || (b(i + 2) & 0xC0) != 0x80 || (b(i + 3) & 0xC0) != 0x80) return 0;
    if (c == 0xF0 && b(i + 1) < 0x90) return 0;   // overlong
    if (c == 0xF4 && b(i + 1) >= 0x90) return 0;  // above U+10FFFF
    return 4;
  }
  return 0;
}

// The Windows-1252 byte for `cp`, or -1.
int cp1252Byte(uint32_t cp) {
  if (cp < 0x80 || (cp >= 0xA0 && cp <= 0xFF)) return static_cast<int>(cp);
  for (unsigned k = 0; k < 32; ++k)
    if (kCp1252[k] == cp) return static_cast<int>(0x80 + k);
  return -1;
}

uint32_t decodeUtf8At(std::string_view s, size_t i, size_t len) {
  const auto b = [&](size_t k) { return static_cast<uint32_t>(static_cast<unsigned char>(s[k])); };
  switch (len) {
    case 1: return b(i);
    case 2: return ((b(i) & 0x1F) << 6) | (b(i + 1) & 0x3F);
    case 3: return ((b(i) & 0x0F) << 12) | ((b(i + 1) & 0x3F) << 6) | (b(i + 2) & 0x3F);
    default: return ((b(i) & 0x07) << 18) | ((b(i + 1) & 0x3F) << 12) | ((b(i + 2) & 0x3F) << 6) | (b(i + 3) & 0x3F);
  }
}

bool hasBom16(std::string_view b, Encoding* e) {
  if (b.size() >= 2) {
    const auto b0 = static_cast<unsigned char>(b[0]), b1 = static_cast<unsigned char>(b[1]);
    if (b0 == 0xFF && b1 == 0xFE) return *e = Encoding::Utf16LE, true;
    if (b0 == 0xFE && b1 == 0xFF) return *e = Encoding::Utf16BE, true;
  }
  return false;
}

// Decodes UTF-16 (after the BOM). Returns false for odd lengths and unpaired surrogates.
bool decodeUtf16(std::string_view b, bool be, std::string* out) {
  if (b.size() % 2) return false;
  auto unit = [&](size_t i) -> uint32_t {
    const auto x = static_cast<unsigned char>(b[i]), y = static_cast<unsigned char>(b[i + 1]);
    return be ? (x << 8 | y) : (y << 8 | x);
  };
  if (out) out->reserve(b.size());
  for (size_t i = 0; i < b.size(); i += 2) {
    uint32_t u = unit(i);
    if (u >= 0xD800 && u <= 0xDBFF) {
      if (i + 3 >= b.size()) return false;
      const uint32_t lo = unit(i + 2);
      if (lo < 0xDC00 || lo > 0xDFFF) return false;
      u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
      i += 2;
    } else if (u >= 0xDC00 && u <= 0xDFFF) {
      return false;
    }
    if (out) appendUtf8(*out, u);
  }
  return true;
}

}  // namespace

const char* encodingName(Encoding e) {
  switch (e) {
    case Encoding::Utf8: return "UTF-8";
    case Encoding::Utf16LE: return "UTF-16 LE";
    case Encoding::Utf16BE: return "UTF-16 BE";
    case Encoding::Windows1252: return "Windows-1252";
  }
  return "UTF-8";
}

bool isValidUtf8(std::string_view s) {
  size_t i = 0;
  const size_t n = s.size();
  while (i < n) {
    // ASCII fast path, 8 bytes at a time.
    while (i + 8 <= n) {
      uint64_t w;
      std::memcpy(&w, s.data() + i, 8);
      if (w & 0x8080808080808080ULL) break;
      i += 8;
    }
    if (i >= n) break;
    const size_t len = utf8SeqLen(s, i);
    if (!len) return false;
    i += len;
  }
  return true;
}

std::string sanitizeUtf8(std::string_view s) {
  if (isValidUtf8(s)) return std::string(s);
  std::string out;
  out.reserve(s.size() + 8);
  for (size_t i = 0; i < s.size();) {
    const size_t len = utf8SeqLen(s, i);
    if (len) {
      out.append(s.data() + i, len);
      i += len;
    } else {
      out += "\xEF\xBF\xBD";
      ++i;
    }
  }
  return out;
}

std::string decodeAs(std::string_view bytes, Encoding e) {
  std::string out;
  switch (e) {
    case Encoding::Utf8: return std::string(bytes);
    case Encoding::Utf16LE:
    case Encoding::Utf16BE: {
      Encoding bom;
      if (hasBom16(bytes, &bom)) bytes.remove_prefix(2);
      if (!decodeUtf16(bytes, e == Encoding::Utf16BE, &out))
        out = sanitizeUtf8(bytes);  // not well-formed: caller's bug
      return out;
    }
    case Encoding::Windows1252:
      out.reserve(bytes.size() + bytes.size() / 8);
      for (unsigned char c : bytes) {
        if (c < 0x80) out += static_cast<char>(c);
        else if (c < 0xA0) appendUtf8(out, kCp1252[c - 0x80]);
        else appendUtf8(out, c);
      }
      return out;
  }
  return std::string(bytes);
}

Encoding encodingOf(EncodingChoice c) {
  switch (c) {
    case EncodingChoice::Utf8:
    case EncodingChoice::Utf8Signature: return Encoding::Utf8;
    case EncodingChoice::Utf16LE: return Encoding::Utf16LE;
    case EncodingChoice::Utf16BE: return Encoding::Utf16BE;
    case EncodingChoice::Windows1252: return Encoding::Windows1252;
  }
  return Encoding::Utf8;
}

const char* encodingLabel(EncodingChoice c) {
  switch (c) {
    case EncodingChoice::Utf8: return "UTF-8";
    case EncodingChoice::Utf8Signature: return "UTF-8 with signature";
    case EncodingChoice::Utf16LE: return "UTF-16 LE";
    case EncodingChoice::Utf16BE: return "UTF-16 BE";
    case EncodingChoice::Windows1252: return "Windows-1252";
  }
  return "UTF-8";
}

const char* encodingChoiceSetting(EncodingChoice c) {
  switch (c) {
    case EncodingChoice::Utf8: return "utf-8";
    case EncodingChoice::Utf8Signature: return "utf-8-bom";
    case EncodingChoice::Utf16LE: return "utf-16le";
    case EncodingChoice::Utf16BE: return "utf-16be";
    case EncodingChoice::Windows1252: return "windows-1252";
  }
  return "utf-8";
}

std::optional<EncodingChoice> parseEncodingChoice(std::string_view setting) {
  std::string s;
  for (char c : setting)
    if (c != ' ' && c != '\t') s += static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
  for (EncodingChoice c : kEncodingChoices)
    if (s == encodingChoiceSetting(c)) return c;
  if (s == "utf8") return EncodingChoice::Utf8;
  if (s == "utf8-bom" || s == "utf-8-sig") return EncodingChoice::Utf8Signature;
  if (s == "cp1252" || s == "windows1252" || s == "ansi") return EncodingChoice::Windows1252;
  return std::nullopt;
}

namespace {
std::atomic<unsigned> g_ansiOverride{0};
}

unsigned systemAnsiCodePage() {
  if (const unsigned o = g_ansiOverride.load()) return o;
#ifdef _WIN32
  return GetACP();
#else
  return 1252;
#endif
}

void setAnsiCodePageForTesting(unsigned codePage) { g_ansiOverride = codePage; }

Encoding fallbackEncoding(unsigned codePage, bool* exact) {
  if (codePage == 0) codePage = systemAnsiCodePage();
  if (exact) *exact = codePage == 1252;
  return Encoding::Windows1252;
}

DecodedText decodeFileBytes(std::string_view bytes) { return decodeFileBytes(bytes, DetectOptions{}); }

bool decodeForced(std::string_view bytes, Encoding e, std::string& out, std::string* error) {
  out.clear();
  switch (e) {
    case Encoding::Utf8: out.assign(bytes); return true;
    case Encoding::Windows1252: out = decodeAs(bytes, e); return true;
    case Encoding::Utf16LE:
    case Encoding::Utf16BE: {
      const bool be = e == Encoding::Utf16BE;
      if (bytes.size() >= 2) {
        const auto b0 = static_cast<unsigned char>(bytes[0]), b1 = static_cast<unsigned char>(bytes[1]);
        if (be ? (b0 == 0xFE && b1 == 0xFF) : (b0 == 0xFF && b1 == 0xFE)) bytes.remove_prefix(2);
      }
      if (!decodeUtf16(bytes, be, &out)) {
        out.clear();
        if (error) *error = "the file is not well-formed " + std::string(encodingName(e)) + " text";
        return false;
      }
      return true;
    }
  }
  return false;
}

DecodedText decodeFileBytes(std::string_view bytes, const DetectOptions& opts) {
  DecodedText d;
  const bool bom8 = bytes.size() >= 3 && bytes.substr(0, 3) == "\xEF\xBB\xBF";
  bool ascii = true;
  if (!opts.autoDetectUtf8 && !bom8)
    for (unsigned char c : bytes)
      if (c >= 0x80) {
        ascii = false;
        break;
      }
  if ((opts.autoDetectUtf8 || ascii || bom8) && isValidUtf8(bytes)) {
    d.text = std::string(bytes);
    return d;
  }
  Encoding e;
  if (hasBom16(bytes, &e) && decodeUtf16(bytes.substr(2), e == Encoding::Utf16BE, nullptr)) {
    d.encoding = e;
    d.text = decodeAs(bytes, e);
    return d;
  }
  d.encoding = fallbackEncoding(opts.ansiCodePage);
  d.text = decodeAs(bytes, d.encoding);
  return d;
}

bool encodeText(std::string_view text, Encoding e, std::string& out, size_t* firstBad) {
  out.clear();
  switch (e) {
    case Encoding::Utf8: out.assign(text); return true;
    case Encoding::Utf16LE:
    case Encoding::Utf16BE: {
      const bool be = e == Encoding::Utf16BE;
      auto put = [&](uint32_t u) {
        const char hi = static_cast<char>(u >> 8), lo = static_cast<char>(u & 0xFF);
        out += be ? hi : lo;
        out += be ? lo : hi;
      };
      out.reserve(text.size() * 2 + 2);
      put(0xFEFF);
      for (size_t i = 0; i < text.size();) {
        const size_t len = utf8SeqLen(text, i);
        if (!len) {  // not UTF-8 (a stray byte, a lone surrogate): UTF-16 would have to invent a character
          if (firstBad) *firstBad = i;
          return false;
        }
        uint32_t cp = decodeUtf8At(text, i, len);
        i += len;
        if (cp >= 0x10000) {
          cp -= 0x10000;
          put(0xD800 + (cp >> 10));
          put(0xDC00 + (cp & 0x3FF));
        } else {
          put(cp);
        }
      }
      return true;
    }
    case Encoding::Windows1252: {
      out.reserve(text.size());
      for (size_t i = 0; i < text.size();) {
        const size_t len = utf8SeqLen(text, i);
        const uint32_t cp = len ? decodeUtf8At(text, i, len) : 0xFFFD;
        const int b = cp1252Byte(cp);
        if (b < 0 || !len) {
          if (firstBad) *firstBad = i;
          return false;
        }
        out += static_cast<char>(b);
        i += len;
      }
      return true;
    }
  }
  return false;
}

void collectUnencodable(std::string_view line, int lineNo, UnencodableReport& rep, int columnBase) {
  if (rep.encoding == Encoding::Utf8) return;  // UTF-8 holds every character (and writes stray bytes as they are)
  for (size_t i = 0; i < line.size();) {
    const size_t len = utf8SeqLen(line, i);
    UnencodableChar c;
    size_t step = len ? len : 1;
    if (len) {
      c.codePoint = decodeUtf8At(line, i, len);
      if (rep.encoding == Encoding::Windows1252 ? cp1252Byte(c.codePoint) >= 0 : true) {
        i += step;
        continue;
      }
      c.text = std::string(line.substr(i, len));
    } else {
      const auto b = [&](size_t k) { return static_cast<unsigned char>(line[k]); };
      c.invalidByte = b(i);
      c.codePoint = 0xFFFD;
      // A CESU-style surrogate (ED A0..BF 80..BF) is reported as the surrogate it spells, all three bytes at once.
      if (b(i) == 0xED && i + 2 < line.size() && b(i + 1) >= 0xA0 && b(i + 1) <= 0xBF && (b(i + 2) & 0xC0) == 0x80) {
        c.codePoint = 0xD000 | ((b(i + 1) & 0x3Fu) << 6) | (b(i + 2) & 0x3Fu);
        step = 3;
      }
    }
    ++rep.total;
    c.line = lineNo;
    c.column = columnBase + static_cast<int>(i);
    if (rep.chars.size() < kMaxUnencodableListed) rep.chars.push_back(std::move(c));
    i += step;
  }
}

std::string codePointLabel(uint32_t cp) {
  static const char* hex = "0123456789ABCDEF";
  int digits = 4;
  while (digits < 8 && (cp >> (4 * digits)) != 0) ++digits;
  std::string s = "U+";
  for (int k = digits - 1; k >= 0; --k) s += hex[(cp >> (4 * k)) & 0xF];
  return s;
}

std::string unencodableLabel(const UnencodableChar& c) {
  if (c.invalidByte >= 0 && c.codePoint == 0xFFFD) {
    static const char* hex = "0123456789ABCDEF";
    return std::string("invalid byte 0x") + hex[(c.invalidByte >> 4) & 0xF] + hex[c.invalidByte & 0xF];
  }
  return codePointLabel(c.codePoint);
}

std::string describeUnencodableChar(const UnencodableChar& c) {
  return unencodableLabel(c) + " at line " + std::to_string(c.line + 1) + ", column " + std::to_string(c.column + 1);
}

std::string describeUnencodable(const UnencodableReport& rep) {
  std::string s = std::string(encodingName(rep.encoding)) + " cannot hold " +
                  (rep.total == 1 ? std::string("1 character") : std::to_string(rep.total) + " characters");
  if (!rep.chars.empty()) s += " (first: " + describeUnencodableChar(rep.chars[0]) + ")";
  return s;
}

}  // namespace tf
