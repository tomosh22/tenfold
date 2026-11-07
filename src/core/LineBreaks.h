#pragma once
#include <algorithm>
#include <cstring>
#include <string_view>

namespace tf {

// The line breaks of the editor (Visual Studio's): "\r\n", "\n", a lone '\r', and the Unicode separators NEL (U+0085,
// UTF-8 C2 85), LS (U+2028, E2 80 A8) and PS (U+2029, E2 80 A9). clang and clangd know only the first three; the
// Unicode ones are ordinary characters to them (docs/ARCHITECTURE.md, "Text for clangd").
namespace linebreak {

// Length (2 or 3) of the NEL / LS / PS sequence that starts at t[i], else 0.
inline size_t separatorLen(std::string_view t, size_t i) {
  if (i >= t.size()) return 0;
  const unsigned char c = static_cast<unsigned char>(t[i]);
  if (c == 0xC2) return (i + 1 < t.size() && static_cast<unsigned char>(t[i + 1]) == 0x85) ? 2 : 0;
  if (c == 0xE2)
    return (i + 2 < t.size() && static_cast<unsigned char>(t[i + 1]) == 0x80 &&
            (static_cast<unsigned char>(t[i + 2]) == 0xA8 || static_cast<unsigned char>(t[i + 2]) == 0xA9))
               ? 3
               : 0;
  return 0;
}

// Length of the line break that starts at t[i] ("\r\n" = 2), 0 if there is none there.
inline size_t breakLenAt(std::string_view t, size_t i) {
  if (i >= t.size()) return 0;
  const char c = t[i];
  if (c == '\n') return 1;
  if (c == '\r') return (i + 1 < t.size() && t[i + 1] == '\n') ? 2 : 1;
  return separatorLen(t, i);
}

// Does `t` end with a line break?
inline bool endsWithBreak(std::string_view t) {
  if (t.empty()) return false;
  if (t.back() == '\n' || t.back() == '\r') return true;
  return (t.size() >= 2 && separatorLen(t, t.size() - 2) == 2) || (t.size() >= 3 && separatorLen(t, t.size() - 3) == 3);
}

// Does `t` contain a NEL / LS / PS? (Two memchr passes; ASCII-only text costs nothing more.)
inline bool hasSeparator(std::string_view t) {
  size_t from = 0;
  while (from < t.size()) {
    const void* p = std::memchr(t.data() + from, 0xC2, t.size() - from);
    const void* q = std::memchr(t.data() + from, 0xE2, t.size() - from);
    if (!p && !q) return false;
    size_t k = p && q ? static_cast<size_t>(std::min(static_cast<const char*>(p), static_cast<const char*>(q)) - t.data())
                      : static_cast<size_t>((p ? static_cast<const char*>(p) : static_cast<const char*>(q)) - t.data());
    if (separatorLen(t, k)) return true;
    from = k + 1;
  }
  return false;
}

// Finds the line breaks of a text. next(from, len) gives the offset of the first break at or after `from` (npos if
// none) and its length. '\n' is found with memchr per line; the positions of the next '\r', NEL and LS/PS are
// remembered, so a text without them (or with CRLF everywhere) costs one extra memchr pass over the text in total, not
// one per line.
class BreakFinder {
 public:
  explicit BreakFinder(std::string_view t) : t_(t) {}
  size_t next(size_t from, size_t& len) {
    constexpr size_t npos = std::string_view::npos;
    const size_t n = t_.size();
    if (from >= n) return npos;
    const char* base = t_.data();
    const char* nlp = static_cast<const char*>(std::memchr(base + from, '\n', n - from));
    size_t best = nlp ? static_cast<size_t>(nlp - base) : npos;
    size_t blen = 1;
    if (!crKnown_ || (cr_ != npos && cr_ < from)) {
      const char* c = static_cast<const char*>(std::memchr(base + from, '\r', n - from));
      cr_ = c ? static_cast<size_t>(c - base) : npos;
      crKnown_ = true;
    }
    if (cr_ != npos && cr_ < best) {
      best = cr_;
      blen = (cr_ + 1 < n && base[cr_ + 1] == '\n') ? 2 : 1;
    }
    if (!nelKnown_ || (nel_ != npos && nel_ < from)) {
      nel_ = scan(from, 0xC2);
      nelKnown_ = true;
    }
    if (nel_ != npos && nel_ < best) {
      best = nel_;
      blen = 2;
    }
    if (!lsKnown_ || (ls_ != npos && ls_ < from)) {
      ls_ = scan(from, 0xE2);
      lsKnown_ = true;
    }
    if (ls_ != npos && ls_ < best) {
      best = ls_;
      blen = 3;
    }
    if (best == npos) return npos;
    len = blen;
    return best;
  }

 private:
  // The first NEL (lead 0xC2) or LS/PS (lead 0xE2) at or after `from`.
  size_t scan(size_t from, unsigned char lead) const {
    const size_t n = t_.size();
    while (from < n) {
      const void* p = std::memchr(t_.data() + from, lead, n - from);
      if (!p) return std::string_view::npos;
      const size_t k = static_cast<size_t>(static_cast<const char*>(p) - t_.data());
      if (separatorLen(t_, k)) return k;
      from = k + 1;
    }
    return std::string_view::npos;
  }

  std::string_view t_;
  size_t cr_ = 0, nel_ = 0, ls_ = 0;
  bool crKnown_ = false, nelKnown_ = false, lsKnown_ = false;
};

}  // namespace linebreak
}  // namespace tf
