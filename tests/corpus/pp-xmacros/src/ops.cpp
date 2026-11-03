// The list defines the functions, then a table points at them, then a dispatcher calls them.
#define OP(name, expr) int /*@op_prefix*/op_##name(int a, int b) { return expr; }
#include "ops.def"
#undef OP

typedef int (*OpFn)(int, int);

struct /*@OpEntry*/OpEntry {
  const char* /*@OpEntry_name*/name;
  OpFn /*@OpEntry_fn*/fn;
};

const OpEntry /*@op_table*/op_table[] = {
#define OP(name, expr) { #name, &op_##name },
#include "ops.def"
#undef OP
};

int run(int which, int a, int b) { return /*@use_op_table*/op_table[which]./*@use_fn_member*/fn(a, b); }

// op_add / op_sub / op_mul exist only through token pasting inside the X-macro expansion.
int direct() { return /*@use_op_add*/op_add(1, 2) + /*@use_op_sub*/op_sub(3, 1) + /*@use_op_mul*/op_mul(2, 2); }
