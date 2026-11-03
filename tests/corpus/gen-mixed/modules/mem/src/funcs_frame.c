#include "mem/funcs_frame.h"
#include "base/bas_record.h"
#include "mem/funcs_parser.h"
#include "base/bas_socket.h"
#include "base/bas_signal.h"
#include "mem/types_customer.h"
#include "base/macros_notice.h"
#include "base/bas_list.h"
#include "base/bas_cache.h"
#include "base/funcs_region.h"

long mem_verify_frame(unsigned p0) {
    double r = 0;
    r += p0;
    bas_record_t o1;
    r += bas_record_measure(&o1, 1, 1);
    r += bas_record_init(&o1, 3.5);
    r += bas_record_init(&o1, 3.5);
    r += mem_find_parser(1, 1);
    bas_socket_t o2;
    r += bas_socket_verify(&o2, 3.5);
    r += bas_socket_select(&o2);
    r += mem_update_parser();
    bas_signal_t o3;
    r += bas_signal_process(&o3, 3.5, 1);
    r += bas_signal_process(&o3, 3.5, 1);
    r += o3.stamp;
    mem_size_72_t a4 = 1;
    r += a4;
    return (long)(r);
}

int mem_collect_frame(double p0, int p1) {
    double r = 0;
    r += p0;
    r += bas_compute_19(1);
    bas_signal_t o1;
    r += bas_signal_derive(&o1, 2L);
    r += bas_signal_filter(&o1, 7u);
    return (int)(r);
}

long mem_flush_frame(int p0) {
    double r = 0;
    r += p0;
    r += mem_find_parser(1, 1);
    bas_signal_t o1;
    r += bas_signal_process(&o1, 3.5, 1);
    bas_list_t o2;
    r += bas_list_name(&o2, 7u);
    r += bas_list_collect(&o2, 3.5);
    return (long)(r);
}

int mem_apply_frame(int p0) {
    double r = 0;
    bas_cache_t o1;
    r += o1.scale;
    r += bas_cache_flush(&o1, 1, 1);
    r += bas_cache_scale(&o1, 1, 1);
    r += /*@BAS_JOB_15_43_use9*/BAS_JOB_15;
    r += /*@BAS_JOB_15_43_use10*/BAS_JOB_15;
    r += bas_save_region(7u);
    return (int)(r);
}

long mem_build_frame(int p0, double p1) {
    double r = 0;
    r += p0;
    r += BAS_SCALE_16(2);
    r += mem_apply_frame(1);
    r += mem_probe_parser(3.5, 1);
    r += mem_probe_parser(3.5, 1);
    return (long)(r);
}

