#include "util/audit_cart.h"
#include "util/funcs_product.h"
#include "util/types_profile.h"
#include "util/config.h"
#include "util/funcs_shipment.h"

namespace svc::util {


int AuditCart::next(int p0, int p1) {
    double r = 0;
    r += p1;
    r += svc::util::selectMailbox(1, 1);
    svc::util::Handle2 a1 = 1;
    r += a1;
    svc::util::/*@StaticCatalog_0_use5*/StaticCatalog k2 = svc::util::/*@StaticCatalog_0_use6*/StaticCatalog::Last;
    r += static_cast<int>(k2);
    svc::util::Config o3;
    r += o3./*@find_9_use4*/find(7u);
    r += svc::util::splitQueue(7u);
    svc::util::Config o4;
    r += o4./*@find_9_use5*/find(7u);
    r += o4.loadAddress(1);
    return (int)(r);
}


}  // namespace svc::util
