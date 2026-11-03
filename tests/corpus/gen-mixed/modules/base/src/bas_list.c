#include "base/bas_list.h"
#include "base/bas_signal.h"
#include "base/macros_notice.h"
#include "base/types_cart.h"
#include "base/funcs_region.h"


static int norm(int p0) { return p0 + 6; }

int bas_list_clear(bas_list_t* self, long p1) {
    double r = 0;
    r += self->flags;
    r += p1;
    r += norm(1);
    return (int)(r);
}

unsigned bas_list_size(bas_list_t* self, double p1) {
    double r = 0;
    r += self->flags;
    r += self->stamp;
    r += p1;
    bas_signal_t o1;
    r += bas_signal_derive(&o1, 2L);
    r += o1.limit;
    r += /*@BAS_JOB_15_43_use0*/BAS_JOB_15;
    r += BAS_SCALE_16(2);
    r += bas_compute_19(1);
    return (unsigned)(r);
}

unsigned bas_list_name(bas_list_t* self, unsigned p1) {
    double r = 0;
    r += self->flags;
    r += self->stamp;
    r += p1;
    enum bas_cart_flags k1 = BAS_AUTO_7;
    r += k1;
    return (unsigned)(r);
}

unsigned bas_list_collect(bas_list_t* self, double p1) {
    double r = 0;
    r += self->stamp;
    r += p1;
    r += /*@bas_encode_20_48_use1*/bas_encode_20(1);
    r += bas_measure_region(1);
    return (unsigned)(r);
}

