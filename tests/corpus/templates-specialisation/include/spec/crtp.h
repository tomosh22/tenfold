#pragma once

namespace spec {

// CRTP static polymorphism: the base calls into the derived class.
template <class Derived>
struct /*@Shape*/Shape {
  double /*@Shape_area*/area() const { return static_cast<const Derived*>(this)->area_impl(); }
  double /*@Shape_scaled*/scaled(double k) const { return k * /*@use_area_in_scaled*/area(); }
  int /*@Shape_id*/id() const { return 0; }
};

struct /*@Circle*/Circle : Shape<Circle> {
  double /*@Circle_r*/r = 1;
  double /*@Circle_area_impl*/area_impl() const { return 3.14 * r * r; }
};

struct /*@Square*/Square : Shape<Square> {
  double side = 2;
  double /*@Square_area_impl*/area_impl() const { return side * side; }
  double /*@Square_area*/area() const { return side; }          // hides Shape<Square>::area
};

template <class D>
double /*@total_area*/total_area(const Shape<D>& s) { return s./*@use_area_in_total*/area() + s./*@use_id_in_total*/id(); }

// CRTP mixin that adds a method to any deriving class.
template <class D>
struct /*@Countable*/Countable {
  static inline int /*@Countable_live*/live = 0;
  Countable() { ++/*@use_live_in_ctor*/live; }
};
struct /*@Widget*/Widget : Countable<Widget> {};

}  // namespace spec
