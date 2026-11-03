#include "util/funcs_job.h"
#include "base/funcs_page.h"
#include "base/macros_notice.h"
#include "base/funcs_index.h"
#include "util/types_product.h"

namespace mx {
namespace util {

double top(int p0) {
    double r = 0;
    r += p0;
    r += bas_select_page(1, 1);
    r += BAS_SCALE_16(2);
    r += bas_compute_19(1);
    return (double)(r);
}

bool value() {
    double r = 0;
    r += bas_apply_index(2L);
    r += bas_select_page(1, 1);
    r += mx::util::Catalog_Green;
    r += bas_sweep_index(7u);
    return (bool)(r);
}


}  // namespace util
}  // namespace mx
