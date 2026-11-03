#include "domain/funcs_quota.h"
#include "logging/funcs_ledger.h"
#include "logging/macros_account.h"
#include "util/buffer.h"
#include "logging/funcs_balance.h"
#include "domain/funcs_index.h"
#include "util/static_ledger.h"
#include "domain/types_report.h"
#include "logging/funcs_policy.h"
#include "util/config.h"
#include "domain/config.h"
#include "util/types_catalog.h"
#include "domain/types_mailbox.h"
#include "logging/dynamic_contract.h"
#include "logging/masked_event_index.h"

namespace svc::domain {

static int clamp(int p0) { return p0 + 2; }

double split_token(long p0) {
    double r = 0;
    r += p0;
    r += svc::logging::update_queue(1, 1);
    r += LOGGING_ENDPOINT_82;
    svc::util::Buffer o1;
    r += o1.total;
    r += o1.apply();
    r += o1.apply();
    return (double)(r);
}

bool apply(int p0, int p1) {
    double r = 0;
    r += p1;
    r += svc::logging::flush();
    r += svc::domain::/*@get_551_use1*/get(2L);
    svc::util::Buffer d1;
    svc::util::StaticLedger* b2 = &d1;
    r += b2->combineCustomer(1, 1);
    return (bool)(r);
}

int count_quota(long p0) {
    double r = 0;
    r += svc::domain::FrozenBalance_None;
    r += svc::domain::init(1, 1);
    r += svc::domain::init(1, 1);
    r += svc::logging::update_policy(3.5, 1);
    r += clamp(1);
    return (int)(r);
}

unsigned count_quota(int p0, double p1) {
    double r = 0;
    r += p0;
    r += clamp(1);
    svc::util::Config o1;
    r += o1.loadAddress(1);
    r += o1.mergeCart(2L);
    r += o1.loadAddress(1);
    r += svc::domain::/*@sweep_queue_547_use2*/sweep_queue(2.5);
    r += build_mapper(1, 1);
    r += svc::util::/*@kStep6_40_use0*/kStep6;
    svc::util::Config o2;
    r += o2.mergeCart(2L);
    return (unsigned)(r);
}

int count_quota(int p0) {
    double r = 0;
    r += p0;
    svc::domain::/*@StableToken_535_use5*/StableToken k1 = svc::domain::/*@StableToken_535_use6*/StableToken::First;
    r += static_cast<int>(k1);
    r += svc::domain::kMargin96;
    r += svc::domain::FrozenBalance_Count;
    r += svc::domain::/*@get_551_use2*/get(2L);
    svc::logging::DynamicContract d2;
    svc::logging::MaskedEventIndex* b3 = &d2;
    r += b3->/*@visit_order_466_use2*/visit_order(1, 3.5);
    return (int)(r);
}


}  // namespace svc::domain
