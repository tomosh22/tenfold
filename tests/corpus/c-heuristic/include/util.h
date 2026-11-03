#ifndef UTIL_H
#define UTIL_H

/* C-only identifiers: keywords in C++, ordinary names in C */
typedef struct {
    int /*@f_new*/new;
    int /*@f_private*/private;
} /*@try_td*/try_t;

int /*@class_decl*/class(int template);
int /*@delete_decl*/delete(try_t *typename);

void /*@copy_decl*/copy(char *restrict dst, const char *restrict src, int n);
int /*@arr_sum_decl*/arr_sum(int n, int a[static n]);
_Bool /*@is_even_decl*/is_even(int v);

#endif
