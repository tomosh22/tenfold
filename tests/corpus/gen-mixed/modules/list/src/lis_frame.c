#include "list/lis_frame.h"
#include "list/lis_region.h"
#include "base/funcs_region.h"
#include "base/types_cart.h"
#include "base/macros_notice.h"
#include "list/table158.h"
#include "list/types_handler.h"


static int norm(int p0) { return p0 + 7; }

unsigned lis_frame_reset(lis_frame_t* self, long p1) {
    double r = 0;
    r += self->limit;
    r += p1;
    lis_region_t o1;
    r += lis_region_count(&o1);
    r += lis_region_visit(&o1, 1);
    r += bas_measure_region(1);
    enum bas_cart_state k2 = BAS_COUNT_1;
    r += k2;
    r += BAS_SCALE_16(2);
    return (unsigned)(r);
}

double lis_frame_add(lis_frame_t* self, double p1, int p2) {
    double r = 0;
    r += self->mass;
    r += self->limit;
    r += p1;
    r += p2;
    r += /*@lis_table158_fn_0_752_use3*/lis_table158_fn_0(1);
    r += lis_table158_fn_3(1);
    enum bas_cart_state k1 = BAS_COUNT_1;
    r += k1;
    r += /*@lis_cap_156_733_use4*/lis_cap_156;
    return (double)(r);
}

double lis_frame_clear(lis_frame_t* self, unsigned p1) {
    double r = 0;
    r += self->limit;
    r += p1;
    lis_region_t o1;
    r += lis_region_visit(&o1, 1);
    r += lis_region_count(&o1);
    r += lis_region_add(&o1, 2L);
    return (double)(r);
}

double lis_frame_shift(lis_frame_t* self, long p1) {
    double r = 0;
    r += self->stamp;
    r += self->mass;
    r += p1;
    r += lis_table158_fn_5(1);
    return (double)(r);
}

