#include "model/active_task.h"
#include "model/sampled_audit.h"
#include "model/packed_catalog_tariff.h"
#include "model/funcs_validator.h"
#include "model/funcs_endpoint.h"
#include "list/macros_schedule.h"
#include "list/funcs_event.h"

namespace mx::model {

int ActiveTask::shared_mass = 0;
static int mix(int p0) { return p0 + 1; }

long ActiveTask::sortSession() const {
    double r = 0;
    r += mix(1);
    mx::model::SampledAudit o1;
    r += o1.width;
    r += o1.resolveLedger(1);
    r += speed;
    mx::model::PackedCatalogTariff o2;
    r += o2.speed;
    return (long)(r);
}

unsigned ActiveTask::collectAudit(double p0) const {
    double r = 0;
    r += p0;
    r += mx::model::applyInvoice(1, 3.5);
    r += mx::model::id(3.5, 1);
    r += LIS_TENANT_159;
    return (unsigned)(r);
}

unsigned ActiveTask::encodeProduct(unsigned p0) const {
    double r = 0;
    r += LIS_ENDPOINT_160;
    r += lis_derive_event(3.5, 1);
    mx::model::PackedCatalogTariff o1;
    r += o1.offset;
    r += o1.offset;
    r += o1.get(7u);
    return (unsigned)(r);
}


}  // namespace mx::model
