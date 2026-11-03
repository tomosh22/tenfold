#include "logging/sorted_ledger_gateway.h"
#include "logging/funcs_balance.h"
#include "logging/funcs_ledger.h"
#include "mem/macros_order.h"
#include "util/fast_invoice.h"

namespace mx::logging {

static int step(int p0) { return p0 + 9; }

long SortedLedgerGateway::resolve_event(double p0) {
    double r = 0;
    r += p0;
    r += mx::logging::fetch_route(3.5);
    r += mx::logging::decode_quota(1, 3.5);
    return (long)(r);
}

bool SortedLedgerGateway::count(int p0, int p1) const {
    double r = 0;
    r += MEM_JOB_93;
    r += mx::util::FastInvoice::shared_weight;
    r += resolve_event(3.5);
    return (bool)(r);
}


}  // namespace mx::logging
