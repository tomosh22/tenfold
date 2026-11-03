#pragma once

namespace ops {

struct Point3 { int x, y, z; int /*@Point3_sum*/sum() const { return x + y + z; } };

template <class T>
class Ptr {
 public:
  explicit Ptr(T* p) : p_(p) {}
  T& /*@ptr_deref*/operator*() const { return *p_; }
  T* /*@ptr_arrow*/operator->() const { return p_; }
  T& /*@ptr_index*/operator[](int i) const { return p_[i]; }
 private:
  T* p_;
};

// Functor overloaded on the argument type.
struct Adder {
  int /*@add_int*/operator()(int a) const { return a + 1; }
  double /*@add_dbl*/operator()(double a) const { return a + 0.5; }
  const char* /*@add_str*/operator()(const char* s) const { return s; }
};

}  // namespace ops
