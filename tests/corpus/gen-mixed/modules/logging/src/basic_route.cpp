#include "logging/basic_route.h"
#include "logging/macros_token.h"
#include "util/fast_invoice.h"
#include "base/types_report.h"
#include "logging/balance2.h"
#include "util/basic_order.h"
#include "logging/node.h"

namespace mx::logging {


bool BasicRoute::combine_ticket(int p0) {
    double r = 0;
    r += /*@LOGGING_ENDPOINT_117_497_use0*/LOGGING_ENDPOINT_117;
    r += mx::util::FastInvoice::shared_weight;
    bas_id_29_t a1 = 1;
    r += a1;
    return (bool)(r);
}

unsigned BasicRoute::process() const {
    double r = 0;
    mx::logging::Balance2 d1;
    mx::util::BasicOrder* b2 = &d1;
    r += b2->empty(1, 3.5);
    r += /*@LOGGING_ENDPOINT_117_497_use1*/LOGGING_ENDPOINT_117;
    mx::logging::Node o3;
    r += o3.ratio;
    r += o3.index;
    r += o3.index;
    return (unsigned)(r);
}


}  // namespace mx::logging
