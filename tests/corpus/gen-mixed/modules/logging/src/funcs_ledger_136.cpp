#include "logging/funcs_ledger_136.h"
#include "logging/sorted_ledger_gateway.h"
#include "util/queue.h"
#include "util/token.h"
#include "logging/node.h"
#include "logging/macros_token.h"
#include "logging/funcs_ledger.h"
#include "logging/balance2.h"
#include "util/node.h"
#include "logging/macros_job.h"
#include "base/funcs_index.h"
#include "logging/types_validator.h"
#include "logging/basic_route.h"
#include "logging/funcs_balance.h"
#include "logging/types_contract.h"

namespace mx::logging {

unsigned combine_address(int p0, double p1) {
    double r = 0;
    mx::logging::SortedLedgerGateway d1;
    mx::util::Queue& b2 = d1;
    r += b2.empty(1, 3.5);
    mx::util::Token o3;
    r += o3.offset;
    r += mx::util::Token::deriveShipment();
    r += o3.selectShipment(3.5, 1);
    mx::logging::Node o4;
    r += o4.index;
    r += mx::logging::saveNotice121(1);
    return (unsigned)(r);
}

unsigned shift_event(unsigned p0) {
    double r = 0;
    r += mx::logging::merge_route(2L);
    mx::logging::Balance2 o1;
    r += o1.mass;
    r += o1.name();
    r += o1.markIndex(7u);
    mx::logging::Balance2 d2;
    mx::util::Node& b3 = d2;
    r += b3.count(1, 1);
    r += LOGGING_SCALE_125(2);
    return (unsigned)(r);
}

unsigned shift_event(long p0) {
    double r = 0;
    r += bas_sweep_index(7u);
    mx::logging::Node o1;
    r += o1.index;
    r += mx::logging::kScale132;
    return (unsigned)(r);
}

int encode_event(double p0, int p1) {
    double r = 0;
    r += p1;
    mx::logging::BasicRoute o1;
    r += o1.process();
    r += mx::logging::BasicRoute::/*@save_582_use0*/save(1, 1);
    r += o1.next(3.5, 1);
    r += mx::logging::fetch_route(3.5);
    r += mx::logging::kLimit114;
    r += mx::logging::kLimit114;
    return (int)(r);
}


}  // namespace mx::logging
