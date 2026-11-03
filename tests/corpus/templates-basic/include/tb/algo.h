#pragma once
#include <initializer_list>

namespace tb {

// Declared here, defined in algo.inl.
template <class It, class F>
void /*@apply_each_decl*/apply_each(It first, It last, F f);

template <class T>
constexpr T /*@clamp_decl*/clamp_to(T v, T lo, T hi);

template <class T>
auto /*@twice*/twice(T v) { return v + v; }                       // deduced return type

template <class A, class B>
auto /*@add_tr*/add(A a, B b) -> decltype(a + b) { return a + b; }  // trailing return type

template <class C>
decltype(auto) /*@first_ref*/first_ref(C& c) { return (c[0]); }

consteval int /*@fact*/fact(int n) { return n <= 1 ? 1 : n * fact(n - 1); }
constexpr int /*@square*/square(int n) { return n * n; }

}  // namespace tb

#include "algo.inl"
