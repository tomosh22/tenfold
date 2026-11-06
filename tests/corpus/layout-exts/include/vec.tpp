// template member definitions, kept out of the class header
#pragma once

namespace geo {

template <typename T>
Vec<T>::/*@Vec_ctor_def*/Vec(T x_, T y_) : x(x_), y(y_) {}

template <typename T>
T Vec<T>::/*@Vec_dot_def*/dot(const Vec& o) const { return /*@use_x_in_dot*/x * o.x + y * o./*@use_y_in_dot*/y; }

template <typename T>
T Vec<T>::/*@Vec_len2_def*/len2() const { return /*@use_dot_in_len2*/dot(*this); }

}  // namespace geo
