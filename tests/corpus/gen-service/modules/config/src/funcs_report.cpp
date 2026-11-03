#include "config/funcs_report.h"
#include "config/funcs_invoice.h"
#include "config/tiled_tenant.h"
#include "config/macros_account.h"
#include "config/cart.h"
#include "config/types_contract.h"
#include "util/funcs_account.h"
#include "config/types_tenant.h"
#include "util/types_profile.h"
#include "util/buffer.h"
#include "util/funcs_order.h"
#include "config/active_audit.h"

namespace svc {
namespace config {

static int norm(int p0) { return p0 + 1; }

bool split_rule(int p0) {
    double r = 0;
    r += make(7u);
    svc::config::TiledTenant o1;
    r += o1.pop(1, 1);
    r += o1.pop(1, 1);
    r += svc::config::measureAccount37(1);
    r += /*@CONFIG_SHIPMENT_31_198_use3*/CONFIG_SHIPMENT_31;
    r += svc::config::collect_token(2.5);
    return (bool)(r);
}

long flush(int p0, double p1) {
    double r = 0;
    r += p0;
    r += p1;
    r += svc::config::kLimit41;
    r += svc::util::update();
    return (long)(r);
}

double flush() {
    double r = 0;
    r += svc::config::Config_Red;
    svc::util::Handle2 a1 = 1;
    r += a1;
    svc::config::TiledTenant o2;
    r += svc::config::TiledTenant::next(1);
    svc::util::Buffer o3;
    r += o3.combineShipment(3.5);
    r += svc::util::Buffer::/*@value_28_use0*/value(3.5, 1);
    r += o3.total;
    r += CONFIG_SCALE_34(2);
    return (double)(r);
}

int split_session(double p0, int p1) {
    double r = 0;
    r += p0;
    r += p1;
    r += svc::config::/*@kMargin27_194_use1*/kMargin27;
    r += svc::util::scaleTicket(3.5, 1);
    r += norm(1);
    svc::config::ActiveAudit o1;
    r += o1.split_payment(3.5, 1);
    r += o1.count_task(1);
    r += o1.split_payment(3.5, 1);
    svc::config::/*@Cart_209_use2*/Cart<bool> t2;
    r += t2.get();
    t2.set(true);
    r += svc::config::kCap42;
    return (int)(r);
}


}  // namespace config
}  // namespace svc
