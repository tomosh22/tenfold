#include "config/funcs_address.h"
#include "util/types_profile_13.h"
#include "util/types_session.h"
#include "config/active_audit.h"
#include "config/impl.h"
#include "config/macros_account.h"
#include "util/static_ledger.h"
#include "util/funcs_order.h"
#include "config/types_contract.h"
#include "config/funcs_invoice.h"
#include "config/tiled_tenant.h"

namespace svc {
namespace config {

double merge_address(int p0) {
    double r = 0;
    r += p0;
    svc::util::Id16 a1 = 1;
    r += a1;
    r += svc::util::kScale21;
    svc::config::ActiveAudit o2;
    r += o2.count_task(1);
    svc::config::Impl o3;
    r += o3./*@rank_323_use3*/rank;
    return (double)(r);
}

int probe_tariff(unsigned p0) {
    double r = 0;
    r += /*@CONFIG_SHIPMENT_31_198_use1*/CONFIG_SHIPMENT_31;
    svc::util::StaticLedger o1;
    r += o1.verifyAccount(1, 1);
    return (int)(r);
}

int rotate_audit() {
    double r = 0;
    r += svc::util::filterBalance(1);
    r += svc::config::kLimit41;
    r += svc::config::get(1, 3.5);
    svc::config::TiledTenant o1;
    r += o1.process(2L);
    r += o1.limit;
    r += o1.flush(3.5, 1);
    r += svc::util::resolveQueue(2L);
    return (int)(r);
}


}  // namespace config
}  // namespace svc
