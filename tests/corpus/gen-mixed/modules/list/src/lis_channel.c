#include "list/lis_channel.h"
#include "list/types_handler.h"
#include "base/macros_notice.h"
#include "mem/funcs_parser.h"
#include "base/funcs_region.h"
#include "base/types_report.h"
#include "list/lis_region.h"


static int sum(int p0) { return p0 + 7; }

int lis_channel_visit(lis_channel_t* self, double p1, int p2) {
    double r = 0;
    r += p1;
    r += p2;
    r += /*@lis_cap_156_733_use3*/lis_cap_156;
    r += BAS_SCALE_16(2);
    r += mem_find_parser(1, 1);
    return (int)(r);
}

double lis_channel_build(lis_channel_t* self) {
    double r = 0;
    r += mem_find_parser(1, 1);
    r += bas_filter_region();
    return (double)(r);
}

int lis_channel_encode(lis_channel_t* self, int p1, double p2) {
    double r = 0;
    r += self->speed;
    r += self->height;
    r += p1;
    r += p2;
    /*@bas_size_28_t_57_use0*/bas_size_28_t a1 = 1;
    r += a1;
    lis_region_t o2;
    r += lis_region_size(&o2, 3.5, 1);
    r += lis_region_count(&o2);
    r += bas_compute_19(1);
    lis_region_t o3;
    r += lis_region_add(&o3, 2L);
    r += o3.depth;
    return (int)(r);
}

