#include "storage/contract.h"
#include "config/types_contract.h"
#include "storage/types_token.h"
#include "util/funcs_order.h"
#include "config/active_audit.h"
#include "storage/stable_report.h"
#include "storage/funcs_endpoint.h"
#include "storage/address2.h"
#include "storage/base.h"
#include "config/split_report.h"
#include "util/static_ledger.h"

namespace svc {
namespace storage {


unsigned /*@Contract_1055_use0*/Contract::storeHandler(int p0, int p1) const {
    double r = 0;
    r += svc::config::kLimit41;
    r += svc::storage::kMargin210;
    r += svc::util::scaleTicket(3.5, 1);
    r += storeHandler(2L);
    svc::config::ActiveAudit o1;
    r += o1.add();
    r += o1.run(2L);
    r += o1.split_payment(1, 1);
    return (unsigned)(r);
}

double /*@Contract_1055_use1*/Contract::storeHandler(long p0) const {
    double r = 0;
    r += p0;
    svc::storage::/*@StableReport_1005_use3*/StableReport o1;
    r += /*@StableReport_1005_var1*/o1.ratio;
    r += svc::storage::/*@StableReport_1005_use4*/StableReport::markToken(3.5);
    r += o1.level;
    r += svc::storage::combineLedger(1);
    svc::storage::Address2<int> t2;
    r += t2.get();
    t2.set(3);
    svc::storage::/*@Base_1000_use10*/Base o3;
    r += /*@Base_1000_var4*/o3.findTenant(3.5, 1);
    r += svc::storage::/*@Base_1000_use11*/Base::/*@name_1002_use3*/name(3.5);
    r += o3.findTenant(3.5, 1);
    r += this->scale;
    svc::storage::/*@StableReport_1005_use5*/StableReport o4;
    r += /*@StableReport_1005_var2*/o4.ratio;
    r += svc::storage::/*@StableReport_1005_use6*/StableReport::markToken(3.5);
    return (double)(r);
}

double /*@Contract_1055_use2*/Contract::measureEndpoint(double p0) {
    double r = 0;
    r += svc::storage::resetMapper(1, 3.5);
    r += svc::storage::combineLedger(1);
    svc::config::/*@SplitReport_356_use2*/SplitReport d1;
    svc::util::StaticLedger& b2 = d1;
    r += b2.combineCustomer(1, 1);
    return (double)(r);
}


}  // namespace storage
}  // namespace svc
