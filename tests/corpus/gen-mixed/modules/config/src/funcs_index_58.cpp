#include "config/funcs_index_58.h"
#include "util/node.h"
#include "config/primary_mapper.h"
#include "util/macros_endpoint.h"
#include "config/types_cart.h"
#include "util/balance.h"

namespace mx {
namespace config {

long derive_ticket(long p0) {
    double r = 0;
    mx::util::Node d1;
    mx::util::Node* b2 = &d1;
    r += b2->markIndex(7u);
    mx::config::PrimaryMapper o3;
    r += o3.index;
    r += o3.select_repository(1);
    r += o3.split_ledger();
    return (long)(r);
}

bool add() {
    double r = 0;
    r += /*@UTIL_SCHEDULE_40_164_use1*/UTIL_SCHEDULE_40;
    mx::config::/*@Real57_256_use0*/Real57 a1 = 1;
    r += a1;
    mx::util::Balance o2;
    r += o2.mergeAccount(1);
    return (bool)(r);
}


}  // namespace config
}  // namespace mx
