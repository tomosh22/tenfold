#include "logging/dynamic_contract.h"
#include "logging/funcs_balance.h"
#include "logging/macros_account.h"
#include "logging/funcs_policy.h"
#include "util/types_profile.h"
#include "logging/types_mapper.h"
#include "util/funcs_shipment.h"
#include "logging/info.h"
#include "util/funcs_endpoint.h"
#include "logging/impl.h"
#include "util/buffer.h"
#include "util/static_ledger.h"
#include "logging/registry.h"

namespace svc {
namespace logging {


bool DynamicContract::id(double p0) {
    double r = 0;
    r += svc::logging::count(1, 3.5);
    r += reset_task(7u);
    r += LOGGING_SCALE_83(2);
    r += svc::logging::update_policy(3.5, 1);
    return (bool)(r);
}

long DynamicContract::data(double p0) const {
    double r = 0;
    r += svc::util::kScale1;
    svc::logging::Id69 a1 = 1;
    r += a1;
    r += svc::util::splitQueue(7u);
    svc::logging::Info o2;
    r += o2.set(1, 3.5);
    r += svc::util::id(1, 3.5);
    r += reset_task(7u);
    return (long)(r);
}

double DynamicContract::make() {
    double r = 0;
    svc::logging::Impl o1;
    r += o1.flags;
    r += o1.fetch_rule(3.5);
    r += o1.level;
    svc::util::Buffer d2;
    svc::util::StaticLedger& b3 = d2;
    r += b3.combineCustomer(1, 1);
    r += svc::logging::kScale67;
    svc::logging::/*@Registry_413_use8*/Registry o4;
    r += o4.combine_gateway(2L);
    r += o4.combine_gateway(2L);
    r += o4.combine_gateway(2L);
    r += svc::logging::kLimit68;
    return (double)(r);
}


}  // namespace logging
}  // namespace svc
