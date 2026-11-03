#include "list/lis_stream.h"
#include "mem/types_customer.h"
#include "base/bas_signal.h"
#include "list/funcs_buffer.h"
#include "base/types_cart.h"
#include "list/lis_channel.h"
#include "base/bas_queue.h"
#include "list/table158.h"
#include "list/types_handler.h"


unsigned lis_stream_compute(lis_stream_t* self, long p1) {
    double r = 0;
    r += self->index;
    r += self->stamp;
    r += p1;
    r += MEM_NONE_63;
    bas_signal_t o1;
    r += bas_signal_filter(&o1, 7u);
    r += o1.stamp;
    r += lis_collect_buffer(3.5, 1);
    r += BAS_MAX_2;
    return (unsigned)(r);
}

long lis_stream_empty(lis_stream_t* self) {
    double r = 0;
    r += self->speed;
    r += self->index;
    r += self->stamp;
    lis_channel_t o1;
    r += o1.height;
    r += lis_channel_visit(&o1, 3.5, 1);
    lis_channel_t o2;
    r += lis_channel_encode(&o2, 1, 3.5);
    r += lis_channel_visit(&o2, 3.5, 1);
    return (long)(r);
}

long lis_stream_resolve(lis_stream_t* self, int p1) {
    double r = 0;
    r += self->index;
    r += self->stamp;
    r += p1;
    bas_queue_t o1;
    r += bas_queue_apply(&o1, 1);
    r += bas_queue_clear(&o1, 3.5);
    r += lis_table158_fn_3(1);
    return (long)(r);
}

double lis_stream_add(lis_stream_t* self, long p1) {
    double r = 0;
    r += self->index;
    r += self->stamp;
    r += p1;
    r += lis_table158_fn_3(1);
    r += /*@lis_cap_156_733_use8*/lis_cap_156;
    lis_channel_t o1;
    r += lis_channel_build(&o1);
    r += lis_channel_encode(&o1, 1, 3.5);
    r += MEM_UNKNOWN_67;
    return (double)(r);
}

