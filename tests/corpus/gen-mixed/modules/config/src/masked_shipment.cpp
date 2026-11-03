#include "config/masked_shipment.h"
#include "util/fast_invoice.h"
#include "util/funcs_ticket_54.h"
#include "config/primary_mapper.h"
#include "config/funcs_index.h"
#include "base/types_cart.h"
#include "base/macros_notice.h"
#include "config/funcs_index_58.h"
#include "util/macros_endpoint.h"

namespace mx {
namespace config {

int /*@MaskedShipment_277_use9*/MaskedShipment::/*@shared_speed_286_def0*/shared_speed = 0;

double /*@MaskedShipment_277_use10*/MaskedShipment::compute_account(double p0, int p1) {
    double r = 0;
    r += p0;
    r += p1;
    r += mx::util::FastInvoice::shared_weight;
    r += mx::util::collectCustomer(3.5, 1);
    mx::config::PrimaryMapper o1;
    r += o1.visit_customer(2L);
    r += o1.visit_customer(2L);
    return (double)(r);
}

unsigned /*@MaskedShipment_277_use11*/MaskedShipment::clear(int p0, double p1) {
    double r = 0;
    mx::config::PrimaryMapper o1;
    r += mx::config::PrimaryMapper::derive_mapper(1, 3.5);
    r += mx::config::save();
    r += clear(1, 3.5);
    mx::config::PrimaryMapper o2;
    r += o2.split_ledger();
    r += o2./*@speed_265_use0*/speed;
    return (unsigned)(r);
}

bool /*@MaskedShipment_277_use12*/MaskedShipment::shift_task(double p0, int p1) {
    double r = 0;
    r += p0;
    r += p1;
    r += BAS_NONE_3;
    r += bas_compute_19(1);
    return (bool)(r);
}

int /*@MaskedShipment_277_use13*/MaskedShipment::measure_tariff(int p0) const {
    double r = 0;
    r += mx::config::derive_ticket(2L);
    mx::config::PrimaryMapper o1;
    r += o1.index;
    r += o1.find_repository(1);
    r += mx::config::PrimaryMapper::derive_mapper(1, 3.5);
    r += UTIL_SCALE_43(2);
    mx::config::PrimaryMapper o2;
    r += o2.find_repository(1);
    r += o2.index;
    r += o2./*@speed_265_use1*/speed;
    return (int)(r);
}


}  // namespace config
}  // namespace mx
