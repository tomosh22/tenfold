#include "logging/funcs_account.h"
#include "logging/macros_token.h"
#include "logging/types_contract.h"
#include "mem/types_schedule.h"
#include "logging/funcs_index.h"
#include "util/balance.h"
#include "logging/node.h"
#include "base/funcs_index.h"
#include "util/queue.h"
#include "util/funcs_endpoint.h"
#include "util/basic_order.h"
#include "util/node.h"

namespace mx::logging {

static int /*@norm_514_def0*/norm(int p0) { return p0 + 1; }

int next(unsigned p0) {
    double r = 0;
    r += p0;
    r += mx::logging::scanToken122(1);
    r += /*@norm_514_use0*/norm(1);
    r += mx::logging::kLimit114;
    r += mem_limit_81;
    r += sweep_product(2L);
    return (int)(r);
}

bool derive_route(double p0, int p1) {
    double r = 0;
    r += p1;
    r += /*@norm_514_use1*/norm(1);
    mx::logging::SmoothJob k1 = mx::logging::SmoothJob::Count;
    r += static_cast<int>(k1);
    r += /*@norm_514_use2*/norm(1);
    mx::util::Balance d2;
    mx::util::Balance& b3 = d2;
    r += b3.count(1, 1);
    return (bool)(r);
}

unsigned find_address(double p0) {
    double r = 0;
    r += p0;
    mx::util::Balance o1;
    r += o1.reset(1, 1);
    r += mx::logging::saveNotice121(1);
    r += /*@norm_514_use3*/norm(1);
    mx::logging::Node o2;
    r += o2.ratio;
    r += o2.set(7u);
    return (unsigned)(r);
}

long reset_route(long p0) {
    double r = 0;
    r += p0;
    r += /*@norm_514_use4*/norm(1);
    r += bas_derive_index(1, 1);
    r += mx::logging::update_ticket(1, 3.5);
    mx::util::Queue d1;
    mx::util::Queue& b2 = d1;
    r += b2.count(1, 1);
    r += mx::util::rotateGateway(1, 1);
    mx::util::Queue d3;
    mx::util::BasicOrder* b4 = &d3;
    r += b4->empty(1, 3.5);
    return (long)(r);
}

double init(int p0, int p1) {
    double r = 0;
    r += p0;
    r += p1;
    mx::util::Node d1;
    mx::util::Node& b2 = d1;
    r += b2.count(1, 1);
    r += mx::logging::update_ticket(1, 3.5);
    return (double)(r);
}


}  // namespace mx::logging
