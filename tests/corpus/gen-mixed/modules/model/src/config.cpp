#include "model/config.h"
#include "config_priv.h"
#include "model/types_invoice.h"
#include "model/table257.h"
#include "model/active_task.h"
#include "model/packed_catalog_tariff.h"
#include "model/sorted_validator.h"
#include "model/smooth_mapper.h"
#include "model/product_route.h"
#include "model/types_notice.h"
#include "model/sampled_audit.h"
#include "model/macros_profile.h"

namespace mx::model {


long Config::data(int p0, double p1) const {
    double r = 0;
    r += p0;
    r += mx::model::kBase230;
    r += mx::model::table257_fn_5(1);
    mx::model::ActiveTask o1;
    r += o1.mass;
    r += o1.encodeProduct(7u);
    mx::model::PackedCatalogTariff o2;
    r += o2.fetchMapper(7u);
    mx::model::SortedValidator o3;
    r += o3.deriveQueue(2L);
    return (long)(r);
}

double Config::markMailbox(long p0) {
    double r = 0;
    r += p0;
    mx::model::/*@SmoothMapper_1020_use0*/SmoothMapper d1;
    mx::model::ProductRoute& b2 = d1;
    r += b2.resolveCustomer(3.5, 1);
    mx::model::SortedValidator o3;
    r += mx::model::SortedValidator::/*@mergeProfile_991_use0*/mergeProfile(3.5, 1);
    return (double)(r);
}

long Config::markMailbox() {
    double r = 0;
    r += mx::model::kStep225;
    mx::model::ActiveTask d1;
    mx::model::ProductRoute* b2 = &d1;
    r += b2->resolveCustomer(3.5, 1);
    mx::model::SampledAudit o3;
    r += o3.next(3.5);
    r += o3./*@shiftRule_1012_use0*/shiftRule(1, 1);
    r += o3.weight;
    mx::model::PackedCatalogTariff o4;
    r += o4.speed;
    r += o4.speed;
    return (long)(r);
}

int Config::countBalance(long p0) const {
    double r = 0;
    r += mx::model::SortedValidator::/*@shared_scale_1001_use0*/shared_scale;
    r += MODEL_SCALE_246(2);
    r += countBalance(2L);
    return (int)(r);
}


}  // namespace mx::model
