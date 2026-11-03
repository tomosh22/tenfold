#include "cache/product2.h"
#include "cache/config.h"
#include "config/types_tenant.h"

namespace svc::cache {

static int /*@pick_1235_def0*/pick(int p0) { return p0 + 2; }

double Product2::/*@apply_1229_def0*/apply(unsigned p0) const {
    double r = 0;
    r += p0;
    svc::cache::Config o1;
    r += o1.offset;
    r += svc::config::kMargin28;
    svc::cache::Config o2;
    r += o2.deriveToken(7u);
    r += svc::cache::Config::top();
    r += svc::cache::Config::top();
    r += /*@pick_1235_use0*/pick(1);
    return (double)(r);
}


}  // namespace svc::cache
