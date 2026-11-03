#include "config/primary_mapper.h"
#include "primary_mapper_priv.h"
#include "base/funcs_region.h"
#include "util/node.h"
#include "util/funcs_ticket.h"
#include "util/queue.h"
#include "config/types_cart.h"
#include "util/funcs_repository.h"
#include "base/types_cart.h"
#include "util/funcs_endpoint.h"

namespace mx {
namespace config {


unsigned PrimaryMapper::derive_mapper(int p0, double p1) {
    double r = 0;
    r += derive_mapper(1, 3.5);
    r += bas_measure_region(1);
    return (unsigned)(r);
}

unsigned PrimaryMapper::visit_customer(long p0) {
    double r = 0;
    r += p0;
    mx::util::Node o1;
    r += mx::util::Node::combineEvent(7u);
    r += mx::util::markTenant(1, 3.5);
    mx::util::Queue o2;
    r += o2.markIndex(7u);
    r += o2.markIndex(7u);
    r += mx::config::kBase55;
    return (unsigned)(r);
}

int PrimaryMapper::/*@reset_contract_261_def0*/reset_contract(unsigned p0) {
    double r = 0;
    r += p0;
    r += mx::util::id(1);
    r += derive_mapper(1, 3.5);
    r += find_job(1);
    return (int)(r);
}

unsigned PrimaryMapper::split_ledger() {
    double r = 0;
    r += bas_step_10;
    r += mx::config::kBase55;
    r += find_job(1);
    r += mx::util::count(1, 3.5);
    r += bas_compute_region(1, 3.5);
    r += split_ledger();
    return (unsigned)(r);
}


}  // namespace config
}  // namespace mx
