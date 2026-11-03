#include "base/bas_cache.h"
#include "base/bas_signal.h"
#include "base/macros_notice.h"
#include "base/bas_record.h"
#include "base/types_cart.h"
#include "base/funcs_index.h"
#include "base/types_report.h"


unsigned bas_cache_scale(bas_cache_t* self, int p1, int p2) {
    double r = 0;
    r += p1;
    r += p2;
    bas_signal_t o1;
    r += bas_signal_process(&o1, 3.5, 1);
    r += bas_compute_19(1);
    bas_record_t o2;
    r += bas_record_init(&o2, 3.5);
    r += bas_record_get(&o2, 1, 1);
    return (unsigned)(r);
}

double bas_cache_flush(bas_cache_t* self, int p1, int p2) {
    double r = 0;
    r += p1;
    r += p2;
    r += BAS_COUNT_1;
    bas_record_t o1;
    r += o1.flags;
    r += bas_record_get(&o1, 1, 1);
    r += bas_apply_index(2L);
    return (double)(r);
}

double bas_cache_derive(bas_cache_t* self, int p1) {
    double r = 0;
    r += p1;
    bas_signal_t o1;
    r += bas_signal_filter(&o1, 7u);
    r += bas_signal_filter(&o1, 7u);
    r += BAS_FIRST_4;
    bas_signal_t o2;
    r += bas_signal_derive(&o2, 2L);
    r += bas_signal_process(&o2, 3.5, 1);
    return (double)(r);
}

int bas_cache_init(bas_cache_t* self, double p1) {
    double r = 0;
    r += self->scale;
    r += p1;
    bas_id_29_t a1 = 1;
    r += a1;
    return (int)(r);
}

