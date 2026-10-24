// Deliberately awkward C++ for exercising the semantic engine: templates,
// explicit specialisations, aliases, typedefs, auto return types, decltype,
// macro-generated members, overrides, scoped and unscoped enums.
#pragma once
#include <cstddef>
#include <memory>
#include <vector>

namespace geo {

template <typename T>
struct Vec2 {                                   // @MARK:Vec2
  T x, y;
  T length_sq() const;                          // @MARK:Vec2_length_sq_decl
  Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }  // @MARK:Vec2_plus
};

template <typename T>
T Vec2<T>::length_sq() const { return x * x + y * y; }  // @MARK:Vec2_length_sq_def

using Vec2f = Vec2<float>;                      // @MARK:Vec2f
typedef Vec2<int> Vec2i;                        // @MARK:Vec2i

template <typename T>
struct Traits {                                 // @MARK:Traits
  using value_type = T;                         // @MARK:Traits_value_type
  static constexpr int dims = 2;
};

template <>
struct Traits<double> {                         // @MARK:Traits_double
  using value_type = long double;               // @MARK:Traits_double_value_type
  static constexpr int dims = 3;
};

struct Shape {                                  // @MARK:Shape
  virtual ~Shape() = default;
  virtual double area() const = 0;              // @MARK:Shape_area
  virtual const char* name() const;             // @MARK:Shape_name_decl
};

struct Circle : Shape {                         // @MARK:Circle
  double r = 1.0;
  double area() const override;                 // @MARK:Circle_area_decl
  const char* name() const override;            // @MARK:Circle_name_decl
};

#define GEO_GETTER(type, member) \
  type get_##member() const { return member; }

struct Box : Shape {                            // @MARK:Box
  double w = 1, h = 1;
  GEO_GETTER(double, w)                         // @MARK:Box_get_w
  GEO_GETTER(double, h)
  double area() const override { return w * h; }  // @MARK:Box_area
  const char* name() const override { return "box"; }  // @MARK:Box_name
};

auto make_default_shape() -> std::unique_ptr<Shape>;  // @MARK:make_default_shape_decl

template <typename C>
auto first_area(const C& c) -> decltype(c.front()->area()) {  // @MARK:first_area
  return c.front()->area();
}

inline constexpr int kMaxShapes = 64;           // @MARK:kMaxShapes

enum class Kind { Circle, Box };                // @MARK:Kind
enum Flags { kVisible = 1, kSolid = 2 };        // @MARK:Flags

}  // namespace geo
