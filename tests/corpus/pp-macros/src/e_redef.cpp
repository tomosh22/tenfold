#include "redef_a.h"
int after_a = /*@use_scale_after_a*/SCALE;
#include "redef_b.h"
int after_b = /*@use_scale_after_b*/SCALE;

#define /*@LEVEL_first*/LEVEL 1
int a1 = /*@use_level1*/LEVEL;
#undef /*@undef_first*/LEVEL
#define /*@LEVEL_second*/LEVEL 2
int a2 = /*@use_level2*/LEVEL;
#undef /*@undef_second*/LEVEL
int /*@LEVEL_as_var*/LEVEL = 3;
int a3 = /*@use_level_var*/LEVEL;

#define /*@OP_add*/OP(a, b) ((a) + (b))
int o1 = /*@use_op_add*/OP(1, 2);
#undef /*@undef_op*/OP
#define /*@OP_mul*/OP(a, b) ((a) * (b))
int o2 = /*@use_op_mul*/OP(3, 4);

// Used before the macro exists: a variable. After the definition the same spelling is the macro.
int /*@var_late*/late_name = 1;
int use_before = /*@use_late_before*/late_name;
#define /*@late_macro*/late_name 99
int use_after = /*@use_late_after*/late_name;

// Macro redefined with identical text is legal; the latest definition wins.
#define /*@SAME_1*/SAME 5
#define /*@SAME_2*/SAME 5
int same_value = /*@use_same*/SAME;

// A macro defined inside a function body is visible after the function too.
void body() {
#define /*@IN_BODY*/IN_BODY 7
  int x = /*@use_in_body*/IN_BODY;
  (void)x;
}
int after_body = /*@use_in_body_after*/IN_BODY;
