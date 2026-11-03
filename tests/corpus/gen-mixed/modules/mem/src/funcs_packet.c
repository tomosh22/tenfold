#include "mem/funcs_packet.h"
#include "base/bas_signal.h"
#include "mem/macros_mailbox.h"
#include "mem/types_customer.h"
#include "mem/funcs_queue.h"
#include "base/bas_record.h"
#include "mem/mem_buffer.h"
#include "mem/mem_stack.h"
#include "mem/funcs_parser.h"

long mem_fetch_packet(int p0, int p1) {
    double r = 0;
    r += p1;
    bas_signal_t o1;
    r += bas_signal_filter(&o1, 7u);
    r += o1.stamp;
    r += MEM_TENANT_99;
    r += MEM_DEFAULT_66;
    r += mem_count_queue(3.5, 1);
    r += mem_count_queue(3.5, 1);
    bas_record_t o2;
    r += bas_record_init(&o2, 3.5);
    r += bas_record_get(&o2, 1, 1);
    r += o2.flags;
    return (long)(r);
}

unsigned mem_sort_packet(int p0, double p1) {
    double r = 0;
    mem_buffer_t o1;
    r += mem_buffer_clear(&o1, 2L);
    r += o1.flags;
    r += mem_buffer_reset(&o1, 3.5);
    mem_stack_t o2;
    r += o2.count;
    mem_size_72_t a3 = 1;
    r += a3;
    mem_stack_t o4;
    r += mem_stack_find(&o4);
    r += mem_stack_mark(&o4, 1);
    r += mem_decode_parser(7u);
    return (unsigned)(r);
}

