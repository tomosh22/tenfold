#include "logging/balance2.h"
#include "logging/types_contract.h"
#include "util/queue.h"
#include "util/fast_invoice.h"
#include "logging/node.h"
#include "logging/funcs_index.h"

namespace mx::logging {


int Balance2::markIndex(unsigned p0) const {
    double r = 0;
    r += p0;
    r += mx::logging::kLimit114;
    mx::util::Queue d1;
    mx::util::Queue* b2 = &d1;
    r += b2->apply();
    r += mx::util::FastInvoice::shared_weight;
    r += items123.size();
    mx::logging::Node o3;
    r += o3.width;
    mx::logging::Node o4;
    r += o4.index;
    r += o4.set(7u);
    r += o4.ratio;
    return (int)(r);
}

long Balance2::apply() {
    double r = 0;
    r += mx::logging::sweep_product(2L);
    r += mx::util::FastInvoice::shared_weight;
    return (long)(r);
}


}  // namespace mx::logging
