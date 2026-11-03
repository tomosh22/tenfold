#include "list/lis_stack.h"
#include "list/table158.h"
#include "list/funcs_socket.h"
#include "mem/funcs_entry.h"
#include "list/types_handler.h"
#include "base/types_cart.h"
#include "list/types_mapper.h"
#include "list/types_mapper_192.h"


long lis_stack_find(lis_stack_t* self, unsigned p1) {
    double r = 0;
    r += self->phase;
    r += self->offset;
    r += p1;
    r += /*@lis_table158_fn_0_752_use4*/lis_table158_fn_0(1);
    return (long)(r);
}

unsigned lis_stack_count(lis_stack_t* self, unsigned p1) {
    double r = 0;
    r += self->phase;
    r += self->offset;
    r += p1;
    r += lis_store_socket(2L);
    r += mem_measure_entry(3.5, 1);
    enum /*@lis_handler_state_721_use1*/lis_handler_state k1 = LIS_LAST_146;
    r += k1;
    return (unsigned)(r);
}

unsigned lis_stack_measure(lis_stack_t* self, int p1) {
    double r = 0;
    r += self->offset;
    r += p1;
    r += /*@lis_cap_156_733_use7*/lis_cap_156;
    r += bas_step_10;
    r += LIS_FIRST_171;
    /*@lis_real_200_t_877_use0*/lis_real_200_t a1 = 1;
    r += a1;
    return (unsigned)(r);
}

