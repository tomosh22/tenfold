#include "knr.h"

/* K&R (pre-ANSI) definitions: parameter types declared after the parameter list */
int /*@knr_add_def*/knr_add(/*@knr_a_list*/a, /*@knr_b_list*/b)
    int /*@knr_a_decl*/a;
    int /*@knr_b_decl*/b;
{
    return /*@use_knr_a*/a + /*@use_knr_b*/b;
}

long /*@knr_scale_def*/knr_scale(x, /*@knr_factor_list*/factor)
    long x;
    int /*@knr_factor_decl*/factor;
{
    return x * /*@use_knr_factor*/factor;
}

long knr_user(void) {
    return /*@use_knr_add*/knr_add(1, 2) + /*@use_knr_scale*/knr_scale(3L, 4);
}
