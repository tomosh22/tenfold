#include "core/TextEncoding.h"

#include <cstdint>
#include <cstring>

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

DecodedText decodeFileBytes(std::string_view bytes) {
  DecodedText d;
  if (isValidUtf8(bytes)) {
    d.text = std::string(bytes);
    return d;
  }
  Encoding e;
  if (hasBom16(bytes, &e) && decodeUtf16(bytes.substr(2), e == Encoding::Utf16BE, nullptr)) {
    d.encoding = e;
    d.text = decodeAs(bytes, e);
    return d;
  }
  d.encoding = Encoding::Windows1252;
  d.text = decodeAs(bytes, Encoding::Windows1252);
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
        size_t len = utf8SeqLen(text, i);
        uint32_t cp = len ? decodeUtf8At(text, i, len) : 0xFFFD;
        i += len ? len : 1;
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
        unsigned char b = 0;
        bool ok = true;
        if (cp < 0x80 || (cp >= 0xA0 && cp <= 0xFF)) {
          b = static_cast<unsigned char>(cp);
        } else {
          ok = false;
          for (unsigned k = 0; k < 32; ++k)
            if (kCp1252[k] == cp) {
              b = static_cast<unsigned char>(0x80 + k);
              ok = true;
              break;
            }
        }
        if (!ok) {
          if (firstBad) *firstBad = i;
          return false;
        }
        out += static_cast<char>(b);
        i += len ? len : 1;
      }
      return true;
    }
  }
  return false;
}

}  // namespace tf
