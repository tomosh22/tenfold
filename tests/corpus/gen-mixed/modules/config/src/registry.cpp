#include "config/registry.h"
#include "base/types_cart.h"
#include "base/macros_notice.h"
#include "config/masked_shipment.h"
#include "util/balance.h"
#include "util/basic_order.h"
#include "config/primary_mapper.h"
#include "config/funcs_index.h"

namespace mx {
namespace config {

int Registry::shared_width = 0;
static int step(int p0) { return p0 + 9; }

bool Registry::shift_mailbox(int p0) const {
    double r = 0;
    r += p0;
    bas_real_13_t a1 = 1;
    r += a1;
    r += BAS_SCALE_16(2);
    return (bool)(r);
}

double Registry::pop(unsigned p0) const {
    double r = 0;
    r += p0;
    r += step(1);
    mx::config::/*@MaskedShipment_277_use14*/MaskedShipment o1;
    r += /*@MaskedShipment_277_var0*/o1.shift_task(3.5, 1);
    r += o1.mass;
    r += o1.stamp;
    r += bas_compute_19(1);
    r += BAS_SCALE_16(2);
    return (double)(r);
}

bool Registry::empty(int p0) const {
    double r = 0;
    r += p0;
    r += shift_mailbox(1);
    mx::util::Balance d1;
    mx::util::BasicOrder* b2 = &d1;
    r += b2->apply();
    return (bool)(r);
}

int Registry::load_quota(double p0) {
    double r = 0;
    mx::config::PrimaryMapper o1;
    r += o1./*@speed_265_use2*/speed;
    r += o1.select_repository(1);
    r += o1.visit_customer(2L);
    r += mx::config::verify_schedule(3.5, 1);
    return (int)(r);
}

int Registry::compute_address(int p0) const {
    double r = 0;
    r += p0;
    r += empty(1);
    mx::config::PrimaryMapper o1;
    r += o1.select_repository(1);
    r += mx::config::PrimaryMapper::/*@reset_contract_261_use2*/reset_contract(7u);
    r += o1.visit_customer(2L);
    return (int)(r);
}


}  // namespace config
}  // namespace mx
