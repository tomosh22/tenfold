#include "logging/funcs_route.h"
#include "logging/macros_token.h"
#include "logging/balance2.h"
#include "util/queue.h"
#include "mem/macros_address.h"
#include "logging/funcs_account.h"
#include "logging/funcs_ledger.h"
#include "util/basic_order.h"
#include "mem/funcs_parser.h"
#include "logging/sorted_ledger_gateway.h"
#include "util/funcs_ticket_54.h"
#include "logging/types_validator.h"
#include "logging/basic_route.h"
#include "logging/types_contract.h"
#include "logging/funcs_customer.h"
#include "logging/node.h"

namespace mx::logging {

unsigned value(int p0) {
    double r = 0;
    r += mx::logging::saveNotice121(1);
    mx::logging::Balance2 d1;
    mx::util::Queue& b2 = d1;
    r += b2.apply();
    r += mem_mark_112(1);
    r += reset_route(3.5, 1);
    r += mx::logging::decode_quota(1, 3.5);
    mx::logging::Balance2 d3;
    mx::util::BasicOrder& b4 = d3;
    r += b4.empty(1, 3.5);
    return (unsigned)(r);
}

int apply() {
    double r = 0;
    r += mem_find_parser(1, 1);
    mx::logging::SortedLedgerGateway o1;
    r += o1.empty(1, 3.5);
    r += mx::util::load(1, 3.5);
    mx::util::Queue o2;
    r += o2.empty(1, 3.5);
    mx::logging::Balance2 d3;
    mx::util::BasicOrder* b4 = &d3;
    r += b4->empty(1, 3.5);
    return (int)(r);
}

double decode_event(long p0) {
    double r = 0;
    r += p0;
    r += mx::logging::saveNotice121(1);
    mx::logging::SortedLedgerGateway o1;
    r += o1.empty(1, 3.5);
    r += o1./*@value_574_use0*/value(7u);
    r += mx::logging::SortedLedgerGateway::resolve_event(3.5);
    r += mx::logging::kCap131;
    mx::logging::BasicRoute o2;
    r += o2.index;
    return (double)(r);
}

bool split_cart(double p0, int p1) {
    double r = 0;
    mx::logging::Balance2 o1;
    r += o1.empty(1, 3.5);
    r += o1.apply();
    r += o1.mass;
    r += mx::logging::kLimit114;
    mx::logging::Balance2 o2;
    r += o2.mass;
    r += o2.empty(1, 3.5);
    r += o2.measure_balance(1);
    return (bool)(r);
}

long split_cart(int p0) {
    double r = 0;
    r += push(7u);
    mx::logging::Balance2 o1;
    r += o1.mass;
    r += o1.name();
    mx::logging::SortedLedgerGateway o2;
    r += o2./*@value_574_use1*/value(7u);
    r += o2./*@value_574_use2*/value(7u);
    r += o2.count(1, 1);
    mx::logging::BasicRoute o3;
    r += o3.phase;
    r += mx::logging::kLimit114;
    return (long)(r);
}

bool split_cart(int p0, int p1) {
    double r = 0;
    r += p1;
    mx::logging::SortedLedgerGateway o1;
    r += o1.empty(1, 3.5);
    mx::logging::Node o2;
    r += o2.merge_schedule();
    r += mx::logging::saveNotice121(1);
    r += mx::logging::kScale132;
    return (bool)(r);
}


}  // namespace mx::logging
