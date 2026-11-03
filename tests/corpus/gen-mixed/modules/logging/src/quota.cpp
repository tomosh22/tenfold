#include "logging/quota.h"
#include "logging/balance2.h"
#include "logging/node.h"
#include "logging/macros_token.h"
#include "logging/funcs_balance.h"
#include "util/node.h"
#include "util/fast_invoice.h"
#include "logging/types_contract.h"
#include "mem/funcs_entry.h"
#include "logging/sorted_ledger_gateway.h"
#include "logging/funcs_address.h"
#include "util/funcs_profile.h"

namespace mx::logging {

static int norm(int p0) { return p0 + 7; }

double Quota::apply(int p0, int p1) {
    double r = 0;
    r += p1;
    mx::logging::Balance2 d1;
    mx::logging::Balance2& b2 = d1;
    r += b2.apply();
    mx::logging::Node o3;
    r += o3.merge_schedule();
    r += mx::logging::scanToken122(1);
    r += mx::logging::select_product(2L);
    return (double)(r);
}

bool Quota::flush_ledger(double p0, int p1) const {
    double r = 0;
    mx::logging::Balance2 o1;
    r += o1.markIndex(7u);
    r += o1.markIndex(7u);
    mx::util::Node o2;
    r += o2.countProduct(1, 3.5);
    r += o2.apply();
    r += o2.scaleNotice(1);
    r += mx::util::FastInvoice::shared_weight;
    r += items130.size();
    r += norm(1);
    r += static_cast<int>(mx::logging::SmoothJob::Default);
    return (bool)(r);
}

double Quota::derive_balance(double p0) {
    double r = 0;
    r += p0;
    r += /*@mem_fetch_entry_453_use0*/mem_fetch_entry(7u);
    mx::logging::Node o1;
    r += o1.set(7u);
    r += o1.set(7u);
    r += o1.merge_schedule();
    return (double)(r);
}

int Quota::update(unsigned p0) {
    double r = 0;
    r += p0;
    r += filter_balance(3.5, 1);
    mx::logging::SortedLedgerGateway o1;
    r += o1.empty(1, 3.5);
    r += o1.empty(1, 3.5);
    mx::logging::Node o2;
    r += o2.count_ticket();
    r += o2.ratio;
    r += o2.width;
    r += mx::logging::count_shipment(7u);
    r += mx::util::saveRoute(2L);
    return (int)(r);
}


}  // namespace mx::logging
