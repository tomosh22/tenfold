#include "domain/funcs_account.h"
#include "domain/nested_endpoint.h"
#include "util/static_ledger.h"
#include "domain/funcs_quota.h"
#include "domain/funcs_index.h"
#include "util/types_profile.h"
#include "util/buffer.h"
#include "domain/types_gateway.h"

namespace svc::domain {

static int clamp(int p0) { return p0 + 7; }

unsigned shift_catalog(int p0, int p1) {
    double r = 0;
    r += p1;
    svc::domain::NestedEndpoint d1;
    svc::util::StaticLedger* b2 = &d1;
    r += b2->combineCustomer(1, 1);
    svc::domain::NestedEndpoint o3;
    r += o3.combineCustomer(1, 1);
    r += o3.combineCustomer(1, 1);
    r += svc::domain::count_quota(2L);
    return (unsigned)(r);
}

long encode_mapper() {
    double r = 0;
    r += svc::domain::flush(1);
    r += svc::util::kScale1;
    svc::util::Buffer o1;
    r += o1.scale;
    r += o1.apply();
    return (long)(r);
}

long reset(double p0, int p1) {
    double r = 0;
    r += svc::domain::init(1, 1);
    r += svc::domain::MaskedLedger_Default;
    r += init(1, 1);
    svc::domain::NestedEndpoint o1;
    r += o1./*@scan_token_624_use4*/scan_token(3.5, 1);
    r += svc::domain::NestedEndpoint::verify_tenant(1);
    r += o1.combineCustomer(1, 1);
    return (long)(r);
}


}  // namespace svc::domain
