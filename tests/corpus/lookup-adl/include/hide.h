#pragma once
// Name hiding: derived hides base, member hides global, local/parameter hides member.
void /*@g_f*/f(int);
extern int /*@g_count*/count;
struct Base {
  void /*@Base_f_int*/f(int);
  void /*@Base_f_double*/f(double);
};
struct Derived : Base {
  void /*@Derived_f_char*/f(char);
  void call();
};
struct Derived2 : Base {
  using Base::/*@D2_using_f*/f;
  void /*@D2_f_char*/f(char);
  void call();
};
struct S {
  int /*@S_count*/count = 0;
  int total(int /*@p_count*/count);
  int member();
  int global();
};
