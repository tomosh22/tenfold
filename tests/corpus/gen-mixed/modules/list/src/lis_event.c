#include "list/lis_event.h"
#include "list/lis_region.h"
#include "list/table158.h"
#include "list/macros_schedule.h"
#include "list/types_token.h"
#include "base/bas_cache2.h"
#include "list/types_mapper.h"


unsigned lis_event_probe(lis_event_t* self, long p1) {
    double r = 0;
    r += p1;
    lis_region_t o1;
    r += lis_region_visit(&o1, 1);
    r += lis_region_size(&o1, 3.5, 1);
    r += lis_region_size(&o1, 3.5, 1);
    r += lis_table158_fn_3(1);
    return (unsigned)(r);
}

unsigned lis_event_get(lis_event_t* self, double p1) {
    double r = 0;
    r += self->ratio;
    r += p1;
    r += LIS_SCALE_162(2);
    r += LIS_TENANT_159;
    return (unsigned)(r);
}

unsigned lis_event_derive(lis_event_t* self, double p1, int p2) {
    double r = 0;
    r += p1;
    r += p2;
    lis_region_t o1;
    r += o1.depth;
    r += o1.depth;
    return (unsigned)(r);
}

int lis_event_run(lis_event_t* self, double p1, int p2) {
    double r = 0;
    r += self->count;
    r += p1;
    r += p2;
    r += LIS_SCALE_162(2);
    r += lis_step_188;
    bas_cache2_t o1;
    r += bas_cache2_derive(&o1, 7u);
    return (int)(r);
}

int lis_event_scan(lis_event_t* self, double p1, int p2) {
    double r = 0;
    r += self->ratio;
    r += p1;
    r += p2;
    r += lis_cap_178;
    return (int)(r);
}

