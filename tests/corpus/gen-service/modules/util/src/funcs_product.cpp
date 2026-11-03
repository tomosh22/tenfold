#include "util/funcs_product.h"
#include "util/types_profile.h"
#include "util/funcs_shipment.h"
#include "util/config.h"
#include "util/buffer.h"
#include "util/static_ledger.h"

namespace svc::util {

int encodePayment() {
    double r = 0;
    svc::util::/*@StaticCatalog_0_use14*/StaticCatalog k1 = svc::util::/*@StaticCatalog_0_use15*/StaticCatalog::First;
    r += static_cast<int>(k1);
    r += svc::util::splitQueue(7u);
    svc::util::Config o2;
    r += o2.loadAddress(1);
    return (int)(r);
}

long run(int p0, double p1) {
    double r = 0;
    r += svc::util::splitQueue(7u);
    svc::util::Buffer d1;
    svc::util::Buffer& b2 = d1;
    r += b2.combineShipment(3.5);
    svc::util::Buffer o3;
    r += o3.delta;
    return (long)(r);
}

long selectMailbox(int p0, int p1) {
    double r = 0;
    svc::util::Config o1;
    r += o1./*@find_9_use9*/find(7u);
    r += o1./*@find_9_use10*/find(7u);
    svc::util::Config o2;
    r += o2.splitContract(3.5, 1);
    r += svc::util::combineMailbox(3.5);
    r += svc::util::splitQueue(7u);
    return (long)(r);
}

long /*@selectMailbox_62_def0*/selectMailbox(int p0) {
    double r = 0;
    svc::util::Config o1;
    r += o1.width;
    r += svc::util::splitQueue(7u);
    r += svc::util::Buffer::/*@shared_ratio_33_use19*/shared_ratio;
    return (long)(r);
}

bool sortValidator(long p0) {
    double r = 0;
    r += static_cast<int>(svc::util::/*@StaticCatalog_0_use16*/StaticCatalog::Last);
    svc::util::Config o1;
    r += o1.width;
    r += splitQueue(7u);
    r += svc::util::combineMailbox(3.5);
    svc::util::Buffer d2;
    svc::util::Buffer* b3 = &d2;
    r += b3->/*@combineCustomer_27_use8*/combineCustomer(1, 1);
    r += svc::util::measureToken(2L);
    return (bool)(r);
}

bool sortValidator(unsigned p0) {
    double r = 0;
    svc::util::Config o1;
    r += o1.width;
    r += o1.mergeCart(2L);
    svc::util::StaticLedger o2;
    r += o2.speed;
    r += o2.speed;
    r += o2.verifyAccount(1, 1);
    svc::util::Config o3;
    r += o3.mergeCart(2L);
    r += o3.splitContract(3.5, 1);
    return (bool)(r);
}


}  // namespace svc::util
