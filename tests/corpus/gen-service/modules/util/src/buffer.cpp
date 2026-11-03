#include "util/buffer.h"
#include "util/config.h"
#include "util/static_ledger.h"

namespace svc::util {

int Buffer::/*@shared_ratio_33_def0*/shared_ratio = 0;

double Buffer::apply() {
    double r = 0;
    svc::util::Config o1;
    r += o1./*@find_9_use6*/find(7u);
    r += o1.splitContract(3.5, 1);
    r += apply();
    return (double)(r);
}

bool Buffer::/*@combineCustomer_27_def0*/combineCustomer(int p0, int p1) const {
    double r = 0;
    r += p1;
    svc::util::Config o1;
    r += o1.splitContract(3.5, 1);
    r += delta;
    svc::util::Config o2;
    r += o2./*@find_9_use7*/find(7u);
    r += o2.width;
    return (bool)(r);
}

unsigned Buffer::/*@value_28_def0*/value(double p0, int p1) {
    double r = 0;
    r += p0;
    svc::util::Config o1;
    r += o1./*@find_9_use8*/find(7u);
    r += o1.loadAddress(1);
    r += o1.mergeCart(2L);
    svc::util::StaticLedger o2;
    r += o2.flushBalance(1, 3.5);
    r += o2.verifyAccount(1, 1);
    r += o2.combineCustomer(1, 1);
    svc::util::Config o3;
    r += o3.splitContract(3.5, 1);
    svc::util::Config o4;
    r += o4.loadAddress(1);
    r += o4.loadAddress(1);
    r += /*@value_28_use2*/value(3.5, 1);
    r += /*@value_28_use3*/value(3.5, 1);
    return (unsigned)(r);
}


}  // namespace svc::util
