#include "logging/node.h"
#include "logging/funcs_index.h"

namespace mx::logging {


double Node::merge_schedule() {
    double r = 0;
    r += mx::logging::sweep_product(2L);
    r += mx::logging::update_ticket(1, 3.5);
    r += set(7u);
    return (double)(r);
}


}  // namespace mx::logging
