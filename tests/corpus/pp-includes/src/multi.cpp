#define BOX_TYPE int
#define BOX_FN /*@def_box_int*/box_int
#define BOX_VAL 1
#include "gen.h"
#undef BOX_TYPE
#undef BOX_FN
#undef BOX_VAL
#define BOX_TYPE double
#define BOX_FN /*@def_box_double*/box_double
#define BOX_VAL 2.5
#include "gen.h"

int a = /*@use_box_int*/box_int();
double b = /*@use_box_double*/box_double();
