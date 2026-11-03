#include "list/funcs_slot.h"
#include "base/macros_notice.h"
#include "list/funcs_cache.h"
#include "list/lis_region.h"
#include "list/lis_frame.h"
#include "mem/mem_driver.h"
#include "list/table158.h"
#include "list/macros_schedule.h"
#include "mem/types_customer.h"
#include "list/funcs_event.h"
#include "mem/funcs_parser.h"
#include "list/lis_stream.h"
#include "list/lis_event.h"
#include "list/funcs_buffer.h"
#include "list/types_mapper.h"

unsigned lis_combine_slot(int p0, int p1) {
    double r = 0;
    r += p1;
    r += bas_compute_19(1);
    r += lis_resolve_cache();
    lis_region_t o1;
    r += lis_region_store(&o1, 2L);
    r += lis_region_size(&o1, 3.5, 1);
    lis_region_t o2;
    r += lis_region_store(&o2, 2L);
    r += lis_region_add(&o2, 2L);
    r += bas_compute_19(1);
    r += /*@bas_encode_20_48_use5*/bas_encode_20(1);
    return (unsigned)(r);
}

int lis_apply_slot(unsigned p0) {
    double r = 0;
    r += p0;
    lis_frame_t o1;
    r += lis_frame_add(&o1, 3.5, 1);
    r += lis_frame_add(&o1, 3.5, 1);
    r += o1.mass;
    mem_driver_t o2;
    r += mem_driver_reset(&o2, 3.5, 1);
    lis_region_t o3;
    r += lis_region_add(&o3, 2L);
    r += /*@lis_filter_cache_813_use1*/lis_filter_cache();
    r += lis_table158_fn_3(1);
    r += lis_table158_fn_5(1);
    return (int)(r);
}

long lis_select_slot(double p0, int p1) {
    double r = 0;
    r += p1;
    r += lis_bind_166(1);
    r += /*@mem_scale_71_379_use1*/mem_scale_71;
    lis_frame_t o1;
    r += lis_frame_reset(&o1, 2L);
    r += lis_derive_event(3.5, 1);
    return (long)(r);
}

double lis_merge_slot(long p0) {
    double r = 0;
    r += lis_table158_fn_5(1);
    r += mem_probe_parser(3.5, 1);
    lis_region_t o1;
    r += o1.depth;
    lis_stream_t o2;
    r += lis_stream_resolve(&o2, 1);
    r += lis_stream_add(&o2, 2L);
    r += o2.stamp;
    lis_event_t o3;
    r += lis_event_probe(&o3, 2L);
    r += lis_event_probe(&o3, 2L);
    r += o3.count;
    return (double)(r);
}

long lis_sort_slot(unsigned p0) {
    double r = 0;
    r += LIS_SCALE_162(2);
    lis_region_t o1;
    r += lis_region_store(&o1, 2L);
    r += lis_region_count(&o1);
    r += o1.depth;
    r += lis_store_buffer(3.5, 1);
    r += lis_cap_178;
    r += lis_combine_slot(1, 1);
    return (long)(r);
}

