#include "logging/funcs_cart.h"
#include "logging/funcs_customer.h"
#include "base/types_report.h"
#include "base/chain36/c2.h"
#include "logging/macros_token.h"
#include "logging/types_contract.h"
#include "util/funcs_event.h"
#include "logging/types_validator.h"
#include "util/fast_invoice.h"
#include "util/funcs_job.h"
#include "util/types_product.h"
#include "mem/types_customer.h"
#include "logging/quota.h"
#include "logging/balance2.h"

namespace mx::logging {

static int clamp(int p0) { return p0 + 7; }

double resolve_gateway(unsigned p0) {
    double r = 0;
    r += mx::logging::push(3.5, 1);
    r += clamp(1);
    r += bas_limit_26;
    r += bas_chain36_l2(1);
    r += mx::logging::saveNotice121(1);
    return (double)(r);
}

unsigned resolve_gateway() {
    double r = 0;
    r += static_cast<int>(mx::logging::SmoothJob::Default);
    r += mx::util::next(3.5);
    r += mx::logging::kScale132;
    r += mx::util::FastInvoice::shared_weight;
    return (unsigned)(r);
}

unsigned resolve_contract(unsigned p0) {
    double r = 0;
    r += p0;
    r += mx::util::top(1);
    r += clamp(1);
    r += mx::util::/*@Catalog_First_126_use1*/Catalog_First;
    r += MEM_NONE_63;
    return (unsigned)(r);
}

long top(long p0) {
    double r = 0;
    r += mx::logging::scanToken122(1);
    mx::logging::Quota o1;
    r += o1.update(7u);
    r += o1.ratio;
    mx::logging::/*@Handle133_615_use0*/Handle133 a2 = 1;
    r += a2;
    mx::logging::Quota2 k3 = mx::logging::Quota2::/*@Last_612_use0*/Last;
    r += static_cast<int>(k3);
    mx::logging::Balance2 d4;
    mx::logging::Balance2* b5 = &d4;
    r += b5->apply();
    return (long)(r);
}


}  // namespace mx::logging
