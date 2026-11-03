// Include names produced by the preprocessor.
#define HDR "computed_a.h"
#include HDR
#define STR_(x) #x
#define STR(x) STR_(x)
#define PICK computed_b
#include STR(PICK.h)
#define ANGLE <computed_c.h>
#include ANGLE
#if __has_include(HDR) && __has_include(ANGLE)
#include "computed_d.h"
#endif

int a = /*@use_comp_a*/comp_a() + /*@use_comp_b*/comp_b() + /*@use_comp_c*/comp_c() + /*@use_comp_d*/comp_d();
