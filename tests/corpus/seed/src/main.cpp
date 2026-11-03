#include "geometry.h"
#include "mode.h"

int main() {
  geo::/*@use_Vec2f*/Vec2f /*@var_v*/v{1.f, 2.f};
  float l = v./*@use_length_sq*/length_sq();
  geo::Vec2i /*@var_w*/w{1, 2};
  int wx = w./*@use_wx*/x;
  auto s = geo::/*@use_make_default_shape*/make_default_shape();
  double a = s->/*@use_area*/area();
  geo::Traits<double>::/*@use_traits_double*/value_type ld = 0;
  geo::Traits<int>::/*@use_traits_int*/value_type i = 0;
  geo::Box /*@var_b*/b;
  double bw = b./*@use_get_w*/get_w();
  geo::List<geo::Owner<geo::Shape>> shapes;
  shapes.items[0] = s;
  double fa = geo::/*@use_first_area*/first_area(shapes);
  const char* n = b./*@use_box_name*/name();
  geo::Kind k = geo::Kind::/*@use_kind_box*/Box;
  geo::Vec2f sum = v /*@use_plus*/+ v;
  int mode = /*@use_mode*/mode_value();

  return static_cast<int>(l + wx + a + ld + i + bw + fa + sum.x) + static_cast<int>(k) + (n ? 0 : 1) + mode;
}
