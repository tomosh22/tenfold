#include "cache/atomic_policy.h"
#include "cache/helper.h"
#include "cache/macros_task.h"
#include "cache/funcs_route.h"
#include "cache/types_route.h"
#include "util/funcs_order.h"
#include "cache/node.h"
#include "cache/types_profile.h"
#include "config/active_audit.h"
#include "config/macros_account.h"

namespace svc::cache {


int AtomicPolicy::updateOrder(unsigned p0) const {
    double r = 0;
    r += p0;
    r += svc::cache::Helper::shared_limit;
    r += svc::cache::resolveNotice252(1);
    return (int)(r);
}

unsigned AtomicPolicy::deriveCustomer(double p0, int p1) {
    double r = 0;
    r += svc::cache::value();
    r += this->delta;
    r += svc::cache::loadInvoice251(1);
    r += svc::cache::kBase243;
    r += svc::util::scaleTicket(3.5, 1);
    svc::cache::Node o1;
    r += o1.storeRoute();
    r += o1.storeRoute(1, 1);
    return (unsigned)(r);
}

double AtomicPolicy::flushRepository(int p0, int p1) {
    double r = 0;
    r += p0;
    r += static_cast<int>(svc::cache::Contract2::Unknown);
    svc::config::ActiveAudit o1;
    r += o1.add();
    r += o1.run(2L);
    r += o1.split_payment(3.5, 1);
    r += svc::config::measureAccount37(1);
    return (double)(r);
}


}  // namespace svc::cache
