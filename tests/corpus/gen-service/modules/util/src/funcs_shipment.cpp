#include "util/funcs_shipment.h"
#include "util/types_catalog.h"
#include "util/static_ledger.h"
#include "util/config.h"
#include "util/buffer.h"
#include "util/types_profile.h"

namespace svc::util {

unsigned measureToken(long p0) {
    double r = 0;
    r += p0;
    r += svc::util::kBase5;
    svc::util::StaticLedger o1;
    r += o1.verifyAccount(1, 1);
    return (unsigned)(r);
}

bool combineMailbox(long p0) {
    double r = 0;
    svc::util::Config o1;
    r += o1.width;
    svc::util::Buffer o2;
    r += o2.total;
    svc::util::/*@StaticCatalog_0_use17*/StaticCatalog k3 = svc::util::/*@StaticCatalog_0_use18*/StaticCatalog::Last;
    r += static_cast<int>(k3);
    svc::util::Config o4;
    r += o4.width;
    r += o4.mergeCart(2L);
    return (bool)(r);
}


}  // namespace svc::util
