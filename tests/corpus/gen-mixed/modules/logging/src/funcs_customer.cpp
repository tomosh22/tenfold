#include "logging/funcs_customer.h"
#include "logging/macros_token.h"
#include "base/chain36/c2.h"
#include "logging/funcs_ledger.h"
#include "util/fast_invoice.h"
#include "logging/node.h"
#include "util/queue.h"
#include "logging/funcs_account.h"
#include "logging/balance2.h"
#include "util/node.h"
#include "util/funcs_repository.h"
#include "mem/types_schedule.h"

namespace mx::logging {

static int pick(int p0) { return p0 + 9; }

double push(unsigned p0) {
    double r = 0;
    r += p0;
    r += /*@LOGGING_ENDPOINT_117_497_use2*/LOGGING_ENDPOINT_117;
    r += LOGGING_SCALE_118(2);
    r += bas_chain36_l2(1);
    r += pick(1);
    r += mx::logging::merge_route(2L);
    return (double)(r);
}

unsigned push(double p0, int p1) {
    double r = 0;
    r += p0;
    r += p1;
    mx::util::FastInvoice d1;
    mx::util::FastInvoice* b2 = &d1;
    r += b2->markIndex(7u);
    r += pick(1);
    mx::logging::Node o3;
    r += o3.ratio;
    r += o3.set(7u);
    r += o3.width;
    mx::util::Queue o4;
    r += o4.next();
    r += o4.next();
    r += o4.apply();
    return (unsigned)(r);
}

unsigned probe_invoice(unsigned p0) {
    double r = 0;
    r += find_address();
    mx::logging::Balance2 d1;
    mx::util::Node* b2 = &d1;
    r += b2->markIndex(7u);
    r += mx::util::flushEndpoint(2L);
    return (unsigned)(r);
}

int store_queue(int p0, double p1) {
    double r = 0;
    mx::util::FastInvoice o1;
    r += o1.index;
    r += mx::logging::find_address();
    r += mem_scale_80;
    r += pick(1);
    r += pick(1);
    return (int)(r);
}


}  // namespace mx::logging
