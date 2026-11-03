#pragma once
#include <concepts>
#include <cstddef>
#include <ostream>
#include <type_traits>

namespace cx {

template <class T>
concept /*@Numeric*/Numeric = std::is_arithmetic_v<T>;

template <class T>
concept /*@Printable*/Printable = requires(const T& t, std::ostream& os) {
  { os << t } -> std::same_as<std::ostream&>;
};

template <class T>
concept /*@Container*/Container = requires(T c) {
  typename T::/*@Container_vt*/value_type;
  { c.size() } -> std::convertible_to<std::size_t>;
  c.begin();
};

// Refinement: Sized subsumes Container, which makes a Sized overload more constrained.
template <class T>
concept /*@Sized*/Sized = /*@use_Container_in_Sized*/Container<T> && requires(T c) { c.size(); };

template <class T, class U>
concept /*@SameSize*/SameSize = sizeof(T) == sizeof(U);

template <class T>
concept /*@Pointerish*/Pointerish = std::is_pointer_v<T> || requires(T p) { *p; };

}  // namespace cx
