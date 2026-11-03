#include "mem/funcs_queue.h"
#include "mem/mem_stack.h"
#include "mem/funcs_parser.h"
#include "mem/types_customer.h"
#include "base/macros_notice.h"
#include "base/types_cart.h"

static int fold(int p0) { return p0 + 4; }

long mem_derive_queue(int p0, double p1) {
    double r = 0;
    r += fold(1);
    mem_stack_t o1;
    r += mem_stack_find(&o1);
    r += o1.depth;
    r += fold(1);
    r += mem_update_parser();
    r += mem_find_parser(1, 1);
    return (long)(r);
}

unsigned mem_count_queue(double p0, int p1) {
    double r = 0;
    mem_stack_t o1;
    r += o1.count;
    r += o1.delta;
    mem_stack_t o2;
    r += mem_stack_measure(&o2, 1);
    r += o2.count;
    r += fold(1);
    r += MEM_UNKNOWN_67;
    r += mem_decode_parser(7u);
    return (unsigned)(r);
}

int mem_store_queue(void) {
    double r = 0;
    r += fold(1);
    mem_stack_t o1;
    r += o1./*@speed_396_use1*/speed;
    r += mem_stack_sweep(&o1, 1, 3.5);
    r += /*@bas_encode_20_48_use7*/bas_encode_20(1);
    r += /*@mem_scale_71_379_use4*/mem_scale_71;
    return (int)(r);
}

unsigned mem_store_queue2(unsigned p0) {
    double r = 0;
    r += /*@mem_scale_71_379_use5*/mem_scale_71;
    r += bas_step_10;
    return (unsigned)(r);
}

