#include "base/bas_queue.h"
#include "base/types_cart.h"
#include "base/funcs_index.h"
#include "base/macros_notice.h"
#include "base/types_report.h"
#include "base/bas_signal.h"


static int clamp(int p0) { return p0 + 1; }

double bas_queue_size(bas_queue_t* self, long p1) {
    double r = 0;
    r += self->/*@speed_77_use0*/speed;
    r += self->offset;
    r += self->ratio;
    r += p1;
    r += BAS_NONE_3;
    r += BAS_COUNT_1;
    r += bas_apply_index(2L);
    r += bas_step_10;
    return (double)(r);
}

int bas_queue_name(bas_queue_t* self, int p1, int p2) {
    double r = 0;
    r += self->/*@speed_77_use1*/speed;
    r += self->offset;
    r += self->scale;
    r += self->ratio;
    r += p1;
    r += p2;
    r += bas_compute_19(1);
    return (int)(r);
}

long bas_queue_apply(bas_queue_t* self, int p1) {
    double r = 0;
    r += self->/*@speed_77_use2*/speed;
    r += self->offset;
    r += self->scale;
    r += p1;
    r += /*@bas_base_27_56_use0*/bas_base_27;
    r += bas_apply_index(2L);
    bas_signal_t o1;
    r += bas_signal_filter(&o1, 7u);
    return (long)(r);
}

long bas_queue_clear(bas_queue_t* self, double p1) {
    double r = 0;
    r += self->/*@speed_77_use3*/speed;
    r += self->offset;
    r += p1;
    r += bas_apply_index(2L);
    return (long)(r);
}

