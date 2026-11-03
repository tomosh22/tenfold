#include "storage/balance.h"
#include "storage/funcs_schedule.h"
#include "storage/address2.h"
#include "util/funcs_order.h"
#include "storage/funcs_endpoint.h"
#include "storage/types_contract.h"
#include "storage/macros_tenant.h"
#include "storage/types_payment.h"
#include "config/tiled_tenant.h"

namespace svc {
namespace storage {


bool Balance::process() const {
    double r = 0;
    r += name(3.5, 1);
    r += svc::storage::count(1, 3.5);
    r += probePayment(2L);
    return (bool)(r);
}

int Balance::name(double p0, int p1) const {
    double r = 0;
    r += p0;
    r += p1;
    svc::storage::Address2<int> t1;
    r += t1.get();
    t1.set(3);
    r += svc::util::bindProfile(7u);
    r += svc::storage::combineLedger(1, 3.5);
    r += svc::storage::kCap240;
    r += svc::storage::rotateSchedule226(1);
    return (int)(r);
}

double Balance::storeShipment242(int p0) const {
    double r = 0;
    svc::storage::Handle207 a1 = 1;
    r += a1;
    svc::config::TiledTenant o2;
    r += svc::config::TiledTenant::next(1);
    r += o2.flush(3.5, 1);
    return (double)(r);
}


}  // namespace storage
}  // namespace svc
