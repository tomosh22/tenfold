#include "util/node.h"
#include "util/types_product.h"
#include "util/fast_invoice.h"
#include "util/queue.h"
#include "base/types_cart.h"

namespace mx {
namespace util {


long Node::apply() {
    double r = 0;
    r += mx::util::Catalog_Red;
    r += apply();
    return (long)(r);
}

int Node::combineEvent(unsigned p0) {
    double r = 0;
    r += p0;
    mx::util::FastInvoice::shared_weight++;
    mx::util::Queue o1;
    r += mx::util::Queue::deriveInvoice(1, 3.5);
    r += mx::util::Queue::update(1, 3.5);
    r += mx::util::Queue::deriveInvoice(1, 3.5);
    r += mx::util::FastInvoice::shared_weight;
    r += /*@bas_step_11_12_use4*/bas_step_11;
    r += mx::util::FastInvoice::shared_weight;
    r += mx::util::Report_Blue;
    return (int)(r);
}


}  // namespace util
}  // namespace mx
