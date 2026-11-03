#include "logging/funcs_address.h"
#include "logging/macros_token.h"
#include "logging/node.h"
#include "logging/balance2.h"
#include "util/fast_invoice.h"
#include "base/funcs_index.h"
#include "logging/funcs_account.h"
#include "logging/types_contract.h"
#include "base/types_cart.h"
#include "util/types_mailbox.h"
#include "base/macros_notice.h"
#include "util/funcs_payment.h"
#include "util/queue.h"

namespace mx::logging {

static int mix(int p0) { return p0 + 5; }

double rotate_notice(double p0) {
    double r = 0;
    r += mx::logging::saveNotice121(1);
    mx::logging::Node o1;
    r += o1.ratio;
    r += o1.set(7u);
    r += o1.index;
    mx::logging::Balance2 o2;
    r += o2.measure_balance(1);
    r += mx::util::FastInvoice::shared_weight;
    r += bas_apply_index(2L);
    return (double)(r);
}

int rotate_notice(long p0) {
    double r = 0;
    r += p0;
    r += mx::util::FastInvoice::shared_weight;
    r += derive_route(3.5, 1);
    return (int)(r);
}

unsigned count_shipment(unsigned p0) {
    double r = 0;
    r += p0;
    r += mx::logging::scanToken122(1);
    r += mix(1);
    r += derive_route(3.5, 1);
    r += mx::logging::kLimit114;
    return (unsigned)(r);
}

long count_shipment(double p0, int p1) {
    double r = 0;
    mx::logging::Node o1;
    r += o1.merge_schedule();
    r += o1.merge_schedule();
    r += o1.ratio;
    mx::logging::Balance2 d2;
    mx::logging::Balance2* b3 = &d2;
    r += b3->apply();
    r += mx::logging::kLimit114;
    r += mx::util::FastInvoice::shared_weight;
    return (long)(r);
}

unsigned add(double p0, int p1) {
    double r = 0;
    r += /*@bas_step_11_12_use2*/bas_step_11;
    r += bas_encode_index(1, 3.5);
    r += mix(1);
    mx::logging::Balance2 o1;
    r += o1.apply();
    r += o1.find(3.5, 1);
    r += o1.empty(1, 3.5);
    r += mx::logging::saveNotice121(1);
    r += mx::util::kStep50;
    return (unsigned)(r);
}

bool make(int p0) {
    double r = 0;
    r += bas_compute_19(1);
    r += mx::util::flush(1, 1);
    mx::util::Queue d1;
    mx::util::Queue* b2 = &d1;
    r += b2->apply();
    return (bool)(r);
}


}  // namespace mx::logging
