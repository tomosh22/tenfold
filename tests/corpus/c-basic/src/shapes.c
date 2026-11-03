#include "shapes.h"

double /*@square_area_def*/square_area(square_t s) { return s./*@use_side*/side * s./*@use_side2*/side; }
double /*@circle_area_def*/circle_area(circle_t c) { return 3.14159 * c./*@use_radius*/radius * c./*@use_radius2*/radius; }

int /*@op_add_def*/op_add(int a, int b) { return a + b; }
int /*@op_sub_def*/op_sub(int a, int b) { return a - b; }
static int /*@op_mul*/op_mul(int a, int b) { return a * b; }

/* table of function pointers, designated initialisers */
const binop_entry /*@binop_table_def*/binop_table[] = {
    { .name = "add", ./*@use_desig_fn*/fn = /*@use_tbl_add*/op_add },
    { .name = "sub", ./*@use_desig_fn2*/fn = /*@use_tbl_sub*/op_sub },
    { .name = "mul", ./*@use_desig_fn3*/fn = /*@use_tbl_mul*/op_mul },
};
const int /*@binop_count_def*/binop_count = (int)(sizeof(binop_table) / sizeof(binop_table[0]));

int /*@apply_def*/apply(binop_fn f, int a, int b) { return f(a, b); }

void /*@each_value_def*/each_value(const int *vals, int n, /*@use_visit_cb_c*/visit_cb /*@cb_param*/cb, void *ctx) {
    for (int i = 0; i < n; ++i) /*@use_cb_call*/cb(vals[i], ctx);
}

#include <stdlib.h>
buf_t */*@buf_new_def*/buf_new(int n) {
    buf_t *b = malloc(sizeof(buf_t) + (size_t)n);
    b->/*@use_len_new*/len = n;
    b->/*@use_data_new*/data[0] = 0;
    return b;
}

point /*@origin_def*/origin(void) {
    point p = { ./*@use_desig_y*/y = 2.0, ./*@use_desig_x*/x = 1.0 };
    return p;
}
