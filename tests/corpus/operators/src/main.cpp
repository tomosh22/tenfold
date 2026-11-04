#include <iostream>
#include <sstream>
#include "ops/counter.h"
#include "ops/literals.h"
#include "ops/ptr.h"
#include "ops/vec.h"

using namespace ops;
using namespace ops::literals;

int main() {
  Vec a{1, 2}, b{3, 4};
  const Vec ca{5, 6};
  // ---- member / non-member / friend, unary vs binary
  a /*@use_pluseq*/+= b;
  Vec n = /*@use_neg*/-a;
  Vec s = a /*@use_plus*/+ b;
  Vec d = a /*@use_minus*/- b;
  Vec m1 = a /*@use_mul_vs*/* 2.0;
  Vec m2 = 2.0 /*@use_mul_sv*/* a;
  // ---- subscript, call
  double i0 = a/*@use_index*/[0];
  double i1 = ca/*@use_index_c*/[1];
  double c1 = a/*@use_call1*/(2.0);
  double c2 = a/*@use_call2*/(2.0, 3.0);
  double c3 = a./*@use_call_explicit*/operator()(1.0);   // explicit operator-function-id
  // ---- conversions (explicit spelling; implicit conversions have no token to click)
  bool bv = a./*@use_to_bool*/operator bool();
  double dv = a./*@use_to_double*/operator double();
  // ---- chained stream output: each << is a different function
  std::ostringstream os;
  os << "v=" /*@use_shl*/<< a << ' ';
  os /*@use_shl_str*/<< "x" /*@use_shl_vec*/<< b;
  // ---- defaulted <=> and ==
  bool lt = a /*@use_lt*/< b;
  bool ge = a /*@use_ge*/>= b;
  bool ee = a /*@use_eqeq*/== b;
  auto cmp = a /*@use_spaceship*/<=> b;
  // ---- ++ / --
  Counter cnt;
  /*@use_pre_inc*/++cnt;
  cnt/*@use_post_inc*/++;
  /*@use_pre_dec*/--cnt;
  cnt/*@use_post_dec*/--;
  // ---- C++20 rewritten candidates
  Id x{1}, y{2};
  bool e1 = x /*@use_id_eq*/== y;
  bool e2 = x /*@use_id_ne*/!= y;                         // rewritten to !(x == y)
  bool e3 = 1 /*@use_id_rev*/== y;                        // reversed operands, 1 converts through Id(int)
  // ---- enum operators
  Flags f = Flags::A /*@use_flags_or*/| Flags::B;
  Flags g = f /*@use_flags_and*/& Flags::B;
  Flags h = /*@use_flags_not*/~f;
  h /*@use_flags_or_eq*/|= Flags::A;
  // ---- smart-pointer style operators and the member they lead to
  Point3 pt{1, 2, 3};
  Ptr<Point3> pp(&pt);
  int px = pp/*@use_arrow*/->/*@use_arrow_x*/x;
  int ps = pp->/*@use_arrow_sum*/sum();
  int dy = (/*@use_deref*/*pp)./*@use_deref_y*/y;
  Ptr<int> pi(&px);
  int pi0 = pi/*@use_ptr_index*/[0];
  // ---- functor overloads
  Adder add;
  int r1 = add/*@use_add_int*/(1);
  double r2 = add/*@use_add_dbl*/(1.5);
  const char* r3 = add/*@use_add_str*/("s");
  // ---- user-defined literals
  Length l1 = /*@use_lit_km*/2.5_km;
  Length l2 = /*@use_lit_m*/3.0_m;
  Length l3 = /*@use_lit_m_int*/3_m;
  Name nm = /*@use_lit_name*/"abc"_name;
  (void)n; (void)s; (void)d; (void)m1; (void)m2; (void)i0; (void)i1; (void)c1; (void)c2; (void)c3; (void)bv;
  (void)dv; (void)lt; (void)ge; (void)ee; (void)cmp; (void)e1; (void)e2; (void)e3; (void)g; (void)px; (void)ps;
  (void)dy; (void)pi0; (void)r1; (void)r2; (void)r3; (void)l1; (void)l2; (void)l3; (void)nm;
  return 0;
}
