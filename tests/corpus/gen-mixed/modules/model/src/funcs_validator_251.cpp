#include "model/funcs_validator_251.h"
#include "model/sampled_audit.h"
#include "model/product_route.h"
#include "model/packed_mailbox_policy.h"
#include "model/types_notice.h"

namespace mx::model {

static int total(int p0) { return p0 + 7; }

long save(int p0) {
    double r = 0;
    mx::model::SampledAudit d1;
    mx::model::ProductRoute* b2 = &d1;
    r += b2->resolveCustomer(3.5, 1);
    r += total(1);
    r += mx::model::/*@markPayment_1054_use7*/markPayment(2.5);
    r += mx::model::kStep225;
    return (long)(r);
}


}  // namespace mx::model
