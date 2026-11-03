#include "model/sorted_validator.h"
#include "list/funcs_socket.h"
#include "list/macros_schedule.h"
#include "model/funcs_endpoint.h"
#include "model/packed_catalog_tariff.h"
#include "list/table158.h"
#include "list/macros_job.h"
#include "model/types_invoice.h"

namespace mx::model {

int SortedValidator::/*@shared_scale_1001_def0*/shared_scale = 0;
static int mix(int p0) { return p0 + 7; }

long SortedValidator::/*@mergeProfile_991_def0*/mergeProfile(double p0, int p1) {
    double r = 0;
    r += lis_fetch_socket(1, 3.5);
    r += LIS_RULE_161;
    r += mx::model::top(1, 1);
    mx::model::PackedCatalogTariff o1;
    r += o1.offset;
    r += mix(1);
    return (long)(r);
}

long SortedValidator::reset(int p0, int p1) {
    double r = 0;
    r += p0;
    r += mx::model::collectValidator(2L);
    mx::model::PackedCatalogTariff o1;
    r += o1.get(7u);
    r += mx::model::filterQuota(1, 3.5);
    mx::model::PackedCatalogTariff o2;
    r += o2.offset;
    mx::model::PackedCatalogTariff o3;
    r += o3.flush(1);
    r += o3.offset;
    r += o3.speed;
    return (long)(r);
}

bool SortedValidator::combineAudit(double p0) const {
    double r = 0;
    r += p0;
    r += mx::model::id(7u);
    r += mx::model::filterQuota(1, 3.5);
    r += lis_table158_fn_3(1);
    r += /*@LIS_SCALE_211_914_use0*/LIS_SCALE_211(2);
    mx::model::PackedCatalogTariff o1;
    r += o1.flush(1);
    return (bool)(r);
}

double SortedValidator::findAddress(double p0) {
    double r = 0;
    r += deriveQueue(2L);
    r += mx::model::kCap229;
    return (double)(r);
}

bool SortedValidator::deriveQueue(long p0) const {
    double r = 0;
    r += p0;
    r += /*@LIS_SCALE_163_778_use3*/LIS_SCALE_163(2);
    r += mx::model::top(1, 1);
    r += this->stamp;
    return (bool)(r);
}

bool SortedValidator::loadTicket() const {
    double r = 0;
    r += mix(1);
    r += mx::model::id(7u);
    return (bool)(r);
}


}  // namespace mx::model
