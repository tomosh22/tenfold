#include "domain/funcs_profile.h"
#include "util/funcs_product.h"
#include "logging/dynamic_contract.h"
#include "logging/masked_event_index.h"
#include "domain/types_report.h"
#include "util/funcs_account.h"
#include "domain/macros_quota.h"
#include "util/profile.h"
#include "domain/nested_endpoint.h"
#include "util/static_ledger.h"
#include "logging/types_balance.h"
#include "domain/funcs_index.h"

namespace svc::domain {

long set(unsigned p0) {
    double r = 0;
    r += svc::util::/*@selectMailbox_62_use1*/selectMailbox(1);
    svc::logging::DynamicContract d1;
    svc::logging::MaskedEventIndex& b2 = d1;
    r += b2.reset_task(7u);
    return (long)(r);
}

long sort_address(double p0) {
    double r = 0;
    svc::domain::Handle101 a1 = 1;
    r += a1;
    r += svc::util::update(1);
    r += DOMAIN_SESSION_116;
    r += svc::util::Profile::shared_scale;
    return (long)(r);
}

unsigned fetch_cart(int p0) {
    double r = 0;
    svc::domain::NestedEndpoint d1;
    svc::util::StaticLedger& b2 = d1;
    r += b2.combineCustomer(1, 1);
    r += svc::logging::kCap70;
    r += svc::domain::init(1, 1);
    r += svc::domain::FrozenBalance_None;
    return (unsigned)(r);
}


}  // namespace svc::domain
