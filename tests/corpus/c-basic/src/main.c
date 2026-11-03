#include <stdio.h>
#include "list.h"
#include "handle.h"
#include "shapes.h"
#include "macros.h"
#include "globals.h"
#include "log.h"

enum { LOCAL_FLAG_A = 1 };

static void /*@collect_cb*/collect(int v, void *ctx) { *(int *)ctx += v; }

int main(void) {
    node */*@head_var*/head = /*@use_list_push*/list_push(0, 1);
    /*@use_node_td_main*/node *second = list_push(head, 2);
    struct /*@use_node_tag_main*/node *third = list_push(second, 3);
    printf("%d\n", /*@use_list_sum*/list_sum(third));
    printf("%d\n", third->/*@use_value_main*/value);

    handle_t *h = /*@use_handle_open*/handle_open(7);
    int id = /*@use_handle_id*/handle_id(h);
    handle_close(h);

    flags /*@perm_var*/perm = /*@use_F_READ*/F_READ | /*@use_F_WRITE*/F_WRITE;
    if (perm & /*@use_F_EXEC*/F_EXEC) perm = /*@use_F_NONE*/F_NONE;
    perm |= /*@use_F_ALL*/F_ALL;

    point p = { ./*@use_desig_x_main*/x = 1.0, ./*@use_desig_y_main*/y = 2.0 };
    printf("%f\n", /*@use_p_var*/p./*@use_pt_x*/x + p./*@use_pt_y*/y);
    value v;
    v./*@use_val_f*/f = 1.5f;
    printf("%d\n", v./*@use_val_i*/i);

    square_t sq = { 2.0 };
    circle_t ci = { 1.0 };
    double a1 = /*@use_area_sq*/area(sq);
    double a2 = /*@use_area_ci*/area(ci);
    const char *tn = /*@use_type_name*/type_name(a1);

    binop_fn fp = /*@use_fp_add*/op_add;
    int r = /*@use_apply*/apply(fp, 1, 2) + apply(/*@use_fp_sub*/op_sub, 5, 3);
    r += /*@use_binop_table*/binop_table[0]./*@use_entry_fn*/fn(1, 2);
    r += /*@use_binop_count*/binop_count;
    int arr[/*@use_LIMIT*/LIMIT];
    r += (int)/*@use_COUNT_OF*/COUNT_OF(arr);
    r += /*@use_SQUARE*/SQUARE(3) + /*@use_MAX*/MAX(r, 2);

    r += /*@use_next_id*/next_id() + /*@use_counter_main*/g_counter;
    /*@use_verbosity_main*/g_verbosity = 2;

    int vals[3] = { 1, 2, 3 }, sum = 0;
    /*@use_each_value*/each_value(vals, 3, /*@use_collect_cb*/collect, &sum);
    buf_t *fb = /*@use_buf_new*/buf_new(4);
    fb->/*@use_data_main*/data[1] = 'x';
    r += fb->/*@use_len_main*/len + sum;

    return (int)(a2 + id + r) + (int)tn[0];
}
