#include "std_macros.h"

int /*@op_one*/op_one() { return 1; }
int /*@op_two*/op_two() { return 2; }
REGISTER(/*@reg_op_one*/op_one)
REGISTER(/*@reg_op_two*/op_two)

#define /*@COLORS*/COLORS(X) X(/*@c_Red*/Red) X(/*@c_Green*/Green) X(/*@c_Blue*/Blue)
/*@decl_enum*/DEFINE_ENUM(/*@Color*/Color, /*@use_COLORS_arg*/COLORS)

int /*@sum_fn*/sum(const std::vector<int>& v) {
  int total = 0;
  FOR_EACH(/*@it_decl*/it, v) { total += *(/*@use_it*/it); }
  /*@use_CHECK*/CHECK(total >= 0);
  return total;
}

int use_all() {
  auto names = /*@use_Color_names*/Color_names();
  /*@use_Color*/Color c = Color::/*@use_Red*/Red;
  std::vector<int> v = /*@use_VEC_OF*/VEC_OF(int, 1, 2, 3);
  std::string t = /*@use_trace*/trace_name();
  bool e = /*@use_checks*/checks_enabled();
  /*@use_MaybeInt*/MaybeInt m = 3;
  auto& reg = /*@use_registry*/registry();
  (void)names; (void)c; (void)e; (void)m; (void)reg;
  return /*@use_sum*/sum(v) + (int)t.size();
}
