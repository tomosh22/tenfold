// Definitions for algo.h
#pragma once

namespace tb {

template <class It, class F>
void /*@apply_each_def*/apply_each(It first, It last, F f) {
  for (; first != last; ++first) f(*first);
}

template <class T>
constexpr T /*@clamp_def*/clamp_to(T v, T lo, T hi) { return v < lo ? lo : (hi < v ? hi : v); }

}  // namespace tb
