#include "mem/funcs_parser.h"
#include "base/funcs_region.h"
#include "mem/types_customer.h"
#include "base/funcs_page.h"
#include "base/bas_cache2.h"
#include "base/bas_record.h"
#include "base/bas_signal.h"
#include "base/types_cart.h"
#include "base/bas_list.h"

int mem_find_parser(int p0, int p1) {
    double r = 0;
    r += p1;
    r += bas_compute_region(1, 3.5);
    enum mem_customer_kind k1 = MEM_BLUE_65;
    r += k1;
    mem_size_72_t a2 = 1;
    r += a2;
    r += bas_shift_page(2L);
    mem_real_73_t a3 = 1;
    r += a3;
    return (int)(r);
}

double mem_decode_parser(unsigned p0) {
    double r = 0;
    r += mem_find_parser(1, 1);
    bas_cache2_t o1;
    r += bas_cache2_save(&o1, 7u);
    return (double)(r);
}

double mem_probe_parser(double p0, int p1) {
    double r = 0;
    r += mem_find_parser(1, 1);
    bas_record_t o1;
    r += o1.flags;
    r += mem_find_parser(1, 1);
    return (double)(r);
}

unsigned mem_mark_parser(void) {
    double r = 0;
    bas_signal_t o1;
    r += o1.limit;
    r += bas_signal_process(&o1, 3.5, 1);
    r += /*@bas_step_11_12_use3*/bas_step_11;
    bas_record_t o2;
    r += bas_record_measure(&o2, 1, 1);
    r += bas_record_measure(&o2, 1, 1);
    r += bas_record_get(&o2, 1, 1);
    bas_list_t o3;
    r += bas_list_name(&o3, 7u);
    r += bas_list_name(&o3, 7u);
    return (unsigned)(r);
}

long mem_update_parser(void) {
    double r = 0;
    r += mem_decode_parser(7u);
    r += bas_step_10;
    r += /*@mem_scale_71_379_use3*/mem_scale_71;
    bas_signal_t o1;
    r += bas_signal_process(&o1, 3.5, 1);
    r += o1.stamp;
    return (long)(r);
}

