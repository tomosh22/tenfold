#include "domain/funcs_event.h"
#include "domain/funcs_contract.h"
#include "logging/types_mapper.h"
#include "domain/types_quota.h"
#include "util/funcs_order.h"
#include "util/buffer.h"
#include "domain/macros_quota.h"
#include "logging/types_balance.h"
#include "domain/nested_endpoint.h"
#include "logging/dynamic_contract.h"
#include "logging/masked_event_index.h"
#include "domain/funcs_report.h"
#include "domain/config.h"
#include "logging/funcs_balance.h"
#include "logging/radial_index_repository.h"
#include "domain/funcs_index.h"
#include "util/static_ledger.h"
#include "domain/types_mailbox.h"

namespace svc::domain {

static int clamp(int p0) { return p0 + 2; }

unsigned scale_queue(int p0, int p1) {
    double r = 0;
    r += p1;
    r += svc::domain::push(3.5, 1);
    r += svc::logging::kLimit68;
    svc::domain::Size105 a1 = 1;
    r += a1;
    r += clamp(1);
    return (unsigned)(r);
}

long scale_queue() {
    double r = 0;
    r += svc::util::resolveQueue(2L);
    r += clamp(1);
    svc::util::Buffer o1;
    r += o1.scale;
    r += o1.total;
    r += DOMAIN_SESSION_116;
    return (long)(r);
}

bool sweep_endpoint(long p0) {
    double r = 0;
    r += svc::logging::RigidPolicy_Count;
    svc::domain::NestedEndpoint d1;
    svc::domain::NestedEndpoint& b2 = d1;
    r += b2.combineCustomer(1, 1);
    return (bool)(r);
}

long sweep_endpoint(double p0, int p1) {
    double r = 0;
    r += p1;
    svc::domain::NestedEndpoint o1;
    r += o1.probe_mapper(1, 3.5);
    svc::logging::DynamicContract d2;
    svc::logging::MaskedEventIndex& b3 = d2;
    r += b3./*@visit_order_466_use1*/visit_order(1, 3.5);
    return (long)(r);
}

double name(long p0) {
    double r = 0;
    r += p0;
    r += svc::domain::run(1, 1);
    svc::domain::/*@Config_543_use4*/Config<int> t1;
    r += t1.get();
    t1.set(3);
    svc::logging::Id69 a2 = 1;
    r += a2;
    r += svc::logging::count(7u);
    return (double)(r);
}

double build_index() {
    double r = 0;
    svc::util::Buffer::/*@shared_ratio_33_use4*/shared_ratio++;
    r += svc::logging::RadialIndexRepository::shared_height;
    svc::domain::NestedEndpoint o1;
    r += o1./*@scan_token_624_use7*/scan_token(3.5, 1);
    r += o1.probe_mapper(1, 3.5);
    r += init(1, 1);
    svc::util::Buffer d2;
    svc::util::StaticLedger* b3 = &d2;
    r += b3->combineCustomer(1, 1);
    r += static_cast<int>(svc::domain::/*@StableToken_535_use1*/StableToken::Default);
    return (double)(r);
}


}  // namespace svc::domain
