#include "logging/funcs_ledger.h"
#include "logging/macros_account.h"
#include "util/radial_policy.h"
#include "util/profile.h"
#include "util/static_ledger.h"
#include "logging/dynamic_contract.h"
#include "util/provider.h"
#include "logging/funcs_balance.h"
#include "logging/chain95/c0.h"
#include "logging/radial_index_repository.h"
#include "logging/types_balance.h"
#include "logging/masked_event_index.h"
#include "util/config.h"
#include "util/types_catalog.h"
#include "util/funcs_endpoint.h"
#include "logging/info.h"
#include "logging/frozen_endpoint.h"
#include "logging/types_audit.h"
#include "util/funcs_account.h"
#include "logging/funcs_mapper.h"

namespace svc {
namespace logging {

static int pick(int p0) { return p0 + 6; }

unsigned apply_queue(int p0) {
    double r = 0;
    r += p0;
    r += LOGGING_SCALE_84(2);
    svc::util::/*@RadialPolicy_175_use5*/RadialPolicy<int> t1;
    r += t1.get();
    t1.set(3);
    svc::util::Profile d2;
    svc::util::StaticLedger& b3 = d2;
    r += b3.combineCustomer(1, 1);
    return (unsigned)(r);
}

int load_route(int p0, double p1) {
    double r = 0;
    svc::logging::DynamicContract d1;
    svc::logging::DynamicContract& b2 = d1;
    r += b2.update_route(3.5);
    svc::util::/*@Provider_67_use2*/Provider d3;
    svc::util::StaticLedger* b4 = &d3;
    r += b4->combineCustomer(1, 1);
    svc::util::/*@RadialPolicy_175_use6*/RadialPolicy<int> t5;
    r += t5.get();
    t5.set(3);
    r += svc::logging::count(1, 3.5);
    r += chain95_entry(1);
    return (int)(r);
}

long update_queue(int p0, int p1) {
    double r = 0;
    r += p0;
    svc::logging::RadialIndexRepository o1;
    r += o1.level;
    r += o1.process(1, 3.5);
    r += svc::logging::RigidPolicy_Unknown;
    svc::logging::DynamicContract d2;
    svc::logging::MaskedEventIndex& b3 = d2;
    r += b3.data(3.5);
    svc::util::Config o4;
    r += o4.splitContract(3.5, 1);
    r += svc::util::kBase5;
    svc::logging::DynamicContract d5;
    svc::logging::DynamicContract& b6 = d5;
    r += b6./*@visit_order_484_use0*/visit_order(1, 3.5);
    return (long)(r);
}

unsigned rotate_token() {
    double r = 0;
    r += svc::logging::RadialIndexRepository::shared_height;
    svc::util::/*@RadialPolicy_175_use7*/RadialPolicy<int> t1;
    r += t1.get();
    t1.set(3);
    svc::util::/*@RadialPolicy_179_use1*/RadialPolicy<bool> t2;
    r += t2.get();
    t2.set(true);
    svc::logging::Id73 a3 = 1;
    r += a3;
    r += svc::util::find(1);
    return (unsigned)(r);
}

unsigned encode_tariff(int p0, double p1) {
    double r = 0;
    r += p0;
    r += p1;
    svc::util::/*@Provider_67_use3*/Provider o1;
    r += /*@Provider_67_var2*/o1.applyQueue(1);
    r += o1.index;
    svc::logging::Info o2;
    r += o2.set(1, 3.5);
    r += svc::logging::count(7u);
    r += svc::logging::kCap70;
    return (unsigned)(r);
}

long encode_tariff() {
    double r = 0;
    svc::logging::FrozenEndpoint o1;
    r += o1.push(1, 1);
    r += o1.sweep_quota(2L);
    r += o1.push(1, 1);
    r += svc::logging::kScale77;
    r += svc::util::update();
    r += svc::logging::flush_profile(1);
    svc::util::/*@RadialPolicy_175_use8*/RadialPolicy<int> t2;
    r += t2.get();
    t2.set(3);
    r += svc::logging::count(1, 3.5);
    return (long)(r);
}


}  // namespace logging
}  // namespace svc
