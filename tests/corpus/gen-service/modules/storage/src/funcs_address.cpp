#include "storage/funcs_address.h"
#include "config/tiled_tenant.h"
#include "storage/policy.h"
#include "config/split_report.h"
#include "config/cart.h"
#include "storage/node.h"
#include "storage/base.h"
#include "util/lazy_event.h"
#include "config/chain59/c0.h"
#include "storage/stable_report.h"
#include "storage/funcs_endpoint.h"
#include "config/chain59/c2.h"
#include "util/buffer.h"
#include "util/static_ledger.h"
#include "util/funcs_product.h"
#include "config/macros_account.h"
#include "storage/macros_tenant.h"

namespace svc {
namespace storage {

int flush(int p0, double p1) {
    double r = 0;
    svc::config::TiledTenant o1;
    r += svc::config::TiledTenant::next(1);
    r += o1.flush(3.5, 1);
    svc::storage::/*@Policy_1045_use2*/Policy o2;
    r += /*@Policy_1045_var2*/o2.reset();
    r += o2.speed;
    r += o2.speed;
    svc::config::/*@SplitReport_356_use3*/SplitReport o3;
    r += /*@SplitReport_356_var0*/o3.visit_cart(1);
    r += svc::config::collect_token(2.5);
    svc::storage::/*@Node_1077_use3*/Node o4;
    r += /*@Node_1077_var0*/o4.ratio;
    svc::storage::/*@Base_1000_use12*/Base o5;
    r += /*@Base_1000_var5*/o5.findTenant(3.5, 1);
    r += o5.rank;
    r += svc::storage::/*@Base_1000_use13*/Base::/*@name_1002_use4*/name(3.5);
    return (int)(r);
}

long sweepJob(double p0, int p1) {
    double r = 0;
    svc::util::/*@LazyEvent_148_use1*/LazyEvent o1;
    r += /*@LazyEvent_148_var1*/o1.rank;
    r += svc::config::chain59_l0(1);
    svc::storage::/*@StableReport_1005_use7*/StableReport o2;
    r += /*@StableReport_1005_var3*/o2.combineShipment(3.5);
    r += o2.id(3.5);
    r += o2.id(3.5);
    r += svc::storage::resetMapper(1, 3.5);
    return (long)(r);
}

double scanAudit(double p0, int p1) {
    double r = 0;
    r += p1;
    r += svc::config::chain59_l2(1);
    svc::util::Buffer d1;
    svc::util::StaticLedger& b2 = d1;
    r += b2.combineCustomer(1, 1);
    r += svc::util::selectLedger();
    return (double)(r);
}

unsigned scanAudit() {
    double r = 0;
    r += /*@CONFIG_WORKER_33_200_use1*/CONFIG_WORKER_33;
    svc::util::Buffer d1;
    svc::util::Buffer& b2 = d1;
    r += b2.combineShipment(3.5);
    r += svc::storage::buildContract227(1);
    return (unsigned)(r);
}


}  // namespace storage
}  // namespace svc
