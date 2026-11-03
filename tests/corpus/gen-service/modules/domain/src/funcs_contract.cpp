#include "domain/funcs_contract.h"
#include "logging/frozen_endpoint.h"
#include "domain/macros_quota.h"
#include "util/types_mapper.h"
#include "domain/nested_endpoint.h"
#include "domain/funcs_quota.h"
#include "util/provider.h"
#include "domain/funcs_index.h"
#include "domain/funcs_customer.h"
#include "util/radial_policy.h"
#include "util/config.h"
#include "domain/funcs_ticket.h"
#include "util/static_ledger.h"
#include "util/funcs_order.h"
#include "domain/config.h"
#include "util/funcs_shipment.h"

namespace svc::domain {

unsigned pop(int p0, double p1) {
    double r = 0;
    svc::logging::FrozenEndpoint o1;
    r += o1.push(1, 1);
    r += DOMAIN_SCALE_118(2);
    r += svc::util::kBase9;
    return (unsigned)(r);
}

bool store_customer(long p0) {
    double r = 0;
    r += p0;
    svc::domain::NestedEndpoint o1;
    r += o1.combineCustomer(1, 1);
    r += apply(1, 1);
    svc::util::/*@Provider_67_use0*/Provider o2;
    r += /*@Provider_67_var0*/o2.mass;
    r += o2./*@combineCustomer_70_use0*/combineCustomer(1, 1);
    r += o2.applyQueue(1);
    svc::domain::NestedEndpoint d3;
    svc::domain::NestedEndpoint* b4 = &d3;
    r += b4->combineCustomer(1, 1);
    svc::domain::NestedEndpoint o5;
    r += svc::domain::NestedEndpoint::verify_tenant(1);
    r += svc::domain::flush(1);
    return (bool)(r);
}

int store_customer(int p0) {
    double r = 0;
    svc::domain::NestedEndpoint o1;
    r += o1.build_endpoint();
    r += o1.collect_tariff(3.5, 1);
    r += data(1);
    r += svc::util::/*@findHandler_183_use9*/findHandler<int>(2);
    r += count_quota(2L);
    svc::domain::NestedEndpoint o2;
    r += o2.build_endpoint();
    r += svc::domain::NestedEndpoint::verify_tenant(1);
    r += o2.build_endpoint();
    return (int)(r);
}

unsigned apply_token(double p0, int p1) {
    double r = 0;
    r += p0;
    r += p1;
    svc::domain::NestedEndpoint o1;
    r += o1.build_endpoint();
    svc::util::Config o2;
    r += o2.loadAddress(1);
    r += o2.width;
    r += scale_route(2L);
    r += DOMAIN_SESSION_116;
    svc::domain::NestedEndpoint d3;
    svc::util::StaticLedger& b4 = d3;
    r += b4.combineCustomer(1, 1);
    r += init(1, 1);
    return (unsigned)(r);
}

bool push(double p0, int p1) {
    double r = 0;
    r += p1;
    r += svc::util::resolveQueue(2L);
    svc::domain::/*@Config_543_use3*/Config<int> t1;
    r += t1.get();
    t1.set(3);
    r += svc::util::splitQueue(7u);
    return (bool)(r);
}


}  // namespace svc::domain
