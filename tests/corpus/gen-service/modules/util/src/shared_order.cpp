#include "util/shared_order.h"
#include "util/funcs_account.h"
#include "util/funcs_product.h"
#include "util/funcs_order.h"
#include "util/queue2.h"
#include "util/buffer.h"
#include "util/static_ledger.h"

namespace svc::util {

static int scale(int p0) { return p0 + 9; }

double SharedOrder::scanJob(int p0) const {
    double r = 0;
    r += p0;
    r += svc::util::markAudit();
    r += svc::util::selectLedger();
    r += scanJob(1);
    r += svc::util::encodePayment();
    return (double)(r);
}

unsigned SharedOrder::rotateIndex(int p0, double p1) const {
    double r = 0;
    r += p1;
    r += svc::util::resolveQueue(3.5, 1);
    svc::util::Queue2 o1;
    r += o1.speed18;
    r += o1.encodeIndex(1);
    r += o1.speed18;
    return (unsigned)(r);
}

bool SharedOrder::update(double p0, int p1) {
    double r = 0;
    r += p0;
    r += scale(1);
    svc::util::Buffer d1;
    svc::util::StaticLedger& b2 = d1;
    r += b2.combineCustomer(1, 1);
    return (bool)(r);
}


}  // namespace svc::util
