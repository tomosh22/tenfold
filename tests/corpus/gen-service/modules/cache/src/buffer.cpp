#include "cache/buffer.h"
#include "cache/config.h"
#include "cache/node.h"
#include "cache/types_profile.h"
#include "cache/macros_task.h"
#include "util/types_profile.h"
#include "cache/types_route.h"

namespace svc::cache {

static int scale(int p0) { return p0 + 5; }

long Buffer::applyInvoice(int p0, double p1) {
    double r = 0;
    r += p0;
    svc::cache::Config o1;
    r += o1.offset;
    r += svc::cache::Node::shared_width;
    r += svc::cache::kMargin258;
    r += CACHE_CATALOG_246;
    return (long)(r);
}

unsigned Buffer::size(double p0, int p1) {
    double r = 0;
    r += p0;
    r += p1;
    r += size(3.5, 1);
    r += svc::util::kScale1;
    svc::cache::Size244 a1 = 1;
    r += a1;
    return (unsigned)(r);
}


}  // namespace svc::cache
