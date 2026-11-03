#include "base/funcs_index.h"
#include "base/macros_notice.h"
#include "base/bas_socket.h"
#include "base/bas_signal.h"
#include "base/bas_record.h"
#include "base/types_cart.h"

double bas_derive_index(int p0, int p1) {
    double r = 0;
    r += p0;
    r += bas_compute_19(1);
    bas_socket_t o1;
    r += bas_socket_combine(&o1, 3.5, 1);
    r += o1.mass;
    r += BAS_SCALE_16(2);
    bas_signal_t o2;
    r += bas_signal_filter(&o2, 7u);
    r += bas_compute_19(1);
    bas_socket_t o3;
    r += bas_socket_pop(&o3, 3.5);
    r += bas_socket_verify(&o3, 3.5);
    r += bas_socket_combine(&o3, 3.5, 1);
    return (double)(r);
}

int bas_apply_index(long p0) {
    double r = 0;
    r += p0;
    r += /*@bas_encode_20_48_use2*/bas_encode_20(1);
    r += bas_derive_index(1, 1);
    bas_signal_t o1;
    r += bas_signal_derive(&o1, 2L);
    r += bas_signal_process(&o1, 3.5, 1);
    r += bas_signal_derive(&o1, 2L);
    return (int)(r);
}

double bas_sweep_index(unsigned p0) {
    double r = 0;
    r += p0;
    bas_signal_t o1;
    r += bas_signal_derive(&o1, 2L);
    r += bas_signal_derive(&o1, 2L);
    r += bas_signal_derive(&o1, 2L);
    bas_record_t o2;
    r += o2.level;
    r += bas_record_get(&o2, 1, 1);
    r += o2.height;
    r += /*@bas_encode_20_48_use3*/bas_encode_20(1);
    return (double)(r);
}

int bas_encode_index(int p0, double p1) {
    double r = 0;
    r += bas_step_10;
    r += bas_compute_19(1);
    return (int)(r);
}

