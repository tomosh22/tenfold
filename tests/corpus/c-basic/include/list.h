#ifndef LIST_H
#define LIST_H
#include <stddef.h>

/* struct tag and typedef name are the same identifier */
typedef struct /*@node_tag_fwd*/node /*@node_td*/node;
struct /*@node_tag*/node {
    int /*@node_value*/value;
    node *next;
};

node */*@list_push_decl*/list_push(node *head, int v);
int /*@list_sum_decl*/list_sum(const node *head);
void list_free(node *head);

#endif
