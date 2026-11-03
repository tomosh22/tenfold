#pragma once
#include <cstddef>
#include <utility>

namespace tb {

template <class... /*@Ts_list*/Ts>
struct /*@TypeList*/TypeList {
  static constexpr std::size_t /*@TypeList_size*/size = sizeof...(/*@use_Ts_sizeof*/Ts);
};

template <class... Args>
int /*@count_args*/count_args(Args&&... /*@args_param*/args) { return static_cast<int>(sizeof...(/*@use_args_sizeof*/args)); }

// Fold expressions.
template <class... Ts>
auto /*@sum_fold*/sum_fold(Ts... /*@ts_param*/ts) { return (/*@use_ts_fold*/ts + ... + 0); }

template <class... Ts>
bool /*@all_of_fold*/all_of_fold(Ts... ts) { return (... && ts); }

template <class F, class... Args>
decltype(auto) /*@call_with*/call_with(F&& f, Args&&... args) { return std::forward<F>(f)(std::forward<Args>(args)...); }

// Recursive variadic overloads.
template <class T>
T /*@vmax_base*/vmax(T a) { return a; }
template <class T, class... Rest>
T /*@vmax_rec*/vmax(T a, Rest... rest) { T m = vmax(rest...); return a > m ? a : m; }

// Pack of non-type parameters and a pack expansion in a base list.
template <int... Is>
struct /*@IntSeq*/IntSeq { static constexpr int /*@IntSeq_len*/len = sizeof...(Is); };

template <class... Bases>
struct /*@Mixin*/Mixin : Bases... {};

}  // namespace tb
