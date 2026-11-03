#include "model/funcs_endpoint.h"
#include "list/types_token.h"
#include "model/types_notice.h"
#include "list/table158.h"
#include "model/packed_catalog_tariff.h"
#include "list/macros_schedule.h"
#include "model/types_invoice.h"
#include "list/macros_payment.h"

namespace mx::model {

static int /*@scale_983_def0*/scale(int p0) { return p0 + 8; }

double top(int p0, int p1) {
    double r = 0;
    r += p0;
    r += lis_limit_189;
    r += /*@scale_983_use0*/scale(1);
    return (double)(r);
}

bool id(double p0, int p1) {
    double r = 0;
    r += p0;
    r += /*@scale_983_use1*/scale(1);
    r += mx::model::kStep225;
    r += lis_table158_fn_3(1);
    mx::model::PackedCatalogTariff o1;
    r += o1.get(7u);
    r += o1.fetchMapper(7u);
    mx::model::PackedCatalogTariff o2;
    r += o2.flush(1);
    return (bool)(r);
}

unsigned filterQuota(int p0, double p1) {
    double r = 0;
    r += p1;
    r += LIS_SCALE_162(2);
    r += mx::model::kCap229;
    return (unsigned)(r);
}

double collectValidator(long p0) {
    double r = 0;
    r += p0;
    mx::model::PackedCatalogTariff o1;
    r += o1.get(7u);
    r += o1.get(7u);
    r += LIS_ENDPOINT_201;
    mx::model::PackedCatalogTariff o2;
    r += o2.speed;
    r += mx::model::kBase230;
    r += lis_table158_fn_3(1);
    return (double)(r);
}


}  // namespace mx::model
