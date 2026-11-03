#include "logging/notice.h"
#include "util/funcs_shipment.h"
#include "util/radial_policy.h"
#include "logging/dynamic_contract.h"
#include "logging/funcs_mapper.h"
#include "logging/types_audit.h"
#include "logging/types_mapper.h"
#include "logging/macros_job.h"
#include "logging/frozen_endpoint.h"
#include "util/queue2.h"
#include "util/buffer.h"
#include "logging/info.h"
#include "util/context.h"
#include "logging/macros_account.h"
#include "logging/radial_index_repository.h"
#include "logging/funcs_balance.h"
#include "logging/registry.h"

namespace svc {
namespace logging {


double Notice::visit_tenant(int p0, double p1) {
    double r = 0;
    r += p0;
    r += svc::util::combineMailbox(2L);
    svc::util::/*@RadialPolicy_175_use9*/RadialPolicy<int> t1;
    r += t1.get();
    t1.set(3);
    r += index;
    r += svc::util::/*@findHandler_183_use17*/findHandler<int>(2);
    svc::logging::DynamicContract o2;
    r += o2.update(7u);
    r += svc::logging::bind_session(1, 1);
    return (double)(r);
}

unsigned Notice::push(int p0, int p1) const {
    double r = 0;
    r += p0;
    r += p1;
    r += svc::logging::kStep75;
    svc::util::/*@RadialPolicy_179_use3*/RadialPolicy<bool> t1;
    r += t1.get();
    t1.set(true);
    svc::logging::/*@Handle_374_use10*/Handle k2 = svc::logging::/*@Handle_374_use11*/Handle::Auto;
    r += static_cast<int>(k2);
    r += svc::logging::resetRule94(1);
    return (unsigned)(r);
}

bool Notice::process(int p0) {
    double r = 0;
    svc::logging::FrozenEndpoint d1;
    svc::util::Queue2& b2 = d1;
    r += b2.combineCustomer(1, 1);
    svc::logging::Id69 a3 = 1;
    r += a3;
    r += svc::util::/*@findHandler_183_use18*/findHandler<int>(2);
    svc::util::Buffer::/*@shared_ratio_33_use9*/shared_ratio++;
    return (bool)(r);
}

bool Notice::sweep_rule() const {
    double r = 0;
    r += push(1, 1);
    svc::logging::Info o1;
    r += o1.top(7u);
    r += o1.update_audit(7u);
    r += o1.offset;
    svc::logging::DynamicContract o2;
    r += o2.reset_task(7u);
    r += o2.next(3.5, 1);
    r += o2.flags;
    svc::util::/*@Context_184_use3*/Context o3;
    r += /*@Context_184_var2*/o3.markRule(7u);
    return (bool)(r);
}

long Notice::next(long p0) {
    double r = 0;
    r += LOGGING_ENDPOINT_82;
    r += svc::util::/*@findHandler_183_use19*/findHandler<int>(2);
    svc::logging::FrozenEndpoint o1;
    r += o1.push(1, 1);
    r += o1.apply(1);
    svc::logging::RadialIndexRepository o2;
    r += o2.level;
    r += o2.level;
    svc::logging::FrozenEndpoint d3;
    svc::util::Queue2& b4 = d3;
    r += b4.combineCustomer(1, 1);
    return (long)(r);
}

bool Notice::collect_tariff(unsigned p0) {
    double r = 0;
    r += svc::logging::flush();
    r += this->index;
    r += LOGGING_ENDPOINT_82;
    svc::logging::/*@Registry_413_use19*/Registry o1;
    r += o1.save_notice(1);
    r += o1.data(7u);
    r += o1.name(1);
    return (bool)(r);
}

long Notice::set(int p0) {
    double r = 0;
    r += p0;
    svc::logging::/*@Registry_413_use20*/Registry o1;
    r += o1.mass;
    svc::logging::FrozenEndpoint o2;
    r += o2.apply(1);
    r += o2.apply(1);
    return (long)(r);
}


}  // namespace logging
}  // namespace svc
