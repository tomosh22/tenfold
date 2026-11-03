#include "config/linear_product.h"
#include "util/queue.h"
#include "util/types_product.h"
#include "config/types_cart.h"

namespace mx {
namespace config {


bool LinearProduct::save_task(int p0, int p1) const {
    double r = 0;
    mx::util::Queue d1;
    mx::util::Queue& b2 = d1;
    r += b2.empty(1, 3.5);
    r += mx::util::Catalog_Green;
    r += mx::config::kBase55;
    return (bool)(r);
}


}  // namespace config
}  // namespace mx
