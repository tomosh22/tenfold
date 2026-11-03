#include "macros.h"

static int /*@helper_b*/helper(int v) { return /*@use_MAX_b*/MAX(v, 3) - 1; }
static int /*@counter_b*/counter;

int /*@call_b_def*/call_b(int v) {
    /*@use_counter_b0*/counter -= 1;
    return /*@use_helper_b*/helper(v) + /*@use_counter_b*/counter;
}
