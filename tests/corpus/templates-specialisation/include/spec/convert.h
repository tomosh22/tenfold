#pragma once
#include <string>

namespace spec {

// Function template with a full specialisation: a different function from the overload.
template <class T>
std::string /*@convert_primary*/convert(T v) { return std::to_string(v); }

template <>
inline std::string /*@convert_bool*/convert<bool>(bool v) { return v ? "true" : "false"; }

inline std::string /*@convert_str_overload*/convert(const char* s) { return s; }   // an overload, not a specialisation

template <class T>
std::string /*@convert_decl_only*/convert_checked(T v);                             // defined in convert_checked.inl

template <class T>
void /*@swap_primary*/swap_it(T& a, T& b) { T t = a; a = b; b = t; }
template <>
inline void /*@swap_int*/swap_it<int>(int& a, int& b) { int t = a; a = b; b = t; }

template <class T, class U = T>
struct /*@Conv*/Conv {
  U /*@Conv_apply*/apply(T t) const { return static_cast<U>(t); }
};

}  // namespace spec

#include "convert_checked.inl"
