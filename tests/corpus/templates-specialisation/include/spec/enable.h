#pragma once
#include <type_traits>

namespace spec {

// SFINAE overloads selected by enable_if on a defaulted non-type parameter.
template <class T, std::enable_if_t<std::is_integral_v<T>, int> = 0>
T /*@half_int*/half(T v) { return v / 2; }
template <class T, std::enable_if_t<std::is_floating_point_v<T>, int> = 0>
T /*@half_fp*/half(T v) { return v * 0.5; }

// enable_if in the return type.
template <class T>
std::enable_if_t<std::is_pointer_v<T>, int> /*@kind_ptr*/kind(T) { return 1; }
template <class T>
std::enable_if_t<!std::is_pointer_v<T>, int> /*@kind_val*/kind(T) { return 0; }

// Tag dispatch and if constexpr.
template <class T>
int /*@dispatch_impl_true*/dispatch_impl(T, std::true_type) { return 1; }
template <class T>
int /*@dispatch_impl_false*/dispatch_impl(T, std::false_type) { return 2; }
template <class T>
int /*@dispatch*/dispatch(T v) { return /*@use_dispatch_impl*/dispatch_impl(v, std::is_integral<T>{}); }

template <class T>
auto /*@describe*/describe(T v) {
  if constexpr (std::is_pointer_v<T>) return /*@use_kind_in_describe*/kind(v);
  else return /*@use_half_in_describe*/half(v);
}

}  // namespace spec
