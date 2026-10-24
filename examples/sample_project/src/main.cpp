#include <utility>
#include <vector>

#include "geometry.h"

int main() {
  geo::Vec2f v{1.f, 2.f};                       // @MARK:decl_v
  float l = v.length_sq();                      // @MARK:use_length_sq
  geo::Vec2i w{1, 2};
  int wx = w.x;                                 // @MARK:use_wx
  auto s = geo::make_default_shape();           // @MARK:use_make_default_shape
  double a = s->area();                         // @MARK:use_area
  geo::Traits<double>::value_type ld = 0;       // @MARK:use_traits_double
  geo::Traits<int>::value_type i = 0;           // @MARK:use_traits_int
  geo::Box b;
  double bw = b.get_w();                        // @MARK:use_get_w
  std::vector<std::unique_ptr<geo::Shape>> shapes;
  shapes.push_back(std::move(s));
  double fa = geo::first_area(shapes);          // @MARK:use_first_area
  const char* n = b.name();                     // @MARK:use_box_name
  geo::Kind k = geo::Kind::Box;                 // @MARK:use_kind_box
  geo::Vec2f sum = v + v;                       // @MARK:use_plus
  return static_cast<int>(l + wx + a + ld + i + bw + fa + sum.x) + static_cast<int>(k) + (n ? 0 : 1);
}
