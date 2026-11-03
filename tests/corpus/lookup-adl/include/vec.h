#pragma once
#include "io.h"
namespace vec {
struct /*@vec_V*/V {
  int x = 0;
  // hidden friends: only visible through argument-dependent lookup
  friend V /*@hf_plus*/operator+(V a, V b) { return V{a.x + b.x}; }
  friend int /*@hf_dot*/dot(V a, V b) { return a.x * b.x; }
};
V /*@vec_scale*/scale(V v, int k);
bool /*@vec_eq*/operator==(const V& a, const V& b);
io::Out& /*@vec_shl*/operator<<(io::Out& o, const V& v);
} // namespace vec
