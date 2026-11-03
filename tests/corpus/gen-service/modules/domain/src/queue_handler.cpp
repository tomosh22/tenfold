#include "domain/queue_handler.h"
#include "logging/funcs_balance.h"
#include "domain/types_mailbox.h"
#include "domain/funcs_customer.h"
#include "domain/nested_endpoint.h"
#include "domain/macros_quota.h"
#include "util/config.h"
#include "util/profile.h"
#include "domain/types_gateway.h"
#include "domain/types_validator.h"
#include "util/funcs_order.h"
#include "domain/types_quota.h"
#include "util/radial_policy.h"
#include "domain/funcs_account.h"
#include "logging/radial_index_repository.h"

namespace svc::domain {

static int total(int p0) { return p0 + 5; }

double QueueHandler::/*@visit_order_713_def0*/visit_order(int p0, double p1) {
    double r = 0;
    r += svc::logging::flush();
    r += svc::domain::kMargin96;
    r += svc::domain::collect_tenant(2L);
    return (double)(r);
}

unsigned QueueHandler::bind_notice() {
    double r = 0;
    svc::domain::NestedEndpoint o1;
    r += o1./*@mass_628_use2*/mass;
    r += o1./*@mass_628_use3*/mass;
    r += svc::domain::probeProfile121(1);
    svc::util::Config o2;
    r += o2.width;
    r += o2.loadAddress(1);
    svc::util::Profile o3;
    r += o3.init(1, 3.5);
    r += o3.findAccount();
    r += svc::domain::MaskedLedger_Red;
    svc::domain::NestedEndpoint o4;
    r += svc::domain::NestedEndpoint::verify_tenant(1);
    return (unsigned)(r);
}

double QueueHandler::resolve_payment(int p0, double p1) {
    double r = 0;
    r += p0;
    r += p1;
    svc::domain::NestedEndpoint o1;
    r += o1.probe_mapper(1, 3.5);
    r += o1./*@scan_token_624_use8*/scan_token(3.5, 1);
    r += value(1, 1);
    svc::domain::/*@Count114_610_use0*/Count114 a2 = 1;
    r += a2;
    return (double)(r);
}

long QueueHandler::value(int p0, int p1) {
    double r = 0;
    r += p0;
    r += svc::domain::/*@kLimit107_586_use0*/kLimit107;
    r += total(1);
    r += svc::util::resolveQueue(2L);
    svc::domain::Handle106 a1 = 1;
    r += a1;
    r += svc::util::/*@findHandler_183_use11*/findHandler<int>(2);
    return (long)(r);
}

double QueueHandler::empty(unsigned p0) {
    double r = 0;
    r += svc::domain::build_ledger(2L);
    svc::domain::Size97 a1 = 1;
    r += a1;
    return (double)(r);
}

bool QueueHandler::id(double p0) {
    double r = 0;
    r += p0;
    r += svc::logging::RadialIndexRepository::shared_height;
    r += total(1);
    svc::domain::/*@StableToken_535_use10*/StableToken k1 = svc::domain::/*@StableToken_535_use11*/StableToken::Default;
    r += static_cast<int>(k1);
    svc::domain::NestedEndpoint o2;
    r += o2.build_endpoint();
    r += o2.probe_mapper(1, 3.5);
    return (bool)(r);
}

long QueueHandler::reset_task(unsigned p0) const {
    double r = 0;
    r += p0;
    r += reset_task(7u);
    r += data(3.5);
    r += value(1, 1);
    r += svc::domain::kMargin96;
    return (long)(r);
}


}  // namespace svc::domain
