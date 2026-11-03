#include "macros.h"
#include "globals.h"

/* a static function named like one in static_b.c; both are private to their file */
static int /*@helper_a*/helper(int v) { return /*@use_SQUARE_a*/SQUARE(v) + 1; }
static int /*@counter_a*/counter;

int /*@call_a_def*/call_a(int v) {
    /*@use_counter_a0*/counter += 1;
    return /*@use_helper_a*/helper(v) + /*@use_counter_a*/counter;
}
