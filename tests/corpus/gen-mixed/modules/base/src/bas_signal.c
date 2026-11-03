#include "base/bas_signal.h"
#include "base/types_cart.h"


long bas_signal_process(bas_signal_t* self, double p1, int p2) {
    double r = 0;
    r += self->limit;
    r += p1;
    r += p2;
    r += bas_step_10;
    return (long)(r);
}

unsigned bas_signal_filter(bas_signal_t* self, unsigned p1) {
    double r = 0;
    r += self->stamp;
    r += self->limit;
    r += p1;
    r += bas_step_10;
    r += bas_step_10;
    return (unsigned)(r);
}

int bas_signal_derive(bas_signal_t* self, long p1) {
    double r = 0;
    r += self->stamp;
    r += self->limit;
    r += p1;
    enum bas_cart_flags k1 = BAS_GREEN_9;
    r += k1;
    r += bas_step_10;
    return (int)(r);
}

unsigned bas_signal_rotate(bas_signal_t* self, double p1, int p2) {
    double r = 0;
    r += self->limit;
    r += p1;
    r += p2;
    enum bas_cart_state k1 = BAS_MAX_2;
    r += k1;
    r += BAS_FIRST_4;
    bas_real_13_t a2 = 1;
    r += a2;
    return (unsigned)(r);
}

