#include "storage/job2.h"
#include "config/cart.h"
#include "storage/funcs_endpoint.h"
#include "storage/address2.h"
#include "util/funcs_product.h"
#include "config/types_catalog.h"
#include "config/macros_account.h"
#include "util/buffer.h"
#include "storage/policy.h"
#include "storage/node.h"
#include "util/static_ledger.h"
#include "util/funcs_account.h"
#include "config/macros_route.h"
#include "storage/stable_report.h"
#include "util/types_session.h"
#include "storage/base.h"
#include "storage/types_rule.h"

namespace svc {
namespace storage {


double Job2::filterValidator(int p0, double p1) {
    double r = 0;
    r += p0;
    r += p1;
    r += svc::config::collect_token<int>(2);
    r += svc::storage::resetMapper(1, 3.5);
    r += svc::storage::mergeNotice<int>(2);
    r += svc::util::selectLedger(3.5);
    r += apply(3.5, 1);
    return (double)(r);
}

double Job2::apply(double p0, int p1) const {
    double r = 0;
    r += p0;
    r += svc::config::/*@kCap48_254_use0*/kCap48;
    r += svc::storage::mergeNotice(2.5);
    r += svc::config::measureAccount37(1);
    r += svc::util::Buffer::/*@shared_ratio_33_use12*/shared_ratio;
    svc::storage::/*@Policy_1045_use3*/Policy o1;
    r += /*@Policy_1045_var3*/o1.reset(7u);
    r += o1.mergeAudit(1);
    return (double)(r);
}

long Job2::scanPolicy(long p0) const {
    double r = 0;
    r += CONFIG_SCALE_34(2);
    svc::storage::/*@Node_1077_use5*/Node d1;
    svc::util::StaticLedger* b2 = &d1;
    r += b2->combineCustomer(1, 1);
    r += svc::util::probeCatalog();
    r += svc::util::/*@selectMailbox_62_use2*/selectMailbox(1);
    r += svc::config::shiftEndpoint58(1);
    svc::storage::/*@StableReport_1005_use11*/StableReport o3;
    r += o3.id(3.5);
    return (long)(r);
}

bool Job2::sweepRoute() const {
    double r = 0;
    svc::util::/*@Count23_174_use0*/Count23 a1 = 1;
    r += a1;
    svc::storage::/*@Base_1000_use29*/Base o2;
    r += o2.findTenant(3.5, 1);
    r += o2.rank;
    r += /*@CONFIG_ENDPOINT_32_199_use0*/CONFIG_ENDPOINT_32;
    r += svc::storage::kBase215;
    r += CONFIG_SCALE_34(2);
    r += this->limit;
    return (bool)(r);
}

unsigned Job2::flushSession(int p0, double p1) {
    double r = 0;
    r += p0;
    svc::storage::/*@StableReport_1005_use12*/StableReport d1;
    svc::util::Buffer* b2 = &d1;
    r += b2->combineShipment(3.5);
    r += CONFIG_SCALE_34(2);
    return (unsigned)(r);
}


}  // namespace storage
}  // namespace svc
