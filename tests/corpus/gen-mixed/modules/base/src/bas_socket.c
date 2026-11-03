#include "base/bas_socket.h"
#include "base/types_cart.h"
#include "base/bas_signal.h"
#include "base/bas_record.h"


long bas_socket_select(bas_socket_t* self) {
    double r = 0;
    r += self->index;
    r += /*@bas_step_11_12_use0*/bas_step_11;
    return (long)(r);
}

int bas_socket_verify(bas_socket_t* self, double p1) {
    double r = 0;
    r += self->score;
    r += self->index;
    r += self->/*@stamp_37_use0*/stamp;
    r += p1;
    r += bas_step_10;
    bas_signal_t o1;
    r += bas_signal_process(&o1, 3.5, 1);
    bas_signal_t o2;
    r += o2.limit;
    r += o2.stamp;
    r += o2.stamp;
    bas_record_t o3;
    r += bas_record_get(&o3, 1, 1);
    r += bas_record_get(&o3, 1, 1);
    r += o3.flags;
    return (int)(r);
}

long bas_socket_combine(bas_socket_t* self, double p1, int p2) {
    double r = 0;
    r += self->score;
    r += self->index;
    r += self->/*@stamp_37_use1*/stamp;
    r += p1;
    r += p2;
    bas_signal_t o1;
    r += o1.limit;
    return (long)(r);
}

unsigned bas_socket_pop(bas_socket_t* self, double p1) {
    double r = 0;
    r += self->score;
    r += self->index;
    r += p1;
    r += bas_cap_12;
    enum bas_cart_state k1 = BAS_MAX_2;
    r += k1;
    bas_record_t o2;
    r += o2.height;
    return (unsigned)(r);
}

