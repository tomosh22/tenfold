#ifndef HANDLE_H
#define HANDLE_H

/* opaque handle: declared here, defined in handle.c only */
typedef struct /*@handle_tag*/handle /*@handle_td*/handle_t;

handle_t */*@handle_open_decl*/handle_open(int id);
int /*@handle_id_decl*/handle_id(const handle_t *h);
void handle_close(handle_t *h);

#endif
