#include <stdlib.h>
#include "list.h"
#include "macros.h"

/*@use_node_td_c*/node */*@list_push_def*/list_push(node *head, int v) {
    struct node *n = malloc(sizeof(struct /*@use_node_tag_list*/node));
    if (!n) return head;
    n->/*@use_value_push*/value = v;
    n->next = head;
    return n;
}

int /*@list_sum_def*/list_sum(const node *head) {
    int total = 0;
    for (const /*@use_node_td_sum*/node *p = head; p; p = p->next)
        total += p->/*@use_value_sum*/value;
    return total;
}

void list_free(node *head) {
    while (head) {
        node *next = head->next;
        free(head);
        head = next;
    }
}
