// Seed corpus (W9a): examples/sample_project with anchors (/*@name*/token). Deliberately awkward C++ for the semantic
// engine: templates, explicit specialisations, aliases, typedefs, auto return types, decltype, macro-generated
// members, overrides, scoped and unscoped enums.
//
// No standard headers: this corpus is built from seed.sln, and on a non-Windows host the generated clang-cl commands
// cannot see the MSVC standard library. Owner / List stand in for std::unique_ptr / std::vector.
#pragma once

namespace geo {

template <typename T>
struct /*@Owner*/Owner {
  T* p = nullptr;
  T* operator->() const { return p; }
};

template <typename T>
struct /*@List*/List {
  T items[4];
  T& front() { return items[0]; }
  const T& front() const { return items[0]; }
};

template <typename T>
struct /*@Vec2*/Vec2 {
  T /*@Vec2_x*/x, y;
  T /*@Vec2_length_sq_decl*/length_sq() const;
  Vec2 /*@Vec2_plus*/operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
};

template <typename T>
T Vec2<T>::/*@Vec2_length_sq_def*/length_sq() const { return x * x + y * y; }

using /*@Vec2f*/Vec2f = Vec2<float>;
typedef Vec2<int> /*@Vec2i*/Vec2i;

template <typename T>
struct /*@Traits*/Traits {
  using /*@Traits_value_type*/value_type = T;
  static constexpr int dims = 2;
};

template <>
struct /*@Traits_double*/Traits<double> {
  using /*@Traits_double_value_type*/value_type = long double;
  static constexpr int dims = 3;
};

struct /*@Shape*/Shape {
  virtual ~Shape() = default;
  virtual double /*@Shape_area*/area() const = 0;
  virtual const char* /*@Shape_name_decl*/name() const;
};

struct /*@Circle*/Circle : Shape {
  double /*@Circle_r*/r = 1.0;
  double /*@Circle_area_decl*/area() const override;
  const char* /*@Circle_name_decl*/name() const override;
};

#define GEO_GETTER(type, member) \
  type get_##member() const { return member; }

struct /*@Box*/Box : Shape {
  double w = 1, h = 1;
  /*@Box_get_w*/GEO_GETTER(double, w)
  double /*@Box_area*/area() const override { return w * h; }
  const char* /*@Box_name*/name() const override { return "box"; }
};

auto /*@make_default_shape_decl*/make_default_shape() -> Owner<Shape>;

template <typename C>
auto /*@first_area*/first_area(const C& c) -> decltype(c.front()->area()) {
  return c.front()->area();
}

inline constexpr int /*@kMaxShapes*/kMaxShapes = 64;

enum class /*@Kind*/Kind { Circle, /*@Kind_Box*/Box };
enum /*@Flags*/Flags { kVisible = 1, kSolid = 2 };

}  // namespace geo
