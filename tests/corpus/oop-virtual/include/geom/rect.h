#pragma once
#include "geom/shape.h"
namespace geom {
class /*@Rect*/Rect : public Shape {
 public:
  Rect(double w, double h) : w_(w), h_(h) {}
  double /*@Rect_area*/area() const override;
  Rect* /*@Rect_clone*/clone() const override;
  const char* /*@Rect_name*/name() const override { return "rect"; }
  int /*@Rect_sides*/sides() const override { return 4; }
 protected:
  double w_, h_;
};
class /*@Square*/Square : public Rect {
 public:
  explicit Square(double s) : Rect(s, s) {}
  double /*@Square_area*/area() const override;
  Square* /*@Square_clone*/clone() const override;
  const char* /*@Square_name*/name() const override { return "square"; }
};
} // namespace geom
