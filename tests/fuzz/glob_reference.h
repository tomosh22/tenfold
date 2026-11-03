#pragma once

namespace tf::fuzz {

// The glob matcher as it was before it got a memo (exponential, but obviously right): the reference the optimised
// TextSearch::globMatch is compared with on small inputs.
inline bool referenceGlob(const char* p, const char* pe, const char* s, const char* se) {
  while (p < pe) {
    if (*p == '*') {
      if (p + 1 < pe && p[1] == '*') {
        while (p < pe && *p == '*') ++p;
        if (p < pe && *p == '/' && referenceGlob(p + 1, pe, s, se)) return true;
        for (const char* t = s;; ++t) {
          if (referenceGlob(p, pe, t, se)) return true;
          if (t == se) break;
        }
        return false;
      }
      ++p;
      for (const char* t = s;; ++t) {
        if (referenceGlob(p, pe, t, se)) return true;
        if (t == se || *t == '/') break;
      }
      return false;
    }
    if (s == se) return false;
    if (*p == '?') {
      if (*s == '/') return false;
    } else if (*p != *s) {
      return false;
    }
    ++p;
    ++s;
  }
  return s == se;
}

}  // namespace tf::fuzz
