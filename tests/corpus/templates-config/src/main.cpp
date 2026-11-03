#include "policy.h"

extern int other();

int main() {
  int a = Policy<int>::/*@use_run_int*/run(1);
  double b = Policy<double>::/*@use_run_dbl*/run(2.0);
  /*@use_Policy*/Policy<long> /*@var_pl*/pl;
  int c1 = /*@use_cost_int*/cost<int>();
  int c2 = /*@use_cost_dbl*/cost<double>();
  int t = /*@use_twice*/twice(3);
  return a + static_cast<int>(b) + c1 + c2 + t + other() + static_cast<int>(pl.run(1L));
}
