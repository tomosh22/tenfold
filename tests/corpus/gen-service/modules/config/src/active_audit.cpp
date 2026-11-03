#include "config/active_audit.h"
#include "config/macros_account.h"
#include "util/buffer.h"
#include "util/static_ledger.h"
#include "config/cart.h"
#include "util/types_catalog.h"
#include "config/funcs_invoice.h"
#include "util/funcs_product.h"
#include "util/profile.h"

namespace svc {
namespace config {

static int step(int p0) { return p0 + 7; }

unsigned ActiveAudit::split_payment(double p0, int p1) {
    double r = 0;
    r += p1;
    r += svc::config::measureAccount37(1);
    svc::util::Buffer d1;
    svc::util::StaticLedger& b2 = d1;
    r += b2.combineCustomer(1, 1);
    r += /*@CONFIG_WORKER_33_200_use0*/CONFIG_WORKER_33;
    r += svc::config::collect_token<int>(2);
    r += step(1);
    svc::util::StaticLedger o3;
    r += o3.verifyAccount(1, 1);
    return (unsigned)(r);
}

int ActiveAudit::count_task(int p0) const {
    double r = 0;
    r += p0;
    r += svc::config::measureAccount37(1);
    r += svc::util::kBase5;
    r += svc::config::make(7u);
    r += svc::config::measureAccount37(1);
    return (int)(r);
}

int ActiveAudit::run(long p0) {
    double r = 0;
    r += p0;
    r += svc::util::selectLedger(3.5);
    svc::util::Profile o1;
    r += o1.combineShipment(3.5);
    r += o1.findAccount();
    r += svc::config::make(3.5, 1);
    return (int)(r);
}


}  // namespace config
}  // namespace svc
