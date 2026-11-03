#include "base/bas_cache2.h"
#include "base/macros_notice.h"
#include "base/types_cart.h"
#include "base/bas_record.h"
#include "base/bas_signal.h"
#include "base/bas_list.h"


long bas_cache2_mark(bas_cache2_t* self) {
    double r = 0;
    r += self->speed;
    r += self->count;
    r += /*@bas_encode_20_48_use0*/bas_encode_20(1);
    return (long)(r);
}

unsigned bas_cache2_derive(bas_cache2_t* self, unsigned p1) {
    double r = 0;
    r += self->depth;
    r += self->speed;
    r += self->count;
    r += p1;
    bas_real_13_t a1 = 1;
    r += a1;
    return (unsigned)(r);
}

int bas_cache2_save(bas_cache2_t* self, unsigned p1) {
    double r = 0;
    r += p1;
    bas_record_t o1;
    r += bas_record_init(&o1, 3.5);
    bas_signal_t o2;
    r += bas_signal_filter(&o2, 7u);
    r += bas_signal_derive(&o2, 2L);
    bas_list_t o3;
    r += o3.stamp;
    r += bas_list_collect(&o3, 3.5);
    r += bas_list_clear(&o3, 2L);
    return (int)(r);
}

