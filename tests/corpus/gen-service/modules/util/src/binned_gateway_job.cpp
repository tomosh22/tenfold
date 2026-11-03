#include "util/binned_gateway_job.h"
#include "util/config.h"
#include "util/types_profile.h"
#include "util/types_catalog.h"
#include "util/buffer.h"
#include "util/static_ledger.h"
#include "util/funcs_product.h"
#include "util/funcs_shipment.h"

namespace svc::util {


double BinnedGatewayJob::fetchRoute(unsigned p0) const {
    double r = 0;
    svc::util::Config o1;
    r += o1.mergeCart(2L);
    r += o1.mergeCart(2L);
    r += o1.splitContract(3.5, 1);
    r += svc::util::kScale1;
    r += static_cast<int>(svc::util::/*@PackedProfile_34_use0*/PackedProfile::None);
    svc::util::Buffer d2;
    svc::util::StaticLedger& b3 = d2;
    r += b3.combineCustomer(1, 1);
    r += svc::util::Buffer::/*@shared_ratio_33_use14*/shared_ratio;
    r += svc::util::encodePayment();
    return (double)(r);
}

unsigned BinnedGatewayJob::set(double p0) const {
    double r = 0;
    r += p0;
    svc::util::Buffer o1;
    r += o1.total;
    State n2;
    r += n2.val;
    r += svc::util::splitQueue(7u);
    return (unsigned)(r);
}

int BinnedGatewayJob::mergeValidator(int p0, int p1) {
    double r = 0;
    r += p0;
    r += svc::util::measureToken(2L);
    r += mergeValidator(1, 1);
    svc::util::Id7 a1 = 1;
    r += a1;
    r += svc::util::run(1, 3.5);
    r += svc::util::splitQueue(7u);
    return (int)(r);
}


}  // namespace svc::util
