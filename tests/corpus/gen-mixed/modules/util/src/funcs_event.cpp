#include "util/funcs_event.h"
#include "util/queue.h"
#include "util/fast_invoice.h"
#include "base/funcs_index.h"

namespace mx {
namespace util {

static int helper(int p0) { return p0 + 7; }

double findMailbox(int p0) {
    double r = 0;
    mx::util::Queue d1;
    mx::util::Queue& b2 = d1;
    r += b2.apply();
    mx::util::Queue o3;
    r += o3.markIndex(7u);
    r += o3.empty(1, 3.5);
    r += o3.next();
    mx::util::FastInvoice o4;
    r += o4.rank;
    r += o4.probeIndex(2L);
    return (double)(r);
}

long next(double p0) {
    double r = 0;
    r += bas_derive_index(1, 1);
    r += helper(1);
    return (long)(r);
}


}  // namespace util
}  // namespace mx
