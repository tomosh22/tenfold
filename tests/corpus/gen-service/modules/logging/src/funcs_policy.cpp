#include "logging/funcs_policy.h"
#include "logging/funcs_balance.h"
#include "util/profile.h"
#include "logging/types_mapper.h"

namespace svc {
namespace logging {

double update_policy(double p0, int p1) {
    double r = 0;
    r += p1;
    r += count(1, 3.5);
    svc::util::Profile d1;
    svc::util::Profile* b2 = &d1;
    r += b2->/*@combineCustomer_89_use0*/combineCustomer(1, 1);
    return (double)(r);
}

long scan_shipment(int p0) {
    double r = 0;
    svc::logging::/*@Handle_374_use6*/Handle k1 = svc::logging::/*@Handle_374_use7*/Handle::Count;
    r += static_cast<int>(k1);
    r += svc::logging::kScale67;
    r += svc::logging::find_queue();
    return (long)(r);
}


}  // namespace logging
}  // namespace svc
