#include "variadic.h"
#include "basic.h"

int /*@twice_fn*/twice(int x) { return x * 2; }
int /*@thrice_fn*/thrice(int x) { return x * 3; }

void exercise_variadic() {
  /*@use_LOG_plain*/LOG("plain");
  /*@use_LOG_args*/LOG("args %d %d", /*@use_twice_in_log*/twice(1), 2);
  int /*@a_local*/a = /*@use_APPLY*/APPLY(/*@use_ADD3_via_apply*/ADD3, 1, 2, 3);
  int b = /*@use_APPLY_b*/APPLY(/*@use_helper_via_apply*/helper_sum, /*@use_a_in_b*/a, 1);
  int c = /*@use_CALL_BOTH*/CALL_BOTH(/*@use_twice_arg*/twice, /*@use_thrice_arg*/thrice, 4);
  int d = /*@use_FIRST*/FIRST(/*@use_first_arg*/a, b, c);
  (void)d;
  (void)/*@use_REST_COUNT*/REST_COUNT(1, 2, 3);
}
