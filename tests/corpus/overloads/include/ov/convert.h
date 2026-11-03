#pragma once
#include <initializer_list>

namespace ov {

// Implicit conversion ranking: promotions beat conversions, exact beats promotion.
int /*@conv_int*/conv(int);
int /*@conv_dbl*/conv(double);
int /*@conv_cstr*/conv(const char*);

// 0 is an int, nullptr is a pointer.
int /*@ptrq_int*/ptrq(int);
int /*@ptrq_ptr*/ptrq(void*);

struct Celsius { double deg; };
struct Kelvin {
  double k;
  Kelvin(double v) : k(v) {}
  /*@kelvin_from_c*/Kelvin(Celsius c) : k(c.deg + 273.15) {}
};
int /*@temp_k*/temp(Kelvin);
int /*@temp_s*/temp(const char*);

// Brace initialisation prefers the initializer_list constructor.
struct /*@Bag_class*/Bag {
  explicit /*@bag_n*/Bag(int n);
  /*@bag_il*/Bag(std::initializer_list<int> items);
  /*@bag_nn*/Bag(int a, int b);
  int count;
};

}  // namespace ov
