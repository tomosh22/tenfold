#include "list/lis_stream2.h"
#include "list/lis_frame.h"
#include "mem/mem_buffer.h"
#include "mem/macros_order.h"
#include "mem/funcs_queue.h"
#include "base/types_cart.h"
#include "list/types_mapper_192.h"
#include "list/macros_schedule.h"
#include "list/lis_channel.h"
#include "list/lis_region.h"
#include "list/lis_stream.h"
#include "list/table158.h"


long lis_stream2_empty(lis_stream2_t* self, double p1) {
    double r = 0;
    r += self->limit;
    r += self->height;
    r += p1;
    lis_frame_t o1;
    r += lis_frame_reset(&o1, 2L);
    r += lis_frame_reset(&o1, 2L);
    r += o1.mass;
    mem_buffer_t o2;
    r += mem_buffer_reset(&o2, 3.5);
    r += mem_buffer_clear(&o2, 2L);
    r += mem_buffer_reset(&o2, 3.5);
    return (long)(r);
}

long lis_stream2_make(lis_stream2_t* self, double p1, int p2) {
    double r = 0;
    r += self->index;
    r += p1;
    r += p2;
    r += MEM_JOB_93;
    r += mem_combine_queue(7u);
    r += BAS_AUTO_7;
    /*@lis_real_200_t_877_use1*/lis_real_200_t a1 = 1;
    r += a1;
    return (long)(r);
}

int lis_stream2_collect(lis_stream2_t* self, int p1, int p2) {
    double r = 0;
    r += self->height;
    r += p1;
    r += p2;
    r += /*@LIS_SCALE_163_778_use2*/LIS_SCALE_163(2);
    lis_channel_t o1;
    r += o1.height;
    return (int)(r);
}

int lis_stream2_split(lis_stream2_t* self, int p1, int p2) {
    double r = 0;
    r += self->index;
    r += p1;
    r += p2;
    lis_region_t o1;
    r += o1.depth;
    r += lis_region_store(&o1, 2L);
    r += o1.depth;
    return (int)(r);
}

unsigned lis_stream2_init(lis_stream2_t* self, long p1) {
    double r = 0;
    r += self->limit;
    r += p1;
    r += LIS_SCALE_162(2);
    lis_stream_t o1;
    r += o1.speed;
    r += lis_table158_fn_3(1);
    return (unsigned)(r);
}

