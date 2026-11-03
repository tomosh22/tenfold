#include "mixed.h"
// lf
int /*@mx_fn_def*/mx_fn(int v) { return v; }
int /*@mx_other_def*/mx_other(int v) {
    return /*@use_mx_fn_in*/mx_fn(v);
}
int /*@mx_last_def*/mx_last(void) { return /*@use_mx_other_in*/mx_other(1); }