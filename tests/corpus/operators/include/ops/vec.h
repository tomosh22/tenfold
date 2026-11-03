#pragma once
#include <compare>
#include <iosfwd>

namespace ops {

struct Vec {
  double x = 0, y = 0;

  Vec& /*@vec_pluseq*/operator+=(const Vec& o);
  Vec /*@vec_neg*/operator-() const;                        // unary minus (member)
  double& /*@vec_index*/operator[](int i);
  const double& /*@vec_index_c*/operator[](int i) const;
  double /*@vec_call1*/operator()(double scale) const;      // call operator overloaded by arity
  double /*@vec_call2*/operator()(double sx, double sy) const;
  explicit /*@vec_to_bool*/operator bool() const;
  /*@vec_to_pair*/operator double() const;                  // implicit conversion
  friend Vec /*@vec_plus*/operator+(Vec a, const Vec& b);   // friend declared here, defined in vec.cpp
  auto /*@vec_cmp*/operator<=>(const Vec&) const = default; // also declares the defaulted operator==
};

Vec /*@vec_minus*/operator-(const Vec& a, const Vec& b);   // binary minus (non-member)
Vec /*@vec_mul_vs*/operator*(const Vec& v, double s);
Vec /*@vec_mul_sv*/operator*(double s, const Vec& v);
std::ostream& /*@vec_shl*/operator<<(std::ostream& os, const Vec& v);

}  // namespace ops
