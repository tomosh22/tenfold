#include "cache/atomic_profile.h"
#include "cache/node.h"
#include "cache/helper.h"
#include "cache/macros_task.h"
#include "util/types_profile.h"

namespace svc::cache {

static int clamp(int p0) { return p0 + 7; }

long AtomicProfile::pop(unsigned p0) const {
    double r = 0;
    r += p0;
    svc::cache::Node o1;
    r += o1.storeRoute();
    r += svc::cache::Node::shared_width;
    r += svc::cache::Helper::shared_limit;
    r += svc::cache::loadInvoice251(1);
    r += static_cast<int>(svc::util::/*@StaticCatalog_0_use0*/StaticCatalog::Default);
    r += height;
    return (long)(r);
}


}  // namespace svc::cache
