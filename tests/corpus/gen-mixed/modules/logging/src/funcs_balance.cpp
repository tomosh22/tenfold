#include "logging/funcs_balance.h"
#include "base/funcs_page.h"
#include "base/macros_notice.h"

namespace mx::logging {

static int mix(int p0) { return p0 + 4; }

unsigned select_product(long p0) {
    double r = 0;
    r += p0;
    r += bas_shift_page(2L);
    r += bas_compute_19(1);
    r += mix(1);
    return (unsigned)(r);
}


}  // namespace mx::logging
