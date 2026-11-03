#include "storage/stable_report.h"
#include "config/funcs_invoice.h"
#include "config/impl.h"
#include "util/binned_gateway_job.h"
#include "util/radial_policy.h"
#include "storage/types_payment.h"
#include "config/macros_account.h"
#include "storage/base.h"
#include "config/chain59/c0.h"
#include "util/buffer.h"
#include "util/funcs_product.h"
#include "config/cart.h"
#include "util/funcs_shipment.h"

namespace svc {
namespace storage {


int /*@StableReport_1005_use13*/StableReport::combineShipment(double p0) {
    double r = 0;
    r += svc::config::get(1, 3.5);
    svc::config::Impl::/*@shared_delta_325_use11*/shared_delta++;
    svc::util::BinnedGatewayJob d1;
    svc::util::BinnedGatewayJob& b2 = d1;
    r += b2.combineCustomer(1, 1);
    r += svc::util::/*@findHandler_183_use21*/findHandler<int>(2);
    return (int)(r);
}

bool /*@StableReport_1005_use14*/StableReport::flush(double p0, int p1) {
    double r = 0;
    r += p0;
    r += svc::storage::kCap205;
    r += svc::config::measureAccount37(1);
    svc::storage::/*@Base_1000_use38*/Base o1;
    r += o1.rank;
    r += svc::storage::/*@Base_1000_use39*/Base::/*@name_1002_use15*/name(3.5);
    r += svc::storage::Base::/*@name_1002_use16*/name(3.5);
    r += svc::storage::kCap204;
    r += svc::config::chain59_l0(1);
    return (bool)(r);
}

long /*@StableReport_1005_use15*/StableReport::splitTicket(double p0) {
    double r = 0;
    r += p0;
    r += ratio;
    svc::util::/*@RadialPolicy_179_use5*/RadialPolicy<bool> t1;
    r += t1.get();
    t1.set(true);
    svc::storage::Base d2;
    svc::util::Buffer* b3 = &d2;
    r += b3->/*@combineCustomer_27_use4*/combineCustomer(1, 1);
    r += svc::util::Buffer::/*@shared_ratio_33_use13*/shared_ratio;
    r += save(1);
    r += svc::util::selectLedger(3.5);
    return (long)(r);
}

bool /*@StableReport_1005_use16*/StableReport::/*@combineCustomer_1011_def0*/combineCustomer(int p0, int p1) const {
    double r = 0;
    r += svc::config::collect_token(2.5);
    svc::util::/*@RadialPolicy_175_use10*/RadialPolicy<int> t1;
    r += t1.get();
    t1.set(3);
    r += svc::util::splitQueue(7u);
    r += svc::util::combineMailbox(3.5);
    r += svc::config::bind_session(1, 1);
    return (bool)(r);
}

int /*@StableReport_1005_use17*/StableReport::markToken(double p0) {
    double r = 0;
    r += p0;
    svc::storage::Base o1;
    r += svc::storage::Base::/*@name_1002_use17*/name(3.5);
    r += CONFIG_SCALE_35(2);
    return (int)(r);
}


}  // namespace storage
}  // namespace svc
