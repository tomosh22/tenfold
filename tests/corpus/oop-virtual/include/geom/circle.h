#pragma once
#include "geom/shape.h"
namespace geom {
class /*@Circle*/Circle : public Shape {
 public:
  explicit /*@Circle_ctor*/Circle(double r) : r_(r) {}
  /*@Circle_dtor*/~Circle() override {}
  double /*@Circle_area*/area() const override { return 3.14 * r_ * r_; }
  Circle* /*@Circle_clone*/clone() const override { return new Circle(*this); }  // covariant return
  const char* /*@Circle_name*/name() const override final { return "circle"; }
  int /*@Circle_get*/get() const { return 1; }
  double /*@Circle_r*/r_;
};
} // namespace geom
