#include <stdlib.h>
#include "handle.h"

/* the only place that knows the layout */
struct /*@handle_tag_def*/handle {
    int /*@handle_id_field*/id;
    int open;
};

handle_t */*@handle_open_def*/handle_open(int id) {
    /*@use_handle_td_c*/handle_t *h = malloc(sizeof *h);
    if (!h) return 0;
    h->/*@use_id_open*/id = id;
    h->open = 1;
    return h;
}

int /*@handle_id_def*/handle_id(const handle_t *h) {
    return h->/*@use_id_get*/id;
}

void handle_close(handle_t *h) {
    h->open = 0;
    free(h);
}
