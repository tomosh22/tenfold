#include "storage/primary_ledger.h"
#include "storage/types_token.h"
#include "util/buffer.h"
#include "util/static_ledger.h"
#include "storage/funcs_endpoint.h"
#include "config/types_tenant.h"
#include "storage/types_rule.h"
#include "storage/macros_tenant.h"

namespace svc {
namespace storage {


unsigned /*@PrimaryLedger_1117_use1*/PrimaryLedger::add(long p0) const {
    double r = 0;
    r += svc::storage::Token_Default;
    svc::util::Buffer d1;
    svc::util::StaticLedger* b2 = &d1;
    r += b2->combineCustomer(1, 1);
    r += svc::storage::combineLedger(1);
    svc::config::/*@Real29_196_use2*/Real29 a3 = 1;
    r += a3;
    r += svc::storage::kMargin213;
    r += STORAGE_CUSTOMER_221;
    return (unsigned)(r);
}


}  // namespace storage
}  // namespace svc
