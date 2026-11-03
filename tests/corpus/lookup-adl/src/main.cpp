#include "vec.h"
#include "shape.h"
#include "swap.h"
#include "hide.h"
#include "friends.h"

void Derived::call() {
  /*@use_Derived_f*/f(1);        // Derived::f hides Base::f and ::f
  Base::/*@use_Base_f_qual*/f(1);
  ::/*@use_g_f_qual*/f(1);
}
void Derived2::call() {
  /*@use_D2_f_int*/f(1);         // Base::f(int) re-exposed by the using-declaration
  /*@use_D2_f_double*/f(1.5);
  /*@use_D2_f_char*/f('c');
}
int S::total(int /*@p_count_def*/count) {
  return /*@use_p_count*/count + this->/*@use_S_count*/count + ::/*@use_g_count*/count;
}
int S::member() { return /*@use_member_count*/count; }
int S::global() { int /*@local_count*/count = 7; return /*@use_local_count*/count; }

int main() {
  shape::Circle c;
  shape::Square sq;
  /*@use_draw_circle*/draw(c);
  /*@use_draw_square*/draw(sq);
  /*@use_draw_int*/draw(3);
  other::/*@use_other_draw*/draw(c);

  vec::V a{1}, b{2};
  vec::V s = a /*@use_plus*/+ b;
  int d = /*@use_dot*/dot(a, b);
  vec::V k = vec::/*@use_scale_qual*/scale(a, 2);
  vec::V k2 = /*@use_scale_adl*/scale(a, 3);
  bool same = a /*@use_eq*/== b;
  io::Out out;
  out /*@use_shl_v*/<< a /*@use_shl_int*/<< 1;

  adl_swap::W w1, w2;
  using lib::/*@using_lib_swap*/swap;
  /*@use_swap*/swap(w1, w2);       // non-template adl_swap::swap wins over lib::swap<W>
  int i1 = 0, i2 = 1;
  /*@use_swap_int*/swap(i1, i2);   // only lib::swap<int> is viable

  int fa = via::/*@use_via_fa*/fa();
  int fb = via::/*@use_via_fb*/fb();

  Bank bank;
  /*@use_audit*/audit(bank);
  /*@use_Account*/Account acct;
  return d + s.x + k.x + k2.x + same + fa + fb + acct.peek(bank) + out.n + bank./*@use_Bank_get*/get();
}
