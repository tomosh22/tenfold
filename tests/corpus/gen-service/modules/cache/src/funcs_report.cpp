#include "cache/funcs_report.h"
#include "cache/macros_task.h"
#include "util/funcs_shipment.h"
#include "util/queue2.h"
#include "util/radial_policy.h"
#include "util/buffer.h"
#include "util/static_ledger.h"
#include "cache/types_route.h"
#include "config/cart.h"
#include "config/chain59/c0.h"

namespace svc::cache {

bool next(long p0) {
    double r = 0;
    r += resolveNotice252(1);
    r += svc::util::splitQueue(7u);
    svc::util::Queue2 o1;
    r += o1.encodeIndex(7u);
    return (bool)(r);
}

unsigned next(double p0, int p1) {
    double r = 0;
    r += p0;
    r += p1;
    svc::util::/*@RadialPolicy_175_use0*/RadialPolicy<int> t1;
    r += t1.get();
    t1.set(3);
    svc::util::Buffer d2;
    svc::util::StaticLedger* b3 = &d2;
    r += b3->combineCustomer(1, 1);
    r += /*@CACHE_SCALE_248_1199_use0*/CACHE_SCALE_248(2);
    r += svc::cache::kBase243;
    svc::cache::Id245 a4 = 1;
    r += a4;
    svc::config::/*@Cart_205_use4*/Cart<int> t5;
    r += t5.get();
    t5.set(3);
    return (unsigned)(r);
}

double get(int p0, double p1) {
    double r = 0;
    r += p0;
    r += svc::config::chain59_entry(1);
    r += svc::cache::kBase243;
    svc::cache::Id245 a1 = 1;
    r += a1;
    svc::util::/*@RadialPolicy_175_use1*/RadialPolicy<int> t2;
    r += t2.get();
    t2.set(3);
    return (double)(r);
}

bool scanTask(int p0) {
    double r = 0;
    r += svc::util::combineMailbox(2L);
    r += svc::util::splitQueue(7u);
    return (bool)(r);
}


}  // namespace svc::cache
