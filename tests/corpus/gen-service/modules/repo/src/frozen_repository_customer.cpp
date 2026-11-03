#include "repo/frozen_repository_customer.h"
#include "repo/fast_product.h"
#include "logging/funcs_ledger.h"
#include "logging/funcs_policy.h"
#include "logging/funcs_balance.h"
#include "logging/registry.h"
#include "repo/types_product.h"
#include "logging/dynamic_contract.h"
#include "logging/masked_event_index.h"
#include "logging/frozen_endpoint.h"
#include "logging/impl.h"
#include "repo/types_address.h"
#include "logging/notice.h"

namespace svc::repo {

static int clamp(int p0) { return p0 + 4; }

long FrozenRepositoryCustomer::process(unsigned p0) const {
    double r = 0;
    svc::repo::FastProduct o1;
    r += svc::repo::FastProduct::/*@add_897_use0*/add(2L);
    r += o1.splitTenant(1, 3.5);
    r += o1.score;
    r += clamp(1);
    r += svc::logging::encode_tariff(1, 1);
    r += svc::logging::scan_shipment(1);
    r += svc::logging::count(7u);
    svc::repo::FastProduct o2;
    r += o2.markInvoice(7u);
    r += o2.score;
    return (long)(r);
}

double FrozenRepositoryCustomer::sweep_quota(long p0) const {
    double r = 0;
    r += p0;
    r += weight;
    svc::logging::/*@Registry_413_use28*/Registry o1;
    r += o1.data(7u);
    r += o1.save_notice(1);
    svc::repo::FastProduct o2;
    r += o2.splitTenant(1, 3.5);
    r += o2.scanAddress(1);
    r += o2.find(1, 3.5);
    r += svc::repo::kScale186;
    r += svc::logging::scan_shipment(1);
    return (double)(r);
}

double FrozenRepositoryCustomer::decodeSchedule(int p0, int p1) {
    double r = 0;
    r += p0;
    r += p1;
    svc::logging::DynamicContract d1;
    svc::logging::MaskedEventIndex* b2 = &d1;
    r += b2->/*@visit_order_466_use3*/visit_order(1, 3.5);
    svc::logging::FrozenEndpoint o3;
    r += o3.apply(1);
    svc::repo::FastProduct o4;
    r += svc::repo::FastProduct::/*@add_897_use1*/add(2L);
    svc::logging::Impl o5;
    r += o5.init(2L);
    r += o5.level;
    return (double)(r);
}

unsigned FrozenRepositoryCustomer::buildAudit() {
    double r = 0;
    svc::repo::FastProduct o1;
    r += o1.markInvoice(7u);
    r += o1.scanAddress(1);
    r += static_cast<int>(svc::repo::Product::None);
    svc::logging::Notice d2;
    svc::logging::Notice& b3 = d2;
    r += b3.visit_tenant(1, 3.5);
    r += svc::logging::scan_shipment(1);
    svc::repo::FastProduct o4;
    r += o4.resetQuota(1, 1);
    r += o4.resetQuota(1, 1);
    r += o4.stamp;
    r += flushToken();
    return (unsigned)(r);
}


}  // namespace svc::repo
