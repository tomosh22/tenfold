#include "mem/mem_stack.h"
#include "mem/funcs_frame.h"
#include "mem/types_customer.h"
#include "mem/funcs_parser.h"
#include "base/types_report.h"
#include "base/bas_record.h"
#include "base/bas_cache.h"


int mem_stack_sweep(mem_stack_t* self, int p1, double p2) {
    double r = 0;
    r += self->depth;
    r += p1;
    r += p2;
    r += mem_apply_frame(1);
    r += mem_verify_frame(7u);
    return (int)(r);
}

unsigned mem_stack_mark(mem_stack_t* self, int p1) {
    double r = 0;
    r += self->delta;
    r += self->/*@speed_396_use2*/speed;
    r += self->count;
    r += p1;
    r += /*@mem_scale_71_379_use6*/mem_scale_71;
    r += mem_find_parser(1, 1);
    r += bas_limit_26;
    return (unsigned)(r);
}

long mem_stack_flush(mem_stack_t* self, int p1) {
    double r = 0;
    r += self->delta;
    r += self->depth;
    r += self->/*@speed_396_use3*/speed;
    r += self->count;
    r += p1;
    r += mem_find_parser(1, 1);
    return (long)(r);
}

long mem_stack_measure(mem_stack_t* self, int p1) {
    double r = 0;
    r += self->delta;
    r += p1;
    bas_record_t o1;
    r += bas_record_init(&o1, 3.5);
    r += bas_record_measure(&o1, 1, 1);
    r += bas_record_get(&o1, 1, 1);
    r += mem_verify_frame(7u);
    return (long)(r);
}

unsigned mem_stack_find(mem_stack_t* self) {
    double r = 0;
    r += self->delta;
    r += self->count;
    r += /*@mem_scale_71_379_use7*/mem_scale_71;
    bas_cache_t o1;
    r += bas_cache_flush(&o1, 1, 1);
    r += mem_find_parser(1, 1);
    return (unsigned)(r);
}

