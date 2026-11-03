#include "util.h"

int /*@class_def*/class(int /*@template_param*/template) { return /*@use_template*/template + 1; }

int /*@delete_def*/delete(try_t */*@typename_param*/typename) {
    return typename->/*@use_f_new*/new + typename->/*@use_f_private*/private;
}

void /*@copy_def*/copy(char *restrict dst, const char *restrict src, int n) {
    for (int i = 0; i < n; ++i) dst[i] = src[i];
}

int /*@arr_sum_def*/arr_sum(int n, int a[static n]) {
    int total = 0;
    for (int i = 0; i < n; ++i) total += a[i];
    return total;
}

_Bool /*@is_even_def*/is_even(int v) { return (v & 1) == 0; }
