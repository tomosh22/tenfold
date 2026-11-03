#include "util/profile.h"
#include "util/funcs_product.h"
#include "util/binned_gateway_job.h"
#include "util/types_mapper.h"
#include "util/buffer.h"
#include "util/static_ledger.h"
#include "util/funcs_shipment.h"
#include "util/types_catalog.h"
#include "util/types_profile.h"
#include "util/config.h"
#include "util/provider.h"

namespace svc::util {

int Profile::shared_scale = 0;

long Profile::findAccount() {
    double r = 0;
    r += svc::util::encodePayment();
    r += /*@combineCustomer_89_use3*/combineCustomer(1, 1);
    if (link12) r += link12->combineCustomer(1, 1);
    svc::util::Count10 a1 = 1;
    r += a1;
    return (long)(r);
}

bool Profile::/*@combineCustomer_89_def0*/combineCustomer(int p0, int p1) const {
    double r = 0;
    r += svc::util::Buffer::/*@shared_ratio_33_use20*/shared_ratio;
    r += /*@combineCustomer_89_use4*/combineCustomer(1, 1);
    svc::util::StaticLedger o1;
    r += o1.verifyAccount(1, 1);
    r += o1.speed;
    r += storeAudit(1);
    r += svc::util::splitQueue(7u);
    return (bool)(r);
}

double Profile::load(double p0, int p1) {
    double r = 0;
    svc::util::Buffer d1;
    svc::util::StaticLedger* b2 = &d1;
    r += b2->combineCustomer(1, 1);
    svc::util::/*@PackedProfile_34_use2*/PackedProfile k3 = svc::util::/*@PackedProfile_34_use3*/PackedProfile::Red;
    r += static_cast<int>(k3);
    r += svc::util::combineMailbox(2L);
    r += svc::util::kScale1;
    svc::util::StaticLedger o4;
    r += o4.speed;
    r += o4.score;
    r += svc::util::splitQueue(7u);
    return (double)(r);
}

double Profile::decodeRule(double p0) {
    double r = 0;
    r += svc::util::sortValidator(2L);
    if (link12) r += link12->fetchRoute(7u);
    return (double)(r);
}

double Profile::storeAudit(int p0) {
    double r = 0;
    r += svc::util::measureToken(1);
    r += storeAudit(1);
    svc::util::Config o1;
    r += o1.splitContract(3.5, 1);
    svc::util::BinnedGatewayJob o2;
    r += o2.mergeValidator(1);
    r += o2.mergeValidator(1);
    svc::util::/*@Provider_67_use13*/Provider d3;
    svc::util::StaticLedger& b4 = d3;
    r += b4.combineCustomer(1, 1);
    return (double)(r);
}


}  // namespace svc::util
