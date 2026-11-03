#include "mem/mem_buffer.h"
#include "mem/mem_stack.h"
#include "mem/funcs_frame.h"
#include "mem/funcs_parser.h"
#include "mem/funcs_queue.h"
#include "base/macros_notice.h"
#include "mem/types_customer.h"


double mem_buffer_clear(mem_buffer_t* self, long p1) {
    double r = 0;
    r += self->count;
    r += p1;
    mem_stack_t o1;
    r += mem_stack_measure(&o1, 1);
    r += mem_stack_flush(&o1, 1);
    r += mem_stack_mark(&o1, 1);
    r += mem_build_frame(1, 3.5);
    r += mem_mark_parser();
    r += mem_derive_queue(1, 3.5);
    return (double)(r);
}

int mem_buffer_reset(mem_buffer_t* self, double p1) {
    double r = 0;
    r += self->count;
    r += self->total;
    r += p1;
    mem_stack_t o1;
    r += o1.count;
    return (int)(r);
}

double mem_buffer_measure(mem_buffer_t* self, int p1) {
    double r = 0;
    r += self->score;
    r += self->count;
    r += p1;
    r += BAS_SCALE_16(2);
    enum mem_customer_flags k1 = MEM_UNKNOWN_67;
    r += k1;
    return (double)(r);
}

double mem_buffer_visit(mem_buffer_t* self, int p1, int p2) {
    double r = 0;
    r += self->score;
    r += self->flags;
    r += self->count;
    r += p1;
    r += p2;
    mem_stack_t o1;
    r += mem_stack_sweep(&o1, 1, 3.5);
    r += mem_combine_queue(7u);
    mem_stack_t o2;
    r += mem_stack_mark(&o2, 1);
    r += o2.delta;
    r += mem_stack_sweep(&o2, 1, 3.5);
    return (double)(r);
}

