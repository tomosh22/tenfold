#pragma once

namespace ops {

struct Counter {
  int n = 0;
  Counter& /*@pre_inc*/operator++() { ++n; return *this; }
  Counter /*@post_inc*/operator++(int) { Counter old = *this; ++n; return old; }
  Counter& /*@pre_dec*/operator--() { --n; return *this; }
  Counter /*@post_dec*/operator--(int) { Counter old = *this; --n; return old; }
};

// C++20 rewritten comparison candidates: != and reversed == are synthesised from operator==.
struct Id {
  int v;
  /*@id_from_int*/Id(int x) : v(x) {}
  bool /*@id_eq*/operator==(const Id& o) const { return v == o.v; }
};

enum class Flags : unsigned { None = 0, A = 1, B = 2 };
constexpr Flags /*@flags_or*/operator|(Flags a, Flags b) { return static_cast<Flags>(static_cast<unsigned>(a) | static_cast<unsigned>(b)); }
constexpr Flags /*@flags_and*/operator&(Flags a, Flags b) { return static_cast<Flags>(static_cast<unsigned>(a) & static_cast<unsigned>(b)); }
constexpr Flags /*@flags_not*/operator~(Flags a) { return static_cast<Flags>(~static_cast<unsigned>(a) & 3u); }
inline Flags& /*@flags_or_eq*/operator|=(Flags& a, Flags b) { return a = a | b; }

}  // namespace ops
