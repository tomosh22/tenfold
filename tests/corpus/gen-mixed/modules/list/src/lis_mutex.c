#include "list/lis_mutex.h"
#include "list/funcs_socket.h"
#include "list/types_handler.h"
#include "list/types_token.h"
#include "list/macros_payment.h"
#include "base/funcs_page.h"
#include "base/bas_signal.h"
#include "base/types_cart.h"
#include "list/lis_region.h"


static int scale(int p0) { return p0 + 5; }

long lis_mutex_combine(lis_mutex_t* self, unsigned p1) {
    double r = 0;
    r += self->phase;
    r += p1;
    r += /*@lis_apply_socket_887_use1*/lis_apply_socket(3.5, 1);
    r += LIS_BLUE_148;
    lis_id_191_t a1 = 1;
    r += a1;
    r += /*@LIS_QUEUE_202_902_use0*/LIS_QUEUE_202;
    return (long)(r);
}

long lis_mutex_resolve(lis_mutex_t* self, double p1, int p2) {
    double r = 0;
    r += p1;
    r += p2;
    r += bas_flush_page(1, 3.5);
    bas_signal_t o1;
    r += o1.stamp;
    r += o1.limit;
    r += bas_signal_derive(&o1, 2L);
    bas_real_13_t a2 = 1;
    r += a2;
    return (long)(r);
}

long lis_mutex_data(lis_mutex_t* self, int p1, int p2) {
    double r = 0;
    r += self->phase;
    r += p1;
    r += p2;
    bas_signal_t o1;
    r += bas_signal_derive(&o1, 2L);
    lis_region_t o2;
    r += lis_region_size(&o2, 3.5, 1);
    lis_region_t o3;
    r += o3.depth;
    r += lis_region_visit(&o3, 1);
    r += lis_region_count(&o3);
    r += scale(1);
    return (long)(r);
}

