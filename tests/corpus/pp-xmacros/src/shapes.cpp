// Classes generated from a list, then a function template-free factory over the same list.
#define SHAPE(Name, sides) struct Name { static constexpr int /*@sides_member*/sides_ = sides; int /*@shape_area_decl*/area() const; };
#include "shapes.def"
#undef SHAPE

#define SHAPE(Name, sides) int Name::/*@shape_area_def*/area() const { return sides_ * 2; }
#include "shapes.def"
#undef SHAPE

int total_sides() {
  return /*@use_Triangle_scope*/Triangle::/*@use_sides*/sides_ + Square::sides_ + Hexagon::sides_;
}
int tri_area() { /*@use_Triangle*/Triangle t; return t./*@use_area*/area(); }
int hex_area() { return Hexagon().area(); }
