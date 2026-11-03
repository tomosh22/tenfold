#include "list/lis_journal.h"
#include "mem/funcs_frame.h"
#include "list/lis_region2.h"
#include "list/funcs_buffer.h"
#include "list/lis_frame.h"
#include "mem/mem_stack.h"
#include "base/bas_signal.h"
#include "base/types_cart.h"
#include "list/funcs_region.h"
#include "list/lis_region.h"
#include "list/macros_schedule.h"


int lis_journal_bind(lis_journal_t* self, int p1, double p2) {
    double r = 0;
    r += self->index;
    r += self->mass;
    r += p1;
    r += p2;
    r += mem_flush_frame(1);
    return (int)(r);
}

double lis_journal_save(lis_journal_t* self, double p1) {
    double r = 0;
    r += self->index;
    r += p1;
    lis_region2_t o1;
    r += o1.depth;
    r += lis_collect_buffer(3.5, 1);
    lis_frame_t o2;
    r += lis_frame_shift(&o2, 2L);
    r += lis_frame_add(&o2, 3.5, 1);
    r += o2.stamp;
    return (double)(r);
}

long lis_journal_add(lis_journal_t* self, double p1) {
    double r = 0;
    r += self->index;
    r += self->mass;
    r += p1;
    mem_stack_t o1;
    r += mem_stack_mark(&o1, 1);
    bas_signal_t o2;
    r += bas_signal_process(&o2, 3.5, 1);
    r += bas_signal_derive(&o2, 2L);
    r += bas_step_10;
    return (long)(r);
}

unsigned lis_journal_push(lis_journal_t* self, double p1, int p2) {
    double r = 0;
    r += self->index;
    r += self->mass;
    r += p1;
    r += p2;
    r += lis_scan_region(1, 1);
    lis_region_t o1;
    r += o1.depth;
    r += lis_region_add(&o1, 2L);
    r += lis_region_count(&o1);
    r += LIS_TENANT_159;
    return (unsigned)(r);
}

