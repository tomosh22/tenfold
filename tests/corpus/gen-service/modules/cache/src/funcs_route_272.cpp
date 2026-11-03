#include "cache/funcs_route_272.h"
#include "cache/funcs_account.h"
#include "config/impl.h"
#include "util/radial_policy.h"
#include "config/cart.h"
#include "cache/funcs_report.h"
#include "cache/macros_account.h"
#include "util/types_mapper.h"
#include "util/funcs_product.h"
#include "cache/product2.h"
#include "util/static_ledger.h"
#include "cache/macros_task.h"
#include "cache/invoice.h"
#include "util/funcs_shipment.h"
#include "cache/node.h"

namespace svc::cache {

static int total(int p0) { return p0 + 4; }

int find(double p0) {
    double r = 0;
    r += p0;
    r += svc::cache::computeAudit(1);
    svc::config::Impl::/*@shared_delta_325_use7*/shared_delta++;
    r += svc::util::/*@findHandler_183_use2*/findHandler<int>(2);
    r += total(1);
    svc::config::/*@Cart_205_use5*/Cart<int> t1;
    r += t1.get();
    t1.set(3);
    return (int)(r);
}

double find(long p0) {
    double r = 0;
    r += svc::cache::next(2L);
    r += total(1);
    r += total(1);
    r += CACHE_REPOSITORY_262;
    svc::config::Impl o1;
    r += o1./*@rank_323_use1*/rank;
    r += o1.phase;
    r += o1./*@rank_323_use2*/rank;
    svc::util::/*@RadialPolicy_179_use0*/RadialPolicy<bool> t2;
    r += t2.get();
    t2.set(true);
    return (double)(r);
}

long run(unsigned p0) {
    double r = 0;
    r += svc::util::kBase8;
    r += total(1);
    r += svc::util::/*@selectMailbox_62_use0*/selectMailbox(1);
    r += svc::util::/*@findHandler_183_use3*/findHandler<int>(2);
    return (long)(r);
}

double scaleMapper(double p0, int p1) {
    double r = 0;
    svc::cache::Product2 d1;
    svc::util::StaticLedger* b2 = &d1;
    r += b2->combineCustomer(1, 1);
    r += resolveNotice252(1);
    return (double)(r);
}

bool /*@sortAccount_1327_def0*/sortAccount(int p0, double p1) {
    double r = 0;
    r += svc::cache::next(2L);
    r += scanTask(1);
    r += total(1);
    svc::cache::Invoice o1;
    r += o1.rotateBalance(2L);
    r += svc::util::/*@findHandler_183_use4*/findHandler(2.5);
    r += svc::util::measureToken(2L);
    return (bool)(r);
}

int sortAccount(long p0) {
    double r = 0;
    r += p0;
    svc::cache::Node o1;
    r += o1.storeRoute();
    r += o1.name(3.5);
    r += total(1);
    return (int)(r);
}


}  // namespace svc::cache
