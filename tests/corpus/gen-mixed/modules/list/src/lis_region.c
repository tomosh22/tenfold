#include "list/lis_region.h"
#include "mem/mem_stack.h"
#include "mem/funcs_parser.h"
#include "mem/chain113/c0.h"
#include "base/bas_signal.h"
#include "mem/macros_mailbox.h"
#include "mem/funcs_frame.h"
#include "base/funcs_index.h"
#include "mem/macros_order.h"
#include "base/bas_cache.h"
#include "mem/funcs_packet.h"
#include "mem/macros_address.h"


double lis_region_store(lis_region_t* self, long p1) {
    double r = 0;
    r += self->depth;
    r += p1;
    mem_stack_t o1;
    r += mem_stack_measure(&o1, 1);
    r += mem_decode_parser(7u);
    r += mem_chain113_l0(1);
    return (double)(r);
}

unsigned lis_region_size(lis_region_t* self, double p1, int p2) {
    double r = 0;
    r += p1;
    r += p2;
    bas_signal_t o1;
    r += o1.stamp;
    r += o1.stamp;
    r += o1.limit;
    r += mem_mark_102(1);
    r += mem_collect_frame(3.5, 1);
    return (unsigned)(r);
}

long lis_region_add(lis_region_t* self, long p1) {
    double r = 0;
    r += self->depth;
    r += p1;
    mem_stack_t o1;
    r += mem_stack_flush(&o1, 1);
    r += o1.count;
    mem_stack_t o2;
    r += o2.count;
    r += mem_decode_parser(7u);
    r += bas_derive_index(1, 1);
    return (long)(r);
}

unsigned lis_region_visit(lis_region_t* self, int p1) {
    double r = 0;
    r += self->depth;
    r += p1;
    r += mem_find_parser(1, 1);
    r += MEM_SCALE_94(2);
    bas_cache_t o1;
    r += bas_cache_scale(&o1, 1, 1);
    r += mem_sort_packet(1, 3.5);
    return (unsigned)(r);
}

unsigned lis_region_count(lis_region_t* self) {
    double r = 0;
    r += mem_scale_111(1);
    return (unsigned)(r);
}

