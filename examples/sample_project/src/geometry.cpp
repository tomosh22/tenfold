#include "geometry.h"

namespace geo {

const char* Shape::name() const { return "shape"; }      // @MARK:Shape_name_def
double Circle::area() const { return 3.14159 * r * r; }  // @MARK:Circle_area_def
const char* Circle::name() const { return "circle"; }    // @MARK:Circle_name_def

auto make_default_shape() -> std::unique_ptr<Shape> {    // @MARK:make_default_shape_def
  return std::make_unique<Circle>();
}

}  // namespace geo
