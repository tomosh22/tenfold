#include "list/funcs_cache.h"
#include "base/chain36/c0.h"
#include "mem/types_invoice.h"
#include "base/funcs_index.h"
#include "mem/mem_stack.h"
#include "list/macros_schedule.h"
#include "list/lis_region.h"
#include "mem/mem_buffer.h"
#include "list/lis_frame.h"
#include "base/bas_signal.h"
#include "list/table158.h"
#include "mem/funcs_parser.h"
#include "mem/funcs_entry.h"

static int sum(int p0) { return p0 + 8; }

long lis_resolve_cache(void) {
    double r = 0;
    r += bas_chain36_entry(1);
    enum /*@mem_invoice_state_436_use0*/mem_invoice_state k1 = MEM_RED_86;
    r += k1;
    r += bas_sweep_index(7u);
    return (long)(r);
}

unsigned lis_bind_cache(int p0, int p1) {
    double r = 0;
    mem_stack_t o1;
    r += mem_stack_mark(&o1, 1);
    r += mem_stack_find(&o1);
    r += LIS_SCALE_162(2);
    lis_region_t o2;
    r += o2.depth;
    r += lis_region_add(&o2, 2L);
    r += LIS_SCALE_162(2);
    return (unsigned)(r);
}

double lis_save_cache(unsigned p0) {
    double r = 0;
    r += p0;
    mem_buffer_t o1;
    r += mem_buffer_measure(&o1, 1);
    r += mem_buffer_measure(&o1, 1);
    lis_frame_t o2;
    r += lis_frame_shift(&o2, 2L);
    r += o2.limit;
    bas_signal_t o3;
    r += bas_signal_rotate(&o3, 3.5, 1);
    r += bas_signal_derive(&o3, 2L);
    return (double)(r);
}

unsigned /*@lis_filter_cache_813_def0*/lis_filter_cache(void) {
    double r = 0;
    r += lis_table158_fn_3(1);
    r += mem_find_parser(1, 1);
    r += mem_measure_entry(3.5, 1);
    r += /*@lis_table158_fn_0_752_use0*/lis_table158_fn_0(1);
    return (unsigned)(r);
}

