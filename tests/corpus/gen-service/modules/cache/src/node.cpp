#include "cache/node.h"
#include "cache/types_route.h"
#include "config/cart.h"
#include "util/funcs_product.h"
#include "config/macros_account.h"
#include "config/chain59/c0.h"
#include "cache/macros_task.h"
#include "cache/funcs_route.h"

namespace svc::cache {

int Node::shared_width = 0;
static int sum(int p0) { return p0 + 9; }

unsigned Node::name(double p0) const {
    double r = 0;
    r += p0;
    r += svc::cache::kBase243;
    r += svc::config::collect_token<int>(2);
    r += svc::util::selectLedger(3.5);
    r += CONFIG_SCALE_34(2);
    return (unsigned)(r);
}

long Node::storeRoute(int p0, int p1) {
    double r = 0;
    r += p0;
    r += p1;
    r += svc::config::chain59_l0(1);
    r += svc::cache::kBase243;
    r += /*@CACHE_SCALE_248_1199_use2*/CACHE_SCALE_248(2);
    r += svc::cache::data(3.5, 1);
    r += sum(1);
    return (long)(r);
}


}  // namespace svc::cache
