#include "cache/funcs_account.h"
#include "cache/macros_account.h"
#include "util/radial_policy.h"
#include "cache/macros_task.h"
#include "util/funcs_shipment.h"
#include "cache/node.h"
#include "util/funcs_account.h"
#include "cache/types_profile.h"
#include "cache/funcs_report.h"
#include "config/entry.h"
#include "cache/types_cart.h"
#include "cache/types_invoice.h"

namespace svc::cache {

long name(double p0, int p1) {
    double r = 0;
    r += svc::cache::decodeJob266(1);
    r += svc::util::/*@findHandler_183_use0*/findHandler<int>(2);
    r += CACHE_SCALE_249(2);
    r += svc::util::splitQueue(7u);
    r += svc::cache::Node::shared_width;
    return (long)(r);
}

int computeAudit(int p0) {
    double r = 0;
    r += svc::util::probeCatalog();
    svc::cache::Real260 a1 = 1;
    r += a1;
    r += svc::cache::get(1, 3.5);
    svc::config::Entry o2;
    r += o2.bind_policy(1, 3.5);
    return (int)(r);
}

unsigned bindLedger(long p0) {
    double r = 0;
    r += p0;
    r += svc::cache::kStep269;
    svc::cache::MappedQueue k1 = svc::cache::MappedQueue::Blue;
    r += static_cast<int>(k1);
    r += CACHE_CATALOG_246;
    r += static_cast<int>(svc::cache::Contract2::Red);
    return (unsigned)(r);
}


}  // namespace svc::cache
