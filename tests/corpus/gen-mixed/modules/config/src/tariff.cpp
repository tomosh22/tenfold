#include "config/tariff.h"
#include "config/masked_shipment.h"
#include "util/macros_endpoint.h"
#include "config/funcs_index.h"
#include "config/primary_mapper.h"
#include "util/balance.h"

namespace mx {
namespace config {


double /*@Tariff_356_use0*/Tariff::size(long p0) {
    double r = 0;
    r += p0;
    r += merge_handler(3.5, 1);
    mx::config::/*@MaskedShipment_277_use16*/MaskedShipment o1;
    r += /*@MaskedShipment_277_var2*/o1.stamp;
    return (double)(r);
}

bool /*@Tariff_356_use1*/Tariff::merge_handler(double p0, int p1) const {
    double r = 0;
    r += p0;
    r += p1;
    r += /*@UTIL_TARIFF_41_165_use3*/UTIL_TARIFF_41;
    r += merge_handler(3.5, 1);
    r += mx::config::verify_schedule(3.5, 1);
    return (bool)(r);
}

double /*@Tariff_356_use2*/Tariff::process(long p0) {
    double r = 0;
    r += /*@mark_mailbox_358_use0*/mark_mailbox(1, 3.5);
    mx::config::PrimaryMapper o1;
    r += mx::config::PrimaryMapper::/*@reset_contract_261_use4*/reset_contract(7u);
    r += o1.index;
    mx::util::Balance d2;
    mx::util::Balance* b3 = &d2;
    r += b3->count(1, 1);
    return (double)(r);
}


}  // namespace config
}  // namespace mx
