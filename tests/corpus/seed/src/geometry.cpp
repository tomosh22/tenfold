#include "geometry.h"

namespace geo {

const char* /*@Shape_name_def*/Shape::name() const { return "shape"; }
double Circle::/*@Circle_area_def*/area() const { return 3.14159 * /*@circle_r_use1*/r * /*@circle_r_use2*/r; }
const char* Circle::/*@Circle_name_def*/name() const { return "circle"; }

auto /*@make_default_shape_def*/make_default_shape() -> Owner<Shape> {
  return Owner<Shape>{new Circle};
}

}  // namespace geo
