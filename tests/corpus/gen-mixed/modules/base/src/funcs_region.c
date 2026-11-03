#include "base/funcs_region.h"
#include "base/types_cart.h"
#include "base/bas_signal.h"
#include "base/macros_notice.h"

long bas_measure_region(int p0) {
    double r = 0;
    r += bas_step_10;
    bas_signal_t o1;
    r += bas_signal_process(&o1, 3.5, 1);
    r += bas_signal_derive(&o1, 2L);
    return (long)(r);
}

int bas_save_region(unsigned p0) {
    double r = 0;
    r += p0;
    r += /*@BAS_JOB_15_43_use1*/BAS_JOB_15;
    r += BAS_COUNT_1;
    return (int)(r);
}

