#include "list/funcs_buffer.h"
#include "list/types_mapper.h"
#include "mem/funcs_frame.h"
#include "list/table158.h"
#include "mem/macros_order.h"
#include "base/funcs_index.h"
#include "list/macros_schedule.h"
#include "base/bas_signal.h"
#include "list/funcs_cache.h"
#include "list/lis_frame.h"
#include "list/types_handler.h"

static int clamp(int p0) { return p0 + 8; }

int lis_collect_buffer(double p0, int p1) {
    double r = 0;
    r += p0;
    r += p1;
    r += lis_cap_177;
    r += mem_flush_frame(1);
    r += lis_table158_fn_3(1);
    r += MEM_CUSTOMER_92;
    return (int)(r);
}

unsigned lis_store_buffer(double p0, int p1) {
    double r = 0;
    r += p0;
    r += p1;
    r += lis_cap_178;
    r += bas_apply_index(2L);
    r += lis_merge_165(1);
    r += lis_cap_177;
    r += lis_merge_165(1);
    return (unsigned)(r);
}

long lis_find_buffer(int p0, int p1) {
    double r = 0;
    r += p1;
    bas_signal_t o1;
    r += bas_signal_process(&o1, 3.5, 1);
    r += bas_signal_rotate(&o1, 3.5, 1);
    r += lis_save_cache(7u);
    lis_frame_t o2;
    r += lis_frame_reset(&o2, 2L);
    r += /*@lis_cap_156_733_use1*/lis_cap_156;
    return (long)(r);
}

