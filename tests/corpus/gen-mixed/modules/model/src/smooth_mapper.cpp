#include "model/smooth_mapper.h"
#include "model/sorted_validator.h"
#include "model/funcs_endpoint.h"
#include "model/types_notice.h"
#include "list/macros_schedule.h"
#include "model/sampled_audit.h"
#include "model/packed_catalog_tariff.h"
#include "model/product_route.h"

namespace mx::model {

static int total(int p0) { return p0 + 3; }

unsigned /*@SmoothMapper_1020_use2*/SmoothMapper::resolveCustomer(double p0, int p1) {
    double r = 0;
    mx::model::SortedValidator o1;
    r += mx::model::SortedValidator::/*@mergeProfile_991_use1*/mergeProfile(3.5, 1);
    r += o1.findAddress(3.5);
    r += o1.reset(1, 1);
    r += mx::model::id(3.5, 1);
    r += static_cast<int>(mx::model::LayeredCart::None);
    return (unsigned)(r);
}

bool /*@SmoothMapper_1020_use3*/SmoothMapper::bindRule(unsigned p0) const {
    double r = 0;
    r += p0;
    r += total(1);
    r += LIS_TENANT_159;
    mx::model::SortedValidator::/*@shared_scale_1001_use3*/shared_scale++;
    r += mx::model::top(1, 1);
    mx::model::SampledAudit o1;
    r += o1.resolveLedger(1);
    mx::model::SampledAudit o2;
    r += o2.stamp;
    r += o2.resolveLedger(1);
    r += o2.next(3.5);
    return (bool)(r);
}

bool /*@SmoothMapper_1020_use4*/SmoothMapper::selectCustomer(int p0, int p1) {
    double r = 0;
    r += p1;
    r += static_cast<int>(mx::model::LayeredCart::Unknown);
    mx::model::PackedCatalogTariff o1;
    r += o1.fetchMapper(7u);
    r += o1.fetchMapper(7u);
    mx::model::SampledAudit d2;
    mx::model::ProductRoute& b3 = d2;
    r += b3.resolveCustomer(3.5, 1);
    return (bool)(r);
}


}  // namespace mx::model
