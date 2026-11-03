#include "logging/frozen_endpoint.h"
#include "logging/types_balance.h"
#include "util/buffer.h"
#include "util/static_ledger.h"
#include "logging/types_audit.h"
#include "logging/macros_job.h"
#include "util/types_profile.h"
#include "logging/radial_index_repository.h"
#include "logging/types_mapper.h"
#include "logging/info.h"
#include "logging/chain95/c2.h"
#include "logging/registry.h"
#include "logging/funcs_balance.h"

namespace svc {
namespace logging {

static int step(int p0) { return p0 + 1; }

double FrozenEndpoint::push(int p0, int p1) {
    double r = 0;
    r += p1;
    r += svc::logging::RigidPolicy_Red;
    svc::util::Buffer d1;
    svc::util::StaticLedger* b2 = &d1;
    r += b2->combineCustomer(1, 1);
    svc::logging::Size79 a3 = 1;
    r += a3;
    r += LOGGING_SCALE_90(2);
    r += svc::util::kScale1;
    return (double)(r);
}

long FrozenEndpoint::reset(int p0, double p1) const {
    double r = 0;
    r += p0;
    svc::logging::RadialIndexRepository o1;
    r += o1.find_repository(1, 1);
    r += o1.limit;
    r += static_cast<int>(svc::logging::/*@Handle_374_use2*/Handle::/*@Last_377_use0*/Last);
    svc::logging::Info o2;
    r += o2.top(7u);
    r += o2.combine_queue(1);
    r += svc::logging::chain95_l2(1);
    svc::logging::RadialIndexRepository::shared_height++;
    return (long)(r);
}

double FrozenEndpoint::apply(int p0) const {
    double r = 0;
    r += apply(1);
    r += svc::logging::kCap70;
    r += sweep_quota(2L);
    svc::logging::/*@Registry_413_use9*/Registry o1;
    r += o1.combine_invoice(3.5, 1);
    r += o1.combine_gateway(2L);
    r += o1.mass;
    r += svc::logging::chain95_l2(1);
    return (double)(r);
}

double FrozenEndpoint::sweep_quota(long p0) const {
    double r = 0;
    r += LOGGING_TENANT_87;
    r += svc::logging::count(1, 3.5);
    r += svc::logging::resetRule94(1);
    return (double)(r);
}


}  // namespace logging
}  // namespace svc
