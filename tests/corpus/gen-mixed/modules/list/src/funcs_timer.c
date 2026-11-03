#include "list/funcs_timer.h"
#include "list/lis_cache.h"
#include "list/lis_channel.h"
#include "list/lis_journal.h"
#include "mem/types_customer.h"
#include "base/bas_cache.h"

double lis_store_timer(int p0, int p1) {
    double r = 0;
    lis_cache_t o1;
    r += lis_cache_filter(&o1, 1, 3.5);
    lis_channel_t o2;
    r += lis_channel_encode(&o2, 1, 3.5);
    r += lis_channel_build(&o2);
    r += lis_channel_build(&o2);
    lis_journal_t o3;
    r += lis_journal_save(&o3, 3.5);
    r += lis_journal_bind(&o3, 1, 3.5);
    r += o3.mass;
    r += /*@mem_scale_71_379_use2*/mem_scale_71;
    bas_cache_t o4;
    r += bas_cache_scale(&o4, 1, 1);
    r += bas_cache_derive(&o4, 1);
    return (double)(r);
}

