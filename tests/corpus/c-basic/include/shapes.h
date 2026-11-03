#ifndef SHAPES_H
#define SHAPES_H

/* typedef'd anonymous struct, union and enum */
typedef /*@point_struct*/struct {
    double /*@pt_x*/x;
    double /*@pt_y*/y;
} /*@point_td*/point;

typedef /*@value_union*/union {
    int /*@val_i*/i;
    float /*@val_f*/f;
    unsigned char /*@val_bytes*/bytes[4];
} /*@value_td*/value;

typedef enum {
    /*@F_NONE*/F_NONE = 0,
    /*@F_READ*/F_READ = 1,
    /*@F_WRITE*/F_WRITE = 2,
    /*@F_EXEC*/F_EXEC = 4,
    /*@F_ALL*/F_ALL = /*@all_r*/F_READ | /*@all_w*/F_WRITE | /*@all_x*/F_EXEC
} /*@flags_td*/flags;

/* flexible array member */
typedef struct {
    int /*@buf_len*/len;
    char /*@buf_data*/data[];
} /*@buf_td*/buf_t;

typedef struct { double /*@sq_side*/side; } square_t;
typedef struct { double /*@ci_radius*/radius; } circle_t;

double /*@square_area_decl*/square_area(square_t s);
double /*@circle_area_decl*/circle_area(circle_t c);

/* generic selection picks the function by argument type */
#define /*@area_macro*/area(s) _Generic((s), square_t: /*@gen_square_area*/square_area, circle_t: /*@gen_circle_area*/circle_area)(s)
#define /*@type_name_macro*/type_name(x) _Generic((x), int: "int", double: "double", default: "other")

/* function pointers and callbacks */
typedef int (*/*@binop_fn_td*/binop_fn)(int, int);
typedef void (*/*@visit_cb_td*/visit_cb)(int v, void *ctx);

typedef struct {
    const char *name;
    binop_fn /*@entry_fn*/fn;
} /*@binop_entry_td*/binop_entry;

int /*@op_add_decl*/op_add(int a, int b);
int /*@op_sub_decl*/op_sub(int a, int b);
extern const binop_entry /*@binop_table_decl*/binop_table[];
extern const int /*@binop_count_decl*/binop_count;

int /*@apply_decl*/apply(binop_fn f, int a, int b);

#endif
