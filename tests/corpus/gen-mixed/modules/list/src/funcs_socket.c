#include "list/funcs_socket.h"
#include "list/types_handler.h"
#include "list/funcs_cache.h"
#include "base/types_event.h"
#include "list/lis_cache.h"
#include "list/types_mapper_192.h"
#include "list/lis_region.h"
#include "list/table158.h"
#include "list/macros_schedule.h"
#include "list/funcs_event.h"
#include "base/macros_notice.h"
#include "list/types_mapper.h"
#include "mem/types_schedule.h"
#include "mem/mem_stack.h"

unsigned lis_sweep_socket(unsigned p0) {
    double r = 0;
    r += /*@lis_cap_156_733_use2*/lis_cap_156;
    lis_size_157_t a1 = 1;
    r += a1;
    r += lis_bind_cache(1, 1);
    return (unsigned)(r);
}

int lis_flush_socket(unsigned p0) {
    double r = 0;
    r += p0;
    lis_size_157_t a1 = 1;
    r += a1;
    r += BAS_LAST_30;
    lis_cache_t o2;
    r += lis_cache_filter(&o2, 1, 3.5);
    r += o2.count;
    r += o2.count;
    lis_id_199_t a3 = 1;
    r += a3;
    lis_region_t o4;
    r += lis_region_size(&o4, 3.5, 1);
    r += lis_region_size(&o4, 3.5, 1);
    r += /*@lis_table158_fn_0_752_use2*/lis_table158_fn_0(1);
    return (int)(r);
}

int lis_store_socket(long p0) {
    double r = 0;
    r += lis_merge_165(1);
    r += lis_update_event(7u);
    r += BAS_SCALE_17(2);
    r += lis_cap_177;
    r += MEM_UNKNOWN_79;
    mem_stack_t o1;
    r += mem_stack_sweep(&o1, 1, 3.5);
    return (int)(r);
}

long /*@lis_apply_socket_887_def0*/lis_apply_socket(double p0, int p1) {
    double r = 0;
    r += p1;
    r += LIS_LAST_172;
    lis_region_t o1;
    r += lis_region_size(&o1, 3.5, 1);
    r += lis_region_add(&o1, 2L);
    r += lis_merge_165(1);
    lis_size_157_t a2 = 1;
    r += a2;
    lis_size_157_t a3 = 1;
    r += a3;
    return (long)(r);
}

