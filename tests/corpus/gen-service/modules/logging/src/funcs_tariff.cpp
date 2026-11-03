#include "logging/funcs_tariff.h"
#include "logging/funcs_balance.h"
#include "logging/dynamic_contract.h"
#include "logging/masked_event_index.h"
#include "logging/funcs_ledger.h"
#include "logging/radial_index_repository.h"
#include "util/funcs_shipment.h"

namespace svc {
namespace logging {

int build_product(int p0, double p1) {
    double r = 0;
    r += p0;
    r += p1;
    r += count(1, 3.5);
    svc::logging::DynamicContract d1;
    svc::logging::MaskedEventIndex& b2 = d1;
    r += b2.reset_task(7u);
    r += rotate_token();
    svc::logging::RadialIndexRepository o3;
    r += o3.mass;
    r += o3.process(1, 3.5);
    r += o3.mass;
    r += svc::util::combineMailbox(3.5);
    return (int)(r);
}


}  // namespace logging
}  // namespace svc
