#pragma once
#include <string>
#include "concepts.h"

namespace cx {

// Partial specialisation selected by a constraint.
template <class T>
struct /*@Holder*/Holder { static constexpr int /*@Holder_kind*/kind = 0; };
template <Numeric T>
struct /*@Holder_num*/Holder<T> { static constexpr int /*@Holder_num_kind*/kind = 1; };
template <Pointerish T>
struct /*@Holder_ptr*/Holder<T> { static constexpr int /*@Holder_ptr_kind*/kind = 2; };

// Constrained member functions: overloaded on the class's own constraint.
template <class T>
struct /*@Buf*/Buf {
  T /*@Buf_value*/value{};
  T /*@Buf_get_num*/get() const requires Numeric<T> { return value; }
  T /*@Buf_get_other*/get() const requires (!Numeric<T>) { return T{}; }
  /*@Buf_ctor*/Buf() = default;
  /*@Buf_ctor_conv*/Buf(T v) requires Numeric<T> : value(v) {}
};

// Non-type template parameters: class type and auto.
struct /*@Fixed*/Fixed {
  int /*@Fixed_n*/n;
  constexpr /*@Fixed_ctor*/Fixed(int v) : n(v) {}
};
template <Fixed F>
struct /*@Tag*/Tag { static constexpr int /*@Tag_n*/n = F./*@use_Fixed_n_in_Tag*/n; };
template <auto V>
struct /*@Const*/Const { static constexpr auto /*@Const_value*/value = V; };

// Aggregate CTAD (C++20), designated initialisers, using enum.
template <class T>
struct /*@Triple*/Triple { T /*@Triple_a*/a; T /*@Triple_b*/b; T /*@Triple_c*/c; };

struct /*@Config*/Config {
  int /*@Config_port*/port = 0;
  bool /*@Config_tls*/tls = false;
};

enum class /*@Color*/Color { /*@Color_Red*/Red, /*@Color_Green*/Green };

}  // namespace cx
