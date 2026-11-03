#pragma once
namespace geom {
class /*@Shape*/Shape {
 public:
  virtual /*@Shape_dtor*/~Shape() = default;
  virtual double /*@Shape_area*/area() const = 0;
  virtual Shape* /*@Shape_clone*/clone() const = 0;
  virtual const char* /*@Shape_name*/name() const { return "shape"; }
  virtual int /*@Shape_sides*/sides() const;
  int /*@Shape_get*/get() const { return 0; }   // not virtual: Circle::get hides it
  int /*@Shape_size*/size() const { return 1; }
};
} // namespace geom
