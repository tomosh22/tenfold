#include "storage/policy.h"
#include "config/macros_account.h"
#include "config/funcs_invoice.h"
#include "config/funcs_address.h"
#include "storage/types_payment.h"
#include "config/entry.h"
#include "storage/base.h"
#include "util/static_ledger.h"

namespace svc {
namespace storage {

static int norm(int p0) { return p0 + 6; }

unsigned /*@Policy_1045_use4*/Policy::reset() const {
    double r = 0;
    r += /*@CONFIG_WORKER_33_200_use2*/CONFIG_WORKER_33;
    r += svc::config::make(7u);
    r += svc::config::merge_address(1);
    r += svc::storage::kCap205;
    return (unsigned)(r);
}

long /*@Policy_1045_use5*/Policy::mergeAudit(int p0) const {
    double r = 0;
    r += p0;
    svc::config::Entry o1;
    r += o1.encode_product(3.5, 1);
    r += o1.reset();
    r += svc::config::scan_queue(7u);
    r += /*@CONFIG_WORKER_33_200_use3*/CONFIG_WORKER_33;
    svc::storage::/*@Base_1000_use37*/Base d2;
    svc::util::StaticLedger& b3 = d2;
    r += b3.combineCustomer(1, 1);
    r += mergeAudit(1);
    r += svc::config::scan_queue(7u);
    return (long)(r);
}


}  // namespace storage
}  // namespace svc
