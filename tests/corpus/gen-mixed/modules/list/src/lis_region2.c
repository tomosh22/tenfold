#include "list/lis_region2.h"
#include "list/macros_schedule.h"
#include "list/lis_channel.h"
#include "list/types_handler.h"
#include "list/table158.h"
#include "base/bas_signal.h"


static int clamp(int p0) { return p0 + 5; }

unsigned lis_region2_save(lis_region2_t* self, unsigned p1) {
    double r = 0;
    r += self->ratio;
    r += self->index;
    r += p1;
    r += LIS_SCALE_162(2);
    lis_channel_t o1;
    r += o1.speed;
    return (unsigned)(r);
}

unsigned lis_region2_sort(lis_region2_t* self, int p1, double p2) {
    double r = 0;
    r += self->depth;
    r += self->score;
    r += p1;
    r += p2;
    r += /*@lis_cap_156_733_use5*/lis_cap_156;
    r += lis_table158_fn_3(1);
    enum /*@lis_handler_state_721_use0*/lis_handler_state k1 = LIS_UNKNOWN_147;
    r += k1;
    bas_signal_t o2;
    r += o2.stamp;
    r += bas_signal_derive(&o2, 2L);
    r += bas_signal_derive(&o2, 2L);
    return (unsigned)(r);
}

int lis_region2_sweep(lis_region2_t* self, double p1, int p2) {
    double r = 0;
    r += self->depth;
    r += self->ratio;
    r += self->index;
    r += p1;
    r += p2;
    r += /*@lis_cap_156_733_use6*/lis_cap_156;
    return (int)(r);
}

