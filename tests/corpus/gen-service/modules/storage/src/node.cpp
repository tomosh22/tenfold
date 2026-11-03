#include "storage/node.h"
#include "storage/base.h"
#include "storage/funcs_endpoint.h"
#include "util/funcs_shipment.h"
#include "util/profile.h"
#include "util/context.h"
#include "config/funcs_address.h"

namespace svc {
namespace storage {

static int step(int p0) { return p0 + 5; }

bool /*@Node_1077_use6*/Node::bindMapper(double p0, int p1) {
    double r = 0;
    r += p0;
    svc::storage::/*@Base_1000_use30*/Base o1;
    r += svc::storage::/*@Base_1000_use31*/Base::/*@name_1002_use13*/name(3.5);
    r += o1.findTenant(3.5, 1);
    svc::storage::/*@Base_1000_use32*/Base o2;
    r += o2.findTenant(3.5, 1);
    r += o2.rank;
    r += svc::storage::/*@Base_1000_use33*/Base::/*@name_1002_use14*/name(3.5);
    r += svc::storage::combineLedger(1);
    svc::storage::/*@Base_1000_use34*/Base o3;
    r += o3.findTenant(3.5, 1);
    r += o3.findTenant(3.5, 1);
    r += o3.findTenant(3.5, 1);
    return (bool)(r);
}

double /*@Node_1077_use7*/Node::verifyPayment(unsigned p0) {
    double r = 0;
    r += step(1);
    r += total218;
    return (double)(r);
}

bool /*@Node_1077_use8*/Node::/*@combineCustomer_1084_def0*/combineCustomer(int p0, int p1) const {
    double r = 0;
    r += p0;
    r += p1;
    r += svc::util::combineMailbox(3.5);
    svc::util::Profile o1;
    r += o1.resetContract(7u);
    r += o1.decodeRule(3.5);
    svc::util::/*@Context_184_use4*/Context o2;
    r += /*@Context_184_var3*/o2.markRule(7u);
    r += o2.markRule(7u);
    r += o2.load(3.5, 1);
    r += this->ratio;
    return (bool)(r);
}

double /*@Node_1077_use9*/Node::resetCatalog(double p0) {
    double r = 0;
    r += svc::config::merge_address();
    svc::storage::/*@Base_1000_use35*/Base d1;
    svc::storage::/*@Base_1000_use36*/Base& b2 = d1;
    r += b2./*@combineCustomer_70_use1*/combineCustomer(1, 1);
    return (double)(r);
}


}  // namespace storage
}  // namespace svc
