#pragma once
#include "geom/shape.h"
namespace geom {
struct /*@Triangle*/Triangle final : Shape {
  double /*@Triangle_area*/area() const override { return 0.5 * b * h; }
  Triangle* /*@Triangle_clone*/clone() const override { return new Triangle(*this); }
  int /*@Triangle_sides*/sides() const override { return 3; }
  double b = 1, h = 1;
};
} // namespace geom
