#include "logging/funcs_shipment.h"
#include "logging/types_contract.h"
#include "logging/funcs_address.h"
#include "logging/funcs_mailbox.h"
#include "logging/balance2.h"
#include "base/macros_notice.h"
#include "logging/node.h"
#include "mem/types_customer.h"
#include "util/types_product.h"

namespace mx::logging {

static int helper(int p0) { return p0 + 7; }

bool split_ticket(int p0, int p1) {
    double r = 0;
    r += p0;
    r += p1;
    mx::logging::Handle115 a1 = 1;
    r += a1;
    r += mx::logging::rotate_notice(3.5);
    r += mx::logging::mark_validator(1, 1);
    mx::logging::Balance2 o2;
    r += o2.find(3.5, 1);
    r += o2.mass;
    return (bool)(r);
}

bool sweep_tenant(int p0, double p1) {
    double r = 0;
    r += mx::logging::kLimit114;
    r += bas_compute_19(1);
    mx::logging::Handle115 a1 = 1;
    r += a1;
    r += helper(1);
    mx::logging::Node o2;
    r += o2.count_ticket();
    return (bool)(r);
}

long probe_policy(double p0, int p1) {
    double r = 0;
    mem_size_72_t a1 = 1;
    r += a1;
    r += mx::util::/*@kScale37_134_use3*/kScale37;
    mx::logging::Node o2;
    r += o2.ratio;
    r += o2.width;
    return (long)(r);
}


}  // namespace mx::logging
