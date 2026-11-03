#include "list/funcs_region.h"
#include "mem/types_customer.h"
#include "mem/mem_stack.h"
#include "list/funcs_slot.h"
#include "list/lis_region.h"
#include "list/types_mapper_192.h"
#include "list/lis_region2.h"
#include "list/lis_channel.h"
#include "list/funcs_socket.h"
#include "list/types_mapper.h"
#include "base/macros_notice.h"
#include "list/lis_cache.h"
#include "base/types_report.h"
#include "list/macros_schedule.h"

static int step(int p0) { return p0 + 2; }

int lis_build_region(int p0) {
    double r = 0;
    enum mem_customer_kind k1 = MEM_UNKNOWN_64;
    r += k1;
    mem_stack_t o2;
    r += mem_stack_find(&o2);
    return (int)(r);
}

double lis_compute_region(int p0, double p1) {
    double r = 0;
    r += p1;
    r += step(1);
    r += lis_merge_slot(2L);
    lis_region_t o1;
    r += o1.depth;
    r += lis_region_add(&o1, 2L);
    r += o1.depth;
    r += LIS_GREEN_197;
    lis_region2_t o2;
    r += lis_region2_sort(&o2, 1, 3.5);
    r += lis_region2_sweep(&o2, 3.5, 1);
    r += o2.ratio;
    lis_region_t o3;
    r += lis_region_count(&o3);
    return (double)(r);
}

unsigned lis_scan_region(int p0, int p1) {
    double r = 0;
    r += p0;
    lis_channel_t o1;
    r += lis_channel_encode(&o1, 1, 3.5);
    r += lis_channel_build(&o1);
    r += /*@mem_scale_71_379_use0*/mem_scale_71;
    lis_region_t o2;
    r += o2.depth;
    r += lis_region_size(&o2, 3.5, 1);
    r += lis_flush_socket(7u);
    enum /*@lis_mapper_state_793_use0*/lis_mapper_state k3 = /*@LIS_NONE_170_796_use0*/LIS_NONE_170;
    r += k3;
    r += /*@BAS_JOB_15_43_use6*/BAS_JOB_15;
    return (unsigned)(r);
}

unsigned lis_reset_region(void) {
    double r = 0;
    lis_channel_t o1;
    r += lis_channel_visit(&o1, 3.5, 1);
    r += o1.height;
    lis_cache_t o2;
    r += lis_cache_build(&o2);
    r += bas_limit_26;
    r += LIS_TENANT_159;
    r += step(1);
    r += bas_compute_19(1);
    return (unsigned)(r);
}

