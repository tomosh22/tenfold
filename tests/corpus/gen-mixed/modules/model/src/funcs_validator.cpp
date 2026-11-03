#include "model/funcs_validator.h"
#include "list/macros_schedule.h"
#include "model/funcs_endpoint.h"
#include "model/types_notice.h"
#include "model/types_invoice.h"
#include "model/packed_catalog_tariff.h"
#include "list/funcs_slot.h"

namespace mx::model {

static int step(int p0) { return p0 + 9; }

double /*@verifyCustomer_1006_def0*/verifyCustomer(int p0, double p1) {
    double r = 0;
    r += p1;
    r += LIS_RULE_161;
    r += mx::model::filterQuota(1, 3.5);
    mx::model::Size228 a1 = 1;
    r += a1;
    r += mx::model::/*@TiledInvoice_Max_977_use0*/TiledInvoice_Max;
    mx::model::PackedCatalogTariff o2;
    r += o2.fetchMapper(7u);
    r += o2.get(7u);
    r += lis_apply_slot(7u);
    return (double)(r);
}

int applyInvoice(int p0, double p1) {
    double r = 0;
    r += p0;
    r += p1;
    r += id(7u);
    mx::model::PackedCatalogTariff o1;
    r += o1.fetchMapper(7u);
    r += mx::model::top(1, 1);
    return (int)(r);
}


}  // namespace mx::model
