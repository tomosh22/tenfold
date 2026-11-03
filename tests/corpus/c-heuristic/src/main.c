#include <stdlib.h>
#include "util.h"

int main(int argc, char **argv) {
    try_t t = { ./*@desig_new*/new = 1, ./*@desig_private*/private = 2 };
    int vla[argc];
    for (int i = 0; i < argc; ++i) vla[i] = i;
    char *buf = malloc(8);              /* implicit void* conversion: C only */
    /*@use_copy*/copy(buf, argv[0], 8);
    int r = /*@use_class*/class(3) + /*@use_delete*/delete(&t) + /*@use_arr_sum*/arr_sum(argc, vla);
    r += /*@use_is_even*/is_even(r);
    r += t./*@use_f_new_main*/new + (int){ 4 };
    free(buf);
    return r;
}
