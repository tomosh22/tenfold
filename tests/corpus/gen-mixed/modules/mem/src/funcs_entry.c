#include "mem/funcs_entry.h"
#include "base/funcs_index.h"
#include "base/bas_cache2.h"
#include "mem/funcs_parser.h"
#include "mem/mem_buffer.h"
#include "mem/mem_stack.h"
#include "base/macros_notice.h"
#include "mem/types_schedule.h"
#include "mem/chain74/c2.h"

static int scale(int p0) { return p0 + 1; }

long mem_combine_entry(void) {
    double r = 0;
    r += bas_derive_index(1, 1);
    bas_cache2_t o1;
    r += bas_cache2_mark(&o1);
    r += scale(1);
    r += mem_update_parser();
    mem_buffer_t o2;
    r += o2.score;
    mem_stack_t o3;
    r += mem_stack_measure(&o3, 1);
    r += mem_stack_measure(&o3, 1);
    r += o3.depth;
    return (long)(r);
}

int /*@mem_fetch_entry_453_def0*/mem_fetch_entry(unsigned p0) {
    double r = 0;
    r += p0;
    r += mem_find_parser(1, 1);
    r += /*@BAS_JOB_15_43_use8*/BAS_JOB_15;
    r += mem_scale_80;
    r += mem_find_parser(1, 1);
    r += mem_chain74_l2(1);
    return (int)(r);
}

long mem_sweep_entry(int p0, int p1) {
    double r = 0;
    r += p0;
    r += bas_apply_index(2L);
    r += bas_sweep_index(7u);
    r += bas_encode_index(1, 3.5);
    return (long)(r);
}

int mem_measure_entry(double p0, int p1) {
    double r = 0;
    r += p1;
    mem_buffer_t o1;
    r += mem_buffer_measure(&o1, 1);
    r += scale(1);
    r += mem_decode_parser(7u);
    return (int)(r);
}

