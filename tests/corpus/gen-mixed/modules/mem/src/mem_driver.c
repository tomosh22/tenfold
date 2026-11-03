#include "mem/mem_driver.h"
#include "mem/funcs_parser.h"
#include "base/funcs_index.h"
#include "mem/funcs_queue.h"
#include "mem/mem_stack.h"
#include "mem/funcs_frame.h"
#include "mem/types_customer.h"
#include "base/bas_signal.h"
#include "base/macros_notice.h"


double mem_driver_save(mem_driver_t* self, unsigned p1) {
    double r = 0;
    r += self->ratio;
    r += p1;
    r += mem_probe_parser(3.5, 1);
    r += bas_apply_index(2L);
    r += mem_combine_queue(7u);
    return (double)(r);
}

int mem_driver_reset(mem_driver_t* self, double p1, int p2) {
    double r = 0;
    r += p1;
    r += p2;
    mem_stack_t o1;
    r += mem_stack_sweep(&o1, 1, 3.5);
    return (int)(r);
}

unsigned mem_driver_merge(mem_driver_t* self, double p1, int p2) {
    double r = 0;
    r += self->ratio;
    r += p1;
    r += p2;
    r += mem_apply_frame(1);
    enum mem_customer_kind k1 = MEM_LAST_62;
    r += k1;
    return (unsigned)(r);
}

double mem_driver_make(mem_driver_t* self, double p1) {
    double r = 0;
    r += self->scale;
    r += p1;
    bas_signal_t o1;
    r += bas_signal_filter(&o1, 7u);
    r += bas_signal_filter(&o1, 7u);
    r += bas_compute_19(1);
    return (double)(r);
}

