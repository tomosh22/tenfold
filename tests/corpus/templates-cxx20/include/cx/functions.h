#pragma once
#include "concepts.h"

namespace cx {

// Constrained type parameter; defined in functions.tpp.
template <Numeric T>
T /*@sq_decl*/square(T x);

// requires-clause before and after the declarator.
template <class T>
  requires Numeric<T>
T /*@cube_decl*/cube(T x) { return x * x * x; }

template <class T>
T /*@quad_decl*/quad(T x) requires Numeric<T> { return /*@quad_sq1*/square(x) * /*@quad_sq2*/square(x); }

// Abbreviated function templates (auto parameters).
inline auto /*@halve*/halve(Numeric auto x) { return x / 2; }
inline auto /*@combine*/combine(const auto& a, const auto& b) { return a + b; }

// Overload set ordered by constraints: the most constrained viable one wins.
int /*@describe_num*/describe(const Numeric auto&) { return 1; }
int /*@describe_cont*/describe(const Container auto&) { return 2; }
int /*@describe_sized*/describe(const Sized auto&) { return 3; }
int /*@describe_any*/describe(const auto&) { return 4; }

template <class T, class U>
  requires SameSize<T, U>
bool /*@same_bytes*/same_bytes(T, U) { return true; }

// Immediate and constant evaluation.
consteval int /*@ce_len*/ce_len(const char* s) { int n = 0; while (s[n]) ++n; return n; }
constexpr int /*@cx_pow*/cx_pow(int b, int e) { return e == 0 ? 1 : b * cx_pow(b, e - 1); }
constinit inline int /*@g_counter*/g_counter = cx_pow(2, 3);

}  // namespace cx

#include "functions.tpp"
