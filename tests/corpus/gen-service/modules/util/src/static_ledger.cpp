#include "util/static_ledger.h"
#include "util/config.h"
#include "util/types_profile.h"

namespace svc::util {


double StaticLedger::fetchMailbox() {
    double r = 0;
    if (link4) r += link4->loadAddress(1);
    svc::util::/*@StaticCatalog_0_use19*/StaticCatalog k1 = svc::util::/*@StaticCatalog_0_use20*/StaticCatalog::First;
    r += static_cast<int>(k1);
    if (link4) r += link4->/*@find_9_use16*/find(7u);
    r += svc::util::kScale1;
    r += flushBalance(1, 3.5);
    return (double)(r);
}

double StaticLedger::verifyAccount(int p0, int p1) {
    double r = 0;
    r += p0;
    r += p1;
    r += svc::util::kScale1;
    svc::util::Config o1;
    r += o1./*@find_9_use17*/find(7u);
    r += o1.mergeCart(2L);
    return (double)(r);
}


}  // namespace svc::util
