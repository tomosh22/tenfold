#include "cache/bounded_worker_mailbox.h"
#include "util/buffer.h"
#include "cache/invoice.h"
#include "cache/funcs_route.h"
#include "cache/macros_task.h"

namespace svc::cache {


bool BoundedWorkerMailbox::verifyProfile(long p0) const {
    double r = 0;
    svc::util::Buffer o1;
    r += o1./*@combineCustomer_27_use0*/combineCustomer(1, 1);
    r += o1.delta;
    svc::cache::Invoice d2;
    svc::cache::Invoice& b3 = d2;
    r += b3.scanNotice(2L);
    return (bool)(r);
}

bool BoundedWorkerMailbox::resetQuota(double p0) {
    double r = 0;
    r += p0;
    svc::cache::Invoice d1;
    svc::cache::Invoice* b2 = &d1;
    r += b2->scanNotice(2L);
    r += svc::cache::flushBalance(3.5, 1);
    r += svc::cache::loadInvoice251(1);
    return (bool)(r);
}


}  // namespace svc::cache
