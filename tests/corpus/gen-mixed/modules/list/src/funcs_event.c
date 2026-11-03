#include "list/funcs_event.h"
#include "list/lis_channel.h"
#include "list/macros_schedule.h"
#include "list/table158.h"
#include "list/lis_region.h"

long lis_update_event(unsigned p0) {
    double r = 0;
    r += p0;
    lis_channel_t o1;
    r += o1.speed;
    r += lis_channel_build(&o1);
    r += lis_channel_encode(&o1, 1, 3.5);
    r += LIS_SCALE_162(2);
    return (long)(r);
}

long lis_derive_event(double p0, int p1) {
    double r = 0;
    r += p1;
    r += lis_table158_fn_5(1);
    r += /*@lis_table158_fn_0_752_use1*/lis_table158_fn_0(1);
    lis_region_t o1;
    r += lis_region_visit(&o1, 1);
    return (long)(r);
}

