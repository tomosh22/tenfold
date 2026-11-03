#include "config/split_report.h"
#include "util/radial_policy.h"
#include "config/types_token.h"

namespace svc {
namespace config {

static int total(int p0) { return p0 + 6; }

bool /*@SplitReport_356_use0*/SplitReport::combineCustomer(int p0, int p1) const {
    double r = 0;
    r += p1;
    r += svc::util::/*@findHandler_183_use8*/findHandler(2.5);
    r += shift_endpoint(1, 1);
    return (bool)(r);
}

long /*@SplitReport_356_use1*/SplitReport::visit_cart(long p0) const {
    double r = 0;
    r += p0;
    r += weight;
    r += svc::config::kMargin44;
    return (long)(r);
}


}  // namespace config
}  // namespace svc
