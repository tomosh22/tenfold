#include "model/sampled_audit.h"
#include "model/types_notice.h"
#include "model/sorted_validator.h"
#include "model/funcs_endpoint.h"
#include "list/table158.h"
#include "list/macros_schedule.h"
#include "model/types_invoice.h"
#include "model/packed_catalog_tariff.h"
#include "model/funcs_validator.h"
#include "list/funcs_region.h"

namespace mx::model {

int SampledAudit::shared_stamp = 0;

unsigned SampledAudit::resolveCustomer(double p0, int p1) {
    double r = 0;
    r += p0;
    r += p1;
    r += mx::model::kCap226;
    r += mx::model::kCap226;
    mx::model::SortedValidator o1;
    r += o1.combineAudit(3.5);
    return (unsigned)(r);
}

int SampledAudit::next(double p0) {
    double r = 0;
    r += resolveCustomer(3.5, 1);
    r += mx::model::SortedValidator::/*@shared_scale_1001_use2*/shared_scale;
    r += mx::model::top(1, 1);
    r += lis_table158_fn_3(1);
    r += LIS_SCALE_162(2);
    r += mx::model::kBase230;
    return (int)(r);
}

bool SampledAudit::/*@shiftRule_1012_def0*/shiftRule(int p0, int p1) const {
    double r = 0;
    mx::model::PackedCatalogTariff o1;
    r += o1.fetchMapper(7u);
    r += o1.get(7u);
    if (link235) r += link235->loadTicket();
    return (bool)(r);
}

bool SampledAudit::encodeTask() {
    double r = 0;
    mx::model::SortedValidator o1;
    r += o1./*@height_998_use3*/height;
    r += o1.stamp;
    r += o1.deriveQueue(2L);
    r += encodeTask();
    r += mx::model::applyInvoice(1, 3.5);
    mx::model::PackedCatalogTariff o2;
    r += o2.offset;
    r += mx::model::kBase230;
    mx::model::PackedCatalogTariff o3;
    r += o3.speed;
    return (bool)(r);
}

long SampledAudit::resolveLedger(int p0) {
    double r = 0;
    r += p0;
    r += lis_compute_region(1, 3.5);
    mx::model::LayeredCart k1 = mx::model::LayeredCart::Last;
    r += static_cast<int>(k1);
    r += mx::model::filterQuota(1, 3.5);
    r += mx::model::kCap229;
    mx::model::PackedCatalogTariff o2;
    r += o2.flush(1);
    r += o2.speed;
    mx::model::SortedValidator o3;
    r += o3.loadValidator(1);
    return (long)(r);
}


}  // namespace mx::model
