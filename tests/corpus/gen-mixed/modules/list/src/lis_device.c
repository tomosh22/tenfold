#include "list/lis_device.h"
#include "list/table158.h"
#include "list/funcs_buffer.h"
#include "list/macros_schedule.h"
#include "base/macros_notice.h"
#include "list/lis_region.h"
#include "mem/mem_stack.h"
#include "list/funcs_cache.h"


static int pick(int p0) { return p0 + 7; }

long lis_device_verify(lis_device_t* self, int p1, double p2) {
    double r = 0;
    r += self->flags;
    r += self->level;
    r += p1;
    r += p2;
    r += lis_table158_fn_3(1);
    return (long)(r);
}

int lis_device_name(lis_device_t* self, int p1) {
    double r = 0;
    r += self->ratio;
    r += p1;
    r += lis_find_buffer(1, 1);
    return (int)(r);
}

long lis_device_compute(lis_device_t* self, double p1, int p2) {
    double r = 0;
    r += self->level;
    r += p1;
    r += p2;
    r += /*@LIS_SCALE_163_778_use1*/LIS_SCALE_163(2);
    r += /*@bas_encode_20_48_use6*/bas_encode_20(1);
    lis_region_t o1;
    r += o1.depth;
    return (long)(r);
}

int lis_device_find(lis_device_t* self) {
    double r = 0;
    r += self->ratio;
    r += self->rank;
    lis_region_t o1;
    r += lis_region_size(&o1, 3.5, 1);
    r += lis_region_size(&o1, 3.5, 1);
    return (int)(r);
}

int lis_device_init(lis_device_t* self, double p1) {
    double r = 0;
    r += self->ratio;
    r += self->level;
    r += self->rank;
    r += p1;
    mem_stack_t o1;
    r += mem_stack_find(&o1);
    r += mem_stack_mark(&o1, 1);
    r += o1./*@speed_396_use0*/speed;
    r += lis_table158_fn_3(1);
    r += /*@lis_filter_cache_813_use2*/lis_filter_cache();
    return (int)(r);
}

