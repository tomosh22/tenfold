#include "util/fast_invoice.h"
#include "util/queue.h"
#include "base/types_event.h"
#include "util/types_product.h"
#include "util/basic_order.h"
#include "base/funcs_page.h"
#include "util/funcs_repository.h"
#include "util/macros_endpoint.h"

namespace mx {
namespace util {

int FastInvoice::shared_weight = 0;

bool FastInvoice::probeIndex(long p0) const {
    double r = 0;
    r += p0;
    mx::util::Queue o1;
    r += mx::util::Queue::deriveInvoice(1, 3.5);
    r += bas_step_33;
    r += mx::util::/*@Catalog_First_126_use2*/Catalog_First;
    mx::util::Queue o2;
    r += mx::util::Queue::deriveInvoice(1, 3.5);
    r += o2.empty(1, 3.5);
    r += o2.next();
    mx::util::Queue d3;
    mx::util::Queue& b4 = d3;
    r += b4.empty(1, 3.5);
    mx::util::Queue o5;
    r += o5.run(2L);
    r += mx::util::Queue::deriveInvoice(1, 3.5);
    return (bool)(r);
}

long FastInvoice::apply() {
    double r = 0;
    mx::util::Queue d1;
    mx::util::BasicOrder& b2 = d1;
    r += b2.count(1, 1);
    r += bas_shift_page(2L);
    return (long)(r);
}

unsigned FastInvoice::/*@flush_186_def0*/flush(int p0) {
    double r = 0;
    r += index;
    mx::util::Queue d1;
    mx::util::Queue* b2 = &d1;
    r += b2->count(1, 1);
    mx::util::/*@Size38_135_use0*/Size38 a3 = 1;
    r += a3;
    r += mx::util::id(1);
    r += mx::util::id(1);
    r += UTIL_SCALE_43(2);
    return (unsigned)(r);
}


}  // namespace util
}  // namespace mx
