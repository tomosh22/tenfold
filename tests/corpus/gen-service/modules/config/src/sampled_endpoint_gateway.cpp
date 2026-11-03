#include "config/sampled_endpoint_gateway.h"
#include "config/types_tenant.h"
#include "config/active_audit.h"
#include "util/radial_policy.h"
#include "config/table62.h"
#include "config/cart.h"
#include "config/chain59/c2.h"
#include "config/impl.h"
#include "config/funcs_job.h"
#include "config/macros_account.h"
#include "config/table51.h"

namespace svc {
namespace config {


unsigned /*@SampledEndpointGateway_333_use4*/SampledEndpointGateway::value(double p0) {
    double r = 0;
    r += svc::config::Config_Red;
    r += update(2L);
    r += shift_ticket(2L);
    return (unsigned)(r);
}

int /*@SampledEndpointGateway_333_use5*/SampledEndpointGateway::value(double p0, int p1) {
    double r = 0;
    r += p0;
    r += p1;
    r += merge_payment(1);
    svc::config::ActiveAudit o1;
    r += o1.count_task(1);
    r += o1.count_task(1);
    r += o1.count_task(1);
    svc::util::/*@RadialPolicy_175_use3*/RadialPolicy<int> t2;
    r += t2.get();
    t2.set(3);
    r += value(3.5);
    r += svc::config::table62_fn_4(1);
    return (int)(r);
}

double /*@SampledEndpointGateway_333_use6*/SampledEndpointGateway::merge_payment(int p0) const {
    double r = 0;
    svc::config::/*@Cart_205_use7*/Cart<int> t1;
    r += t1.get();
    t1.set(3);
    r += svc::config::chain59_l2(1);
    svc::config::ActiveAudit o2;
    r += o2.split_payment(1, 1);
    r += update(2L);
    r += svc::config::Impl::/*@shared_delta_325_use9*/shared_delta;
    return (double)(r);
}

double /*@SampledEndpointGateway_333_use7*/SampledEndpointGateway::shift_ticket(long p0) const {
    double r = 0;
    r += shift_ticket(2L);
    r += svc::config::load(3.5, 1);
    r += /*@CONFIG_SHIPMENT_31_198_use4*/CONFIG_SHIPMENT_31;
    return (double)(r);
}

unsigned /*@SampledEndpointGateway_333_use8*/SampledEndpointGateway::update(long p0) const {
    double r = 0;
    svc::config::Impl::/*@shared_delta_325_use10*/shared_delta++;
    r += svc::config::load(3.5, 1);
    r += svc::util::/*@findHandler_183_use7*/findHandler(2.5);
    r += svc::config::table51_fn_5(1);
    r += svc::config::measureAccount37(1);
    return (unsigned)(r);
}


}  // namespace config
}  // namespace svc
