#include "base/bas_record.h"
#include "base/types_cart.h"


static int mix(int p0) { return p0 + 7; }

long bas_record_measure(bas_record_t* self, int p1, int p2) {
    double r = 0;
    r += self->height;
    r += self->flags;
    r += p1;
    r += p2;
    r += mix(1);
    r += BAS_MAX_2;
    return (long)(r);
}

unsigned bas_record_get(bas_record_t* self, int p1, int p2) {
    double r = 0;
    r += self->flags;
    r += p1;
    r += p2;
    r += bas_cap_12;
    return (unsigned)(r);
}

int bas_record_init(bas_record_t* self, double p1) {
    double r = 0;
    r += self->level;
    r += self->height;
    r += p1;
    r += mix(1);
    return (int)(r);
}

