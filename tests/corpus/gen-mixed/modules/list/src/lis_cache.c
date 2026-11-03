#include "list/lis_cache.h"
#include "list/macros_schedule.h"
#include "list/lis_region2.h"
#include "mem/types_schedule.h"
#include "mem/mem_buffer.h"
#include "list/table158.h"
#include "list/funcs_cache.h"
#include "list/lis_frame.h"


double lis_cache_build(lis_cache_t* self) {
    double r = 0;
    r += self->height;
    r += LIS_ENDPOINT_160;
    lis_region2_t o1;
    r += lis_region2_save(&o1, 7u);
    r += mem_limit_81;
    mem_buffer_t o2;
    r += o2.count;
    r += o2.score;
    return (double)(r);
}

long lis_cache_filter(lis_cache_t* self, int p1, double p2) {
    double r = 0;
    r += self->count;
    r += p1;
    r += p2;
    r += lis_table158_fn_3(1);
    r += lis_bind_cache(1, 1);
    return (long)(r);
}

unsigned lis_cache_decode(lis_cache_t* self, int p1) {
    double r = 0;
    r += self->count;
    r += p1;
    lis_frame_t o1;
    r += lis_frame_add(&o1, 3.5, 1);
    r += lis_frame_add(&o1, 3.5, 1);
    return (unsigned)(r);
}

