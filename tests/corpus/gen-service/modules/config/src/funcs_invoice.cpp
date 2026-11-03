#include "config/funcs_invoice.h"
#include "util/profile.h"
#include "config/macros_account.h"
#include "config/types_tenant.h"
#include "util/static_ledger.h"
#include "util/binned_gateway_job.h"
#include "util/radial_policy.h"
#include "util/funcs_order.h"

namespace svc {
namespace config {

double get(int p0, double p1) {
    double r = 0;
    r += svc::util::Profile::shared_scale;
    r += svc::config::measureAccount37(1);
    svc::config::/*@Real29_196_use1*/Real29 a1 = 1;
    r += a1;
    svc::util::StaticLedger o2;
    r += o2.flushBalance(1, 3.5);
    r += o2.score;
    r += o2.combineCustomer(1, 1);
    r += svc::config::measureAccount37(1);
    svc::util::BinnedGatewayJob o3;
    r += o3.mergeValidator(1, 1);
    r += o3.fetchRoute(7u);
    return (double)(r);
}

bool make(unsigned p0) {
    double r = 0;
    r += p0;
    r += /*@CONFIG_SHIPMENT_31_198_use2*/CONFIG_SHIPMENT_31;
    r += svc::config::measureAccount37(1);
    r += svc::util::/*@findHandler_183_use5*/findHandler(2.5);
    r += svc::util::/*@findHandler_183_use6*/findHandler<int>(2);
    r += svc::config::measureAccount37(1);
    return (bool)(r);
}

bool make(double p0, int p1) {
    double r = 0;
    r += p1;
    r += svc::config::measureAccount37(1);
    r += svc::config::measureAccount37(1);
    r += svc::util::/*@visitTenant_104_use0*/visitTenant(3.5);
    return (bool)(r);
}


}  // namespace config
}  // namespace svc
