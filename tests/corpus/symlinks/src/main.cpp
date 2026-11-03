#include "../link_lib/lib.h"
int main() {
  /*@use_sl_T*/SlT t;
  t./*@use_sl_val*/val = /*@use_sl_fn*/sl_fn(1);
  return t./*@use_sl_val2*/val;
}
