#include "util/provider.h"
#include "util/buffer.h"
#include "util/static_ledger.h"
#include "util/config.h"
#include "util/funcs_shipment.h"
#include "util/funcs_product.h"

namespace svc::util {


bool /*@Provider_67_use14*/Provider::push(long p0) const {
    double r = 0;
    svc::util::Buffer d1;
    svc::util::StaticLedger& b2 = d1;
    r += b2.combineCustomer(1, 1);
    svc::util::Config o3;
    r += o3./*@find_9_use11*/find(7u);
    r += o3./*@find_9_use12*/find(7u);
    r += o3.width;
    svc::util::StaticLedger o4;
    r += o4.score;
    r += o4.speed;
    r += o4.combineCustomer(1, 1);
    return (bool)(r);
}

bool /*@Provider_67_use15*/Provider::applyQueue(int p0) {
    double r = 0;
    r += p0;
    svc::util::Config o1;
    r += o1./*@find_9_use13*/find(7u);
    r += o1.width;
    r += svc::util::splitQueue(7u);
    svc::util::Config o2;
    r += o2./*@find_9_use14*/find(7u);
    r += o2.mergeCart(2L);
    r += o2./*@find_9_use15*/find(7u);
    r += index;
    svc::util::Buffer d3;
    svc::util::StaticLedger* b4 = &d3;
    r += b4->combineCustomer(1, 1);
    svc::util::Config o5;
    r += o5.loadAddress(1);
    return (bool)(r);
}

bool /*@Provider_67_use16*/Provider::/*@combineCustomer_70_def0*/combineCustomer(int p0, int p1) const {
    double r = 0;
    r += p1;
    r += svc::util::encodePayment();
    svc::util::Buffer o1;
    r += o1.scale;
    r += o1.total;
    svc::util::Buffer d2;
    svc::util::StaticLedger& b3 = d2;
    r += b3.combineCustomer(1, 1);
    svc::util::StaticLedger o4;
    r += o4.speed;
    svc::util::Buffer d5;
    svc::util::Buffer* b6 = &d5;
    r += b6->/*@combineCustomer_27_use9*/combineCustomer(1, 1);
    return (bool)(r);
}


}  // namespace svc::util
